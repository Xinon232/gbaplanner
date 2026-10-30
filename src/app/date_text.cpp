// Month and weekday names, the build date and the clamped date steps of the
// screens; the date math itself is the core's (include/core/date.h).
#include "app/date.h"

namespace app {

namespace {
constexpr const char* months[12] = {"January", "February", "March", "April", "May", "June", "July",
                                    "August", "September", "October", "November", "December"};
constexpr const char* short_days[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
constexpr const char* days[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

}

// The ROM build date; extern "C" so the emulator tests can read it from the ELF.
extern "C" const char planner_build_date[];

Date clamp_date(const Date& d)
{
    if(d.year < YEAR_MIN) return Date{int16_t(YEAR_MIN), 1, 1};
    if(d.year > YEAR_MAX) return Date{int16_t(YEAR_MAX), 12, 31};
    Date r = d;
    if(r.month < 1) r.month = 1;
    if(r.month > 12) r.month = 12;
    if(r.day < 1) r.day = 1;
    const int last = days_in_month(r.year, r.month);
    if(r.day > last) r.day = int8_t(last);
    return r;
}

Date add_days(const Date& d, int n)
{
    int32_t k = day_number(clamp_date(d)) + n;
    if(k < 0) k = 0;
    if(k > LAST_DAY) k = LAST_DAY;
    return from_day_number(k);
}

Date add_months(const Date& d, int n)
{
    int index = d.year * 12 + (d.month - 1) + n;
    if(index < YEAR_MIN * 12) index = YEAR_MIN * 12;
    if(index > YEAR_MAX * 12 + 11) index = YEAR_MAX * 12 + 11;
    Date r{int16_t(index / 12), int8_t(index % 12 + 1), d.day};
    return clamp_date(r);
}

const char* month_name(int m) { return months[(m - 1 + 12) % 12]; }
const char* weekday_short(int wd) { return short_days[(wd % 7 + 7) % 7]; }
const char* weekday_name(int wd) { return days[(wd % 7 + 7) % 7]; }

Date build_date()
{
    // "Oct 23 2026"
    const char* s = planner_build_date;
    static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int m = 1;
    for(int i = 0; i < 12; ++i)
        if(s[0] == names[i * 3] && s[1] == names[i * 3 + 1] && s[2] == names[i * 3 + 2]) m = i + 1;
    const int day = (s[4] == ' ' ? 0 : s[4] - '0') * 10 + (s[5] - '0');
    const int year = (s[7] - '0') * 1000 + (s[8] - '0') * 100 + (s[9] - '0') * 10 + (s[10] - '0');
    return clamp_date(Date{int16_t(year), int8_t(m), int8_t(day)});
}

}
