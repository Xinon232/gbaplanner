#!/usr/bin/env python3
"""Generate gbaplanner's country holiday table (build time only).

Source: the Python `holidays` library (MIT), public holidays, English names
(en_US), years 1900-2200, with the library's own start/end years lifted.
China's lunisolar dates outside the library's tables (1900, 2100-2200) and
Japan's equinox days outside 1949-2099 come from tools/holidays/astro_dates.json
(written by astro.py).

Outputs (deterministic: the same inputs give byte-identical files):
  src/holidays/holiday_data.c / holiday_data.h   the ROM table
  tests/holidays/expected_checksums.txt         per-country SHA-256 of the dump

Usage:
  python tools/holidays/generate.py            write the outputs
  python tools/holidays/generate.py --dump F   also write the full reference
                                               dump (one line per holiday) to F
  python tools/holidays/generate.py --check    fail if outputs are out of date

Table encoding (see docs/HOLIDAYS.md):
  * one shared, de-duplicated name string table;
  * per country, rules derived from the data: a holiday that falls on the
    same "key" for a run of consecutive years becomes one rule
    (fixed date / Easter + n / Orthodox Easter + n / first weekday on or
    after a date) with a year range;
  * everything else (observed days, lunar and equinox dates, one-off days)
    is stored explicitly per country-year as (day of year, name).
The generator decodes its own table and checks it against the library data
before writing anything.
"""

import argparse
import datetime as dt
import hashlib
import heapq
import io
import json
import os
import sys

import holidays
from dateutil.easter import EASTER_ORTHODOX, EASTER_WESTERN, easter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

FIRST_YEAR = 1900
LAST_YEAR = 2200
NYEARS = LAST_YEAR - FIRST_YEAR + 1
LANGUAGE = "en_US"

# Index 1..34 in this order (0 = "No country"). (code, display name, subdiv)
COUNTRIES = [
    ("AT", "Austria", None),
    ("BE", "Belgium", None),
    ("BG", "Bulgaria", None),
    ("HR", "Croatia", None),
    ("CY", "Cyprus", None),
    ("CZ", "Czechia", None),
    ("DK", "Denmark", None),
    ("EE", "Estonia", None),
    ("FI", "Finland", None),
    ("FR", "France", None),
    ("DE", "Germany", None),
    ("GR", "Greece", None),
    ("HU", "Hungary", None),
    ("IE", "Ireland", None),
    ("IT", "Italy", None),
    ("LV", "Latvia", None),
    ("LT", "Lithuania", None),
    ("LU", "Luxembourg", None),
    ("MT", "Malta", None),
    ("NL", "Netherlands", None),
    ("PL", "Poland", None),
    ("PT", "Portugal", None),
    ("RO", "Romania", None),
    ("SK", "Slovakia", None),
    ("SI", "Slovenia", None),
    ("ES", "Spain", None),
    ("SE", "Sweden", None),
    ("GB", "United Kingdom (England & Wales)", "ENG"),
    ("CA", "Canada", None),
    ("US", "USA", None),
    ("AU", "Australia", None),
    ("NZ", "New Zealand", None),
    ("CN", "China", None),
    ("JP", "Japan", None),
]

# Ranges in which the library's own China / Japan astronomical data is used.
CN_LIBRARY_YEARS = (1901, 2099)
JP_LIBRARY_YEARS = (1949, 2099)

# Rule kinds (must match src/holidays/holidays.cpp).
K_FIXED, K_EASTER, K_ORTHODOX, K_WEEKDAY, K_FIXED_NOT_SUNDAY = 0, 1, 2, 3, 4
MIN_RUN = 4  # a rule (10 bytes) pays off from 4 years of 3-byte entries
BLOCK = 16   # years per block of the explicit-day index
MAX_PER_DAY = 8  # most holidays on one day (runtime buffer size)


# --------------------------------------------------------------------------
# Library access


def load_astro():
    with open(os.path.join(HERE, "astro_dates.json"), encoding="utf-8") as f:
        years = json.load(f)["years"]
    return {int(y): v for y, v in years.items()}


ASTRO = load_astro()


