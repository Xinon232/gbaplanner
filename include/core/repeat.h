// Repeat rules: daily / weekly / monthly / yearly, every N, weekly on a set of
// weekdays, monthly on day N or on the Nth / last weekday, ends never / until
// a date / after N occurrences. Semantics follow RFC 5545 RRULE as
// python-dateutil's rrule implements them (and Google Calendar uses them):
//   * the start date is an occurrence only if it matches the rule
//     (weekly on sa,su starting on a Friday: first occurrence is Saturday);
//   * weekly with an interval counts weeks Monday..Sunday (WKST=MO) from the
//     week holding the start date;
//   * monthly on day 29/30/31 skips months without that day (no clamping);
//     monthly on the 5th weekday skips months without one;
//   * yearly repeats the start's month and day: a Feb 29 start (also a
//     birthday) occurs only in leap years;
//   * until is inclusive; count counts generated occurrences, and skipped
//     days (EXDATE) still use up the count, as in an RRULE + EXDATE set.
// Occurrences after 2200-12-31 do not exist.
//
// Two global options (Settings, read at query time; nothing in the files
// changes) move occurrences that do not exist in a period instead of
// skipping them:
//   * Feb 29 in other years: a yearly series starting on Feb 29 occurs on
//     Feb 28 or Mar 1 of the other years (same year, same interval);
//   * Day 29-31 in short months: monthly on day N (also "monthly" from a
//     start day N) lands on the month's last day when the month is shorter;
//     monthly on the 5th weekday falls back to the last such weekday.
// A moved day is the occurrence's date (skip / done / count use it); each
// period still has at most one occurrence, so none is lost or added.
#ifndef PLANNER_CORE_REPEAT_H
#define PLANNER_CORE_REPEAT_H

#include "date.h"

namespace planner {

enum class Freq : uint8_t { NONE = 0, DAILY = 1, WEEKLY = 2, MONTHLY = 3, YEARLY = 4 };
enum class MonthlyMode : uint8_t {
  START_DAY = 0,     // "monthly": the start date's day number
  DAY_N = 1,         // "on day 23"
  NTH_WEEKDAY = 2,   // "on 4th fr"  (1st..5th)
  LAST_WEEKDAY = 3,  // "on last fr"
};
enum class EndKind : uint8_t { NEVER = 0, UNTIL = 1, COUNT = 2 };

enum class Feb29 : uint8_t { SKIP = 0, FEB28 = 1, MAR1 = 2 };
enum class ShortMonth : uint8_t { SKIP = 0, LAST_DAY = 1 };
struct RepeatOptions {
  Feb29 feb29 = Feb29::SKIP;
  ShortMonth short_month = ShortMonth::SKIP;
};
// The options in use (default: skip, as RFC 5545). Change them with
// cal_set_repeat_options() (calendar.h) once a calendar is loaded: it also
// updates the precomputed last days of "after N occurrences" series.
extern RepeatOptions g_repeat;
// Day of a yearly occurrence in year y (start month/day m, d), with the
// Feb 29 option applied; NO_DAY when there is none.
int32_t yearly_day_in(int y, int m, int d);

// Full description of a rule (what the entry form edits and the files store),
// without the skip / done day lists.
struct Rule {
  Freq freq = Freq::NONE;
  uint16_t interval = 1;      // 1..MAX_INTERVAL
  uint8_t weekdays = 0;       // weekly: bit 0 = Monday .. bit 6 = Sunday; 0 = the start's weekday
  MonthlyMode mmode = MonthlyMode::START_DAY;
  uint8_t mday = 0;           // DAY_N: 1..31
  uint8_t nth = 0;            // NTH_WEEKDAY: 1..5
  uint8_t mweekday = 0;       // NTH/LAST_WEEKDAY: 0 = Monday .. 6 = Sunday
  EndKind end = EndKind::NEVER;
  int32_t until = NO_DAY;     // UNTIL: last allowed day (inclusive)
  uint16_t count = 0;         // COUNT: 1..MAX_COUNT
};
bool rule_valid(const Rule& r);
bool rule_equal(const Rule& a, const Rule& b);

// Compact "spec" byte: weekly weekday mask, or monthly mode + value.
uint8_t rule_spec(const Rule& r);
void rule_apply_spec(Rule& r, uint8_t spec);  // r.freq must be set

constexpr int MAX_WINDOW = 56;  // window_mask() covers at most 56 days

// Evaluator for one series: start day, rule core and its last possible day
// (until, or the count-th occurrence, or MAX_DAY). All queries are O(1) or a
// few bounded steps; nothing iterates from the start date.
struct Series {
  int32_t start = 0;
  int32_t last = MAX_DAY;
  uint16_t interval = 1;
  uint8_t freq = 0;   // Freq
  uint8_t spec = 0;

  static Series make(const Rule& r, int32_t start);  // computes last (count -> day)
  bool repeats() const { return freq != uint8_t(Freq::NONE); }
  bool occurs(int32_t day) const;
  // First occurrence >= day (NO_DAY if none).
  int32_t next(int32_t day) const;
  // Bit i set = occurrence on w0 + i, for 0 <= i < n <= MAX_WINDOW.
  uint64_t window_mask(int32_t w0, int n) const;
  // The c-th occurrence (1-based), ignoring `last`; NO_DAY if past 2200.
  int32_t nth(uint32_t c) const;
  // Occurrences in [a, b] into out (at most max); returns how many were written.
  int range(int32_t a, int32_t b, int32_t* out, int max) const;
};

// Day of the occurrence of a monthly spec in month index mi (y*12+m-1), or
// NO_DAY (the short-month option applied).
int32_t monthly_day_in(int mi, uint8_t spec, int start_mday);

}  // namespace planner

#endif
