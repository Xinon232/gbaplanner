#!/usr/bin/env python3
"""Compare tools/holidays/astro_dates.json with the holidays library.

China: Spring Festival, Dragon Boat, Mid-Autumn (library lunar tables) and
Qingming (library approximation) for every year the library covers.
Japan: vernal / autumnal equinox day (library formula) for 1900-2200, showing
where the library's formula agrees with the astronomical date.

Optional (--ephem): an independent cross-check of the skyfield/DE440 results
with PyEphem (VSOP87/ELP based; LGPL, used only here at build time).

Usage: python tools/holidays/verify_astro.py [--ephem]
Exit status 1 if China disagrees with the library inside 1901-2099 or Japan
disagrees inside 1949-2099.
"""

import argparse
import datetime as dt
import json
import os
import sys

from holidays.calendars.chinese import _ChineseLunisolar
from holidays.countries.japan import Japan

HERE = os.path.dirname(os.path.abspath(__file__))


def load_astro():
    with open(os.path.join(HERE, "astro_dates.json"), encoding="utf-8") as f:
        return json.load(f)["years"]


def check_china(astro):
    cal = _ChineseLunisolar()
    funcs = {
        "cn_new_year": cal.lunar_new_year_date,
        "cn_dragon_boat": cal.dragon_boat_date,
        "cn_mid_autumn": cal.mid_autumn_date,
        "cn_qingming": cal.qingming_date,
    }
    bad_in_range = 0
    print("China (astronomical, UTC+8) vs library:")
    for key, fn in funcs.items():
        same = diff = 0
        lines = []
        for y in range(1900, 2201):
            try:
                lib, _ = fn(y)
            except Exception:  # noqa: BLE001 - library has no data
                lib = None
            if lib is None:
                continue
            mine = dt.date.fromisoformat(astro[str(y)][key])
            if lib == mine:
                same += 1
            else:
                diff += 1
                stated = 1901 <= y <= 2099
                bad_in_range += stated
                lines.append((y, lib, mine, stated))
        lo = min(y for y in range(1900, 2201) if _has(fn, y))
        hi = max(y for y in range(1900, 2201) if _has(fn, y))
        print(f"  {key}: library years {lo}-{hi}: {same} agree, {diff} differ")
        _print_diffs(lines)
    return bad_in_range


def _print_diffs(lines):
    """lines: (year, library date, astro date, inside stated range)."""
    inside = [l for l in lines if l[3]]
    outside = [l for l in lines if not l[3]]
    for y, lib, mine, _ in inside:
        print(f"    {y}: library {lib} astro {mine}  <-- inside the library range")
    if outside:
        later = sum(1 for _, lib, mine, _ in outside if mine > lib)
        print(f"    outside the library range: {len(outside)} differ "
              f"(astro later by a day in {later}): "
              + " ".join(str(l[0]) for l in outside))


def _has(fn, y):
    try:
        return fn(y)[0] is not None
    except Exception:  # noqa: BLE001
        return False


def check_japan(astro):
    print("Japan equinox days (astronomical, UTC+9) vs library formula:")
    bad_in_range = 0
    for key, prop in (("jp_vernal_equinox", "_vernal_equinox_date"),
                      ("jp_autumnal_equinox", "_autumnal_equinox_date")):
        same = {"in": 0, "out": 0}
        diffs = []
        for y in range(1900, 2201):
            obj = Japan.__new__(Japan)
            obj._year = y
            m, d = getattr(Japan, prop).fget(obj)
            lib = dt.date(y, m, d)
            mine = dt.date.fromisoformat(astro[str(y)][key])
            rng = "in" if 1949 <= y <= 2099 else "out"
            if lib == mine:
                same[rng] += 1
            else:
                diffs.append((y, lib, mine, rng == "in"))
                bad_in_range += rng == "in"
        n_in, n_out = 2099 - 1949 + 1, 301 - (2099 - 1949 + 1)
        print(f"  {key}: 1949-2099 {same['in']}/{n_in} agree; "
              f"1900-1948 and 2100-2200 {same['out']}/{n_out} agree")
        _print_diffs(diffs)
    return bad_in_range


def check_margins(astro):
    print("Closest calls (deciding instant within 30 min of local midnight):")
    for y in range(1900, 2201):
        a = astro[str(y)]
        for k, v in sorted(a["cn_margin_min"].items()):
            if v < 30:
                print(f"  {y} China {k}: {v} min")
        for k, v in sorted(a["jp_margin_min"].items()):
            if v < 30:
                print(f"  {y} Japan {k} equinox: {v} min")


def check_ephem(astro):
    """Recompute the deciding instants with PyEphem and compare dates."""
    import ephem
    import math

    def sun_lon(d):
        s = ephem.Sun(d)
        return math.degrees(float(ephem.Ecliptic(s, epoch=d).lon))

    def solve_lon(target, guess):
        # Newton/secant on apparent solar longitude (degrees/day ~ 0.9856).
        d = ephem.Date(guess)
        for _ in range(30):
            diff = (sun_lon(d) - target + 180) % 360 - 180
            d = ephem.Date(d - diff / 0.9856)
            if abs(diff) < 1e-7:
                break
        return d

    def local(d, hours):
        return (ephem.Date(d).datetime() + dt.timedelta(hours=hours)).date()

    print("PyEphem cross-check of the astronomical dates:")
    mism = []
    for y in range(1900, 2201):
        a = astro[str(y)]
        for key, lon, hrs, guess in (
            ("cn_qingming", 15, 8, f"{y}/4/4"),
            ("jp_vernal_equinox", 0, 9, f"{y}/3/20"),
            ("jp_autumnal_equinox", 180, 9, f"{y}/9/23"),
        ):
            d = solve_lon(lon, guess)
            if local(d, hrs).isoformat() != a[key]:
                mism.append(f"  {y} {key}: ephem {local(d, hrs)} skyfield {a[key]}")
        # New moons starting the months of the three lunar festivals.
        for key, day in (("cn_new_year", 1), ("cn_dragon_boat", 5),
                         ("cn_mid_autumn", 15)):
            start = dt.date.fromisoformat(a[key]) - dt.timedelta(days=day - 1)
            nm = ephem.next_new_moon(ephem.Date(start - dt.timedelta(days=3)))
            if local(nm, 8) != start:
                mism.append(f"  {y} {key}: ephem new moon {local(nm, 8)} "
                            f"vs month start {start}")
    print(f"  {len(mism)} mismatches")
    for m in mism:
        print(m)
    return len(mism)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ephem", action="store_true")
    args = ap.parse_args()
    astro = load_astro()
    bad = check_china(astro)
    bad += check_japan(astro)
    check_margins(astro)
    if args.ephem:
        check_ephem(astro)
    print("RESULT:", "OK" if bad == 0 else f"{bad} disagreements in library range")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
