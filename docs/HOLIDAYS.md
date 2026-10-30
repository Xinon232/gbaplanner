# Country holidays

How gbaplanner's read-only country holidays are made, checked and looked up.
Design reference: `docs/DESIGN.md`, sections "Country holidays" and
"Calendar".

## Summary

| | |
|---|---|
| Countries | 34 (Settings index 1..34, 0 = No country), national holidays only |
| Years | 1900–2200 (Gregorian) |
| Names | English (`en_US` locale of the library), UTF-8, 539 distinct names |
| Source | Python [`holidays`](https://github.com/vacanza/holidays) 0.107 (MIT), category "public" |
| Astronomy | China's lunisolar dates outside 1901–2099 and Japan's equinox days outside 1949–2099: Skyfield (MIT) + JPL DE440 (public domain) |
| ROM | 56,494 bytes (54,928 table + 1,566 code, `arm-none-eabi -O2 -mthumb`) |
| RAM | 0 bytes static; at most 88 bytes of stack per call |
| Speed | month query at most 1,115 Thumb instructions + 167 ROM loads, below 0.31 ms on the GBA even with a pessimistic estimate |
| Checked | every date and name of all 34 countries × 301 years (122,191 holidays) equals the library output |

Nothing is ever written to a file. The ROM contains only the computed table;
no Python package or astronomy code is part of the ROM.

Credits line for the app:

> Holidays: python-holidays (MIT), generated at build time. Chinese calendar
> and equinox dates computed at build time with Skyfield (MIT) and the JPL
> DE440 ephemeris (public domain).

PyEphem (LGPL) is only used, optionally, for an independent cross-check by
`tools/holidays/verify_astro.py --ephem`; it does not contribute to the table
and nothing of it is in the ROM, so it needs no credit line.

## Files

| File | What |
|---|---|
| `tools/holidays/astro.py` | astronomical dates → `tools/holidays/astro_dates.json` (committed) |
| `tools/holidays/verify_astro.py` | compares `astro_dates.json` with the library |
| `tools/holidays/generate.py` | library + `astro_dates.json` → `src/holidays/holiday_data.c/.h`, `tests/holidays/expected_checksums.txt` |
| `tools/holidays/check_sources.py` | checks of the source choices (UK, Australia, Canada) and a name list for review |
| `tools/holidays/requirements.txt` | pinned build-time Python packages |
| `include/holidays/holidays.h`, `src/holidays/holidays.cpp` | runtime API |
| `src/holidays/holiday_data.c/.h` | generated table (committed; the ROM build needs no Python) |
| `tests/holidays/run.sh` | tests (host with sanitizers, library comparison, GBA build, timing) |
| `tests/holidays/test_holidays.cpp`, `gba_timing.py` | test programs |

## Runtime API

```cpp
#include "holidays/holidays.h"

int holidays_country_count();                 // 35, including "No country"
const char* holidays_country_name(int c);      // "No country", "Austria", ... "Japan"
const char* holidays_country_code(int c);      // "", "AT", ... "JP"

// Holidays on one day: fills up to max items, returns the total number.
int holidays_on_day(int country, int year, int month, int day,
                    HolidayItem* out, int max);   // HolidayItem { const char* name; }

// Month view dots: counts[d-1] per day (31 entries), optional bit mask of
// days with holidays, returns the number of holidays in the month.
int holidays_in_month(int country, int year, int month,
                      uint8_t counts[31], uint32_t* day_mask);
```

- Country 0 and arguments out of range (year outside 1900–2200, invalid
  month/day, 29 February in a common year) give 0 holidays.
- Several holidays on one day are separate items (one dot each in the
  calendar). At most 2 country holidays fall on one day in the whole table
  (e.g. Czechia 1 January: "Independent Czech State Restoration Day" and
  "New Year's Day"). Items of a day come in a fixed order (order of first
  appearance in the name table).
- Names are UTF-8 in ROM. Non-ASCII letters used: `š` (Prešeren's Day,
  Slovenia), `õ` (Camões, Portugal), `ō` (Emperor Shōwa Funeral Ceremony,
  Japan 1989). The fonts need these or the screens must fall back.
- C++, no heap, no static state, no exceptions/RTTI. Builds for host and
  GBA (devkitARM). The table is `const` and stays in ROM.

## Source data

For each country `generate.py` asks the library for
`country_holidays(code, years=y, language="en_US", subdiv=...)`, category
public (the default), for every year 1900–2200, and takes every name of every
date (`get_list`, so two holidays on one day stay separate).

- **English names**: `en_US` is supported by 32 of the 34 classes; Ireland
  and New Zealand have no translations and their default names are English.
  All names were reviewed (`check_sources.py --names`): all English. Where a
  country's own English locale exists the differences are only spelling:
  Canada `en_CA` "Labour Day" (en_US "Labor Day"), UK `en_GB` "Whit Monday"
  (until 1970; en_US "Pentecost Monday"), Cyprus `en_CY` "Labour Day",
  "Pentecost"; Australia `en_AU` identical. The table uses `en_US` throughout
  as agreed; switching one country is a one-line change in `library_year()`.
- **Observed / substitute days** are kept as the library adds them, with the
  library's names: "Christmas Day (observed)", "Boxing Day (observed)",
  Japan "Substitute Holiday" and "National Holiday" (citizens' holiday),
  China "… (observed)" and "Day off (substituted from MM/DD/YYYY)", the
  bridge days of Hungary, Latvia, Bulgaria ("Day off (substituted from …)"),
  etc.
- **Year limits lifted**: each country class is subclassed with
  `start_year = 1900`, `end_year = 2200`. Before the library's own start year
  the library's earliest rules apply; after 2100 (Japan: 2099) today's rules
  continue. No country raises an error in 1900–2200 except China outside its
  lunar tables (see below).

### United Kingdom (England & Wales)

Subdivision `ENG`. Checked: `ENG` and `WLS` give identical days and names in
all 301 years, so the set is the England & Wales bank holidays.

### Australia and Canada: national set

No subdivision. The library's national set is the list of holidays that are
national, on their actual dates:

- **Canada**: New Year's Day, Good Friday, Canada Day (Dominion Day until
  1982), Labor Day, Christmas Day (+ "(observed)" days). Checked by date
  against all 13 provinces and territories: in all 301 years the national
  set equals exactly the dates that are a holiday in every one of them.
- **Australia**: New Year's Day, Australia Day (from 1935), Good Friday,
  Easter Monday, ANZAC Day (from 1921), Christmas Day, Boxing Day (and King's
  Birthday 1902–1935, the 2022 Day of Mourning). The states differ in the
  substitute days for holidays on a weekend (e.g. Tasmania has no 1 January
  holiday when it falls on a Sunday, only the Monday; South Australia calls
  26 December "Proclamation Day"), so the national set has no substitute days
  and shows each holiday on its own date. Compared by date with the 9
  library subdivisions (ACT, NSW, NSW Western Division, NT, QLD, SA, TAS,
  VIC, WA), 66 of 301 years are identical; the others differ only by those
  weekend substitutes (e.g. "Christmas Day (observed)" Monday when 25
  December is a Saturday, which every state has but the national set does
  not) and by Easter Sunday, which the library has in every state from 2027
  but not in the national set. This matches "only the days that apply
  everywhere" in the sense of the holidays themselves; the substitute days
  are state law and are not shown. Details: `check_sources.py`.

### Library start years

The library's own year range (the table lifts it to 1900–2200):

| | | | | | |
|---|---|---|---|---|---|
| AT 1934 | BE 1901 | BG 1991 | HR 1992 | CY 1961 | CZ 1952 |
| DK 1771 | EE 1991 | FI 1853 | FR 1803 | DE 1991 | GR 1901 |
| HU 1945 | IE 1872 | IT 1870 | LV 1991 | LT 1990 | LU 1901 |
| MT 1980 | NL 1966 | PL 1925 | PT 1801 | RO 1997 | SK 1993 |
| SI 1992 | ES 2008 | SE 1953 | GB 1872 | CA 1867 | US 1777 |
| AU 1801 | NZ 1894 | CN 1950 | JP 1949 | | |

All end in 2100 except Japan (2099).

Review of the years before the start year (full list:
`check_sources.py --names`): the output is the library's earliest rules
applied backwards. Anachronistic but harmless, as agreed: Germany "German
Unity Day" from 1900 (and "Repentance and Prayer Day" until 1994), Croatia
"Statehood Day"/Slovenia "Statehood Day", Czechia "Independent
Czechoslovak State Day" before 1918, Bulgaria "Liberation Day", Estonia
"Independence Day", China "National Day" before 1949, Netherlands
"Liberation Day" every fifth year from 1900, Japan's 1949 holiday set from
1900 (including "Emperor's Birthday" on 29 April, the Shōwa emperor's
birthday, and "Vernal/Autumnal Equinox Day" under their post-war names),
Spain's current national set (incl. "Constitution Day") from 1900. Nothing
absurd was found (no errors, duplicates or out-of-year dates). Some library
choices worth knowing: Lithuania lists Mother's Day and Father's Day (Sundays)
and Latvia Mother's Day as public holidays; Bulgaria names both 25 and 26
December "Christmas Day" and both Easter days "Easter"; Spain's national set
leaves out fixed holidays falling on a Sunday (the regions move them).

## China

The library's Chinese lunisolar dates are tables for 1901–2100 (Qingming: an
approximation stated as reliable for 1901–2099); outside them the library
fails (`TypeError`). `astro.py` computes the calendar itself:

- New moons and the 24 solar terms (apparent geocentric ecliptic longitude of
  date, multiples of 15°) with Skyfield 1.55 and the JPL DE440 ephemeris
  (from the MIT-licensed `naif-de440` wheel, which bundles `de440.bsp`,
  1550–2650), Skyfield's built-in Delta T; all dates in China time (UTC+8).
- A month starts on the day of a new moon; the month containing the winter
  solstice (270°) is month 11; if 13 months lie between two months 11, the
  first month after the first month 11 without a principal term (multiple of
  30°) is the leap month. Checks: leap month 8 in 1900, leap month 11 in 2033
  (the "2033 problem" year) come out right.
- Spring Festival = 1/1, Dragon Boat = 5/5, Mid-Autumn = 8/15, Qingming =
  day of the 15° term.

Verification against the library (`verify_astro.py`), every year the library
covers:

| | Library years | Agree |
|---|---|---|
| Spring Festival | 1901–2100 | 200 / 200 |
| Dragon Boat Festival | 1901–2100 | 200 / 200 |
| Mid-Autumn Festival | 1901–2100 | 200 / 200 |
| Qingming | 1901–2099 (stated range) | 199 / 199 (also 1900) |

Beyond 2099 the library's Qingming approximation drifts (53 of 2100–2200 a
day early compared with the astronomical date).