class _AstroChineseCalendar:
    """Wraps the library's Chinese lunisolar calendar; outside 1901-2099 the
    dates come from the astronomical computation (astro.py)."""

    def __init__(self, inner):
        self._inner = inner

    def __getattr__(self, name):
        return getattr(self._inner, name)

    def _get(self, key, year, fallback, *args):
        lo, hi = CN_LIBRARY_YEARS
        if lo <= year <= hi:
            return fallback(year, *args)
        return dt.date.fromisoformat(ASTRO[year][key]), False

    def lunar_new_year_date(self, year, calendar=None):
        return self._get("cn_new_year", year, self._inner.lunar_new_year_date, calendar)

    def dragon_boat_date(self, year):
        return self._get("cn_dragon_boat", year, self._inner.dragon_boat_date)

    def mid_autumn_date(self, year, calendar=None):
        return self._get("cn_mid_autumn", year, self._inner.mid_autumn_date, calendar)

    def qingming_date(self, year, calendar=None):
        return self._get("cn_qingming", year, self._inner.qingming_date, calendar)


def library_class(code):
    base = type(holidays.country_holidays(code, years=[]))
    attrs = {"start_year": FIRST_YEAR, "end_year": LAST_YEAR}
    if code == "CN":
        def get_cal(self):
            return self.__dict__["_gp_cal"]

        def set_cal(self, value):
            self.__dict__["_gp_cal"] = _AstroChineseCalendar(value)

        attrs["_chinese_calendar"] = property(get_cal, set_cal)
    if code == "JP":
        def vernal(self):
            lo, hi = JP_LIBRARY_YEARS
            if lo <= self._year <= hi:
                return base._vernal_equinox_date.fget(self)
            d = dt.date.fromisoformat(ASTRO[self._year]["jp_vernal_equinox"])
            return d.month, d.day

        def autumnal(self):
            lo, hi = JP_LIBRARY_YEARS
            if lo <= self._year <= hi:
                return base._autumnal_equinox_date.fget(self)
            d = dt.date.fromisoformat(ASTRO[self._year]["jp_autumnal_equinox"])
            return d.month, d.day

        attrs["_vernal_equinox_date"] = property(vernal)
        attrs["_autumnal_equinox_date"] = property(autumnal)
    return type(f"GP{base.__name__}", (base,), attrs)


def library_year(cls, subdiv, year):
    """List of (date, name) for one year, in date order and the library's
    order of names within a day."""
    h = cls(years=year, language=LANGUAGE, subdiv=subdiv)
    out = []
    for d in sorted(h):
        for name in h.get_list(d):
            out.append((d, name))
    return out


def collect():
    """{code: {year: [(date, name), ...]}}"""
    data = {}
    for code, _, subdiv in COUNTRIES:
        cls = library_class(code)
        data[code] = {y: library_year(cls, subdiv, y)
                      for y in range(FIRST_YEAR, LAST_YEAR + 1)}
    return data


def dump_lines(data):
    """Reference dump: one line per holiday, sorted by date then name."""
    for code, _, _ in COUNTRIES:
        for y in range(FIRST_YEAR, LAST_YEAR + 1):
            for d, name in sorted(data[code][y], key=lambda e: (e[0], e[1].encode())):
                yield f"{code} {d.isoformat()} {name}"


# --------------------------------------------------------------------------
# Rule derivation


def key_date(kind, p, year):
    """Date of a rule key in a year (None if it does not exist)."""
    if kind == K_FIXED:
        m, d = p[0], p[1]
        if m == 2 and d == 29 and not _leap(year):
            return None
        return dt.date(year, m, d)
    if kind == K_EASTER:
        return easter(year, EASTER_WESTERN) + dt.timedelta(days=p[0])
    if kind == K_ORTHODOX:
        return easter(year, EASTER_ORTHODOX) + dt.timedelta(days=p[0])
    if kind == K_WEEKDAY:
        a = dt.date(year, p[0], p[1])
        return a + dt.timedelta(days=(p[2] - a.weekday()) % 7)
    if kind == K_FIXED_NOT_SUNDAY:
        m, d = p[0], p[1]
        if m == 2 and d == 29 and not _leap(year):
            return None
        a = dt.date(year, m, d)
        return None if a.weekday() == 6 else a
    raise ValueError(kind)


