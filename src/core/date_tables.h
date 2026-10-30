// Calendar tables shared by date.cpp and the IWRAM month-marks kernel, and
// division-free day -> (year, month, day) / weekday.
#ifndef PLANNER_CORE_DATE_TABLES_H
#define PLANNER_CORE_DATE_TABLES_H

#include "core/date.h"

namespace planner {

struct YearTable {
  int32_t start[MAX_YEAR - MIN_YEAR + 2];  // first day of each year, 1900..2201
  constexpr YearTable() : start() {
    int32_t d = 0;
    for (int y = MIN_YEAR; y <= MAX_YEAR + 1; ++y) {
      start[y - MIN_YEAR] = d;
      d += is_leap(y) ? 366 : 365;
    }
  }
};
// (month << 8 | day) for each day of the year, non-leap / leap.
struct MonthDayTable {
  uint16_t md[2][366];
  constexpr MonthDayTable() : md() {
    for (int leap = 0; leap < 2; ++leap) {
      int doy = 0;
      for (int m = 1; m <= 12; ++m) {
        const int dim = m == 2 ? 28 + leap : (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
        for (int d = 1; d <= dim; ++d) md[leap][doy++] = uint16_t(m << 8 | d);
      }
      if (!leap) md[0][365] = uint16_t(13 << 8 | 1);  // never used
    }
  }
};
extern const YearTable YEARS;
extern const MonthDayTable MONTH_DAYS;
// Days before each month (non-leap / leap).
extern const int16_t CUM_DAYS[2][13];

// day / 7 for 0 <= day < 2^20 (multiply instead of a division).
inline uint32_t div7(uint32_t day) { return uint32_t((uint64_t(day) * 149797u) >> 20); }
inline int fast_weekday(int32_t day) { return int(uint32_t(day) - 7 * div7(uint32_t(day))); }

inline void fast_ymd(int32_t day, int& y, int& m, int& d, bool& leap) {
  int yi = int((uint32_t(day) * 2871u) >> 20);  // ~ day / 365.2425
  if (yi > MAX_YEAR - MIN_YEAR) yi = MAX_YEAR - MIN_YEAR;
  if (YEARS.start[yi] > day) --yi;
  else if (YEARS.start[yi + 1] <= day) ++yi;
  y = MIN_YEAR + yi;
  leap = YEARS.start[yi + 1] - YEARS.start[yi] == 366;
  const uint16_t md = MONTH_DAYS.md[leap][day - YEARS.start[yi]];
  m = md >> 8;
  d = md & 0xFF;
}

}  // namespace planner

#endif