The generator wraps the library's calendar: in 1901–2099 the library's own
dates are used; in 1900 and 2100–2200 the astronomical ones. The holidays are
then generated by the library's current rules (`China` class): today's set
(New Year's Day, Chinese New Year's Eve + 3 days, Tomb-Sweeping Day, Labor
Day 1–2 May, Dragon Boat, Mid-Autumn, National Day 1–3 October, and the
library's weekend-substitute "(observed)" days, which it applies from 2000).
For years the library covers, its output is kept as is, including the
published extra golden-week days ("Day off (substituted from …)") for years
with a State Council schedule (2001–2026). 1900–1949 get the 1950 rule set.

Limitations: China's make-up working days (weekends that become working
days) are not shown, as agreed. Years after the last published schedule are
the library's rule-based estimate, not the official schedule.

## Japan

The library computes the equinox days with a formula valid for 1949–2099.
`astro.py` computes the vernal (0°) and autumnal (180°) equinox dates in
Japan time (UTC+9) the same way as above.

| | 1949–2099 | 1900–1948 and 2100–2200 |
|---|---|---|
| Vernal Equinox Day | 151 / 151 agree | 76 / 150 agree (74 a day later astronomically) |
| Autumnal Equinox Day | 151 / 151 agree | 80 / 150 agree (70 a day later) |

The differences outside 1949–2099 are expected: the formula ignores that
1900 and 2100 are not leap years. The generator subclasses `Japan` and
replaces only its two equinox properties: the library's formula in
1949–2099, the astronomical date otherwise. Everything else — substitute
holidays (from 1973), the citizens' "National Holiday" between two holidays
(e.g. September), special days — is the library's own logic, now fed with
the astronomical dates (e.g. the September "National Holiday" in 2105,
2111, 2116 …).

## Precision of the astronomical dates

`astro_dates.json` records, per year, how far (in minutes) the deciding new
moon or solar term is from local midnight. Within 10 minutes, outside the
library ranges where the computed date is used:

- Japan 1917 autumnal equinox 23:59:44 JST on 23 September (16 s before
  midnight), 1927 vernal equinox 23:58:44 JST on 21 March; 2198 autumnal
  (9 min).
- China 2103 Mid-Autumn (8 min), 2137 Qingming (9), 2144, 2164, 2169, 2175
  Dragon Boat (8, 9, 6, 5), 2170 and 2199 Qingming (5, 5).