def _leap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def candidate_keys(d):
    y = d.year
    keys = [(K_FIXED, (d.month, d.day, 0))]
    if d.weekday() != 6 and not (d.month == 2 and d.day == 29):
        # Spain's national set leaves out fixed days that fall on a Sunday.
        keys.append((K_FIXED_NOT_SUNDAY, (d.month, d.day, 0)))
    off = (d - easter(y, EASTER_WESTERN)).days
    if -128 <= off <= 127:
        keys.append((K_EASTER, (off, 0, 0)))
    off = (d - easter(y, EASTER_ORTHODOX)).days
    if -128 <= off <= 127:
        keys.append((K_ORTHODOX, (off, 0, 0)))
    wd = d.weekday()
    for back in range(7):
        a = d - dt.timedelta(days=back)
        if a.year != y or (a.month == 2 and a.day == 29):
            continue
        if a.month == 12 and a.day > 25:
            continue  # the date could fall into the next year
        keys.append((K_WEEKDAY, (a.month, a.day, wd)))
    return keys


def derive(entries):
    """entries: list of (year, date, name_idx). Returns (rules, explicit)
    where rules = [(kind, params, name, first, last)] and explicit =
    {year: [(date, name)]}."""
    combos = {}  # (kind, params, name) -> {year: entry id}
    for eid, (y, d, n) in enumerate(entries):
        for kind, p in candidate_keys(d):
            combos.setdefault((kind, p, n), {})[y] = eid
    covered = [False] * len(entries)

    def best_run(combo):
        """Best run of consecutive years for a combo: (entries covered,
        first year, last year). Years in which the key gives no date are
        neutral (the rule emits nothing there)."""
        kind, p, _ = combo
        ym = combos[combo]
        best = (0, 0, 0)
        gain, first, last = 0, None, None
        for y in range(min(ym), max(ym) + 1):
            if y in ym and not covered[ym[y]]:
                if first is None:
                    first = y
                gain, last = gain + 1, y
                if gain > best[0]:
                    best = (gain, first, last)
            elif kind == K_FIXED_NOT_SUNDAY and key_date(kind, p, y) is None:
                continue
            else:
                gain, first, last = 0, None, None
        return best

    heap = []
    for combo in sorted(combos):
        run = best_run(combo)
        if run[0] >= MIN_RUN:
            heap.append((-run[0], combo, run))
    heapq.heapify(heap)
    rules = []
    while heap:
        neg, combo, run = heapq.heappop(heap)
        now = best_run(combo)
        if now != run:
            if now[0] >= MIN_RUN:
                heapq.heappush(heap, (-now[0], combo, now))
            continue
        kind, p, n = combo
        _, first, last = run
        for y in range(first, last + 1):
            if y in combos[combo]:
                covered[combos[combo][y]] = True
        rules.append((kind, p, n, first, last))
        heapq.heappush(heap, (neg, combo, run))  # re-checked when popped
    explicit = {}
    for eid, (y, d, n) in enumerate(entries):
        if not covered[eid]:
            explicit.setdefault(y, []).append((d, n))
    rules.sort(key=lambda r: (r[3], r[0], r[1], r[2], r[4]))
    return rules, explicit


def decode(rules, explicit, year):
    out = []
    for kind, p, n, first, last in rules:
        if first <= year <= last:
            d = key_date(kind, p, year)
            if d is not None:
                out.append((d, n))
    out.extend(explicit.get(year, []))
    return sorted(out)


# --------------------------------------------------------------------------
# C output


def c_string(s):
    out = []
    for b in s.encode("utf-8"):
        ch = chr(b)
        if ch in '"\\':
            out.append("\\" + ch)
        elif 32 <= b < 127:
            out.append(ch)
        else:
            out.append(f"\\{b:03o}")
    return '"' + "".join(out) + '"'


