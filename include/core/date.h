// Gregorian date math for 1900-01-01 .. 2200-12-31.
// A "day" is the number of days since 1900-01-01 (day 0, a Monday).
#ifndef PLANNER_CORE_DATE_H
#define PLANNER_CORE_DATE_H

#include "planner_config.h"

namespace planner {

constexpr int MIN_YEAR = 1900;
constexpr int MAX_YEAR = 2200;
constexpr int32_t MIN_DAY = 0;          // 1900-01-01
constexpr int32_t MAX_DAY = 109937;     // 2200-12-31
constexpr int32_t NO_DAY = -1;

struct Ymd {
  int16_t y;
  int8_t m;  // 1..12
  int8_t d;  // 1..31
};

constexpr bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
constexpr int days_in_month(int y, int m) {
  return m == 2 ? (is_leap(y) ? 29 : 28) : (m == 4 || m == 6 || m == 9 || m == 11) ? 30 : 31;
}
inline bool valid_day(int32_t day) { return day >= MIN_DAY && day <= MAX_DAY; }

// Day number of the first day of a year (1900..2201; 2201 = one past the end).
int32_t year_start(int y);
// y/m/d -> day; NO_DAY when invalid or outside 1900..2200.
int32_t day_from_ymd(int y, int m, int d);
// day -> y/m/d (day must be valid).
Ymd ymd_from_day(int32_t day);
// 0 = Monday .. 6 = Sunday (day % 7 without a division: valid for day < 2^20).
inline int weekday(int32_t day) {
  const uint32_t u = uint32_t(day);
  return int(u - 7u * uint32_t((uint64_t(u) * 149797u) >> 20));
}
// Month index (y*12 + m-1) of a day, and the first day of a month index.
int month_index(int32_t day);
int32_t month_start(int month_idx);
// Adds months keeping the day number, clamped to the month's last day
// (31 Jan + 1 month = 28/29 Feb). NO_DAY when the result leaves 1900..2200.
int32_t add_months(int32_t day, int months);
int32_t add_years(int32_t day, int years);

// ISO "YYYY-MM-DD" (exactly 10 characters).
bool parse_iso(const char* s, int len, int32_t& day);
int format_iso(int32_t day, char* out);  // writes 10 chars + NUL, returns 10

enum class DateFormat : uint8_t { DMY_DOT = 0, MDY_SLASH = 1, YMD_DASH = 2 };  // DD.MM.YYYY / MM/DD/YYYY / YYYY-MM-DD
enum class TimeFormat : uint8_t { H24 = 0, H12 = 1 };                          // 17:30 / 5:30 PM

int format_date(int32_t day, DateFormat f, char* out);  // 10 chars + NUL
// minutes 0..1439. 24 h: "07:05"; 12 h: "7:05 AM", "12:00 PM" (noon), "12:30 AM".
int format_time(int minutes, TimeFormat f, char* out);   // up to 8 chars + NUL
// "HH:MM" (exactly 5 chars, 00:00..23:59) -> minutes.
bool parse_hhmm(const char* s, int len, int& minutes);
int format_hhmm(int minutes, char* out);                 // "HH:MM" + NUL

}  // namespace planner

#endif