The 1910s/1920s cases are within the accuracy of the almanacs of the time
(and of the then-official holidays, which had other names). For 2100–2200,
Delta T (Earth rotation) is uncertain by minutes, so these few dates could
come out a day different in the real future. Independent cross-check with
PyEphem (VSOP87/ELP based, `verify_astro.py --ephem`): 4 date differences in
the whole 1900–2200 range (China Qingming 1943 and 2199, Japan autumnal
2074 and vernal 2187), all within 15 minutes of midnight, where the two
theories and their Delta T models differ; the DE440 result is used, and in
2074 it agrees with the library.

## Table encoding

`generate.py` turns the per-country list of (date, name) into:

1. A shared name table (539 strings, deduplicated across countries, 19 KB
   incl. 16-bit offsets).
2. Rules, derived automatically from the data: a holiday that falls on the
   same key for at least 4 consecutive years becomes one 10-byte rule with a
   year range. Keys: fixed date; Western Easter + n days; Orthodox Easter + n
   days; first given weekday on or after a date (covers "2nd Monday of
   January", "last Monday of May", "Saturday between 20 and 26 June");
   fixed date unless it is a Sunday (Spain). 585 rules in total.
3. Everything else per country-year as explicit days (day of year 9 bits +
   name index 15 bits, 3 bytes): observed days, lunar and equinox dates,
   one-off days. 5,720 entries, 17 KB. Index: a count byte per country-year
   plus a 16-bit start per 16-year block (11.5 KB).
4. A per-year word (602 bytes) with the weekday of 1 January, both Easter
   dates (from `dateutil.easter`, as the library uses) and the leap flag, so
   the runtime needs no division (the GBA's CPU has none).

The generator decodes its own table and asserts it reproduces the library
data before writing. Output is deterministic (no hash-order dependence;
verified byte-identical across runs with different `PYTHONHASHSEED`).
`astro.py` is deterministic too (verified byte-identical on re-run).

## Tests

`tests/holidays/run.sh` (set `HOLIDAYS_PYTHON` to a Python with the
packages; devkitARM in `/opt/devkitpro/devkitARM` or `PATH`):

1. Host build with `-fsanitize=address,undefined -Wall -Wextra -Werror
   -fno-exceptions -fno-rtti`; the test queries every day and month of all
   34 countries × 301 years, checks month counts against day queries, masks,
   truncated queries and bad arguments, and writes a dump.
2. The dump against `tests/holidays/expected_checksums.txt` (SHA-256 per
   country; works without Python).
3. With the library installed: `generate.py --check` (committed files up to
   date) and the dump against the library's own output, line by line.
4. GBA cross-compile (`arm-none-eabi-g++ -O2 -mthumb -mcpu=arm7tdmi`),
   ROM/RAM sizes; fails if static RAM > 1 KB.
5. With `unicorn` and `pyelftools`: `gba_timing.py` links the lookup at
   0x08000000 and runs it in the Unicorn ARM emulator, counting instructions
   and ROM loads; the cycle estimate assumes 4 cycles per instruction and per
   ROM load (non-sequential ROM access with 3 wait states), which is
   pessimistic. Result: worst month query (Japan, May 2200) 1,115
   instructions, 167 ROM loads, ≤ 5,128 cycles ≈ 0.31 ms; worst day query
   ≈ 0.25 ms. Fails above 0.5 ms.

Last result: all checks pass; 122,191 holidays identical to the library.

## Regenerating

```sh
python3 -m venv hv && hv/bin/pip install -r tools/holidays/requirements.txt
# only if the astronomy changes (about 4 minutes):
hv/bin/python tools/holidays/astro.py
hv/bin/python tools/holidays/verify_astro.py          # add --ephem with ephem installed
# the table:
hv/bin/python tools/holidays/generate.py --stats
hv/bin/python tools/holidays/check_sources.py --names # review
HOLIDAYS_PYTHON=hv/bin/python tests/holidays/run.sh
```

Commit `tools/holidays/astro_dates.json`, `src/holidays/holiday_data.c/.h`
and `tests/holidays/expected_checksums.txt` together. A new `holidays`
release can change dates or names (new laws, corrected history, new
translations); review the diff of the generated files before committing.

## Known limitations

- Future years follow today's rules (as published in the library); new laws,
  one-off holidays and future official schedules are not known.
- Years before the library's start year use its earliest rules (see the
  review above), not the historical holidays.
- China: no make-up working days; golden-week extra days only where the
  library has the published schedule (2001–2026).
- Australia: holidays on their own dates, no state substitute days.
- Astronomical dates 2100–2200 within a few minutes of midnight (listed
  above) depend on the Delta T prediction.
- National holidays only; regional holidays are not included.