def emit_array(f, ctype, name, values, per_line=16, fmt="{}"):
    f.write(f"const {ctype} {name}[{len(values)}] = {{\n")
    for i in range(0, len(values), per_line):
        chunk = values[i:i + per_line]
        f.write("    " + ", ".join(fmt.format(v) for v in chunk) + ",\n")
    f.write("};\n\n")


def build(data):
    # Shared name table, in order of first appearance.
    names, name_idx = [], {}
    for code, _, _ in COUNTRIES:
        for y in range(FIRST_YEAR, LAST_YEAR + 1):
            for _, n in data[code][y]:
                if n not in name_idx:
                    name_idx[n] = len(names)
                    names.append(n)
    assert len(names) < 32768

    all_rules = []
    rule_start = [0]
    x_doy, x_name = [], []
    x_count = []  # per country-year: number of explicit days
    x_block = []  # per country and BLOCK years: index of the first entry
    stats = {}
    for code, _, _ in COUNTRIES:
        entries = []
        for y in range(FIRST_YEAR, LAST_YEAR + 1):
            seen = set()
            for d, n in data[code][y]:
                assert (d, n) not in seen, (code, d, n)
                seen.add((d, n))
                entries.append((y, d, name_idx[n]))
        rules, explicit = derive(entries)
        per_day = {}
        for y, d, n in entries:
            per_day[d] = per_day.get(d, 0) + 1
        assert max(per_day.values()) <= MAX_PER_DAY, code
        # Self-check: the encoding reproduces the library data exactly.
        for y in range(FIRST_YEAR, LAST_YEAR + 1):
            want = sorted((d, name_idx[n]) for d, n in data[code][y])
            got = decode(rules, explicit, y)
            assert got == want, (code, y, got, want)
        all_rules.extend(rules)
        rule_start.append(len(all_rules))
        for y in range(FIRST_YEAR, LAST_YEAR + 1):
            if (y - FIRST_YEAR) % BLOCK == 0:
                x_block.append(len(x_doy))
            x_count.append(len(explicit.get(y, [])))
            assert x_count[-1] < 256
            for d, n in sorted(explicit.get(y, []), key=lambda e: (e[0], e[1])):
                doy = d.timetuple().tm_yday
                x_doy.append(doy)
                x_name.append(n)
        stats[code] = (len(entries), len(rules),
                       sum(len(v) for v in explicit.values()))
    assert len(x_doy) < 65536
    return names, all_rules, rule_start, x_doy, x_name, (x_count, x_block), stats


HEADER = """\
/* Generated by tools/holidays/generate.py from the Python `holidays` library
 * (MIT) and tools/holidays/astro_dates.json. Do not edit. */
"""


