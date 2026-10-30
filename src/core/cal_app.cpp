// In-app calendar store (GBAPLANNER.SYS/CAL.DAT) and conversions.
//
// CAL.DAT layout (all little endian):
//   0      header (64 bytes, CRC32), rest of sector 0 zero
//   512    meta region: `cap` records x 20 bytes (record r at 512 + 20 r);
//          cap follows the number of entries (cap_for: 1.5 x live + 64,
//          multiples of 128, at most 10,000), so a small calendar is a
//          small file
//   DATA   data region (data_off(cap), whole sectors): data records, appended
// Meta record: entry fields, rule, data offset/length, CRC16 (seeded with r).
// Data record: title, 0, notes, 0, skip days, done days (3 bytes each),
//   nskip, ndone, 0, 0, CRC32 over (r, all bytes before).
// An update appends a new data record after data_end (invisible until the
// header says otherwise), syncs, then changes meta record + header through
// the redo journal CAL.JRN. Old data records become garbage; a compaction
// rewrites the file (safe save) with a larger cap when the record numbers
// run out. Files of the first version (cap 10,000) read unchanged.
#include "cal_internal.h"

namespace planner {
namespace {

constexpr uint8_t MAGIC[8] = {'G', 'B', 'P', 'C', 'A', 'L', '0', '1'};
constexpr uint32_t HDR = 64;
constexpr uint32_t META_OFF = 512;
constexpr uint32_t META_SIZE = 20;
constexpr uint32_t data_off(uint32_t cap) { return (META_OFF + META_SIZE * cap + 511) / 512 * 512; }
// Record numbers for a file holding `live` entries (room to add some).
constexpr uint32_t cap_for(uint32_t live) {
  const uint32_t c = (live + live / 2 + 64 + 127) / 128 * 128;
  return c < uint32_t(MAX_ENTRIES) ? c : uint32_t(MAX_ENTRIES);
}

struct Header {
  uint32_t cap = cap_for(0);
  uint32_t hw = 0, data_end = data_off(cap_for(0)), garbage = 0, live = 0;
  void set_cap(uint32_t c) { cap = c; data_end = data_off(c); }
};

void header_pack(uint8_t* b, const Header& h) {
  for (uint32_t i = 0; i < HDR; ++i) b[i] = 0;
  for (int i = 0; i < 8; ++i) b[i] = MAGIC[i];
  wr32(b + 8, 1);
  wr32(b + 12, h.cap);
  wr32(b + 16, META_OFF);
  wr32(b + 20, META_SIZE);
  wr32(b + 24, data_off(h.cap));
  wr32(b + 28, h.hw);
  wr32(b + 32, h.data_end);
  wr32(b + 36, h.garbage);
  wr32(b + 40, h.live);
  wr32(b + 60, crc32(b, 60));
}

bool header_unpack(const uint8_t* b, Header& h) {
  for (int i = 0; i < 8; ++i)
    if (b[i] != MAGIC[i]) return false;
  const uint32_t cap = rd32(b + 12);
  if (rd32(b + 60) != crc32(b, 60) || rd32(b + 8) != 1 || cap < 1 || cap > uint32_t(MAX_ENTRIES) ||
      rd32(b + 16) != META_OFF || rd32(b + 20) != META_SIZE || rd32(b + 24) != data_off(cap))
    return false;
  h.cap = cap;
  h.hw = rd32(b + 28);
  h.data_end = rd32(b + 32);
  h.garbage = rd32(b + 36);
  h.live = rd32(b + 40);
  return h.hw <= h.cap && h.data_end >= data_off(h.cap);
}

struct Meta {
  EntryOut e;
  bool used = false, lists = false;
  uint32_t off = 0;
  uint16_t len = 0;
};

void meta_pack(uint8_t* b, uint32_t r, const Meta& m) {
  const EntryOut& e = m.e;
  const Rule& ru = e.rule;
  const bool timed = e.timed;
  const uint32_t w0 = uint32_t(e.day) | uint32_t(e.cat) << 17 | uint32_t(timed) << 19 |
                      uint32_t(e.cat == Category::TASK && e.ticked) << 20 | uint32_t(timed ? e.start : 0) << 21;
  const uint32_t w1 = uint32_t(timed && e.cat == Category::EVENT ? e.end : 0) | uint32_t(ru.freq) << 11 |
                      uint32_t(ru.freq != Freq::NONE ? rule_spec(ru) : 0) << 14 | uint32_t(ru.end) << 22 |
                      uint32_t(m.used) << 24 | uint32_t(m.lists) << 25;
  const uint32_t ev = ru.end == EndKind::UNTIL ? uint32_t(ru.until) : ru.end == EndKind::COUNT ? ru.count : 0;
  const uint32_t w2 = uint32_t(ru.freq != Freq::NONE ? ru.interval : 1) | ev << 10;
  wr32(b, w0);
  wr32(b + 4, w1);
  wr32(b + 8, w2);
  wr32(b + 12, m.off);
  wr16(b + 16, m.len);
  wr16(b + 18, crc16(b, 18, uint16_t(r)));
}

// false: CRC mismatch. m.used false: free record (not checked further).
bool meta_unpack(const uint8_t* b, uint32_t r, Meta& m) {
  const uint32_t w0 = rd32(b), w1 = rd32(b + 4), w2 = rd32(b + 8);
  m.used = w1 >> 24 & 1;
  if (!m.used) return true;
  if (rd16(b + 18) != crc16(b, 18, uint16_t(r))) return false;
  EntryOut& e = m.e;
  e.day = int32_t(w0 & 0x1FFFF);
  e.cat = Category(w0 >> 17 & 3);
  e.timed = w0 >> 19 & 1;
  e.ticked = w0 >> 20 & 1;
  e.start = int16_t(e.timed ? w0 >> 21 : 0);
  e.end = int16_t(e.timed && e.cat == Category::EVENT ? (w1 & 0x7FF) : 0);
  Rule& ru = e.rule;
  ru = Rule();
  ru.freq = Freq(w1 >> 11 & 7);
  if (ru.freq != Freq::NONE) {
    rule_apply_spec(ru, uint8_t(w1 >> 14));
    ru.interval = uint16_t(w2 & 0x3FF);
    ru.end = EndKind(w1 >> 22 & 3);
    const uint32_t ev = w2 >> 10 & 0x1FFFF;
    if (ru.end == EndKind::UNTIL) ru.until = int32_t(ev);
    if (ru.end == EndKind::COUNT) ru.count = uint16_t(ev);
  }
  m.lists = w1 >> 25 & 1;
  m.off = rd32(b + 12);
  m.len = rd16(b + 16);
  e.skip = e.done = nullptr;
  e.nskip = e.ndone = 0;
  if (!valid_day(e.day) || uint8_t(ru.freq) > 4 || !rule_valid(ru)) return false;
  if (e.timed && (e.start > 1439 || (e.cat == Category::EVENT && e.end <= e.start))) return false;
  return true;
}

Err read_header(Reader& r, Header& h) {
  uint8_t b[HDR];
  Err e = r.seek(0);
  if (e == Err::OK) e = r.read(b, HDR);
  if (e != Err::OK) return e;
  return header_unpack(b, h) ? Err::OK : Err::CORRUPT;
}

Err read_meta(Reader& r, uint32_t rec, Meta& m, bool& ok) {
  uint8_t b[META_SIZE];
  Err e = r.seek(META_OFF + META_SIZE * rec);
  if (e == Err::OK) e = r.read(b, META_SIZE);
  if (e != Err::OK) return e;
  ok = meta_unpack(b, rec, m);
  return Err::OK;
}

// Reads a data record from its start, front to back (no backward seek):
// title and notes go to the hash (if any), the day lists to the scratch;
// checks the CRC and the layout.
Err read_record(Reader& r, uint32_t rec, Meta& m, Hash64* h) {
  EntryOut& e = m.e;
  e.skip = scratch();
  e.nskip = e.ndone = 0;
  e.done = scratch();
  if (m.len < 10) return Err::CORRUPT;
  Err er = r.seek(m.off);
  if (er != Err::OK) return er;
  uint8_t rb[4];
  wr32(rb, rec);
  uint32_t crc = crc32_update(0, rb, 4);
  uint32_t used = 0;
  for (int part = 0; part < 2; ++part) {
    for (;;) {
      const int c = r.get();
      if (c < 0) return r.err() != Err::OK ? r.err() : Err::CORRUPT;
      const uint8_t b = uint8_t(c);
      crc = crc32_update(crc, &b, 1);
      if (h) h->byte(b);
      if (++used > uint32_t(m.len) - 8) return Err::CORRUPT;
      if (!b) break;
    }
  }
  const uint32_t lists = m.len - 8 - used;
  if (lists > uint32_t(3 * SCRATCH_DATES)) return Err::CORRUPT;
  uint8_t* sc = scratch();
  if ((er = r.read(sc, lists)) != Err::OK) return er;
  crc = crc32_update(crc, sc, lists);
  uint8_t t[8];
  if ((er = r.read(t, 8)) != Err::OK) return er;
  crc = crc32_update(crc, t, 4);
  if (rd32(t + 4) != crc || uint32_t(3 * (t[0] + t[1])) != lists || t[0] > MAX_LIST_DATES || t[1] > MAX_LIST_DATES)
    return Err::CORRUPT;
  e.nskip = t[0];
  e.ndone = t[1];
  e.done = sc + 3 * t[0];
  if (m.lists != (lists > 0)) return Err::CORRUPT;
  return Err::OK;
}

// Hash of a record's content in the canonical order (title, 0, notes, 0,
// fields), as HashSink computes it for a calendar.txt line.
Err hash_record(Reader& r, uint32_t rec, Meta& m, Hash64& h) {
  Err e = read_record(r, rec, m, &h);
  if (e != Err::OK) return e;
  hash_fields(h, m.e);
  return Err::OK;
}

// Writes bytes and keeps the data CRC.
struct DataOut {
  Writer& w;
  uint32_t crc;
  uint32_t n = 0;
  DataOut(Writer& w_, uint32_t rec) : w(w_) {
    uint8_t rb[4];
    wr32(rb, rec);
    crc = crc32_update(0, rb, 4);
  }
  void put(uint8_t c) {
    w.put(c);
    crc = crc32_update(crc, &c, 1);
    ++n;
  }
  void write(const uint8_t* p, uint32_t k) {
    for (uint32_t i = 0; i < k; ++i) put(p[i]);
  }
  void tail(const EntryOut& e) {
    write(e.skip, uint32_t(3 * e.nskip));
    write(e.done, uint32_t(3 * e.ndone));
    put(uint8_t(e.nskip));
    put(uint8_t(e.ndone));
    put(0);
    put(0);
    uint8_t cb[4];
    wr32(cb, crc);
    w.write(cb, 4);
    n += 4;
  }
};

uint16_t data_len(int title_bytes, int notes_bytes, const EntryOut& e) {
  return uint16_t(title_bytes + 1 + notes_bytes + 1 + 3 * (e.nskip + e.ndone) + 8);
}

void meta_from(Meta& m, const EntryOut& e, uint32_t off, uint16_t len) {
  m.e = e;
  m.used = true;
  m.lists = e.nskip + e.ndone > 0;
  m.off = off;
  m.len = len;
}

Err create_empty(const char* path) {
  SafeSave ss;
  Err e = safe_begin(ss, path);
  if (e != Err::OK) return e;
  uint8_t b[HDR];
  const Header h;
  header_pack(b, h);
  ss.w.write(b, HDR);
  for (uint32_t i = HDR; i < META_OFF; ++i) ss.w.put(0);
  bool inst = false;
  e = safe_commit(ss, &inst);
  return inst ? Err::OK : (e == Err::OK ? Err::IO : e);
}

}  // namespace

void app_paths(const char* sys_dir, char* app, char* jrn) {
  path_join(app, sys_dir, "CAL.DAT");
  path_join(jrn, sys_dir, "CAL.JRN");
}

Err app_load(LoadReport& rep) {
  Err e = journal_recover(g_cal.app_path, g_cal.app_jrn);
  if (e == Err::OK) e = safe_recover(g_cal.app_path);
  if (e != Err::OK) return e;
  bool exists = false;
  uint32_t size = 0;
  if ((e = file_size(g_cal.app_path, &size, &exists)) != Err::OK) return e;
  g_cal.app_exists = exists;
  g_cal.hw = g_cal.garbage = g_cal.live = 0;
  g_cal.cap = cap_for(0);
  g_cal.data_end = data_off(g_cal.cap);
  if (!exists) return Err::OK;
  rep.bytes = size;
  Header h;
  {
    Reader r(0);
    if ((e = r.open(g_cal.app_path)) != Err::OK) return e;
    if ((e = read_header(r, h)) != Err::OK) return e;
  }
  // Much garbage: compact before loading.
  if (h.garbage > 512u * 1024u && h.garbage > h.data_end - data_off(h.cap) - h.garbage) {
    if ((e = app_compact()) != Err::OK) return e;
    Reader r(0);
    if ((e = r.open(g_cal.app_path)) != Err::OK) return e;
    if ((e = read_header(r, h)) != Err::OK) return e;
  }
  Reader r(0), lr(1);
  if ((e = r.open(g_cal.app_path)) != Err::OK) return e;
  if ((e = lr.open(g_cal.app_path)) != Err::OK) return e;
  if ((e = r.seek(META_OFF)) != Err::OK) return e;
  Admission adm;
  for (uint32_t rec = 0; rec < h.hw; ++rec) {
    uint8_t b[META_SIZE];
    if ((e = r.read(b, META_SIZE)) != Err::OK) return e;
    Meta m;
    if (!meta_unpack(b, rec, m)) { ++rep.bad_records; continue; }
    if (!m.used) continue;
    m.e.nskip = m.e.ndone = 0;
    e = m.lists ? read_record(lr, rec, m, nullptr) : Err::OK;
    if (e == Err::CORRUPT) { ++rep.bad_records; continue; }
    if (e != Err::OK) return e;
    ParsedEntry pe;
    pe.rule = m.e.rule;
    pe.nskip = m.e.nskip;
    pe.ndone = m.e.ndone;
    if (!adm.admit(pe)) { ++rep.limit_lines; continue; }
    Rec x;
    if (!idx_build(m.e, rec, x)) return Err::FULL;
    g_idx[g_n++] = x;
    ++rep.entries;
  }
  g_cal.hw = h.hw;
  g_cal.cap = h.cap;
  g_cal.data_end = h.data_end;
  g_cal.garbage = h.garbage;
  g_cal.live = h.live;
  return Err::OK;
}

Err app_read_text(uint32_t rec, char* title, int tcap, char* notes, int ncap, bool* has_notes) {
  Reader r(0);
  Err e = r.open(g_cal.app_path);
  if (e != Err::OK) return e;
  Meta m;
  bool ok = false;
  if ((e = read_meta(r, rec, m, ok)) != Err::OK) return e;
  if (!ok || !m.used) return Err::CORRUPT;
  if ((e = r.seek(m.off)) != Err::OK) return e;
  int n = 0;
  for (;;) {
    const int c = r.get();
    if (c < 0) return r.err() != Err::OK ? r.err() : Err::CORRUPT;
    if (!c) break;
    if (title && n < tcap - 1) title[n++] = char(c);
  }
  if (title) title[n] = 0;
  n = 0;
  bool any = false;
  for (;;) {
    const int c = r.get();
    if (c < 0) return r.err() != Err::OK ? r.err() : Err::CORRUPT;
    if (!c) break;
    any = true;
    if (!notes) break;
    if (n < ncap - 1) notes[n++] = char(c);
  }
  if (notes) notes[n] = 0;
  if (has_notes) *has_notes = any;
  return Err::OK;
}

Err app_commit(EntryId id, const EntryOut* out, const char* title, const char* notes, EntryId* new_id) {
  Err e = journal_recover(g_cal.app_path, g_cal.app_jrn);
  if (e != Err::OK) return e;
  if (!g_cal.app_exists) {
    if ((e = ensure_dir(g_cal.sys_dir)) != Err::OK) return e;
    if ((e = create_empty(g_cal.app_path)) != Err::OK) return e;
    g_cal.app_exists = true;
    g_cal.hw = g_cal.garbage = g_cal.live = 0;
    g_cal.cap = cap_for(0);
    g_cal.data_end = data_off(g_cal.cap);
  }
  const bool add = id == NO_ENTRY;
  if (add && g_cal.hw >= g_cal.cap) {
    // Out of record numbers: compact (renumbers, makes room for 1.5 x the
    // live entries, reloads), then add.
    if ((e = app_compact()) != Err::OK) return e;
    LoadReport rep;
    if ((e = cal_load(Storage::IN_APP, &rep)) != Err::OK) return Err::NEEDS_RELOAD;
    if (g_cal.hw >= g_cal.cap) return Err::FULL;
  }
  const uint32_t rec = add ? g_cal.hw : rec_loc(g_idx[id]);
  Meta old;
  if (!add) {
    Reader r(0);
    bool ok = false;
    if ((e = r.open(g_cal.app_path)) != Err::OK) return e;
    if ((e = read_meta(r, rec, old, ok)) != Err::OK) return e;
    if (!ok || !old.used) return Err::NEEDS_RELOAD;
  }
  Header h;
  h.cap = g_cal.cap;
  h.hw = add ? g_cal.hw + 1 : g_cal.hw;
  h.data_end = g_cal.data_end;
  h.garbage = g_cal.garbage + (add ? 0 : old.len);
  h.live = g_cal.live + (add ? 1 : 0) - (out ? 0 : 1);
  Meta nm;
  if (out) {
    // 1. Append the data record after data_end.
    Writer w(1);
    if ((e = w.open_existing(g_cal.app_path)) != Err::OK) return e;
    if ((e = w.seek(g_cal.data_end)) != Err::OK) { w.abort(); return e; }
    DataOut d(w, rec);
    Reader r(0);
    if ((title == nullptr || notes == nullptr) && !add) {
      if ((e = r.open(g_cal.app_path)) != Err::OK) { w.abort(); return e; }
      if ((e = r.seek(old.off)) != Err::OK) { w.abort(); return e; }
    }
    int tb = 0, nb = 0;
    for (int part = 0; part < 2; ++part) {
      const char* src = part ? notes : title;
      // Skip / copy the old text of this part.
      if (!add && (title == nullptr || notes == nullptr)) {
        for (;;) {
          const int c = r.get();
          if (c < 0) { w.abort(); return r.err() != Err::OK ? r.err() : Err::CORRUPT; }
          if (!c) break;
          if (!src) { d.put(uint8_t(c)); ++(part ? nb : tb); }
        }
      }
      if (src)
        for (int i = 0; src[i]; ++i) { d.put(uint8_t(src[i])); ++(part ? nb : tb); }
      d.put(0);
    }
    r.close();
    d.tail(*out);
    if ((e = w.finish()) != Err::OK) return e;
    meta_from(nm, *out, g_cal.data_end, uint16_t(d.n));
    if (d.n != data_len(tb, nb, *out)) return Err::IO;
    h.data_end = g_cal.data_end + d.n;
  } else {
    nm = old;
    nm.used = false;
  }
  // 2. Meta record + header through the journal.
  uint8_t mb[META_SIZE], hb[HDR];
  meta_pack(mb, rec, nm);
  header_pack(hb, h);
  Journal j;
  if ((e = j.begin(g_cal.app_jrn)) != Err::OK) return e;
  j.patch(META_OFF + META_SIZE * rec, mb, META_SIZE);
  j.patch(0, hb, HDR);
  e = j.commit(g_cal.app_path);
  if (e == Err::NEEDS_RELOAD) {
    // The journal is complete: a replay finishes the change.
    if (journal_recover(g_cal.app_path, g_cal.app_jrn) != Err::OK) return Err::NEEDS_RELOAD;
    e = Err::OK;
  }
  if (e != Err::OK) return e;
  g_cal.hw = h.hw;
  g_cal.cap = h.cap;
  g_cal.data_end = h.data_end;
  g_cal.garbage = h.garbage;
  g_cal.live = h.live;
  const int pos = add ? -1 : int(id);
  if (out) {
    Rec x;
    if (pos >= 0) idx_release(g_idx[pos]);
    idx_build(*out, rec, x);
    const EntryId nid = idx_apply(pos, &x);
    if (new_id) *new_id = nid;
  } else {
    idx_release(g_idx[pos]);
    idx_apply(pos, nullptr);
  }
  return Err::OK;
}

// ---- whole-store writers ------------------------------------------------------------
namespace {

// Text file walker: one line at a time (BOM skipped), front to back.
struct LineWalker {
  Reader& r;
  uint32_t off = 0, size = 0;
  LineInfo last;
  explicit LineWalker(Reader& rr) : r(rr) {}
  Err start() {
    size = r.size();
    off = 0;
    uint8_t b[3];
    if (size >= 3) {
      Err e = r.seek(0);
      if (e == Err::OK) e = r.read(b, 3);
      if (e != Err::OK) return e;
      if (b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) off = 3;
    }
    return r.seek(off);
  }
  // Parses the line at `off` with `sink` (scratch for the lists).
  Err parse(LineSink& sink) {
    Err e = r.seek(off);
    if (e != Err::OK) return e;
    struct Wrap : LineSink {
      LineSink& in;
      bool got = false;
      LineInfo info;
      explicit Wrap(LineSink& s) : in(s) { wants_text = s.wants_text; }
      void title_char(uint8_t c) override { in.title_char(c); }
      void title_end() override { in.title_end(); }
      void notes_char(uint8_t c) override { in.notes_char(c); }
      bool line(const LineInfo& li) override {
        info = li;
        got = true;
        in.line(li);
        return false;
      }
    } wrap(sink);
    LineParser p;
    p.reset(off, scratch(), &wrap);
    while (!wrap.got) {
      const uint8_t* b;
      const uint32_t k = r.avail(&b);
      if (!k) break;
      r.advance(k);
      p.feed_block(b, k);
    }
    if (r.err() != Err::OK) return r.err();
    if (!wrap.got) p.finish();
    if (!wrap.got) return Err::CORRUPT;  // nothing left
    last = wrap.info;
    return Err::OK;
  }
  void next() { off = last.offset + last.length + last.eol; }
  bool done() const { return off >= size; }
};

struct NullSink : LineSink {
  NullSink() { wants_text = false; }
  bool line(const LineInfo&) override { return true; }
};

struct HashSink : LineSink {
  Hash64 h;
  void title_char(uint8_t c) override { h.byte(c); }
  void title_end() override { h.byte(0); }
  void notes_char(uint8_t c) override { h.byte(c); }
  bool line(const LineInfo&) override {
    h.byte(0);
    return true;
  }
};

struct DataSink : LineSink {
  DataOut& d;
  explicit DataSink(DataOut& dd) : d(dd) {}
  void title_char(uint8_t c) override { d.put(c); }
  void title_end() override { d.put(0); }
  void notes_char(uint8_t c) override { d.put(c); }
  bool line(const LineInfo&) override {
    d.put(0);
    return true;
  }
};

EntryOut out_of(const ParsedEntry& p) {
  EntryOut o;
  o.cat = p.cat; o.day = p.day; o.timed = p.timed; o.start = p.start; o.end = p.end;
  o.ticked = p.ticked; o.rule = p.rule;
  o.skip = p.skip; o.nskip = p.nskip; o.done = p.done; o.ndone = p.ndone;
  return o;
}

void report(Progress p, void* user, int pmin, int pmax, uint32_t done, uint32_t total) {
  if (!p) return;
  const int pc = total ? pmin + int(uint64_t(pmax - pmin) * (done > total ? total : done) / total) : pmax;
  p(pc, user);
}

// Index sorted by location (file / record order) while a conversion walks
// the source front to back; idx_sort() restores the display order.
void idx_sort_by_loc() {
  auto key = [](int i) { return rec_loc(g_idx[i]); };
  auto sift = [&](int i, int n) {
    for (;;) {
      int c = 2 * i + 1;
      if (c >= n) return;
      if (c + 1 < n && key(c + 1) > key(c)) ++c;
      if (key(c) <= key(i)) return;
      const Rec t = g_idx[i]; g_idx[i] = g_idx[c]; g_idx[c] = t;
      i = c;
    }
  };
  for (int i = g_n / 2 - 1; i >= 0; --i) sift(i, g_n);
  for (int n = g_n - 1; n > 0; --n) {
    const Rec t = g_idx[0]; g_idx[0] = g_idx[n]; g_idx[n] = t;
    sift(0, n);
  }
}

struct LocOrder {  // RAII: index by location for the duration of a conversion
  LocOrder() { idx_sort_by_loc(); }
  ~LocOrder() { idx_sort(); }
};

}  // namespace

Err app_write_from_text(const char* app_path, Progress progress, void* user, int pmin, int pmax) {
  // The loaded index holds exactly the admitted lines (location = line
  // offset); in location order it says which lines become records.
  LocOrder by_loc;
  uint32_t bytes = 0;
  bool text_exists = false;
  uint32_t tsize = 0;
  Err e = file_size(g_cal.text_path, &tsize, &text_exists);
  if (e != Err::OK) return e;
  Reader r(0), r2(2);
  LineWalker lw(r), lw2(r2);
  if (text_exists) {
    if ((e = r.open(g_cal.text_path)) != Err::OK || (e = r2.open(g_cal.text_path)) != Err::OK) return e;
    if ((e = lw.start()) != Err::OK || (e = lw2.start()) != Err::OK) return e;
  }
  const uint32_t live = uint32_t(g_n);
  SafeSave ss;
  if ((e = safe_begin(ss, app_path)) != Err::OK) return e;
  Writer& w = ss.w;
  // Pass 1 (r): meta records, with data offsets from the parsed sizes.
  // The header needs the total size, so it is written last... the staging
  // writer is sequential: compute the total first with the second reader.
  {
    int k = 0;
    NullSink ns;
    for (; text_exists && !lw2.done() && k < g_n; lw2.next()) {
      if ((e = lw2.parse(ns)) != Err::OK) { safe_abort(ss); return e; }
      if (lw2.last.offset != rec_loc(g_idx[k])) continue;
      if (lw2.last.kind != LineKind::ENTRY) { safe_abort(ss); return Err::NEEDS_RELOAD; }
      bytes += data_len(lw2.last.e.title_bytes, lw2.last.e.notes_bytes, out_of(lw2.last.e));
      ++k;
    }
    if (k != g_n) { safe_abort(ss); return Err::NEEDS_RELOAD; }
  }
  report(progress, user, pmin, pmax, 1, 4);
  Header h;
  h.set_cap(cap_for(live));
  const uint32_t DATA_OFF = data_off(h.cap);
  h.hw = live;
  h.live = live;
  h.data_end = DATA_OFF + bytes;
  uint8_t hb[HDR];
  header_pack(hb, h);
  w.write(hb, HDR);
  for (uint32_t i = HDR; i < META_OFF; ++i) w.put(0);
  {
    uint32_t off = DATA_OFF;
    int k = 0;
    NullSink ns;
    for (; text_exists && !lw.done() && k < g_n; lw.next()) {
      if ((e = lw.parse(ns)) != Err::OK) { safe_abort(ss); return e; }
      if (lw.last.offset != rec_loc(g_idx[k])) continue;
      const EntryOut o = out_of(lw.last.e);
      Meta m;
      meta_from(m, o, off, data_len(lw.last.e.title_bytes, lw.last.e.notes_bytes, o));
      uint8_t mb[META_SIZE];
      meta_pack(mb, uint32_t(k), m);
      w.write(mb, META_SIZE);
      off += m.len;
      ++k;
    }
  }
  for (uint32_t i = META_OFF + META_SIZE * live; i < DATA_OFF; ++i) w.put(0);
  report(progress, user, pmin, pmax, 2, 4);
  // Pass 3: data records, one parse per line.
  if (text_exists && ((e = lw2.start()) != Err::OK)) { safe_abort(ss); return e; }
  {
    int k = 0;
    NullSink ns;
    for (; text_exists && !lw2.done() && k < g_n; lw2.next()) {
      const bool take = lw2.off == rec_loc(g_idx[k]);
      if (take) {
        DataOut d(w, uint32_t(k));
        DataSink ds(d);
        if ((e = lw2.parse(ds)) != Err::OK) { safe_abort(ss); return e; }
        d.tail(out_of(lw2.last.e));
        ++k;
        if ((k & 255) == 0) report(progress, user, pmin, pmax, 2 * 1024 + uint32_t(k) * 1024 / live, 4 * 1024);
      } else if ((e = lw2.parse(ns)) != Err::OK) {
        safe_abort(ss);
        return e;
      }
    }
  }
  if (w.err() != Err::OK || w.written() != DATA_OFF + bytes) {
    const Err we = w.err() != Err::OK ? w.err() : Err::NEEDS_RELOAD;
    safe_abort(ss);
    return we;
  }
  bool inst = false;
  e = safe_commit(ss, &inst);
  report(progress, user, pmin, pmax, 4, 4);
  return inst ? Err::OK : (e == Err::OK ? Err::IO : e);
}

Err verify_text_against_app(const char* text_path, const char* app_path, Progress progress, void* user, int pmin,
                            int pmax) {
  // Every text entry line that is a record (all of them when the text was
  // written from the store; the loaded ones when the text is the source)
  // against the used app records, in order.
  const bool text_is_source = g_cal.storage == Storage::TEXT;
  Reader r(0), ad(1), am(2);
  Err e = r.open(text_path);
  const bool text_exists = e == Err::OK;
  if (e != Err::OK && e != Err::NOT_FOUND) return e;
  if ((e = ad.open(app_path)) != Err::OK || (e = am.open(app_path)) != Err::OK) return e;
  Header h;
  if ((e = read_header(am, h)) != Err::OK) return e;
  LineWalker lw(r);
  if (text_exists && (e = lw.start()) != Err::OK) return e;
  Admission adm;
  uint32_t rec = 0, compared = 0;
  for (; text_exists && !lw.done(); lw.next()) {
    HashSink hs;
    if ((e = lw.parse(hs)) != Err::OK) return e;
    if (lw.last.kind != LineKind::ENTRY || (text_is_source && !adm.admit(lw.last.e))) continue;
    hash_fields(hs.h, out_of(lw.last.e));
    Meta m;
    bool ok = false;
    for (;;) {  // next used record
      if (rec >= h.hw) return Err::VERIFY;
      if ((e = read_meta(am, rec, m, ok)) != Err::OK) return e;
      if (!ok) return Err::VERIFY;
      if (m.used) break;
      ++rec;
    }
    Hash64 ah;
    e = hash_record(ad, rec, m, ah);
    if (e == Err::CORRUPT) return Err::VERIFY;
    if (e != Err::OK) return e;
    if (ah.v != hs.h.v) return Err::VERIFY;
    ++rec;
    ++compared;
    if ((compared & 255) == 0) report(progress, user, pmin, pmax, lw.off, lw.size);
  }
  for (; rec < h.hw; ++rec) {  // no further used records
    Meta m;
    bool ok = false;
    if ((e = read_meta(am, rec, m, ok)) != Err::OK) return e;
    if (ok && m.used) return Err::VERIFY;
  }
  if (h.live != compared) return Err::VERIFY;
  report(progress, user, pmin, pmax, 1, 1);
  return Err::OK;
}

Err text_write_from_app(const char* text_path, const char* app_path, Progress progress, void* user, int pmin,
                        int pmax) {
  // Records in record order; the day lists come from the RAM index (in
  // location order), so each data record is read front to back once.
  LocOrder by_loc;
  Reader am(0), ad(2);
  Err e = am.open(app_path);
  const bool exists = e == Err::OK;
  if (e != Err::OK && e != Err::NOT_FOUND) return e;
  Header h;
  if (exists && ((e = read_header(am, h)) != Err::OK || (e = ad.open(app_path)) != Err::OK)) return e;
  if (!exists) h = Header();
  SafeSave ss;
  if ((e = safe_begin(ss, text_path)) != Err::OK) return e;
  Writer& w = ss.w;
  int k = 0;
  for (uint32_t rec = 0; rec < h.hw; ++rec) {
    Meta m;
    bool ok = false;
    if ((e = read_meta(am, rec, m, ok)) != Err::OK) { safe_abort(ss); return e; }
    if (!ok || !m.used) continue;
    while (k < g_n && rec_loc(g_idx[k]) < rec) ++k;
    if (k < g_n && rec_loc(g_idx[k]) == rec) {
      EntryOut ro;
      rec_describe(g_idx[k], ro);
      m.e.skip = ro.skip; m.e.nskip = ro.nskip;
      m.e.done = ro.done; m.e.ndone = ro.ndone;
    } else if (m.lists) {  // a record over the RAM limits: read its lists first
      if ((e = read_record(ad, rec, m, nullptr)) != Err::OK) { safe_abort(ss); return e; }
    }
    write_head(w, m.e);
    Escaper esc(w);
    if ((e = ad.seek(m.off)) != Err::OK) { safe_abort(ss); return e; }
    for (int part = 0; part < 2; ++part) {
      bool first = true;
      for (;;) {
        const int c = ad.get();
        if (c < 0) { safe_abort(ss); return ad.err() != Err::OK ? ad.err() : Err::CORRUPT; }
        if (!c) break;
        if (part && first) w.puts(" // ");
        first = false;
        esc.push(uint8_t(c));
      }
      esc.finish();
      if (!part) write_repeat(w, m.e);
    }
    w.put('\n');
    if ((rec & 255) == 0) report(progress, user, pmin, pmax, rec, h.hw);
  }
  if (w.err() != Err::OK) { const Err we = w.err(); safe_abort(ss); return we; }
  if (w.written() > MAX_TEXT_FILE_BYTES) { safe_abort(ss); return Err::TOO_LARGE; }
  bool inst = false;
  e = safe_commit(ss, &inst);
  report(progress, user, pmin, pmax, 1, 1);
  return inst ? Err::OK : (e == Err::OK ? Err::IO : e);
}

Err app_compact() {
  Reader am(0), ad(2);
  Err e = am.open(g_cal.app_path);
  if (e == Err::OK) e = ad.open(g_cal.app_path);
  if (e != Err::OK) return e;
  Header h;
  if ((e = read_header(am, h)) != Err::OK) return e;
  uint32_t live = 0, bytes = 0;
  for (uint32_t rec = 0; rec < h.hw; ++rec) {
    Meta m;
    bool ok = false;
    if ((e = read_meta(am, rec, m, ok)) != Err::OK) return e;
    if (ok && m.used) { ++live; bytes += m.len; }
  }
  SafeSave ss;
  if ((e = safe_begin(ss, g_cal.app_path)) != Err::OK) return e;
  Writer& w = ss.w;
  Header nh;
  nh.set_cap(cap_for(live));
  const uint32_t DATA_OFF = data_off(nh.cap);
  nh.hw = nh.live = live;
  nh.data_end = DATA_OFF + bytes;
  uint8_t hb[HDR];
  header_pack(hb, nh);
  w.write(hb, HDR);
  for (uint32_t i = HDR; i < META_OFF; ++i) w.put(0);
  uint32_t nrec = 0, off = DATA_OFF;
  for (uint32_t rec = 0; rec < h.hw; ++rec) {
    Meta m;
    bool ok = false;
    if ((e = read_meta(am, rec, m, ok)) != Err::OK) { safe_abort(ss); return e; }
    if (!ok || !m.used) continue;
    m.off = off;
    uint8_t mb[META_SIZE];
    meta_pack(mb, nrec, m);
    w.write(mb, META_SIZE);
    off += m.len;
    ++nrec;
  }
  for (uint32_t i = META_OFF + META_SIZE * nrec; i < DATA_OFF; ++i) w.put(0);
  nrec = 0;
  for (uint32_t rec = 0; rec < h.hw; ++rec) {
    Meta m;
    bool ok = false;
    if ((e = read_meta(am, rec, m, ok)) != Err::OK) { safe_abort(ss); return e; }
    if (!ok || !m.used) continue;
    if (m.len < 10 || (e = ad.seek(m.off)) != Err::OK) { safe_abort(ss); return e != Err::OK ? e : Err::CORRUPT; }
    DataOut d(w, nrec);
    for (uint32_t i = 0; i < uint32_t(m.len - 4); ++i) {
      const int c = ad.get();
      if (c < 0) { safe_abort(ss); return ad.err() != Err::OK ? ad.err() : Err::CORRUPT; }
      d.put(uint8_t(c));
    }
    for (int i = 0; i < 4; ++i) ad.get();
    uint8_t cb[4];
    wr32(cb, d.crc);
    w.write(cb, 4);
    ++nrec;
  }
  am.close();
  ad.close();
  if (w.err() != Err::OK) { const Err we = w.err(); safe_abort(ss); return we; }
  if ((e = remove_file(g_cal.app_jrn)) != Err::OK) { safe_abort(ss); return e; }
  bool inst = false;
  e = safe_commit(ss, &inst);
  return inst ? Err::OK : (e == Err::OK ? Err::IO : e);
}

// ---- conversion API -------------------------------------------------------------------
uint32_t cal_convert_lost_lines() {
  if (!g_cal.loaded) return 0;
  return g_cal.storage == Storage::TEXT
             ? g_cal.report.unknown_lines + g_cal.report.limit_lines + g_cal.report.comment_lines
                                        : g_cal.report.limit_lines + g_cal.report.bad_records;
}

static void remove_safe_leftovers(const char* target) {
  // Leftovers of a safe save of a copy that is being dropped.
  char t[PATH_LEN];
  str_copy(t, PATH_LEN, target);
  int n = str_len(t);
  int dot = n;
  for (int i = n - 1; i >= 0 && t[i] != '/'; --i)
    if (t[i] == '.') { dot = i; break; }
  const bool upper = dot < n && t[dot + 1] >= 'A' && t[dot + 1] <= 'Z';
  static const char* const EXT[3] = {"gpn", "gpb", "gpj"};
  for (const char* x : EXT) {
    t[dot] = '.';
    for (int i = 0; i < 3; ++i) t[dot + 1 + i] = char(upper ? x[i] - 32 : x[i]);
    t[dot + 4] = 0;
    remove_file(t);
  }
}

Err cal_convert(Storage target, Progress progress, void* user) {
  if (!g_cal.loaded) return Err::NOT_LOADED;
  if (target == g_cal.storage) return Err::INVALID;
  g_cal.convert_ready = false;
  Err e = core_make_dirs();
  if (e != Err::OK) return e;
  if (target == Storage::IN_APP) {
    if ((e = safe_recover(g_cal.text_path)) != Err::OK) return e;
    // A stale journal must never be replayed onto the new copy.
    if ((e = remove_file(g_cal.app_jrn)) != Err::OK) return e;
    e = app_write_from_text(g_cal.app_path, progress, user, 0, 70);
    if (e == Err::OK) e = verify_text_against_app(g_cal.text_path, g_cal.app_path, progress, user, 70, 100);
    if (e != Err::OK) {
      remove_file(g_cal.app_path);
      remove_safe_leftovers(g_cal.app_path);
      return e;
    }
  } else {
    if ((e = journal_recover(g_cal.app_path, g_cal.app_jrn)) != Err::OK) return e;
    e = text_write_from_app(g_cal.text_path, g_cal.app_path, progress, user, 0, 50);
    if (e == Err::OK) e = verify_text_against_app(g_cal.text_path, g_cal.app_path, progress, user, 50, 100);
    if (e != Err::OK) {
      remove_file(g_cal.text_path);
      remove_safe_leftovers(g_cal.text_path);
      return e;
    }
  }
  g_cal.convert_ready = true;
  g_cal.convert_target = target;
  return Err::OK;
}

Err cal_convert_finish() {
  if (!g_cal.convert_ready) return Err::INVALID;
  g_cal.convert_ready = false;
  const Storage target = g_cal.convert_target;
  Err e;
  if (target == Storage::IN_APP) {
    e = remove_file(g_cal.text_path);
    remove_safe_leftovers(g_cal.text_path);
  } else {
    e = remove_file(g_cal.app_path);
    if (e == Err::OK) e = remove_file(g_cal.app_jrn);
    remove_safe_leftovers(g_cal.app_path);
  }
  const Err l = cal_load(target);
  return e != Err::OK ? e : l;
}

Err cal_convert_cancel() {
  if (!g_cal.convert_ready) return Err::OK;
  g_cal.convert_ready = false;
  if (g_cal.convert_target == Storage::IN_APP) {
    remove_safe_leftovers(g_cal.app_path);
    return remove_file(g_cal.app_path);
  }
  remove_safe_leftovers(g_cal.text_path);
  return remove_file(g_cal.text_path);
}

}  // namespace planner
