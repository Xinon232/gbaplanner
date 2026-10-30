#include "date_tables.h"

namespace planner {

constexpr YearTable YEARS;
constexpr MonthDayTable MONTH_DAYS;
constexpr int16_t CUM_DAYS[2][13] = {{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
                                     {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}};
static_assert(YEARS.start[MAX_YEAR - MIN_YEAR + 1] == MAX_DAY + 1, "day range");

namespace {

constexpr const int16_t (&CUM)[2][13] = CUM_DAYS;

int put2(char* o, int v) {
  o[0] = char('0' + v / 10);
  o[1] = char('0' + v % 10);
  return 2;
}
int put4(char* o, int v) {
  o[0] = char('0' + v / 1000);
  o[1] = char('0' + v / 100 % 10);
  o[2] = char('0' + v / 10 % 10);
  o[3] = char('0' + v % 10);
  return 4;
}
bool digits(const char* s, int n, int& v) {
  v = 0;
  for (int i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  return true;
}

}  // namespace

int32_t year_start(int y) {
  if (y < MIN_YEAR) return 0;
  if (y > MAX_YEAR + 1) y = MAX_YEAR + 1;
  return YEARS.start[y - MIN_YEAR];
}

int32_t day_from_ymd(int y, int m, int d) {
  if (y < MIN_YEAR || y > MAX_YEAR || m < 1 || m > 12 || d < 1) return NO_DAY;
  const int yi = y - MIN_YEAR;
  const int leap = YEARS.start[yi + 1] - YEARS.start[yi] == 366;
  if (d > CUM[leap][m] - CUM[leap][m - 1]) return NO_DAY;
  return YEARS.start[yi] + CUM[leap][m - 1] + d - 1;
}

Ymd ymd_from_day(int32_t day) {
  int y, m, d;
  bool leap;
  fast_ymd(day, y, m, d, leap);
  return Ymd{int16_t(y), int8_t(m), int8_t(d)};
}

int month_index(int32_t day) {
  const Ymd v = ymd_from_day(day);
  return v.y * 12 + v.m - 1;
}

int32_t month_start(int mi) {
  const int y = mi / 12, m = mi % 12 + 1;
  if (y < MIN_YEAR) return NO_DAY;
  if (y > MAX_YEAR) return MAX_DAY + 1;
  return YEARS.start[y - MIN_YEAR] + CUM[is_leap(y)][m - 1];
}

int32_t add_months(int32_t day, int months) {
  if (!valid_day(day)) return NO_DAY;
  const Ymd v = ymd_from_day(day);
  const int mi = v.y * 12 + (v.m - 1) + months;
  if (mi < MIN_YEAR * 12 || mi > MAX_YEAR * 12 + 11) return NO_DAY;
  const int y = mi / 12, m = mi % 12 + 1;
  const int dim = days_in_month(y, m);
  return day_from_ymd(y, m, v.d > dim ? dim : v.d);
}

int32_t add_years(int32_t day, int years) { return add_months(day, years * 12); }

bool parse_iso(const char* s, int len, int32_t& day) {
  int y, m, d;
  if (len != 10 || s[4] != '-' || s[7] != '-' || !digits(s, 4, y) || !digits(s + 5, 2, m) ||
      !digits(s + 8, 2, d))
    return false;
  day = day_from_ymd(y, m, d);
  return day != NO_DAY;
}

int format_iso(int32_t day, char* out) { return format_date(day, DateFormat::YMD_DASH, out); }

int format_date(int32_t day, DateFormat f, char* out) {
  const Ymd v = ymd_from_day(day);
  char* o = out;
  switch (f) {
    case DateFormat::DMY_DOT:
      o += put2(o, v.d); *o++ = '.'; o += put2(o, v.m); *o++ = '.'; o += put4(o, v.y);
      break;
    case DateFormat::MDY_SLASH:
      o += put2(o, v.m); *o++ = '/'; o += put2(o, v.d); *o++ = '/'; o += put4(o, v.y);
      break;
    default:
      o += put4(o, v.y); *o++ = '-'; o += put2(o, v.m); *o++ = '-'; o += put2(o, v.d);
      break;
  }
  *o = 0;
  return int(o - out);
}

int format_hhmm(int minutes, char* out) {
  put2(out, minutes / 60);
  out[2] = ':';
  put2(out + 3, minutes % 60);
  out[5] = 0;
  return 5;
}

int format_time(int minutes, TimeFormat f, char* out) {
  if (f == TimeFormat::H24) return format_hhmm(minutes, out);
  const int h24 = minutes / 60;
  int h = h24 % 12;
  if (h == 0) h = 12;
  char* o = out;
  if (h >= 10) *o++ = '1';
  *o++ = char('0' + h % 10);
  *o++ = ':';
  o += put2(o, minutes % 60);
  *o++ = ' ';
  *o++ = h24 < 12 ? 'A' : 'P';
  *o++ = 'M';
  *o = 0;
  return int(o - out);
}

bool parse_hhmm(const char* s, int len, int& minutes) {
  int h, m;
  if (len != 5 || s[2] != ':' || !digits(s, 2, h) || !digits(s + 3, 2, m) || h > 23 || m > 59)
    return false;
  minutes = h * 60 + m;
  return true;
}

const char* err_name(Err e) {
  switch (e) {
    case Err::OK: return "OK";
    case Err::IO: return "IO";
    case Err::DISK_FULL: return "DISK_FULL";
    case Err::NOT_FOUND: return "NOT_FOUND";
    case Err::CORRUPT: return "CORRUPT";
    case Err::VERIFY: return "VERIFY";
    case Err::RECOVERY_NEEDED: return "RECOVERY_NEEDED";
    case Err::FULL: return "FULL";
    case Err::INVALID: return "INVALID";
    case Err::TOO_LARGE: return "TOO_LARGE";
    case Err::NOT_LOADED: return "NOT_LOADED";
    case Err::NEEDS_RELOAD: return "NEEDS_RELOAD";
  }
  return "?";
}

}  // namespace planner
