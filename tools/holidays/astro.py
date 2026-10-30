#!/usr/bin/env python3
"""Astronomical dates for the holiday generator (build time only).

Computes, for every Gregorian year 1900-2200:
  * the Chinese lunisolar calendar (China time, UTC+8): Spring Festival
    (1st day of month 1), Dragon Boat Festival (5/5), Mid-Autumn Festival
    (8/15) and Qingming (date of the solar term at 15 degrees);
  * Japan's vernal and autumnal equinox dates (Japan time, UTC+9).

Method: skyfield (MIT) with the JPL DE440 ephemeris (public domain; taken
from the MIT-licensed `naif-de440` wheel, which bundles de440.bsp), apparent
geocentric ecliptic longitudes of date, skyfield's built-in Delta T model.

Chinese calendar rules (the modern rules, as used since 1929):
  * a month starts on the day (UTC+8) of a new moon;
  * the month containing the winter solstice (270 degrees) is month 11;
  * if 13 months lie between two months 11 (a leap "sui"), the first month
    after the first month 11 that contains no principal term (solar
    longitude a multiple of 30 degrees) is a leap month and repeats the
    previous month's number.

Output: tools/holidays/astro_dates.json (deterministic, committed), which
generate.py reads, so regenerating the C table needs no ephemeris.

Usage: python tools/holidays/astro.py [--out PATH]
"""

import argparse
import datetime as dt
import json
import os
import sys

import numpy as np
from skyfield import almanac
from skyfield.api import load, load_file
from skyfield.framelib import ecliptic_frame
from skyfield.searchlib import find_discrete

FIRST_YEAR = 1900
LAST_YEAR = 2200
CN_OFFSET = dt.timedelta(hours=8)
JP_OFFSET = dt.timedelta(hours=9)


def load_ephemeris():
    try:
        import naif_de440
    except ImportError:
        sys.exit("pip install naif-de440 (bundles JPL DE440, 1550-2650)")
    return load_file(naif_de440.de440)


class Sky:
    def __init__(self):
        self.ts = load.timescale(builtin=True)
        self.eph = load_ephemeris()
        self.earth = self.eph["earth"]
        self.sun = self.eph["sun"]

    def sun_longitude(self, t):
        app = self.earth.at(t).observe(self.sun).apparent()
        _, lon, _ = app.frame_latlon(ecliptic_frame)
        return lon.degrees

    def solar_terms(self, start, end):
        """List of (utc datetime, term index 0..23) with term k at 15*k deg."""

        def term(t):
            return (np.floor(self.sun_longitude(t) / 15.0).astype(int)) % 24

        term.step_days = 5.0
        t0 = self.ts.utc(start.year, start.month, start.day)
        t1 = self.ts.utc(end.year, end.month, end.day)
        times, values = find_discrete(t0, t1, term, epsilon=1e-3 / 86400)
        return [(t.utc_datetime(), int(v)) for t, v in zip(times, values)]

    def new_moons(self, start, end):
        f = almanac.moon_phases(self.eph)
        t0 = self.ts.utc(start.year, start.month, start.day)
        t1 = self.ts.utc(end.year, end.month, end.day)
        times, values = find_discrete(t0, t1, f, epsilon=1e-3 / 86400)
        return [t.utc_datetime() for t, v in zip(times, values) if v == 0]


def local_date(utc, offset):
    return (utc.replace(tzinfo=None) + offset).date()


def minutes_from_midnight(utc, offset):
    """Distance (minutes) of an instant from the nearest local midnight."""
    local = utc.replace(tzinfo=None) + offset
    m = local.hour * 60 + local.minute + local.second / 60.0
    return min(m, 1440 - m)


