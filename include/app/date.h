#pragma once
// Dates for the screens: a y/m/d value type over the data core's date math
// (include/core/date.h, the one date module of the app; 1900-2200, day
// numbers = days since 1900-01-01, a Monday). Only names and the build date
// are the app's own (src/app/date_text.cpp).
#include <cstdint>
#include "core/date.h"

namespace app {

constexpr int YEAR_MIN = planner::MIN_YEAR, YEAR_MAX = planner::MAX_YEAR;
constexpr int32_t LAST_DAY = planner::MAX_DAY;   // 2200-12-31

struct Date {
    int16_t year = 2026;
    int8_t month = 1;   // 1-12
    int8_t day = 1;     // 1-31
    bool operator==(const Date& o) const { return year == o.year && month == o.month && day == o.day; }
    bool operator!=(const Date& o) const { return !(*this == o); }
};

constexpr bool leap_year(int y) { return planner::is_leap(y); }
constexpr int days_in_month(int y, int m) { return planner::days_in_month(y, m); }
inline bool valid_date(const Date& d) { return planner::day_from_ymd(d.year, d.month, d.day) != planner::NO_DAY; }
// Days since 1900-01-01 and back (d must be valid; n in 0 .. LAST_DAY).
inline int32_t day_number(const Date& d) { return planner::day_from_ymd(d.year, d.month, d.day); }
inline Date from_day_number(int32_t n)
{
    const planner::Ymd v = planner::ymd_from_day(n);
    return Date{v.y, v.m, v.d};
}
// 0 = Monday ... 6 = Sunday.
inline int weekday(const Date& d) { return planner::weekday(day_number(d)); }
Date clamp_date(const Date& d);
// d + n days, kept within 1900-01-01 .. 2200-12-31.
Date add_days(const Date& d, int n);
// Same day number in another month (clamped to its length) and year range.
Date add_months(const Date& d, int n);

const char* month_name(int m);          // "January"
const char* weekday_short(int wd);      // "Mo" (0 = Monday)
const char* weekday_name(int wd);       // "Monday"

// The date the ROM was built (__DATE__), the first cursor day.
Date build_date();

}
