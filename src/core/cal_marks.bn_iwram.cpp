// Month marks: the hot query of the calendar screen. Built as ARM code in
// IWRAM by Butano (*.bn_iwram.cpp). One pass over the index, which is kept
// sorted in display order, so every day takes its first 8 items as they come.
//
// Per call, small tables map "day of the year" (yearly rules) and "day N /
// Nth weekday / last weekday of month k" (monthly rules) to a bit of the
// window. Per entry the work is then a few table lookups and mask operations,
// no divisions (except for intervals > 1), and one-day entries read only one
// word of their index entry. Occurrences become one 64-bit mask per entry;
// the masks are counted with bit-sliced counters. Results equal
// Series::window_mask + skipped days (the tests compare them).
#include "cal_internal.h"
#include "date_tables.h"

namespace planner {

// Monthly series that started before the window, interval 1: window mask by
// spec byte (index 1..31 = START_DAY with that day number). Rebuilt per call.
PLANNER_EWRAM uint64_t g_mmask[256];

namespace {

constexpr int8_t NO_BIT = -1;

struct Window {
  int32_t first, last;
  int n, wd0;
  // yearly: leap-style day of the year (Jan 1 = 0, Feb 29 = 59, Dec 31 = 365) -> bit
  int8_t ybit[366];
  int32_t year2_first;  // first day of the second year in the window (or > last)
  int y1;               // year of the first window day
  // monthly, per window month k
  int nmon;
  int mi[3];
  int32_t mend[3];        // last day of the month
  int8_t dbit[3][32];     // day N (1..31)
  int8_t nbit[3][5][7];   // Nth (1..5) weekday
  int8_t lbit[3][7];      // last weekday
  int32_t month0_first;   // first day of the first window month
  uint64_t whole;         // bits 0 .. n-1
};



constexpr uint8_t DEBRUIJN[32] = {0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
                                   31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9};
[[gnu::always_inline]] inline int ctz32(uint32_t v) { return DEBRUIJN[((v & -v) * 0x077CB531u) >> 27]; }
[[gnu::always_inline]] inline int ctz64(uint64_t v) {
  const uint32_t lo = uint32_t(v);
  return lo ? ctz32(lo) : 32 + ctz32(uint32_t(v >> 32));
}
// Adds k * mask to the bit-sliced counters.
[[gnu::always_inline]] inline void add_run(uint64_t* plane, int planes, uint64_t mask, uint32_t k) {
  for (int j = 0; k && j < planes; ++j, k >>= 1) {
    if (!(k & 1)) continue;
    uint64_t carry = mask;
    for (int p = j; carry && p < planes; ++p) {
      const uint64_t t = plane[p] & carry;
      plane[p] ^= carry;
      carry = t;
    }
  }
}
[[gnu::always_inline]] inline uint64_t span(int a, int b) {  // bits a..b, 0 <= a <= b <= 63
  const uint64_t upto = b >= 63 ? ~uint64_t(0) : ((uint64_t(1) << (b + 1)) - 1);
  return upto & ~((uint64_t(1) << a) - 1);
}
[[gnu::always_inline]] inline bool leap_year(int y) {
  return (y & 3) == 0 && (y % 100 != 0 || y % 400 == 0);  // constant divisors: no division call
}
// Year and leap-style day of the year of a day.
[[gnu::always_inline]] inline void year_doy(int32_t day, int& y, int& ldoy) {
  int yi = int((uint32_t(day) * 2871u) >> 20);
  if (yi > MAX_YEAR - MIN_YEAR) yi = MAX_YEAR - MIN_YEAR;
  int32_t ys = YEARS.start[yi];
  if (ys > day) {
    --yi;
    ys = YEARS.start[yi];
  } else if (YEARS.start[yi + 1] <= day) {
    ++yi;
    ys = YEARS.start[yi];
  }
  y = MIN_YEAR + yi;
  const int doy = int(day - ys);
  ldoy = doy + (doy >= 59 && !leap_year(y));
}

struct StrideTable {
  uint64_t m[64];
  constexpr StrideTable() : m() {
    for (int k = 1; k < 64; ++k)
      for (int i = 0; i < 64; i += k) m[k] |= uint64_t(1) << i;
  }
};
constexpr StrideTable STRIDE;

[[gnu::always_inline]] inline int8_t bit_of(int32_t day, int32_t first, int32_t last) {
  return day >= first && day <= last ? int8_t(day - first) : NO_BIT;
}

PLANNER_IWRAM_LOAD void prepare(Window& w, int32_t first, int32_t last) {
  w.first = first;
  w.last = last;
  w.n = int(last - first + 1);
  w.wd0 = fast_weekday(first);
  w.whole = w.n >= 64 ? ~uint64_t(0) : (uint64_t(1) << w.n) - 1;
  for (int i = 0; i < 366; ++i) w.ybit[i] = NO_BIT;
  int y, ld;
  year_doy(first, y, ld);
  w.y1 = y;
  w.year2_first = y < MAX_YEAR ? YEARS.start[y + 1 - MIN_YEAR] : MAX_DAY + 1;
  for (int b = 0; b < w.n; ++b) {
    year_doy(first + b, y, ld);
    w.ybit[ld] = int8_t(b);
  }
  // Feb 29 series in a common year: Feb 28 (leap-style day 58) or Mar 1
  // (day 60) of that year, per the global option (repeat.h).
  if (w.ybit[59] == NO_BIT && g_repeat.feb29 != Feb29::SKIP) {
    const int alt = g_repeat.feb29 == Feb29::FEB28 ? 58 : 60;
    const int b = w.ybit[alt];
    if (b != NO_BIT) {
      const int yb = first + b >= w.year2_first ? w.y1 + 1 : w.y1;
      if (!leap_year(yb)) w.ybit[59] = int8_t(b);
    }
  }
  const bool to_last = g_repeat.short_month == ShortMonth::LAST_DAY;
  // months
  int my, mm, md;
  bool leap;
  fast_ymd(first, my, mm, md, leap);
  int32_t ms = first - (md - 1);
  w.month0_first = ms;
  w.nmon = 0;
  while (ms <= last && w.nmon < 3) {
    const int k = w.nmon++;
    const int dim = CUM_DAYS[leap][mm] - CUM_DAYS[leap][mm - 1];
    w.mi[k] = my * 12 + mm - 1;
    w.mend[k] = ms + dim - 1;
    const int8_t end_bit = to_last ? bit_of(ms + dim - 1, first, last) : NO_BIT;  // day 29-31 option
    for (int d = 1; d <= 31; ++d) w.dbit[k][d] = d <= dim ? bit_of(ms + d - 1, first, last) : end_bit;
    w.dbit[k][0] = NO_BIT;
    const int wdf = fast_weekday(ms), wdl = fast_weekday(ms + dim - 1);
    for (int wd = 0; wd < 7; ++wd) {
      int lo = wdl - wd;
      if (lo < 0) lo += 7;
      w.lbit[k][wd] = bit_of(ms + dim - 1 - lo, first, last);
      int off = wd - wdf;
      if (off < 0) off += 7;
      for (int nth = 0; nth < 5; ++nth) {
        const int o = off + 7 * nth;
        // No 5th weekday: the last one with the short-month option.
        w.nbit[k][nth][wd] = o < dim ? bit_of(ms + o, first, last) : (to_last && nth == 4 ? w.lbit[k][wd] : NO_BIT);
      }
    }
    ms += dim;
    if (++mm > 12) {
      mm = 1;
      if (++my > MAX_YEAR) break;
      leap = leap_year(my);
    }
  }
  for (int i = 0; i < 256; ++i) {
    const uint32_t mode = uint32_t(i) >> 6;
    uint64_t m = 0;
    for (int k = 0; k < w.nmon; ++k) {
      int b = NO_BIT;
      if (mode == 0) b = i >= 1 && i <= 31 ? w.dbit[k][i] : NO_BIT;
      else if (mode == 1) b = (i & 31) ? w.dbit[k][i & 31] : NO_BIT;
      else if (mode == 2) b = ((i >> 3) & 7) >= 1 && ((i >> 3) & 7) <= 5 && (i & 7) < 7 ? w.nbit[k][((i >> 3) & 7) - 1][i & 7] : NO_BIT;
      else b = (i & 7) < 7 ? w.lbit[k][i & 7] : NO_BIT;
      if (b >= 0) m |= uint64_t(1) << b;
    }
    g_mmask[i] = m;
  }
}

// Occurrences of a repeating entry in the window (skipped days removed).
[[gnu::always_inline]] inline uint64_t repeat_mask(const Window& w, const Rec& r, int32_t s, uint32_t w0,
                                                    const ExtRec* e) {
  const int32_t first = w.first, last = w.last;
  const uint32_t freq = (w0 >> 17) & 7;
  uint32_t ival = ((r.w2 >> 23) & 0xFF) + 1;
  int32_t until = MAX_DAY;
  if (e) {
    until = ext_last(*e);
    if (ext_interval(*e)) ival = uint32_t(ext_interval(*e));
  }
  const int32_t lo = s > first ? s : first;
  const int32_t hi = until < last ? until : last;
  if (lo > hi) return 0;
  const uint32_t spec = w0 >> 24;
  const bool covers = s <= first && until >= last;  // the whole window
  uint64_t mask = 0;
  switch (freq) {
    case 1: {  // daily
      if (ival == 1) {
        mask = covers ? w.whole : span(int(lo - first), int(hi - first));
        break;
      }
      const uint32_t rem = uint32_t(lo - s) % ival;
      const int32_t f = rem ? lo + int32_t(ival - rem) : lo;
      if (f > hi) break;
      mask = ival >= 64 ? uint64_t(1) << (f - first)
                        : (STRIDE.m[ival] << (f - first)) & span(int(lo - first), int(hi - first));
      break;
    }
    case 2: {  // weekly
      const uint64_t wm = spec ? (spec & 0x7F) : (1u << fast_weekday(s));
      if (ival == 1) {
        mask = ((wm * 0x8102040810204081ull) >> w.wd0) & (covers ? w.whole : span(int(lo - first), int(hi - first)));
        break;
      }
      const int32_t mon_s = s - fast_weekday(s);
      int32_t m = lo - fast_weekday(lo);
      uint32_t rem = div7(uint32_t(m - mon_s)) % ival;
      for (; m <= hi; m += 7) {
        if (rem == 0) {
          const int32_t off = m - first;
          mask |= off >= 0 ? wm << off : wm >> -off;
        }
        if (++rem == ival) rem = 0;
      }
      mask &= span(int(lo - first), int(hi - first));
      break;
    }
    case 3: {  // monthly
      const uint32_t mode = spec >> 6;
      if (ival == 1 && s < w.month0_first) {  // table: one lookup
        uint32_t key = spec;
        if (mode == 0) {
          int sy, sm, sd;
          bool sl;
          fast_ymd(s, sy, sm, sd, sl);
          key = uint32_t(sd);
        }
        mask = g_mmask[key];
        if (until < last) mask &= span(0, int(hi - first));
        break;
      }
      int dd = int(spec & 31);
      int sidx = 0;
      if (mode == 0 || ival > 1) {
        int sy, sm, sd;
        bool sl;
        fast_ymd(s, sy, sm, sd, sl);
        sidx = sy * 12 + sm - 1;
        if (mode == 0) dd = sd;
      }
      const int blo = int(lo - first), bhi = int(hi - first);
      for (int k = 0; k < w.nmon; ++k) {
        if (s > w.mend[k]) continue;  // the series starts after this month
        if (ival > 1 && uint32_t(w.mi[k] - sidx) % ival) continue;
        const int b = mode <= 1 ? w.dbit[k][dd]
                      : mode == 2 ? w.nbit[k][((spec >> 3) & 7) - 1][spec & 7]
                                  : w.lbit[k][spec & 7];
        if (b >= blo && b <= bhi) mask |= uint64_t(1) << b;
      }
      break;
    }
    default: {  // yearly
      int sy, ld;
      year_doy(s, sy, ld);
      const int b = w.ybit[ld];
      if (b < 0) break;
      const int y = first + b >= w.year2_first ? w.y1 + 1 : w.y1;
      const int rel = y - sy;
      if (rel < 0 || (ival > 1 && uint32_t(rel) % ival)) break;
      const int32_t od = first + b;
      if (od >= lo && od <= hi) mask = uint64_t(1) << b;
      break;
    }
  }
  if (!mask) return 0;
  if (e && e->nskip) {
    const uint8_t* sk = ext_skip(*e);
    for (int k = dates_lower_bound(sk, e->nskip, first); k < e->nskip; ++k) {
      const int32_t d = int32_t(rd24(sk + 3 * k));
      if (d > last) break;
      mask &= ~(uint64_t(1) << (d - first));
    }
  }
  return mask;
}

}  // namespace

PLANNER_IWRAM_CODE void cal_marks(int32_t first, int ndays, DayMarks* out) {
  if (ndays > MAX_WINDOW) ndays = MAX_WINDOW;
  if (first < 0) first = 0;
  int32_t last = first + ndays - 1;
  if (last > MAX_DAY) last = MAX_DAY;
  if (ndays < 1 || first > last) return;
  for (int d = 0; d < ndays; ++d) {
    out[d].total = 0;
    out[d].shown = 0;
  }
  Window w;
  prepare(w, first, last);
  constexpr int PLANES = 14;  // counts up to 16383 per day
  uint64_t plane[PLANES] = {};
  uint16_t single[MAX_WINDOW] = {};
  uint64_t full = 0;
  uint64_t run_mask = 0;  // consecutive entries with the same days are added at once
  uint32_t run_k = 0;
  const Rec* const idx = g_idx;
  const int n = g_n;
  for (int i = 0; i < n; ++i) {
    const uint32_t w0 = idx[i].w0;
    const int32_t s = int32_t(w0 & 0x1FFFF);
    if (s > last) continue;
    const uint32_t freq = (w0 >> 17) & 7;
    const uint8_t cat = uint8_t((w0 >> 20) & 3);
    if (freq == 0) {  // one day
      if (s < first) continue;
      const int b = int(s - first);
      ++single[b];
      if (!(full >> b & 1)) {
        DayMarks& dm = out[b];
        dm.item[dm.shown++] = uint8_t(cat | (cat == uint8_t(Category::TASK) && (w0 >> 22 & 1) ? MARK_TICKED : 0));
        if (dm.shown == 8) full |= uint64_t(1) << b;
      }
      continue;
    }
    const uint32_t x = idx[i].w1 >> 22;
    const ExtRec* const e = x ? &g_ext[x - 1] : nullptr;
    const uint64_t mask = repeat_mask(w, idx[i], s, w0, e);
    if (!mask) continue;
    if (!(mask & (mask - 1))) {  // one occurrence in the window
      const int b = ctz64(mask);
      ++single[b];
      if (full >> b & 1) continue;
    } else if (mask == run_mask) {  // same days as the entry before: count later
      ++run_k;
    } else {
      add_run(plane, PLANES, run_mask, run_k);
      run_mask = mask;
      run_k = 1;
    }
    uint64_t pend = mask & ~full;
    while (pend) {
      const int b = ctz64(pend);
      pend &= pend - 1;
      uint8_t code = cat;
      if (cat == uint8_t(Category::TASK) && e && e->ndone && dates_contain(ext_done(*e), e->ndone, first + b))
        code |= MARK_TICKED;
      DayMarks& dm = out[b];
      dm.item[dm.shown++] = code;
      if (dm.shown == 8) full |= uint64_t(1) << b;
    }
  }
  add_run(plane, PLANES, run_mask, run_k);
  for (int d = 0; d < ndays; ++d) {
    uint32_t t = single[d];
    for (int p = 0; p < PLANES; ++p) t += uint32_t(plane[p] >> d & 1) << p;
    out[d].total = uint16_t(t > 0xFFFF ? 0xFFFF : t);
  }
}

// All items of one day (display order = index order), same per-entry code.
PLANNER_IWRAM_LOAD int cal_day_items(int32_t day, DayItem* out, int max, int skip) {
  if (!valid_day(day)) return 0;
  Window w;
  prepare(w, day, day);
  int total = 0;
  const Rec* const idx = g_idx;
  const int n = g_n;
  for (int i = 0; i < n; ++i) {
    const Rec& r = idx[i];
    const uint32_t w0 = r.w0;
    const int32_t s = int32_t(w0 & 0x1FFFF);
    if (s > day) continue;
    const uint32_t x = r.w1 >> 22;
    const ExtRec* const e = x ? &g_ext[x - 1] : nullptr;
    if ((w0 >> 17) & 7) {
      if (!repeat_mask(w, r, s, w0, e)) continue;
    } else if (s != day) {
      continue;
    }
    if (total >= skip && total - skip < max) {
      DayItem& it = out[total - skip];
      const Category cat = Category((w0 >> 20) & 3);
      const bool timed = w0 >> 23 & 1;
      bool ticked = false;
      if (cat == Category::TASK)
        ticked = ((w0 >> 17) & 7) ? (e && e->ndone && dates_contain(ext_done(*e), e->ndone, day)) : (w0 >> 22 & 1);
      it.id = EntryId(i);
      it.cat = cat;
      it.flags = uint8_t((timed ? ITEM_TIMED : 0) | (ticked ? ITEM_TICKED : 0) | (((w0 >> 17) & 7) ? ITEM_REPEATS : 0));
      it.start = int16_t(timed ? (r.w1 & 0x7FF) : 0);
      it.end = int16_t(timed && cat == Category::EVENT ? ((r.w1 >> 11) & 0x7FF) : 0);
      it.day = day;
    }
    ++total;
  }
  return total;
}

}  // namespace planner
