// Calendar internals shared by calendar.cpp, cal_app.cpp, cal_convert.cpp and
// the month-marks kernel.
#ifndef PLANNER_CORE_CAL_INTERNAL_H
#define PLANNER_CORE_CAL_INTERNAL_H

#include "cal_codec.h"

namespace planner {

// ---- RAM index: 12 bytes per entry ------------------------------------------
// w0: day 17 [0..16] | freq 3 [17..19] | category 2 [20..21] | ticked 1 [22]
//     | timed 1 [23] | spec 8 [24..31]       (a one-day entry needs only w0)
// w1: start 11 [0..10] | end 11 [11..21] | extension 10 [22..31] (0 = none, else record + 1)
// w2: location 23 [0..22] (text: line offset; in-app: record number)
//     | interval-1 8 [23..30] (0 when the extension holds it) | spare 1 [31]
struct Rec {
  uint32_t w0, w1, w2;
};
static_assert(sizeof(Rec) == 12, "index entry size");

// Rule extension (10 bytes): end, count, large interval, skip/done days.
struct ExtRec {
  uint16_t last_lo;   // last possible occurrence (until, or the count-th day), bits 0..15
  uint16_t hi;        // bit 0: last bit 16 | bits 1-2: EndKind | bits 3-12: interval (0 = inline) | bit 15: in use
  uint16_t count;     // COUNT value
  uint16_t pool_off;  // first date in the pool
  uint8_t nskip, ndone;
};
static_assert(sizeof(ExtRec) == 10, "rule extension size");

constexpr int SCRATCH_DATES = 2 * MAX_LIST_DATES;

extern Rec g_idx[MAX_ENTRIES];
extern int g_n;
extern ExtRec g_ext[MAX_RULE_EXT];
extern uint8_t g_pool[3 * (DATE_POOL + SCRATCH_DATES)];  // dates, then the parse scratch
extern int g_pool_used;                                 // dates
inline uint8_t* scratch() { return g_pool + 3 * DATE_POOL; }

inline int32_t rec_day(const Rec& r) { return int32_t(r.w0 & 0x1FFFF); }
inline uint8_t rec_freq(const Rec& r) { return uint8_t((r.w0 >> 17) & 7); }
inline Category rec_cat(const Rec& r) { return Category((r.w0 >> 20) & 3); }
inline bool rec_ticked(const Rec& r) { return r.w0 >> 22 & 1; }
inline bool rec_timed(const Rec& r) { return r.w0 >> 23 & 1; }
inline uint8_t rec_spec(const Rec& r) { return uint8_t(r.w0 >> 24); }
inline int rec_start(const Rec& r) { return int(r.w1 & 0x7FF); }
inline int rec_end(const Rec& r) { return int((r.w1 >> 11) & 0x7FF); }
inline int rec_ext(const Rec& r) { return int(r.w1 >> 22); }  // 0 = none
inline uint32_t rec_loc(const Rec& r) { return r.w2 & 0x7FFFFF; }
inline int rec_ival(const Rec& r) { return int((r.w2 >> 23) & 0xFF) + 1; }
inline void rec_set_loc(Rec& r, uint32_t loc) { r.w2 = (r.w2 & ~0x7FFFFFu) | (loc & 0x7FFFFF); }
inline void rec_set_ticked(Rec& r, bool t) { r.w0 = (r.w0 & ~(1u << 22)) | (uint32_t(t) << 22); }
inline Rec rec_pack(int32_t day, uint32_t freq, Category cat, bool ticked, bool timed, uint32_t spec, int start,
                    int end, uint32_t ext, uint32_t loc, uint32_t ival_minus_1) {
  Rec r;
  r.w0 = uint32_t(day) | freq << 17 | uint32_t(cat) << 20 | uint32_t(ticked) << 22 | uint32_t(timed) << 23 |
         (spec & 0xFF) << 24;
  r.w1 = (uint32_t(start) & 0x7FF) | (uint32_t(end) & 0x7FF) << 11 | ext << 22;
  r.w2 = (loc & 0x7FFFFF) | (ival_minus_1 & 0xFF) << 23;
  return r;
}

inline int32_t ext_last(const ExtRec& x) { return int32_t(x.last_lo | uint32_t(x.hi & 1) << 16); }
inline EndKind ext_end(const ExtRec& x) { return EndKind((x.hi >> 1) & 3); }
inline int ext_interval(const ExtRec& x) { return (x.hi >> 3) & 0x3FF; }
inline bool ext_used(const ExtRec& x) { return x.hi >> 15; }

// Display order: holidays, birthdays, then events and tasks; all-day first,
// then by start time; ties by location (file / record order).
inline uint64_t rec_key(const Rec& r) {
  const uint32_t c = uint32_t(rec_cat(r));
  const uint32_t group = c > 2 ? 2 : c;
  const uint32_t k = group << 12 | (rec_timed(r) ? (1u << 11 | uint32_t(rec_start(r))) : 0u);
  return uint64_t(k) << 23 | rec_loc(r);
}

inline Series rec_series(const Rec& r) {
  Series s;
  s.start = rec_day(r);
  s.freq = rec_freq(r);
  s.spec = rec_spec(r);
  s.interval = uint16_t(rec_ival(r));
  if (s.freq == 0) {
    s.last = s.start;
    s.interval = 1;
    return s;
  }
  s.last = MAX_DAY;
  const int x = rec_ext(r);
  if (x) {
    const ExtRec& e = g_ext[x - 1];
    s.last = ext_last(e);
    if (ext_interval(e)) s.interval = uint16_t(ext_interval(e));
  }
  return s;
}

// Sorted 3-byte date list helpers.
int dates_lower_bound(const uint8_t* d, int n, int32_t day);
bool dates_contain(const uint8_t* d, int n, int32_t day);
inline const uint8_t* ext_skip(const ExtRec& x) { return g_pool + 3 * x.pool_off; }
inline const uint8_t* ext_done(const ExtRec& x) { return g_pool + 3 * (x.pool_off + x.nskip); }

// Full description of an index entry (for writing / hashing).
void rec_describe(const Rec& r, EntryOut& out);  // lists point into the pool

// Entry admission while loading (and while re-reading for a conversion).
struct Admission {
  int n = 0, ext = 0, pool = 0;
  static bool needs_ext(const ParsedEntry& e) {
    return e.rule.freq != Freq::NONE &&
           (e.rule.end != EndKind::NEVER || e.rule.interval > 256 || e.nskip || e.ndone);
  }
  bool admit(const ParsedEntry& e) {
    const bool x = needs_ext(e);
    if (n >= MAX_ENTRIES || (x && ext >= MAX_RULE_EXT) || pool + e.nskip + e.ndone > DATE_POOL) return false;
    ++n;
    if (x) ++ext;
    pool += e.nskip + e.ndone;
    return true;
  }
};

// Builds a Rec (+ extension and pool lists when needed) for an entry.
// Returns false when the tables are full. loc: line offset / record number.
bool idx_build(const EntryOut& e, uint32_t loc, Rec& out);
void idx_release(const Rec& r);       // frees extension + pool lists
void idx_insert(const Rec& r);        // sorted position
void idx_remove_at(int pos);
void idx_sort();                      // after loading
void idx_clear();

// Storage state.
struct CalState {
  char root[PATH_LEN];
  char text_path[PATH_LEN];
  char sys_dir[PATH_LEN];
  char app_path[PATH_LEN];
  char app_jrn[PATH_LEN];
  Storage storage = Storage::TEXT;
  bool loaded = false;
  LoadReport report;
  // text file
  uint8_t eol = 1;             // 1 LF, 2 CRLF (for new lines)
  bool ends_with_eol = true;
  uint32_t file_size = 0;
  // in-app header
  uint32_t hw = 0, data_end = 0, garbage = 0, live = 0;
  uint32_t cap = 0;            // record numbers of CAL.DAT (grows by compaction)
  bool app_exists = false;
  uint32_t generation = 0;     // bumped on every change (title cache)
  // conversion in progress
  bool convert_ready = false;
  Storage convert_target = Storage::TEXT;
};
extern CalState g_cal;

// In-app backend (cal_app.cpp).
Err app_load(LoadReport& rep);
// Saves one change: add (id NO_ENTRY), replace, or delete (out == nullptr).
// title/notes nullptr = keep the stored text. On OK the RAM index is updated
// and *new_id set.
Err app_commit(EntryId id, const EntryOut* out, const char* title, const char* notes, EntryId* new_id);
Err app_read_text(uint32_t rec, char* title, int tcap, char* notes, int ncap, bool* has_notes);
// Writes a complete store from the text file (conversion) or from the current
// store (compaction) to `path` via a safe save; verify compares it with the
// source entry by entry.
Err app_write_from_text(const char* app_path, Progress progress, void* user, int pmin, int pmax);
Err app_compact();
Err verify_text_against_app(const char* text_path, const char* app_path, Progress progress, void* user, int pmin,
                            int pmax);
Err text_write_from_app(const char* text_path, const char* app_path, Progress progress, void* user, int pmin,
                        int pmax);
void app_paths(const char* sys_dir, char* app, char* jrn);

// Commits a RAM change after a successful save (shared by both backends):
// removes `old_pos` (if >= 0), inserts `rec` (if non-null). Returns new id.
EntryId idx_apply(int old_pos, const Rec* rec);

}  // namespace planner

#endif