def build_chinese_months(moons, terms):
    """Return list of dicts: start date, end date, number, leap, for months
    from the first full sui after the start of the data to the end."""
    nm = [local_date(m, CN_OFFSET) for m in moons]
    zq = [local_date(t, CN_OFFSET) for t, k in terms if k % 2 == 0]
    ws = [local_date(t, CN_OFFSET) for t, k in terms if k == 18]  # 270 deg

    def month_index(day):
        # index i such that nm[i] <= day < nm[i+1]
        lo, hi = 0, len(nm) - 1
        assert nm[0] <= day < nm[-1]
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if nm[mid] <= day:
                lo = mid
            else:
                hi = mid
        return lo

    def has_zhongqi(i):
        return any(nm[i] <= d < nm[i + 1] for d in zq)

    months = []
    for a, b in zip(ws, ws[1:]):
        ia, ib = month_index(a), month_index(b)
        n = ib - ia
        assert n in (12, 13), (a, b, n)
        leap_i = None
        if n == 13:
            for i in range(ia + 1, ib + 1):
                if not has_zhongqi(i):
                    leap_i = i
                    break
            assert leap_i is not None
        num = 11
        for i in range(ia, ib):
            if i == ia:
                number, leap = 11, False
            elif i == leap_i:
                number, leap = num, True
            else:
                num = num % 12 + 1
                number, leap = num, False
            months.append(
                {"start": nm[i], "end": nm[i + 1], "num": number, "leap": leap,
                 "moon": moons[i]}
            )
    return months


def compute():
    sky = Sky()
    start = dt.date(FIRST_YEAR - 1, 9, 1)
    end = dt.date(LAST_YEAR + 1, 3, 1)
    terms = sky.solar_terms(start, end)
    moons = sky.new_moons(start, end)
    months = build_chinese_months(moons, terms)

    def lunar_day(num, day, year):
        """Gregorian date of lunar (num, day) in the lunar year starting in
        Gregorian `year` (non-leap month)."""
        # Month 1 of lunar year `year` starts in Jan/Feb of `year`.
        firsts = [m for m in months if m["num"] == 1 and not m["leap"]
                  and m["start"].year == year]
        assert len(firsts) == 1, (year, firsts)
        i = months.index(firsts[0])
        while not (months[i]["num"] == num and not months[i]["leap"]):
            i += 1
        m = months[i]
        return m["start"] + dt.timedelta(days=day - 1), m

    years = {}
    for y in range(FIRST_YEAR, LAST_YEAR + 1):
        cny, m1 = lunar_day(1, 1, y)
        dbf, m5 = lunar_day(5, 5, y)
        maf, m8 = lunar_day(8, 15, y)
        qm = [t for t, k in terms if k == 1 and t.year == y]
        ve = [t for t, k in terms if k == 0 and t.year == y]
        ae = [t for t, k in terms if k == 12 and t.year == y]
        assert len(qm) == len(ve) == len(ae) == 1
        leap = [m["num"] for m in months if m["leap"] and m["start"].year == y]
        years[str(y)] = {
            "cn_new_year": cny.isoformat(),
            "cn_dragon_boat": dbf.isoformat(),
            "cn_mid_autumn": maf.isoformat(),
            "cn_qingming": local_date(qm[0], CN_OFFSET).isoformat(),
            "cn_leap_month_started": leap,
            # Minutes between the deciding instant and local midnight; small
            # values mean the date is sensitive to Delta T / ephemeris error.
            "cn_margin_min": {
                "new_year": round(minutes_from_midnight(m1["moon"], CN_OFFSET)),
                "dragon_boat": round(minutes_from_midnight(m5["moon"], CN_OFFSET)),
                "mid_autumn": round(minutes_from_midnight(m8["moon"], CN_OFFSET)),
                "qingming": round(minutes_from_midnight(qm[0], CN_OFFSET)),
            },
            "jp_vernal_equinox": local_date(ve[0], JP_OFFSET).isoformat(),
            "jp_autumnal_equinox": local_date(ae[0], JP_OFFSET).isoformat(),
            "jp_margin_min": {
                "vernal": round(minutes_from_midnight(ve[0], JP_OFFSET)),
                "autumnal": round(minutes_from_midnight(ae[0], JP_OFFSET)),
            },
        }
    return {
        "source": "skyfield + JPL DE440 (naif-de440), built-in Delta T; "
                  "China UTC+8, Japan UTC+9",
        "years": years,
    }


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(here, "astro_dates.json"))
    args = ap.parse_args()
    data = compute()
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        # One line per year: compact, deterministic, diff-friendly.
        f.write("{\n")
        f.write(f' "source": {json.dumps(data["source"])},\n')
        f.write(' "years": {\n')
        ys = sorted(data["years"])
        for i, y in enumerate(ys):
            row = json.dumps(data["years"][y], sort_keys=True)
            f.write(f'  "{y}": {row}{"," if i + 1 < len(ys) else ""}\n')
        f.write(" }\n}\n")
    print("wrote", args.out)


if __name__ == "__main__":
    main()