def write_c(names, rules, rule_start, x_doy, x_name, x_index, sizes):
    x_count, x_block = x_index
    ncountries = len(COUNTRIES)
    h = io.StringIO()
    h.write(HEADER)
    h.write("""#ifndef GBAPLANNER_HOLIDAY_DATA_H
#define GBAPLANNER_HOLIDAY_DATA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOL_FIRST_YEAR %d
#define HOL_LAST_YEAR %d
#define HOL_NYEARS %d
#define HOL_NCOUNTRIES %d
#define HOL_NNAMES %d
#define HOL_NRULES %d
#define HOL_NEXPLICIT %d
#define HOL_MAX_PER_DAY %d
#define HOL_BLOCK %d
#define HOL_NBLOCKS %d

/* Rule kinds. */
#define HOL_K_FIXED 0    /* p0 = month, p1 = day */
#define HOL_K_EASTER 1   /* (int8_t)p0 = days after Western Easter Sunday */
#define HOL_K_ORTHODOX 2 /* (int8_t)p0 = days after Orthodox Easter Sunday */
#define HOL_K_WEEKDAY 3  /* first weekday p2 (0 = Monday) on or after p0/p1 */
#define HOL_K_FIXED_NOT_SUNDAY 4 /* p0/p1 unless that day is a Sunday */

typedef struct {
    uint16_t first; /* first year the rule applies */
    uint16_t last;  /* last year the rule applies */
    uint16_t name;  /* index into hol_name_off */
    uint8_t kind;
    uint8_t p0;
    uint8_t p1;
    uint8_t p2;
} HolRule;

extern const char hol_name_chars[];
extern const uint16_t hol_name_off[HOL_NNAMES];
extern const HolRule hol_rules[HOL_NRULES];
/* Rules of country c (0-based): hol_rules[hol_rule_start[c] ..
 * hol_rule_start[c + 1]). */
extern const uint16_t hol_rule_start[HOL_NCOUNTRIES + 1];
/* Explicit days of country c (0-based), year index yi = y - HOL_FIRST_YEAR:
 * hol_x_count[c * HOL_NYEARS + yi] entries starting at
 * hol_x_block[c * HOL_NBLOCKS + yi / HOL_BLOCK] plus the counts of the
 * earlier years of that block. Sorted by day of year.
 * Day of year = hol_x_doy[k] | ((hol_x_name[k] >> 15) << 8); name index =
 * hol_x_name[k] & 0x7fff. */
extern const uint8_t hol_x_count[HOL_NCOUNTRIES * HOL_NYEARS];
extern const uint16_t hol_x_block[HOL_NCOUNTRIES * HOL_NBLOCKS];
extern const uint8_t hol_x_doy[HOL_NEXPLICIT];
extern const uint16_t hol_x_name[HOL_NEXPLICIT];
/* Per year (index y - HOL_FIRST_YEAR): bits 0-2 weekday of January 1st
 * (0 = Monday), bits 3-8 Western Easter Sunday - 22 March in days, bits
 * 9-14 Orthodox Easter Sunday (Gregorian date) - 22 March, bit 15 leap year.
 * Easter dates as computed by dateutil.easter, which the library uses. */
extern const uint16_t hol_year_info[HOL_NYEARS];
extern const char* const hol_country_names[HOL_NCOUNTRIES + 1];
extern const char hol_country_codes[HOL_NCOUNTRIES + 1][3];

#ifdef __cplusplus
}
#endif

#endif
""" % (FIRST_YEAR, LAST_YEAR, NYEARS, ncountries, len(names), len(rules),
       len(x_doy), MAX_PER_DAY, BLOCK, (NYEARS + BLOCK - 1) // BLOCK))

    c = io.StringIO()
    c.write(HEADER)
    c.write("/* Sizes: names %d B, rules + year info %d B, explicit index %d B, explicit "
            "days %d B; total %d B. */\n\n" % sizes)
    c.write('#include "holiday_data.h"\n\n')
    offs, blob = [], []
    pos = 0
    for n in names:
        offs.append(pos)
        pos += len(n.encode("utf-8")) + 1
        blob.append(n)
    assert pos < 65536
    c.write("const char hol_name_chars[] =\n")
    for i, n in enumerate(names):
        c.write(f"    {c_string(n)} \"\\0\" /* {i} */\n")
    c.write("    ;\n\n")
    emit_array(c, "uint16_t", "hol_name_off", offs)
    c.write(f"const HolRule hol_rules[{len(rules)}] = {{\n")
    ci = 0
    for i, (kind, p, n, first, last) in enumerate(rules):
        while i >= rule_start[ci + 1]:
            ci += 1
        if i == rule_start[ci]:
            c.write(f"    /* {COUNTRIES[ci][0]} */\n")
        p0, p1, p2 = p
        if kind in (K_EASTER, K_ORTHODOX):
            p0 &= 0xFF
        c.write(f"    {{{first}, {last}, {n}, {kind}, {p0}, {p1}, {p2}}},\n")
    c.write("};\n\n")
    emit_array(c, "uint16_t", "hol_rule_start", rule_start)
    emit_array(c, "uint8_t", "hol_x_count", x_count, per_line=NYEARS // 7)
    emit_array(c, "uint16_t", "hol_x_block", x_block,
               per_line=(NYEARS + BLOCK - 1) // BLOCK)
    emit_array(c, "uint8_t", "hol_x_doy", [d & 0xFF for d in x_doy] or [0])
    emit_array(c, "uint16_t", "hol_x_name",
               [n | ((d >> 8) << 15) for d, n in zip(x_doy, x_name)] or [0])
    info = []
    for y in range(FIRST_YEAR, LAST_YEAR + 1):
        march22 = dt.date(y, 3, 22)
        w = (easter(y, EASTER_WESTERN) - march22).days
        o = (easter(y, EASTER_ORTHODOX) - march22).days
        assert 0 <= w < 64 and 0 <= o < 64
        info.append(dt.date(y, 1, 1).weekday() | (w << 3) | (o << 9)
                    | (_leap(y) << 15))
    emit_array(c, "uint16_t", "hol_year_info", info, per_line=12, fmt="0x{:04x}")
    c.write(f"const char* const hol_country_names[{len(COUNTRIES) + 1}] = {{\n")
    c.write('    "No country",\n')
    for _, name, _ in COUNTRIES:
        c.write(f"    {c_string(name)},\n")
    c.write("};\n\n")
    c.write(f"const char hol_country_codes[{len(COUNTRIES) + 1}][3] = {{\n")
    c.write('    "",\n')
    for code, _, _ in COUNTRIES:
        c.write(f'    "{code}",\n')
    c.write("};\n")
    return c.getvalue(), h.getvalue()


def checksums(data):
    out = io.StringIO()
    out.write("# SHA-256 per country of the reference dump lines\n"
              "# (\"CC YYYY-MM-DD Name\\n\", sorted by date, then name bytes),\n"
              "# years %d-%d. Written by tools/holidays/generate.py.\n"
              % (FIRST_YEAR, LAST_YEAR))
    total = hashlib.sha256()
    count = 0
    for code, _, _ in COUNTRIES:
        hsh = hashlib.sha256()
        n = 0
        for y in range(FIRST_YEAR, LAST_YEAR + 1):
            for d, name in sorted(data[code][y], key=lambda e: (e[0], e[1].encode())):
                b = f"{code} {d.isoformat()} {name}\n".encode("utf-8")
                hsh.update(b)
                total.update(b)
                n += 1
        count += n
        out.write(f"{code} {n} {hsh.hexdigest()}\n")
    out.write(f"ALL {count} {total.hexdigest()}\n")
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", help="write the full reference dump here")
    ap.add_argument("--check", action="store_true",
                    help="compare with the committed files instead of writing")
    ap.add_argument("--stats", action="store_true")
    args = ap.parse_args()

    data = collect()
    names, rules, rule_start, x_doy, x_name, x_index, stats = build(data)
    names_size = sum(len(n.encode("utf-8")) + 1 for n in names) + 2 * len(names)
    sizes = (names_size, 10 * len(rules) + 2 * len(rule_start) + 2 * NYEARS,
             len(x_index[0]) + 2 * len(x_index[1]), 3 * len(x_doy))
    sizes = sizes + (sum(sizes),)
    c_text, h_text = write_c(names, rules, rule_start, x_doy, x_name,
                             x_index, sizes)
    sums = checksums(data)
    outputs = {
        os.path.join(ROOT, "src", "holidays", "holiday_data.c"): c_text,
        os.path.join(ROOT, "src", "holidays", "holiday_data.h"): h_text,
        os.path.join(ROOT, "tests", "holidays", "expected_checksums.txt"): sums,
    }
    if args.stats:
        for code, _, _ in COUNTRIES:
            e, r, x = stats[code]
            print(f"{code}: {e} holidays -> {r} rules + {x} explicit days")
        print(f"names {len(names)}; rules {len(rules)}; explicit {len(x_doy)}")
        print("bytes: names %d, rules %d, index %d, explicit %d, total %d" % sizes)
    if args.dump:
        with open(args.dump, "w", encoding="utf-8", newline="\n") as f:
            for line in dump_lines(data):
                f.write(line + "\n")
    stale = []
    for path, text in outputs.items():
        if args.check:
            try:
                with open(path, encoding="utf-8", newline="") as f:
                    if f.read() != text:
                        stale.append(path)
            except FileNotFoundError:
                stale.append(path)
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
    if args.check:
        if stale:
            print("out of date:", *stale, sep="\n  ")
            sys.exit(1)
        print("generated files are up to date")
    else:
        print("wrote", *outputs, sep="\n  ")


if __name__ == "__main__":
    main()
