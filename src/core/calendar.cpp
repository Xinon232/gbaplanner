// Calendar: RAM index, queries, edits, calendar.txt backend.
#include "cal_internal.h"
#include "core/todo.h"

namespace planner {

PLANNER_EWRAM Rec g_idx[MAX_ENTRIES];
int g_n = 0;
PLANNER_EWRAM ExtRec g_ext[MAX_RULE_EXT];
PLANNER_EWRAM uint8_t g_pool[3 * (DATE_POOL + SCRATCH_DATES)];
int g_pool_used = 0;
CalState g_cal;

namespace {

// ---- title cache ------------------------------------------------------------
constexpr int TITLE_CACHE = 4;
struct TitleSlot {
  uint32_t gen;
  uint32_t loc;
  bool valid;
  bool notes;
  char text[MAX_TITLE_BYTES + 1];
};
PLANNER_EWRAM TitleSlot g_tcache[TITLE_CACHE];
int g_tnext = 0;

void bump_generation() { ++g_cal.generation; }

// ---- pool / extensions --------------------------------------------------------
void pool_remove(int off, int n) {
  if (n <= 0) return;
  for (int i = 3 * (off + n); i < 3 * g_pool_used; ++i) g_pool[i - 3 * n] = g_pool[i];
  g_pool_used -= n;
  for (int i = 0; i < MAX_RULE_EXT; ++i)
    if (ext_used(g_ext[i]) && g_ext[i].pool_off > off) g_ext[i].pool_off = uint16_t(g_ext[i].pool_off - n);
}

int ext_free_count() {
  int n = 0;
  for (int i = 0; i < MAX_RULE_EXT; ++i) n += !ext_used(g_ext[i]);
  return n;
}

bool needs_ext(const EntryOut& e) {
  return e.rule.freq != Freq::NONE &&
         (e.rule.end != EndKind::NEVER || e.rule.interval > 256 || e.nskip || e.ndone);
}

// Can `e` replace `old` (nullptr: a new entry) in the tables?
bool tables_fit(const EntryOut& e, const Rec* old) {
  if (!old && g_n >= MAX_ENTRIES) return false;
  int free_ext = ext_free_count();
  int free_pool = DATE_POOL - g_pool_used;
  if (old && rec_ext(*old)) {
    ++free_ext;
    const ExtRec& x = g_ext[rec_ext(*old) - 1];
    free_pool += x.nskip + x.ndone;
  }
  if (needs_ext(e) && free_ext < 1) return false;
  return e.nskip + e.ndone <= free_pool;
}

// ---- line sinks ---------------------------------------------------------------
struct StopSink : LineSink {
  StopSink() { wants_text = false; }
  bool stop = false;
  bool got = false;
  LineInfo info;
  bool line(const LineInfo& li) override {
    info = li;
    got = true;
    stop = true;
    return false;
  }
};

struct TextSink : StopSink {
  TextSink() { wants_text = true; }
  char* title = nullptr;
  int tcap = 0, tn = 0;
  char* notes = nullptr;
  int ncap = 0, nn = 0;
  bool has_notes = false;
  bool want_notes = false;
  void title_char(uint8_t c) override {
    if (title && tn < tcap - 1) title[tn++] = char(c);
  }
  void notes_char(uint8_t c) override {
    has_notes = true;
    if (!want_notes) { stop = true; return; }
    if (notes && nn < ncap - 1) notes[nn++] = char(c);
  }
};

// Parses the line at `offset` of the open reader.
Err parse_line_at(Reader& r, uint32_t offset, StopSink& sink) {
  Err e = r.seek(offset);
  if (e != Err::OK) return e;
  LineParser p;
  p.reset(offset, nullptr, &sink);
  for (;;) {
    const int c = r.get();
    if (c < 0) break;
    p.feed(uint8_t(c));
    if (sink.stop) return Err::OK;
  }
  if (r.err() != Err::OK) return r.err();
  p.finish();
  return Err::OK;
}

Err text_read(uint32_t loc, char* title, int tcap, char* notes, int ncap, bool* has_notes) {
  Reader r(0);
  Err e = r.open(g_cal.text_path);
  if (e != Err::OK) return e;
  TextSink s;
  s.title = title; s.tcap = tcap;
  s.notes = notes; s.ncap = ncap;
  s.want_notes = notes != nullptr;
  if ((e = parse_line_at(r, loc, s)) != Err::OK) return e;
  if (s.got && s.info.kind != LineKind::ENTRY) return Err::CORRUPT;
  if (!s.got && !s.stop) return Err::CORRUPT;
  if (title) title[s.tn] = 0;
  if (notes) notes[s.nn] = 0;
  if (has_notes) *has_notes = s.has_notes;
  return Err::OK;
}

// ---- edit helpers ---------------------------------------------------------------
bool text_ok(const char* s, int max_chars, bool allow_newline) {
  int chars = 0;
  for (int i = 0; s[i]; ++i) {
    const uint8_t c = uint8_t(s[i]);
    if (c == '\r' || (c == '\n' && !allow_newline)) return false;
    chars += (c & 0xC0) != 0x80;
  }
  return chars <= max_chars;
}

void fill_out(const EntryInput& in, EntryOut& o) {
  o.cat = in.cat;
  o.day = in.day;
  o.timed = in.timed && (in.cat == Category::EVENT || in.cat == Category::TASK);
  o.start = o.timed ? in.start : 0;
  o.end = o.timed && in.cat == Category::EVENT ? in.end : 0;
  o.ticked = in.cat == Category::TASK && in.ticked;
  o.rule = in.rule;
  if (o.rule.freq == Freq::NONE) o.rule = Rule();
  if (o.rule.end != EndKind::UNTIL) o.rule.until = NO_DAY;
  if (o.rule.end != EndKind::COUNT) o.rule.count = 0;
  o.skip = o.done = nullptr;
  o.nskip = o.ndone = 0;
}

// Copies the entry's lists into the scratch (skip at 0, done at MAX_LIST_DATES).
void lists_to_scratch(const Rec& r, EntryOut& o) {
  uint8_t* sk = scratch();
  uint8_t* dn = scratch() + 3 * MAX_LIST_DATES;
  o.skip = sk;
  o.done = dn;
  o.nskip = o.ndone = 0;
  const int x = rec_ext(r);
  if (!x) return;
  const ExtRec& e = g_ext[x - 1];
  for (int i = 0; i < 3 * e.nskip; ++i) sk[i] = ext_skip(e)[i];
  for (int i = 0; i < 3 * e.ndone; ++i) dn[i] = ext_done(e)[i];
  o.nskip = e.nskip;
  o.ndone = e.ndone;
}

bool list_insert(uint8_t* d, int& n, int32_t day) {
  const int at = dates_lower_bound(d, n, day);
  if (at < n && int32_t(rd24(d + 3 * at)) == day) return true;
  if (n >= MAX_LIST_DATES) return false;
  for (int i = 3 * n - 1; i >= 3 * at; --i) d[i + 3] = d[i];
  wr24(d + 3 * at, uint32_t(day));
  ++n;
  return true;
}

void list_erase(uint8_t* d, int& n, int32_t day) {
  const int at = dates_lower_bound(d, n, day);
  if (at >= n || int32_t(rd24(d + 3 * at)) != day) return;
  for (int i = 3 * at; i < 3 * (n - 1); ++i) d[i] = d[i + 3];
  --n;
}

// ---- text backend: commit one change by rewriting the file --------------------
void copy_bytes(Reader& r, Writer& w, uint32_t n, Identity& seen) {
  while (n) {
    const uint8_t* p;
    uint32_t k = r.avail(&p);
    if (!k) return;
    if (k > n) k = n;
    w.write(p, k);
    seen.add(p, k);
    r.advance(k);
    n -= k;
  }
}

int get_seen(Reader& r, Identity& seen) {
  const int c = r.get();
  if (c >= 0) {
    const uint8_t b = uint8_t(c);
    seen.add(&b, 1);
  }
  return c;
}

struct PatchSink : LineSink {
  Writer& w;
  Escaper esc;
  const EntryOut& e;
  const char* title;
  const char* notes;
  bool notes_open = false;
  bool ok = false;
  PatchSink(Writer& w_, const EntryOut& e_, const char* t, const char* n)
      : w(w_), esc(w_), e(e_), title(t), notes(n) {}
  void title_char(uint8_t c) override {
    if (!title) esc.push(c);
  }
  void title_end() override {
    if (title) esc.text(title, str_len(title));
    esc.finish();
    write_repeat(w, e);
  }
  void notes_char(uint8_t c) override {
    if (notes) return;
    if (!notes_open) { w.puts(" // "); notes_open = true; }
    esc.push(c);
  }
  bool line(const LineInfo& li) override {
    if (notes_open) esc.finish();
    if (notes && *notes) {
      w.puts(" // ");
      esc.text(notes, str_len(notes));
      esc.finish();
    }
    ok = li.kind == LineKind::ENTRY;
    return false;
  }
};

void write_new_line(Writer& w, const EntryOut& e, const char* title, const char* notes) {
  write_head(w, e);
  Escaper esc(w);
  esc.text(title, str_len(title));
  esc.finish();
  write_repeat(w, e);
  if (notes && *notes) {
    w.puts(" // ");
    esc.text(notes, str_len(notes));
    esc.finish();
  }
}

void put_eol(Writer& w, uint8_t eol) {
  if (eol == 2) w.put('\r');
  if (eol) w.put('\n');
}

// pos < 0: add. out == nullptr: delete.
Err text_commit(int pos, const EntryOut* out, const char* title, const char* notes, EntryId* new_id) {
  uint32_t L = 0, OL = 0, line_len = 0;
  uint8_t old_eol = 0;
  bool exists = false;
  uint32_t size = 0;
  Err e = file_size(g_cal.text_path, &size, &exists);
  if (e != Err::OK) return e;
  // The file must still be the one in RAM (no change from outside).
  if (exists ? size != g_cal.file_size : g_cal.file_size != 0) return Err::NEEDS_RELOAD;
  if (pos >= 0) {
    L = rec_loc(g_idx[pos]);
    Reader r(0);
    if ((e = r.open(g_cal.text_path)) != Err::OK) return e;
    StopSink s;
    if ((e = parse_line_at(r, L, s)) != Err::OK) return e;
    if (!s.got || s.info.kind != LineKind::ENTRY || s.info.offset != L) return Err::NEEDS_RELOAD;
    line_len = s.info.length;
    old_eol = s.info.eol;
    OL = line_len + old_eol;
  }
  SafeSave ss;
  if ((e = safe_begin(ss, g_cal.text_path)) != Err::OK) return e;
  Writer& w = ss.w;
  uint32_t new_loc = 0, NL = 0;
  Identity seen;  // of the old file, read completely below
  {
    Reader r(0);
    if (exists && (e = r.open(g_cal.text_path)) != Err::OK) { safe_abort(ss); return e; }
    if (pos >= 0) {
      copy_bytes(r, w, L, seen);
      if (out) {
        const uint32_t before = w.written();
        write_head(w, *out);
        PatchSink ps(w, *out, title, notes);
        LineParser p;
        p.reset(L, nullptr, &ps);
        for (uint32_t i = 0; i < line_len; ++i) {
          const int c = get_seen(r, seen);
          if (c < 0) break;
          p.feed(uint8_t(c));
        }
        p.finish();
        if (!ps.ok) { safe_abort(ss); return r.err() != Err::OK ? r.err() : Err::NEEDS_RELOAD; }
        put_eol(w, old_eol);
        for (int i = 0; i < old_eol; ++i) get_seen(r, seen);
        NL = w.written() - before;
      } else {
        for (uint32_t i = 0; i < OL; ++i) get_seen(r, seen);
      }
      copy_bytes(r, w, size - L - OL, seen);
    } else {
      copy_bytes(r, w, size, seen);
      if (size && !g_cal.ends_with_eol) put_eol(w, g_cal.eol);
      new_loc = w.written();
      write_new_line(w, *out, title, notes);
      put_eol(w, g_cal.eol);
    }
    if (r.err() != Err::OK) { safe_abort(ss); return r.err(); }
    if (exists && r.tell() != size) { safe_abort(ss); return Err::NEEDS_RELOAD; }
  }
  if (w.err() != Err::OK) { const Err we = w.err(); safe_abort(ss); return we; }
  if (w.written() > MAX_TEXT_FILE_BYTES) { safe_abort(ss); return Err::TOO_LARGE; }
  const uint32_t new_size = w.written();
  const bool ends = new_size == 0 || w.last() == '\n';
  bool installed = false;
  e = safe_commit(ss, &installed, exists && seen.size == size ? &seen : nullptr);
  if (!installed) return e == Err::OK ? Err::IO : e;
  // The new file is on the card: update the RAM index to match it.
  g_cal.file_size = new_size;
  g_cal.ends_with_eol = ends;
  if (pos >= 0) {
    const int32_t delta = out ? int32_t(NL) - int32_t(OL) : -int32_t(OL);
    if (delta)
      for (int i = 0; i < g_n; ++i) {
        const uint32_t loc = rec_loc(g_idx[i]);
        if (loc > L) rec_set_loc(g_idx[i], uint32_t(int32_t(loc) + delta));
      }
    new_loc = L;
  }
  Rec rec;
  if (out) {
    if (pos >= 0) idx_release(g_idx[pos]);  // lists of `out` are in the scratch
    idx_build(*out, new_loc, rec);           // cannot fail: tables_fit() checked
  } else {
    idx_release(g_idx[pos]);
  }
  const EntryId id = idx_apply(pos, out ? &rec : nullptr);
  if (new_id) *new_id = id;
  return Err::OK;
}

Err commit(int pos, const EntryOut* out, const char* title, const char* notes, EntryId* new_id) {
  if (!g_cal.loaded) return Err::NOT_LOADED;
  if (out && !tables_fit(*out, pos >= 0 ? &g_idx[pos] : nullptr)) return Err::FULL;
  bump_generation();
  Err e = g_cal.storage == Storage::TEXT ? text_commit(pos, out, title, notes, new_id)
                                         : app_commit(pos < 0 ? NO_ENTRY : EntryId(pos), out, title, notes, new_id);
  bump_generation();
  if (e == Err::NEEDS_RELOAD) g_cal.loaded = false;
  return e;
}

// ---- text loading ---------------------------------------------------------------
struct LoadSink : LineSink {
  LoadSink() { wants_text = false; }
  LoadReport* rep;
  Admission adm;
  bool eol_seen = false;
  uint8_t eol = 1;
  bool last_eol = true;
  bool full_error = false;
  bool line(const LineInfo& li) override {
    if (li.eol && !eol_seen) { eol_seen = true; eol = li.eol; }
    last_eol = li.eol != 0;
    switch (li.kind) {
      case LineKind::EMPTY: ++rep->empty_lines; break;
      case LineKind::COMMENT: ++rep->comment_lines; break;
      case LineKind::UNKNOWN: ++rep->unknown_lines; break;
      case LineKind::ENTRY: {
        const ParsedEntry& p = li.e;
        if (!adm.admit(p)) { ++rep->limit_lines; break; }
        EntryOut o;
        o.cat = p.cat; o.day = p.day; o.timed = p.timed; o.start = p.start; o.end = p.end;
        o.ticked = p.ticked; o.rule = p.rule;
        o.skip = p.skip; o.nskip = p.nskip; o.done = p.done; o.ndone = p.ndone;
        Rec r;
        if (!idx_build(o, li.offset, r)) { full_error = true; return false; }
        g_idx[g_n++] = r;
        ++rep->entries;
        break;
      }
    }
    return true;
  }
};

Err text_load(LoadReport& rep) {
  Err e = safe_recover(g_cal.text_path);
  if (e != Err::OK) return e;
  bool exists = false;
  uint32_t size = 0;
  if ((e = file_size(g_cal.text_path, &size, &exists)) != Err::OK) return e;
  g_cal.file_size = 0;
  g_cal.eol = 1;
  g_cal.ends_with_eol = true;
  if (!exists) return Err::OK;
  if (size > MAX_TEXT_FILE_BYTES) return Err::TOO_LARGE;
  Reader r(0);
  if ((e = r.open(g_cal.text_path, &size)) != Err::OK) return e;
  LoadSink sink;
  sink.rep = &rep;
  LineParser p;
  bool bom = false;
  if ((e = parse_file(r, p, scratch(), &sink, &bom)) != Err::OK) return e;
  if (sink.full_error) return Err::FULL;
  if (r.tell() != size) return Err::CORRUPT;
  rep.bytes = size;
  g_cal.file_size = size;
  g_cal.eol = sink.eol;
  g_cal.ends_with_eol = size == 0 || sink.last_eol;
  return Err::OK;
}

}  // namespace

// ---- shared index operations -----------------------------------------------------
int dates_lower_bound(const uint8_t* d, int n, int32_t day) {
  int lo = 0, hi = n;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (int32_t(rd24(d + 3 * mid)) < day) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

bool dates_contain(const uint8_t* d, int n, int32_t day) {
  const int at = dates_lower_bound(d, n, day);
  return at < n && int32_t(rd24(d + 3 * at)) == day;
}

void rec_describe(const Rec& r, EntryOut& o) {
  o.cat = rec_cat(r);
  o.day = rec_day(r);
  o.timed = rec_timed(r);
  o.start = int16_t(o.timed ? rec_start(r) : 0);
  o.end = int16_t(o.timed && o.cat == Category::EVENT ? rec_end(r) : 0);
  o.ticked = rec_ticked(r);
  Rule& ru = o.rule;
  ru = Rule();
  ru.freq = Freq(rec_freq(r));
  o.skip = o.done = nullptr;
  o.nskip = o.ndone = 0;
  if (ru.freq == Freq::NONE) return;
  ru.interval = uint16_t(rec_ival(r));
  rule_apply_spec(ru, rec_spec(r));
  const int x = rec_ext(r);
  if (x) {
    const ExtRec& e = g_ext[x - 1];
    if (ext_interval(e)) ru.interval = uint16_t(ext_interval(e));
    ru.end = ext_end(e);
    if (ru.end == EndKind::UNTIL) ru.until = ext_last(e);
    if (ru.end == EndKind::COUNT) ru.count = e.count;
    o.skip = ext_skip(e);
    o.nskip = e.nskip;
    o.done = ext_done(e);
    o.ndone = e.ndone;
  }
}

bool idx_build(const EntryOut& e, uint32_t loc, Rec& out) {
  const Rule& ru = e.rule;
  const bool rep = ru.freq != Freq::NONE;
  const bool timed = e.timed && (e.cat == Category::EVENT || e.cat == Category::TASK);
  const uint32_t ival = rep && ru.interval <= 256 ? uint32_t(ru.interval - 1) : 0;
  out = rec_pack(e.day, uint32_t(ru.freq), e.cat, e.cat == Category::TASK && e.ticked, timed,
                 rep ? rule_spec(ru) : 0, timed ? e.start : 0, timed && e.cat == Category::EVENT ? e.end : 0, 0,
                 loc, ival);
  if (!needs_ext(e)) return true;
  int x = 0;
  while (x < MAX_RULE_EXT && ext_used(g_ext[x])) ++x;
  const int n = e.nskip + e.ndone;
  if (x == MAX_RULE_EXT || g_pool_used + n > DATE_POOL) return false;
  ExtRec& r = g_ext[x];
  const int32_t last = Series::make(ru, e.day).last;
  r.last_lo = uint16_t(last & 0xFFFF);
  r.hi = uint16_t((last >> 16) & 1) | uint16_t(uint16_t(ru.end) << 1) |
         uint16_t((ru.interval > 256 ? ru.interval : 0) << 3) | 0x8000;
  r.count = ru.end == EndKind::COUNT ? ru.count : 0;
  r.pool_off = uint16_t(g_pool_used);
  r.nskip = uint8_t(e.nskip);
  r.ndone = uint8_t(e.ndone);
  uint8_t* dst = g_pool + 3 * g_pool_used;
  for (int i = 0; i < 3 * e.nskip; ++i) dst[i] = e.skip[i];
  for (int i = 0; i < 3 * e.ndone; ++i) dst[3 * e.nskip + i] = e.done[i];
  g_pool_used += n;
  out.w1 |= uint32_t(x + 1) << 22;  // extension
  return true;
}

void idx_release(const Rec& r) {
  const int x = rec_ext(r);
  if (!x) return;
  ExtRec& e = g_ext[x - 1];
  if (!ext_used(e)) return;
  pool_remove(e.pool_off, e.nskip + e.ndone);
  e.hi = 0;
  e.nskip = e.ndone = 0;
}

static int idx_find_insert(uint64_t key) {
  int lo = 0, hi = g_n;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (rec_key(g_idx[mid]) < key) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

void idx_insert(const Rec& r) {
  const int at = idx_find_insert(rec_key(r));
  for (int i = g_n; i > at; --i) g_idx[i] = g_idx[i - 1];
  g_idx[at] = r;
  ++g_n;
}

void idx_remove_at(int pos) {
  for (int i = pos; i < g_n - 1; ++i) g_idx[i] = g_idx[i + 1];
  --g_n;
}

EntryId idx_apply(int old_pos, const Rec* rec) {
  if (old_pos >= 0) idx_remove_at(old_pos);
  if (!rec) return NO_ENTRY;
  idx_insert(*rec);
  // find it again (keys are unique: location breaks ties)
  const uint64_t k = rec_key(*rec);
  return EntryId(idx_find_insert(k));
}

void idx_clear() {
  g_n = 0;
  g_pool_used = 0;
  for (int i = 0; i < MAX_RULE_EXT; ++i) g_ext[i] = ExtRec{};
  for (int i = 0; i < TITLE_CACHE; ++i) g_tcache[i].valid = false;
}

// ---- public API -------------------------------------------------------------------
void core_init(const char* root) {
  str_copy(g_cal.root, PATH_LEN, root);
  path_join(g_cal.text_path, root, "calendar.txt");
  path_join(g_cal.sys_dir, root, "GBAPLANNER.SYS");
  app_paths(g_cal.sys_dir, g_cal.app_path, g_cal.app_jrn);
  g_cal.loaded = false;
  g_cal.convert_ready = false;
  g_cal.report = LoadReport();
  idx_clear();
  todo_reset(root);
}

Err core_make_dirs() {
  Err e = ensure_dir(g_cal.root);
  if (e != Err::OK) return e;
  return ensure_dir(g_cal.sys_dir);
}

Err cal_load(Storage s, LoadReport* report) {
  idx_clear();
  bump_generation();
  g_cal.loaded = false;
  g_cal.convert_ready = false;
  g_cal.storage = s;
  LoadReport rep;
  Err e = s == Storage::TEXT ? text_load(rep) : app_load(rep);
  if (e != Err::OK) {
    idx_clear();
    g_cal.report = rep;
    if (report) *report = rep;
    return e;
  }
  idx_sort();
  g_cal.loaded = true;
  g_cal.report = rep;
  if (report) *report = rep;
  return Err::OK;
}

bool cal_loaded() { return g_cal.loaded; }
Storage cal_storage() { return g_cal.storage; }
int cal_count() { return g_n; }
const LoadReport& cal_report() { return g_cal.report; }

static bool rec_on(const Rec& r, int32_t day) {
  if (day < rec_day(r)) return false;
  const Series s = rec_series(r);
  if (!s.occurs(day)) return false;
  const int x = rec_ext(r);
  if (x) {
    const ExtRec& e = g_ext[x - 1];
    if (e.nskip && dates_contain(ext_skip(e), e.nskip, day)) return false;
  }
  return true;
}

static bool rec_done_on(const Rec& r, int32_t day) {
  if (rec_cat(r) != Category::TASK) return false;
  if (rec_freq(r) == 0) return rec_ticked(r);
  const int x = rec_ext(r);
  if (!x) return false;
  const ExtRec& e = g_ext[x - 1];
  return e.ndone && dates_contain(ext_done(e), e.ndone, day);
}

static DayItem make_item(int i, int32_t day) {
  const Rec& r = g_idx[i];
  DayItem it;
  it.id = EntryId(i);
  it.cat = rec_cat(r);
  it.flags = uint8_t((rec_timed(r) ? ITEM_TIMED : 0) | (rec_done_on(r, day) ? ITEM_TICKED : 0) |
                     (rec_freq(r) ? ITEM_REPEATS : 0));
  it.start = int16_t(rec_timed(r) ? rec_start(r) : 0);
  it.end = int16_t(rec_timed(r) && it.cat == Category::EVENT ? rec_end(r) : 0);
  it.day = day;
  return it;
}


bool cal_occurs(EntryId id, int32_t day) { return id < g_n && rec_on(g_idx[id], day); }
bool cal_is_done(EntryId id, int32_t day) { return id < g_n && rec_done_on(g_idx[id], day); }

// First open (not skipped, not done) occurrence >= from, or NO_DAY.
static int32_t first_open(const Rec& r, int32_t from) {
  const Series s = rec_series(r);
  const int x = rec_ext(r);
  const ExtRec* e = x ? &g_ext[x - 1] : nullptr;
  for (int32_t d = s.next(from); d != NO_DAY; d = s.next(d + 1)) {
    if (e && e->nskip && dates_contain(ext_skip(*e), e->nskip, d)) continue;
    if (rec_done_on(r, d)) continue;
    return d;
  }
  return NO_DAY;
}

int cal_tasks(int32_t ref, TaskList mode, TaskCursor* cursor, DayItem* out, int max, int* total, int window) {
  constexpr int MAXP = 64;
  if (max > MAXP) max = MAXP;
  uint64_t keys[MAXP];
  int n = 0, all = 0;
  const uint64_t from = cursor ? cursor->next : 0;
  auto offer = [&](int i, int32_t day, bool overdue) {
    ++all;
    const uint64_t k = uint64_t(overdue ? 0 : 1) << 62 | uint64_t(day) << 20 | uint64_t(i);
    if (k < from || max <= 0) return;
    if (n == max && k >= keys[n - 1]) return;
    int at = n < max ? n : max - 1;
    if (n < max) ++n;
    while (at > 0 && keys[at - 1] > k) {
      keys[at] = keys[at - 1];
      out[at] = out[at - 1];
      --at;
    }
    keys[at] = k;
    out[at] = make_item(i, day);
    if (overdue) out[at].flags |= ITEM_OVERDUE;
  };
  for (int i = 0; i < g_n; ++i) {
    const Rec& r = g_idx[i];
    if (rec_cat(r) != Category::TASK) continue;
    if (rec_freq(r) == 0) {
      if (rec_ticked(r)) continue;
      if (rec_day(r) >= ref) offer(i, rec_day(r), false);
      else if (mode == TaskList::ALL) offer(i, rec_day(r), true);
      continue;
    }
    if (window > 0) {
      if (mode == TaskList::ALL) {
        int k = 0;
        for (int32_t d = first_open(r, rec_day(r)); d != NO_DAY && d < ref && k < TASK_OVERDUE_MAX;
             d = first_open(r, d + 1), ++k)
          offer(i, d, true);
      }
      bool any = false;
      int32_t d = first_open(r, ref);
      for (; d != NO_DAY && d - ref < window; d = first_open(r, d + 1)) {
        offer(i, d, false);
        any = true;
      }
      if (!any && d != NO_DAY) offer(i, d, false);
      continue;
    }
    if (mode == TaskList::ALL) {
      const int32_t d = first_open(r, rec_day(r));
      if (d != NO_DAY && d < ref) offer(i, d, true);
    }
    const int32_t d = first_open(r, ref);
    if (d != NO_DAY) offer(i, d, false);
  }
  if (cursor && n) cursor->next = keys[n - 1] + 1;
  if (total) *total = all;
  return n;
}

void cal_set_repeat_options(const RepeatOptions& o) {
  if (o.feb29 == g_repeat.feb29 && o.short_month == g_repeat.short_month) return;
  g_repeat = o;
  // "after N occurrences": the N-th day may move with the options.
  for (int i = 0; i < g_n; ++i) {
    const int x = rec_ext(g_idx[i]);
    if (!x) continue;
    ExtRec& e = g_ext[x - 1];
    if (ext_end(e) != EndKind::COUNT) continue;
    EntryOut o2;
    rec_describe(g_idx[i], o2);
    const int32_t last = Series::make(o2.rule, rec_day(g_idx[i])).last;
    e.last_lo = uint16_t(last & 0xFFFF);
    e.hi = uint16_t((e.hi & ~1u) | ((last >> 16) & 1));
  }
  bump_generation();
}

RepeatOptions cal_repeat_options() { return g_repeat; }

Err cal_get(EntryId id, EntryInfo* out) {
  if (id >= g_n) return Err::NOT_FOUND;
  EntryOut o;
  rec_describe(g_idx[id], o);
  out->cat = o.cat;
  out->day = o.day;
  out->timed = o.timed;
  out->start = o.start;
  out->end = o.end;
  out->ticked = o.ticked;
  out->rule = o.rule;
  out->nskip = uint8_t(o.nskip);
  out->ndone = uint8_t(o.ndone);
  return Err::OK;
}

int cal_dates(EntryId id, bool done, int32_t* out, int max) {
  if (id >= g_n) return 0;
  const int x = rec_ext(g_idx[id]);
  if (!x) return 0;
  const ExtRec& e = g_ext[x - 1];
  const uint8_t* d = done ? ext_done(e) : ext_skip(e);
  const int n = done ? e.ndone : e.nskip;
  int k = 0;
  for (; k < n && k < max; ++k) out[k] = int32_t(rd24(d + 3 * k));
  return k;
}

static Err read_text(EntryId id, char* title, int tcap, char* notes, int ncap, bool* has_notes) {
  if (!g_cal.loaded) return Err::NOT_LOADED;
  if (id >= g_n) return Err::NOT_FOUND;
  const uint32_t loc = rec_loc(g_idx[id]);
  return g_cal.storage == Storage::TEXT ? text_read(loc, title, tcap, notes, ncap, has_notes)
                                        : app_read_text(loc, title, tcap, notes, ncap, has_notes);
}

Err cal_title(EntryId id, char* out, int cap, bool* has_notes) {
  if (id >= g_n) return Err::NOT_FOUND;
  const uint32_t loc = rec_loc(g_idx[id]);
  for (int i = 0; i < TITLE_CACHE; ++i) {
    TitleSlot& s = g_tcache[i];
    if (s.valid && s.gen == g_cal.generation && s.loc == loc) {
      str_copy(out, cap, s.text);
      if (has_notes) *has_notes = s.notes;
      return Err::OK;
    }
  }
  TitleSlot& s = g_tcache[g_tnext];
  g_tnext = (g_tnext + 1) % TITLE_CACHE;
  s.valid = false;
  bool notes = false;
  Err e = read_text(id, s.text, int(sizeof s.text), nullptr, 0, &notes);
  if (e != Err::OK) {
    if (cap > 0) out[0] = 0;
    return e;
  }
  s.valid = true;
  s.gen = g_cal.generation;
  s.loc = loc;
  s.notes = notes;
  str_copy(out, cap, s.text);
  if (has_notes) *has_notes = notes;
  return Err::OK;
}

Err cal_notes(EntryId id, char* out, int cap) {
  return read_text(id, nullptr, 0, out, cap, nullptr);
}

Err cal_validate(const EntryInput& in) {
  if (uint8_t(in.cat) > 3 || !valid_day(in.day)) return Err::INVALID;
  if (in.timed) {
    if (in.cat == Category::EVENT) {
      if (in.start < 0 || in.end > 1439 || in.end <= in.start) return Err::INVALID;
    } else if (in.cat == Category::TASK) {
      if (in.start < 0 || in.start > 1439) return Err::INVALID;
    } else {
      return Err::INVALID;
    }
  }
  if (!rule_valid(in.rule)) return Err::INVALID;
  if (in.title && (!in.title[0] || !text_ok(in.title, MAX_TITLE_CHARS, false))) return Err::INVALID;
  if (in.notes && !text_ok(in.notes, MAX_NOTES_CHARS, true)) return Err::INVALID;
  return Err::OK;
}

Err cal_add(const EntryInput& in, EntryId* id) {
  if (!in.title) return Err::INVALID;
  Err e = cal_validate(in);
  if (e != Err::OK) return e;
  EntryOut o;
  fill_out(in, o);
  return commit(-1, &o, in.title, in.notes ? in.notes : "", id);
}

Err cal_update(EntryId id, const EntryInput& in, EntryId* new_id) {
  if (id >= g_n) return Err::NOT_FOUND;
  Err e = cal_validate(in);
  if (e != Err::OK) return e;
  EntryOut o;
  fill_out(in, o);
  EntryOut keep;
  lists_to_scratch(g_idx[id], keep);
  if (o.rule.freq != Freq::NONE) {
    o.skip = keep.skip; o.nskip = keep.nskip;
    if (o.cat == Category::TASK) { o.done = keep.done; o.ndone = keep.ndone; }
  }
  return commit(id, &o, in.title, in.notes, new_id);
}

Err cal_delete(EntryId id) {
  if (id >= g_n) return Err::NOT_FOUND;
  return commit(id, nullptr, nullptr, nullptr, nullptr);
}

Err cal_skip_day(EntryId id, int32_t day, EntryId* new_id) {
  if (id >= g_n) return Err::NOT_FOUND;
  const Rec& r = g_idx[id];
  if (!rec_freq(r) || !rec_on(r, day)) return Err::INVALID;
  EntryOut o;
  rec_describe(r, o);
  lists_to_scratch(r, o);
  if (!list_insert(const_cast<uint8_t*>(o.skip), o.nskip, day)) return Err::FULL;
  return commit(id, &o, nullptr, nullptr, new_id);
}

Err cal_set_done(EntryId id, int32_t day, bool done, EntryId* new_id) {
  if (id >= g_n) return Err::NOT_FOUND;
  const Rec& r = g_idx[id];
  if (rec_cat(r) != Category::TASK) return Err::INVALID;
  EntryOut o;
  rec_describe(r, o);
  if (!rec_freq(r)) {
    if (o.ticked == done) { if (new_id) *new_id = id; return Err::OK; }
    o.ticked = done;
    return commit(id, &o, nullptr, nullptr, new_id);
  }
  if (!rec_on(r, day)) return Err::INVALID;
  if (rec_done_on(r, day) == done) { if (new_id) *new_id = id; return Err::OK; }
  lists_to_scratch(r, o);
  if (done) {
    if (!list_insert(const_cast<uint8_t*>(o.done), o.ndone, day)) return Err::FULL;
  } else {
    list_erase(const_cast<uint8_t*>(o.done), o.ndone, day);
  }
  return commit(id, &o, nullptr, nullptr, new_id);
}

extern uint64_t g_mmask[256];  // cal_marks.bn_iwram.cpp

size_t cal_ram_bytes() {
  return sizeof g_mmask + sizeof g_idx + sizeof g_ext + sizeof g_pool + sizeof g_tcache + sizeof g_cal + sizeof g_n +
         sizeof g_pool_used + sizeof g_tnext + sizeof g_fil + sizeof g_iobuf;
}

}  // namespace planner
