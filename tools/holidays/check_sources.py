#!/usr/bin/env python3
"""Checks of the source choices behind generate.py (build time only).

  * United Kingdom: England (ENG) and Wales (WLS) give the same days.
  * Australia, Canada: the library's set without a subdivision equals the
    days that every state / province / territory has (same date and name),
    year by year; differences are listed.
  * Names: every name is printed once per country with its year range, to
    review the English (en_US) names and the years before the library's own
    start year.

Usage: python tools/holidays/check_sources.py [--names]
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import generate  # noqa: E402

YEARS = range(generate.FIRST_YEAR, generate.LAST_YEAR + 1)


def year_set(cls, subdiv, y):
    return set(generate.library_year(cls, subdiv, y))


def check_gb():
    cls = generate.library_class("GB")
    diff = []
    for y in YEARS:
        eng, wls = year_set(cls, "ENG", y), year_set(cls, "WLS", y)
        if eng != wls:
            diff.append((y, sorted(eng - wls), sorted(wls - eng)))
    print(f"GB: ENG vs WLS, {len(YEARS)} years: "
          f"{'identical' if not diff else str(len(diff)) + ' years differ'}")
    for y, a, b in diff:
        print(f"  {y}: only ENG {a}; only WLS {b}")


def check_nationwide(code):
    """Compare by date: the library's no-subdivision set with the dates that
    are a holiday in every subdivision."""
    cls = generate.library_class(code)
    subdivs = sorted(cls.subdivisions)
    same = 0
    summary = {}
    for y in YEARS:
        national = {}
        for d, n in generate.library_year(cls, None, y):
            national.setdefault(d, []).append(n)
        per_sub = {}
        for s in subdivs:
            per_sub[s] = {}
            for d, n in generate.library_year(cls, s, y):
                per_sub[s].setdefault(d, []).append(n)
        common = set.intersection(*(set(v) for v in per_sub.values()))
        if common == set(national):
            same += 1
        for d in sorted(set(national) - common):
            missing = tuple(s for s in subdivs if d not in per_sub[s])
            key = ("national set only (not in " + ", ".join(missing) + ")",
                   " / ".join(national[d]))
            summary.setdefault(key, []).append(y)
        for d in sorted(common - set(national)):
            names = sorted({n for s in subdivs for n in per_sub[s][d]})
            key = ("in every subdivision, not in the national set", " / ".join(names))
            summary.setdefault(key, []).append(y)
    print(f"{code}: no-subdivision dates vs dates that are a holiday in all "
          f"{len(subdivs)} subdivisions ({', '.join(subdivs)}): {same} of "
          f"{len(YEARS)} years identical")
    for (kind, n), ys in sorted(summary.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        print(f"  {n}: {kind}: {len(ys)} years ({_ranges(ys)})")


def _ranges(ys):
    out, start, prev = [], None, None
    for y in sorted(set(ys)):
        if start is None:
            start = prev = y
        elif y == prev + 1:
            prev = y
        else:
            out.append(f"{start}-{prev}" if start != prev else str(start))
            start = prev = y
    if start is not None:
        out.append(f"{start}-{prev}" if start != prev else str(start))
    return ", ".join(out)


def list_names():
    data = generate.collect()
    for code, name, subdiv in generate.COUNTRIES:
        base = generate.library_class(code).__mro__[1]
        print(f"{code} {name} (library {base.start_year}-{base.end_year}"
              f"{', subdiv ' + subdiv if subdiv else ''})")
        years = {}
        for y in YEARS:
            for _, n in data[code][y]:
                years.setdefault(n, []).append(y)
        for n, ys in sorted(years.items(), key=lambda kv: (kv[1][0], kv[0])):
            print(f"  {n}: {_ranges(ys)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--names", action="store_true")
    args = ap.parse_args()
    check_gb()
    check_nationwide("AU")
    check_nationwide("CA")
    if args.names:
        list_names()


if __name__ == "__main__":
    main()
