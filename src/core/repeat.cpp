#include "core/repeat.h"

namespace planner {
namespace {

inline int ctz8(unsigned v) {  // v != 0, < 256
  int i = 0;
  while (!(v & 1u)) { v >>= 1; ++i; }
  return i;
}
inline int popcount8(unsigned v) {
  int n = 0;
  for (; v; v &= v - 1) ++n;
  return n;
}
inline int32_t monday_of(int32_t d) { return d - d % 7; }
inline uint8_t week_mask(const Series& s) {
  return s.spec ? uint8_t(s.spec & 0x7f) : uint8_t(1u << weekday(s.start));
}
inline uint64_t range_mask(int lo, int hi) {  // bits lo..hi inclusive, 0 <= lo <= hi < 64
  const uint64_t upto = hi >= 63 ? ~uint64_t(0) : ((uint64_t(1) << (hi + 1)) - 1);
  return upto & ~((uint64_t(1) << lo) - 1);
}
// Bits 0, k, 2k, ... (k >= 1)
struct StrideTable {
  uint64_t m[64];
  constexpr StrideTable() : m() {
    for (int k = 1; k < 64; ++k)
      for (int i = 0; i < 64; i += k) m[k] |= uint64_t(1) << i;
  }
};
constexpr StrideTable STRIDE;

}  // namespace

PLANNER_EWRAM RepeatOptions g_repeat;

int32_t yearly_day_in(int y, int m, int d) {
  const int32_t od = day_from_ymd(y, m, d);
  if (od != NO_DAY || m != 2 || d != 29 || y < MIN_YEAR || y > MAX_YEAR) return od;
  switch (g_repeat.feb29) {
    case Feb29::FEB28: return day_from_ymd(y, 2, 28);
    case Feb29::MAR1: return day_from_ymd(y, 3, 1);
    default: return NO_DAY;
  }
}

int32_t monthly_day_in(int mi, uint8_t spec, int start_mday) {
  const int y = mi / 12, m = mi % 12 + 1;
  if (y < MIN_YEAR || y > MAX_YEAR) return NO_DAY;
  const int32_t ms = month_start(mi);
  const int dim = days_in_month(y, m);
  const int mode = spec >> 6;
  if (mode <= int(MonthlyMode::DAY_N)) {
    const int d = mode == int(MonthlyMode::START_DAY) ? start_mday : (spec & 31);
    if (d <= dim) return ms + d - 1;
    return g_repeat.short_month == ShortMonth::LAST_DAY ? ms + dim - 1 : NO_DAY;
  }
  const int wd = spec & 7;
  if (mode == int(MonthlyMode::NTH_WEEKDAY)) {
    const int n = (spec >> 3) & 7;
    const int off = (wd - weekday(ms) + 7) % 7 + (n - 1) * 7;
    if (off < dim) return ms + off;
    if (n != 5 || g_repeat.short_month != ShortMonth::LAST_DAY) return NO_DAY;
  }
  const int32_t me = ms + dim - 1;
  return me - (weekday(me) - wd + 7) % 7;
}

uint8_t rule_spec(const Rule& r) {
  switch (r.freq) {
    case Freq::WEEKLY: return uint8_t(r.weekdays & 0x7f);
    case Freq::MONTHLY:
      switch (r.mmode) {
        case MonthlyMode::DAY_N: return uint8_t(0x40 | (r.mday & 31));
        case MonthlyMode::NTH_WEEKDAY: return uint8_t(0x80 | (r.nth & 7) << 3 | (r.mweekday & 7));
        case MonthlyMode::LAST_WEEKDAY: return uint8_t(0xc0 | (r.mweekday & 7));
        default: return 0;
      }
    default: return 0;
  }
}

void rule_apply_spec(Rule& r, uint8_t spec) {
  r.weekdays = 0;
  r.mmode = MonthlyMode::START_DAY;
  r.mday = r.nth = r.mweekday = 0;
  if (r.freq == Freq::WEEKLY) {
    r.weekdays = spec & 0x7f;
  } else if (r.freq == Freq::MONTHLY) {
    r.mmode = MonthlyMode(spec >> 6);
    if (r.mmode == MonthlyMode::DAY_N) r.mday = spec & 31;
    if (r.mmode == MonthlyMode::NTH_WEEKDAY) r.nth = (spec >> 3) & 7;
    if (r.mmode >= MonthlyMode::NTH_WEEKDAY) r.mweekday = spec & 7;
  }
}

bool rule_valid(const Rule& r) {
  if (r.freq == Freq::NONE)
    return r.end == EndKind::NEVER && rule_spec(r) == 0 && r.weekdays == 0 &&
           r.mmode == MonthlyMode::START_DAY;
  if (uint8_t(r.freq) > uint8_t(Freq::YEARLY)) return false;
  if (r.interval < 1 || r.interval > MAX_INTERVAL) return false;
  if (r.freq != Freq::WEEKLY && r.weekdays) return false;
  if (r.weekdays & 0x80) return false;
  if (r.freq != Freq::MONTHLY && r.mmode != MonthlyMode::START_DAY) return false;
  if (r.freq == Freq::MONTHLY) {
    if (r.mmode == MonthlyMode::DAY_N && (r.mday < 1 || r.mday > 31)) return false;
    if (r.mmode == MonthlyMode::NTH_WEEKDAY && (r.nth < 1 || r.nth > 5)) return false;
    if (r.mmode >= MonthlyMode::NTH_WEEKDAY && r.mweekday > 6) return false;
  }
  if (r.end == EndKind::UNTIL && !valid_day(r.until)) return false;
  if (r.end == EndKind::COUNT && (r.count < 1 || r.count > MAX_COUNT)) return false;
  if (uint8_t(r.end) > 2) return false;
  return true;
}

bool rule_equal(const Rule& a, const Rule& b) {
  if (a.freq != b.freq) return false;
  if (a.freq == Freq::NONE) return true;
  return a.interval == b.interval && rule_spec(a) == rule_spec(b) && a.end == b.end &&
         (a.end != EndKind::UNTIL || a.until == b.until) &&
         (a.end != EndKind::COUNT || a.count == b.count);
}

Series Series::make(const Rule& r, int32_t start) {
  Series s;
  s.start = start;
  s.freq = uint8_t(r.freq);
  s.interval = r.freq == Freq::NONE ? 1 : r.interval;
  s.spec = rule_spec(r);
  s.last = MAX_DAY;
  if (r.freq == Freq::NONE) {
    s.last = start;
  } else if (r.end == EndKind::UNTIL) {
    s.last = r.until < MAX_DAY ? r.until : MAX_DAY;
  } else if (r.end == EndKind::COUNT) {
    const int32_t d = s.nth(r.count);
    s.last = d == NO_DAY ? MAX_DAY : d;
  }
  return s;
}

bool Series::occurs(int32_t d) const {
  if (d < start || d > last) return false;
  switch (Freq(freq)) {
    case Freq::NONE: return d == start;
    case Freq::DAILY: return interval == 1 || (d - start) % interval == 0;
    case Freq::WEEKLY: {
      if (!(week_mask(*this) >> weekday(d) & 1)) return false;
      return interval == 1 || ((monday_of(d) - monday_of(start)) / 7) % interval == 0;
    }
    case Freq::MONTHLY: {
      const Ymd sv = ymd_from_day(start);
      const int mi = month_index(d);
      const int rel = mi - (sv.y * 12 + sv.m - 1);
      if (rel < 0 || (interval > 1 && rel % interval)) return false;
      return monthly_day_in(mi, spec, sv.d) == d;
    }
    case Freq::YEARLY: {
      const Ymd sv = ymd_from_day(start), dv = ymd_from_day(d);
      if (interval > 1 && (dv.y - sv.y) % interval) return false;
      return yearly_day_in(dv.y, sv.m, sv.d) == d;
    }
  }
  return false;
}

int32_t Series::next(int32_t d) const {
  if (d < start) d = start;
  if (d > last) return NO_DAY;
  int32_t r = NO_DAY;
  switch (Freq(freq)) {
    case Freq::NONE: r = d == start ? d : NO_DAY; break;
    case Freq::DAILY: {
      const int32_t rem = (d - start) % interval;
      r = rem ? d + (interval - rem) : d;
      break;
    }
    case Freq::WEEKLY: {
      const uint8_t wm = week_mask(*this);
      const int32_t m0 = monday_of(start);
      int32_t rel = (monday_of(d) - m0) / 7;
      int from = weekday(d);
      const int32_t rem = rel % interval;
      if (rem) { rel += interval - rem; from = 0; }
      for (int guard = 0; guard < 2; ++guard) {
        const unsigned bits = wm >> from << from;
        if (bits) { r = m0 + rel * 7 + ctz8(bits); break; }
        rel += interval;
        from = 0;
      }
      break;
    }
    case Freq::MONTHLY: {
      const Ymd sv = ymd_from_day(start);
      const int ms = sv.y * 12 + sv.m - 1;
      int rel = month_index(d) - ms;
      const int rem = rel % interval;
      if (rem) rel += interval - rem;
      for (;; rel += interval) {
        const int mi = ms + rel;
        if (mi > MAX_YEAR * 12 + 11 || month_start(mi) > last) break;
        const int32_t od = monthly_day_in(mi, spec, sv.d);
        if (od != NO_DAY && od >= d) { r = od; break; }
      }
      break;
    }
    case Freq::YEARLY: {
      const Ymd sv = ymd_from_day(start);
      const Ymd dv = ymd_from_day(d);
      int rel = dv.y - sv.y;
      const int rem = rel % interval;
      if (rem) rel += interval - rem;
      for (;; rel += interval) {
        const int y = sv.y + rel;
        if (y > MAX_YEAR) break;
        const int32_t od = yearly_day_in(y, sv.m, sv.d);
        if (od != NO_DAY && od >= d) { r = od; break; }
        if (year_start(y) > last) break;
      }
      break;
    }
  }
  return r != NO_DAY && r <= last ? r : NO_DAY;
}

int32_t Series::nth(uint32_t c) const {
  if (c < 1) return NO_DAY;
  int64_t r = NO_DAY;
  switch (Freq(freq)) {
    case Freq::NONE: return c == 1 ? start : NO_DAY;
    case Freq::DAILY: r = int64_t(start) + int64_t(c - 1) * interval; break;
    case Freq::WEEKLY: {
      const unsigned wm = week_mask(*this);
      const int ws = weekday(start);
      const unsigned first = wm >> ws << ws;
      const uint32_t c0 = uint32_t(popcount8(first));
      const uint32_t pc = uint32_t(popcount8(wm));
      const int32_t m0 = monday_of(start);
      uint32_t idx;  // index of the bit within its week
      int64_t week;
      unsigned bits;
      if (c <= c0) {
        week = 0; idx = c - 1; bits = first;
      } else {
        const uint32_t k = c - c0 - 1;
        week = int64_t(k / pc + 1) * interval; idx = k % pc; bits = wm;
      }
      while (idx--) bits &= bits - 1;
      r = m0 + week * 7 + ctz8(bits);
      break;
    }
    case Freq::MONTHLY: {
      const Ymd sv = ymd_from_day(start);
      const int mode = spec >> 6;
      const int dm = mode == 0 ? sv.d : (spec & 31);
      if (mode <= 1 && dm <= 28) {
        const int64_t mi = int64_t(sv.y) * 12 + sv.m - 1 + (int64_t(c) - 1 + (dm < sv.d ? 1 : 0)) * interval;
        if (mi > MAX_YEAR * 12 + 11) return NO_DAY;
        r = month_start(int(mi)) + dm - 1;
        break;
      }
      // Months may be skipped: bounded walk (at most 3612 months in 1900..2200).
      int32_t d = start;
      for (uint32_t i = 0; i < c; ++i) {
        Series open = *this;
        open.last = MAX_DAY;
        d = open.next(i ? d + 1 : d);
        if (d == NO_DAY) return NO_DAY;
      }
      return d;
    }
    case Freq::YEARLY: {
      const Ymd sv = ymd_from_day(start);
      if (!(sv.m == 2 && sv.d == 29)) {
        const int64_t y = sv.y + int64_t(c - 1) * interval;
        if (y > MAX_YEAR) return NO_DAY;
        return day_from_ymd(int(y), sv.m, sv.d);
      }
      int32_t d = start;
      for (uint32_t i = 0; i < c; ++i) {
        Series open = *this;
        open.last = MAX_DAY;
        d = open.next(i ? d + 1 : d);
        if (d == NO_DAY) return NO_DAY;
      }
      return d;
    }
  }
  return r > MAX_DAY ? NO_DAY : int32_t(r);
}

uint64_t Series::window_mask(int32_t w0, int n) const {
  int32_t lo = w0 > start ? w0 : start;
  int32_t hi = w0 + n - 1 < last ? w0 + n - 1 : last;
  if (lo > hi) return 0;
  const int a = int(lo - w0), b = int(hi - w0);
  const uint64_t rm = range_mask(a, b);
  switch (Freq(freq)) {
    case Freq::NONE: return uint64_t(1) << (start - w0);  // lo == hi == start
    case Freq::DAILY: {
      if (interval == 1) return rm;
      const int32_t rem = (lo - start) % interval;
      const int32_t f = rem ? lo + (interval - rem) : lo;
      if (f > hi) return 0;
      if (interval >= 64) return uint64_t(1) << (f - w0);
      return (STRIDE.m[interval] << (f - w0)) & rm;
    }
    case Freq::WEEKLY: {
      const uint64_t wm = week_mask(*this);
      if (interval == 1) {
        const uint64_t rep = wm * 0x8102040810204081ull;  // mask repeated every 7 bits
        return (rep >> weekday(w0)) & rm;
      }
      const int32_t m0 = monday_of(start);
      int32_t m = monday_of(lo);
      int32_t rel = (m - m0) / 7;
      int32_t rem = rel % interval;
      uint64_t out = 0;
      for (; m <= hi; m += 7, ++rem) {
        if (rem == interval) rem = 0;
        if (rem) continue;
        const int32_t off = m - w0;
        out |= off >= 0 ? wm << off : wm >> -off;
      }
      return out & rm;
    }
    case Freq::MONTHLY: {
      const Ymd sv = ymd_from_day(start);
      const int ms = sv.y * 12 + sv.m - 1;
      const int mlo = month_index(lo), mhi = month_index(hi);
      uint64_t out = 0;
      for (int mi = mlo; mi <= mhi; ++mi) {
        const int rel = mi - ms;
        if (rel < 0 || (interval > 1 && rel % interval)) continue;
        const int32_t od = monthly_day_in(mi, spec, sv.d);
        if (od >= lo && od <= hi) out |= uint64_t(1) << (od - w0);
      }
      return out;
    }
    case Freq::YEARLY: {
      const Ymd sv = ymd_from_day(start);
      const Ymd vlo = ymd_from_day(lo), vhi = ymd_from_day(hi);
      uint64_t out = 0;
      for (int y = vlo.y; y <= vhi.y; ++y) {
        const int rel = y - sv.y;
        if (rel < 0 || (interval > 1 && rel % interval)) continue;
        const int32_t od = yearly_day_in(y, sv.m, sv.d);
        if (od != NO_DAY && od >= lo && od <= hi) out |= uint64_t(1) << (od - w0);
      }
      return out;
    }
  }
  return 0;
}

int Series::range(int32_t a, int32_t b, int32_t* out, int max) const {
  int n = 0;
  if (b > last) b = last;
  for (int32_t d = next(a); d != NO_DAY && d <= b && n < max; d = next(d + 1)) out[n++] = d;
  return n;
}

}  // namespace planner
