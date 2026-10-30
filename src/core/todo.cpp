// To-do list: todo.txt and the in-app store GBAPLANNER.SYS/TODO.DAT.
//
// todo.txt: "- [ ] title" / "- [x] title" lines; other lines are kept where
// they are. A save fills the item lines of the old file, in their order, with
// the items in display order; extra items are appended, missing ones drop
// their line. The save is a safe whole-file replacement.
//
// TODO.DAT: 512-byte header sector, then 300 slots of 256 bytes (slot s at
// 512 + 256 s): CRC32 (slot number + bytes 4..255), flags (1 used, 2 ticked),
// title length, 0, 0, order key, title. Open items are ordered by key
// ascending (list position), ticked items by key descending (tick sequence).
// Every change writes whole slots through the redo journal TODO.JRN.
#include "core/todo.h"
#include "fileio.h"

namespace planner {
namespace {

constexpr uint32_t T_HDR = 512;
constexpr uint32_t SLOT = 256;
constexpr uint8_t TMAGIC[8] = {'G', 'B', 'P', 'T', 'O', 'D', 'O', '1'};
constexpr uint32_t W_LOC = 0x7FFFFF;
constexpr uint32_t W_NEW = 1u << 29;    // title comes from the caller (transient)
constexpr uint32_t W_DIRTY = 1u << 30;  // slot must be written (transient)
constexpr uint32_t W_TICKED = 1u << 31;

struct Item {
  uint32_t w;    // location (text: line offset; in-app: slot) | flags
  uint32_t key;  // in-app order key
};

struct TodoState {
  char text_path[PATH_LEN], app_path[PATH_LEN], app_jrn[PATH_LEN], sys_dir[PATH_LEN];
  Storage storage = Storage::IN_APP;
  bool loaded = false;
  TodoReport report;
  int n = 0, open = 0;
  int ndel = 0;  // deleted items parked after n during a save
  uint8_t eol = 1;
  uint32_t max_pos = 0, max_seq = 0;
  bool app_exists = false;
  bool convert_ready = false;
  Storage convert_target = Storage::IN_APP;
};

PLANNER_EWRAM Item g_items[MAX_TODOS + 1];
TodoState g_t;
uint8_t g_slot[SLOT];

bool ticked(const Item& it) { return it.w & W_TICKED; }
uint32_t loc(const Item& it) { return it.w & W_LOC; }

bool title_ok(const char* s) {
  if (!s || !s[0]) return false;
  int chars = 0;
  for (int i = 0; s[i]; ++i) {
    const uint8_t c = uint8_t(s[i]);
    if (c == '\n' || c == '\r') return false;
    chars += (c & 0xC0) != 0x80;
  }
  return chars <= MAX_TITLE_CHARS && str_len(s) <= MAX_TITLE_BYTES;
}

// ---- todo.txt lines ---------------------------------------------------------------
struct LineScan {
  uint32_t offset = 0, length = 0;
  uint8_t eol = 0;
  bool item = false, is_ticked = false, comment = false, empty = false;
};

// Classifies the line at `off`.
Err scan_line(Reader& r, uint32_t off, LineScan& ls) {
  Err e = r.seek(off);
  if (e != Err::OK) return e;
  ls = LineScan();
  ls.offset = off;
  uint8_t pre[6];
  int np = 0, chars = 0;
  bool space_only = true, pending_cr = false, bad = false;
  uint32_t len = 0;
  for (;;) {
    const int c = r.get();
    if (c < 0) break;
    if (pending_cr) {
      pending_cr = false;
      if (c == '\n') { ls.eol = 2; break; }
      ++len;  // a lone CR is content (such a line is not an item)
      bad = true;
      space_only = false;
    }
    if (c == '\r') { pending_cr = true; continue; }
    if (c == '\n') { ls.eol = 1; break; }
    ++len;
    if (c != ' ' && c != '\t') space_only = false;
    if (c == 0) bad = true;
    if (np < 6) pre[np++] = uint8_t(c);
    else if ((c & 0xC0) != 0x80) ++chars;
  }
  if (r.err() != Err::OK) return r.err();
  if (pending_cr) { ++len; bad = true; space_only = false; }
  ls.length = len;
  if (len == 0 || space_only) { ls.empty = true; return Err::OK; }
  if (pre[0] == '#') { ls.comment = true; return Err::OK; }
  if (!bad && np == 6 && pre[0] == '-' && pre[1] == ' ' && pre[2] == '[' && pre[4] == ']' && pre[5] == ' ' &&
      (pre[3] == ' ' || pre[3] == 'x' || pre[3] == 'X') && chars >= 1 && chars <= MAX_TITLE_CHARS &&
      len - 6 <= uint32_t(MAX_TITLE_BYTES)) {
    ls.item = true;
    ls.is_ticked = pre[3] != ' ';
  }
  return Err::OK;
}

// Copies the title of the item line at `off` (to out and / or a writer).
Err read_text_title(Reader& r, uint32_t off, char* out, int cap, Writer* w) {
  Err e = r.seek(off + 6);
  if (e != Err::OK) return e;
  int n = 0;
  for (int i = 0; i < MAX_TITLE_BYTES; ++i) {
    const int c = r.get();
    if (c < 0 || c == '\n' || c == '\r') break;
    if (out && n < cap - 1) out[n++] = char(c);
    if (w) w->put(uint8_t(c));
  }
  if (r.err() != Err::OK) return r.err();
  if (out) out[n] = 0;
  return Err::OK;
}

uint32_t text_start(Reader& r) {
  uint8_t b[3];
  if (r.size() >= 3 && r.seek(0) == Err::OK && r.read(b, 3) == Err::OK && b[0] == 0xEF && b[1] == 0xBB &&
      b[2] == 0xBF)
    return 3;
  return 0;
}

Err text_load(TodoReport& rep) {
  Err e = safe_recover(g_t.text_path);
  if (e != Err::OK) return e;
  Reader r(0);
  uint32_t size = 0;
  e = r.open(g_t.text_path, &size);
  g_t.eol = 1;
  if (e == Err::NOT_FOUND) return Err::OK;
  if (e != Err::OK) return e;
  if (size > MAX_TEXT_FILE_BYTES) return Err::TOO_LARGE;
  bool eol_seen = false;
  // Two passes keep "open first": open items, then ticked ones in file order.
  for (int pass = 0; pass < 2; ++pass) {
    uint32_t off = text_start(r);
    int items = 0;
    while (off < size) {
      LineScan ls;
      if ((e = scan_line(r, off, ls)) != Err::OK) return e;
      if (pass == 0) {
        if (ls.eol && !eol_seen) { eol_seen = true; g_t.eol = ls.eol; }
        if (ls.empty) ++rep.empty_lines;
        else if (ls.comment) ++rep.comment_lines;
        else if (!ls.item) ++rep.unknown_lines;
      }
      if (ls.item) {
        if (items < MAX_TODOS) {
          if (ls.is_ticked == (pass == 1)) {
            g_items[g_t.n].w = ls.offset | (ls.is_ticked ? W_TICKED : 0);
            g_items[g_t.n].key = 0;
            ++g_t.n;
          }
        } else if (pass == 0) {
          ++rep.limit_lines;
        }
        ++items;
      }
      off += ls.length + ls.eol;
      if (!ls.eol) break;
    }
    if (pass == 0) g_t.open = g_t.n;
  }
  rep.items = uint32_t(g_t.n);
  return Err::OK;
}

// Rewrites todo.txt from g_items (titles of W_NEW items from `title`).
Err text_save(const char* title) {
  SafeSave ss;
  Err e = safe_begin(ss, g_t.text_path);
  if (e != Err::OK) return e;
  Writer& w = ss.w;
  Reader r(0);
  uint32_t size = 0;
  e = r.open(g_t.text_path, &size);
  const bool exists = e == Err::OK;
  if (e != Err::OK && e != Err::NOT_FOUND) { safe_abort(ss); return e; }
  int next = 0, items = 0;
  auto emit = [&](const Item& it) -> Err {
    w.puts(ticked(it) ? "- [x] " : "- [ ] ");
    if (it.w & W_NEW) { w.puts(title); return Err::OK; }
    return read_text_title(r, loc(it), nullptr, 0, &w);
  };
  if (exists) {
    uint32_t off = text_start(r);
    if (off) w.write("\xEF\xBB\xBF", 3);
    while (off < size) {
      LineScan ls;
      if ((e = scan_line(r, off, ls)) != Err::OK) { safe_abort(ss); return e; }
      const bool slot = ls.item && items++ < MAX_TODOS;
      if (slot) {
        if (next < g_t.n) {
          if ((e = emit(g_items[next++])) != Err::OK) { safe_abort(ss); return e; }
          if (ls.eol == 2) w.put('\r');
          if (ls.eol) w.put('\n');
        }
      } else {
        if ((e = r.seek(off)) != Err::OK) { safe_abort(ss); return e; }
        for (uint32_t i = 0; i < ls.length + ls.eol; ++i) {
          const int c = r.get();
          if (c < 0) { safe_abort(ss); return r.err() != Err::OK ? r.err() : Err::CORRUPT; }
          w.put(uint8_t(c));
        }
      }
      off += ls.length + ls.eol;
      if (!ls.eol) break;
    }
  }
  for (; next < g_t.n; ++next) {
    if (w.written() && w.last() != '\n') {
      if (g_t.eol == 2) w.put('\r');
      w.put('\n');
    }
    if ((e = emit(g_items[next])) != Err::OK) { safe_abort(ss); return e; }
    if (g_t.eol == 2) w.put('\r');
    w.put('\n');
  }
  r.close();
  if (w.err() != Err::OK) { const Err we = w.err(); safe_abort(ss); return we; }
  if (w.written() > MAX_TEXT_FILE_BYTES) { safe_abort(ss); return Err::TOO_LARGE; }
  bool inst = false;
  e = safe_commit(ss, &inst);
  return inst ? Err::OK : (e == Err::OK ? Err::IO : e);
}

// ---- TODO.DAT -------------------------------------------------------------------------
void slot_pack(uint8_t* b, uint32_t s, bool is_ticked, uint32_t key, const char* title, int tlen) {
  for (uint32_t i = 0; i < SLOT; ++i) b[i] = 0;
  b[4] = uint8_t(1 | (is_ticked ? 2 : 0));
  b[5] = uint8_t(tlen);
  wr32(b + 8, key);
  for (int i = 0; i < tlen; ++i) b[12 + i] = uint8_t(title[i]);
  uint8_t sb[4];
  wr32(sb, s);
  wr32(b, crc32_update(crc32_update(0, sb, 4), b + 4, SLOT - 4));
}

bool slot_check(const uint8_t* b, uint32_t s) {
  uint8_t sb[4];
  wr32(sb, s);
  return rd32(b) == crc32_update(crc32_update(0, sb, 4), b + 4, SLOT - 4) && b[5] >= 1 &&
         b[5] <= MAX_TITLE_BYTES;
}

void header_pack(uint8_t* b) {  // 64 bytes
  for (uint32_t i = 0; i < 64; ++i) b[i] = 0;
  for (int i = 0; i < 8; ++i) b[i] = TMAGIC[i];
  wr32(b + 8, 1);
  wr32(b + 12, MAX_TODOS);
  wr32(b + 16, SLOT);
  wr32(b + 60, crc32(b, 60));
}

bool header_ok(const uint8_t* b) {
  for (int i = 0; i < 8; ++i)
    if (b[i] != TMAGIC[i]) return false;
  return rd32(b + 60) == crc32(b, 60) && rd32(b + 8) == 1 && rd32(b + 12) == MAX_TODOS && rd32(b + 16) == SLOT;
}

Err app_load(TodoReport& rep) {
  Err e = journal_recover(g_t.app_path, g_t.app_jrn);
  if (e == Err::OK) e = safe_recover(g_t.app_path);
  if (e != Err::OK) return e;
  Reader r(0);
  uint32_t size = 0;
  e = r.open(g_t.app_path, &size);
  g_t.app_exists = e == Err::OK;
  g_t.max_pos = g_t.max_seq = 0;
  if (e == Err::NOT_FOUND) return Err::OK;
  if (e != Err::OK) return e;
  uint8_t* b = g_slot;
  if ((e = r.read(b, 64)) != Err::OK) return e;
  if (!header_ok(b)) return Err::CORRUPT;
  uint32_t slots = size > T_HDR ? (size - T_HDR) / SLOT : 0;
  if (slots > uint32_t(MAX_TODOS)) slots = MAX_TODOS;
  if ((e = r.seek(T_HDR)) != Err::OK) return e;
  int nopen = 0;
  for (uint32_t s = 0; s < slots; ++s) {
    if ((e = r.read(b, SLOT)) != Err::OK) return e;
    if (!(b[4] & 1)) continue;
    if (!slot_check(b, s)) { ++rep.bad_records; continue; }
    Item it;
    it.w = s | ((b[4] & 2) ? W_TICKED : 0);
    it.key = rd32(b + 8);
    // insertion: open items first by key ascending, then ticked by key descending
    int at = g_t.n;
    const bool tk = ticked(it);
    while (at > 0) {
      const Item& p = g_items[at - 1];
      const bool before = tk ? (ticked(p) && p.key < it.key) : (ticked(p) || p.key > it.key);
      if (!before) break;
      --at;
    }
    for (int k = g_t.n; k > at; --k) g_items[k] = g_items[k - 1];
    g_items[at] = it;
    ++g_t.n;
    if (tk) { if (it.key > g_t.max_seq) g_t.max_seq = it.key; }
    else { ++nopen; if (it.key > g_t.max_pos) g_t.max_pos = it.key; }
  }
  g_t.open = nopen;
  rep.items = uint32_t(g_t.n);
  return Err::OK;
}

Err app_create() {
  Err e = ensure_dir(g_t.sys_dir);
  if (e != Err::OK) return e;
  SafeSave ss;
  if ((e = safe_begin(ss, g_t.app_path)) != Err::OK) return e;
  uint8_t h[64];
  header_pack(h);
  ss.w.write(h, 64);
  for (uint32_t i = 64; i < T_HDR; ++i) ss.w.put(0);
  bool inst = false;
  e = safe_commit(ss, &inst);
  if (!inst) return e == Err::OK ? Err::IO : e;
  g_t.app_exists = true;
  return Err::OK;
}

// Writes the dirty items and frees the parked deleted ones.
Err app_save(const char* title) {
  Err e = journal_recover(g_t.app_path, g_t.app_jrn);
  if (e != Err::OK) return e;
  if (!g_t.app_exists && (e = app_create()) != Err::OK) return e;
  Reader r(0);
  if ((e = r.open(g_t.app_path)) != Err::OK) return e;
  const uint32_t size = r.size();
  Journal j;
  if ((e = j.begin(g_t.app_jrn)) != Err::OK) return e;
  for (int i = 0; i < g_t.n + g_t.ndel; ++i) {
    Item& it = g_items[i];
    const uint32_t s = loc(it);
    const uint32_t off = T_HDR + SLOT * s;
    if (i >= g_t.n) {  // deleted
      const uint8_t zero = 0;
      if (off + 4 < size) j.patch(off + 4, &zero, 1);
      continue;
    }
    if (!(it.w & W_DIRTY)) continue;
    char tbuf[MAX_TITLE_BYTES + 1];
    const char* t = title;
    int tl = 0;
    if (!(it.w & W_NEW)) {
      if ((e = r.seek(off)) != Err::OK) return e;
      if ((e = r.read(g_slot, SLOT)) != Err::OK) return e;
      if (!slot_check(g_slot, s)) return Err::CORRUPT;
      tl = g_slot[5];
      for (int k = 0; k < tl; ++k) tbuf[k] = char(g_slot[12 + k]);
      t = tbuf;
    } else {
      tl = str_len(title);
    }
    slot_pack(g_slot, s, ticked(it), it.key, t, tl);
    j.patch(off, g_slot, SLOT);
  }
  r.close();
  e = j.commit(g_t.app_path);
  if (e == Err::NEEDS_RELOAD && journal_recover(g_t.app_path, g_t.app_jrn) == Err::OK) e = Err::OK;
  return e;
}

// ---- edits ------------------------------------------------------------------------------
Err reload() {
  const Storage s = g_t.storage;
  TodoReport rep;
  return todo_load(s, &rep);
}

// Persists the RAM list after an edit. On failure RAM is reloaded from the card.
Err persist(const char* title) {
  Err e = g_t.storage == Storage::TEXT ? text_save(title) : app_save(title);
  g_t.ndel = 0;
  for (int i = 0; i < g_t.n; ++i) g_items[i].w &= ~(W_NEW | W_DIRTY);
  if (e != Err::OK || g_t.storage == Storage::TEXT) {
    // text: item offsets moved; failure: RAM may be ahead of the card.
    const Err l = reload();
    if (e == Err::OK) e = l;
    else if (l != Err::OK) e = Err::NEEDS_RELOAD;
  }
  return e;
}

void move_item(int from, int to) {
  const Item t = g_items[from];
  if (from < to)
    for (int k = from; k < to; ++k) g_items[k] = g_items[k + 1];
  else
    for (int k = from; k > to; --k) g_items[k] = g_items[k - 1];
  g_items[to] = t;
}

// Parks item i after the list (deleted; its slot is freed by app_save).
void park(int i) {
  const bool was_open = !ticked(g_items[i]);
  move_item(i, g_t.n - 1);  // the list shrinks; parked items follow it
  --g_t.n;
  ++g_t.ndel;
  if (was_open) --g_t.open;
}

int free_slot() {
  uint32_t used[(MAX_TODOS + 31) / 32] = {};
  for (int i = 0; i < g_t.n + g_t.ndel; ++i) {
    const uint32_t s = loc(g_items[i]);
    if (s < uint32_t(MAX_TODOS)) used[s / 32] |= 1u << (s % 32);
  }
  for (int s = 0; s < MAX_TODOS; ++s)
    if (!(used[s / 32] >> (s % 32) & 1)) return s;
  return -1;
}

bool ready(int i) { return g_t.loaded && i >= 0 && i < g_t.n; }

// ---- conversion helpers --------------------------------------------------------------
Err item_title_hash(Storage s, const Item& it, Hash64& h) {
  char t[MAX_TITLE_BYTES + 1];
  Err e;
  if (s == Storage::TEXT) {
    Reader r(1);
    if ((e = r.open(g_t.text_path)) != Err::OK) return e;
    if ((e = read_text_title(r, loc(it), t, int(sizeof t), nullptr)) != Err::OK) return e;
  } else {
    Reader r(1);
    if ((e = r.open(g_t.app_path)) != Err::OK) return e;
    uint8_t b[SLOT];
    if ((e = r.seek(T_HDR + SLOT * loc(it))) != Err::OK) return e;
    if ((e = r.read(b, SLOT)) != Err::OK) return e;
    if (!slot_check(b, loc(it))) return Err::CORRUPT;
    for (int k = 0; k < b[5]; ++k) t[k] = char(b[12 + k]);
    t[b[5]] = 0;
  }
  h.byte(ticked(it));
  h.bytes(t, size_t(str_len(t)));
  h.byte(0);
  return Err::OK;
}

}  // namespace

// Ticked to-dos shown (Settings); not data, so todo_reset keeps it.
static int g_visible = TODO_VISIBLE_TICKED_DEFAULT;

// ---- public API -------------------------------------------------------------------------
void todo_reset(const char* root) {
  path_join(g_t.text_path, root, "todo.txt");
  path_join(g_t.sys_dir, root, "GBAPLANNER.SYS");
  path_join(g_t.app_path, g_t.sys_dir, "TODO.DAT");
  path_join(g_t.app_jrn, g_t.sys_dir, "TODO.JRN");
  g_t.loaded = false;
  g_t.n = g_t.open = g_t.ndel = 0;
  g_t.convert_ready = false;
}

Err todo_load(Storage s, TodoReport* report) {
  g_t.loaded = false;
  g_t.convert_ready = false;
  g_t.storage = s;
  g_t.n = g_t.open = g_t.ndel = 0;
  TodoReport rep;
  Err e = s == Storage::TEXT ? text_load(rep) : app_load(rep);
  if (e != Err::OK) g_t.n = g_t.open = 0;
  else g_t.loaded = true;
  g_t.report = rep;
  if (report) *report = rep;
  return e;
}

bool todo_loaded() { return g_t.loaded; }
Storage todo_storage() { return g_t.storage; }
int todo_count() { return g_t.n; }
int todo_open_count() { return g_t.open; }
int todo_ticked_count() { return g_t.n - g_t.open; }
int todo_hidden_count() {
  const int t = g_t.n - g_t.open - g_visible;
  return t > 0 ? t : 0;
}
bool todo_is_ticked(int i) { return ready(i) && ticked(g_items[i]); }
bool todo_is_hidden(int i) { return ready(i) && i >= g_t.open + g_visible; }

void todo_set_visible_ticked(int n) {
  g_visible = n < 0 ? 0 : n > TODO_VISIBLE_TICKED_MAX ? TODO_VISIBLE_TICKED_MAX : n;
}
int todo_visible_ticked() { return g_visible; }

Err todo_apply_visible(bool delete_old) {
  if (!g_t.loaded) return Err::NOT_LOADED;
  if (!delete_old || !todo_hidden_count()) return Err::OK;
  while (g_t.n > g_t.open + g_visible) park(g_t.n - 1);
  return persist(nullptr);
}

Err todo_title(int i, char* out, int cap) {
  if (!ready(i)) return Err::NOT_FOUND;
  if (g_t.storage == Storage::TEXT) {
    Reader r(0);
    Err e = r.open(g_t.text_path);
    if (e != Err::OK) return e;
    return read_text_title(r, loc(g_items[i]), out, cap, nullptr);
  }
  Reader r(0);
  Err e = r.open(g_t.app_path);
  if (e != Err::OK) return e;
  uint8_t b[SLOT];
  if ((e = r.seek(T_HDR + SLOT * loc(g_items[i]))) != Err::OK) return e;
  if ((e = r.read(b, SLOT)) != Err::OK) return e;
  if (!slot_check(b, loc(g_items[i]))) return Err::CORRUPT;
  int n = 0;
  for (; n < b[5] && n < cap - 1; ++n) out[n] = char(b[12 + n]);
  out[n] = 0;
  return Err::OK;
}

Err todo_add(const char* title, int* index) {
  if (!g_t.loaded) return Err::NOT_LOADED;
  if (!title_ok(title)) return Err::INVALID;
  if (g_t.n >= MAX_TODOS) return Err::FULL;
  Item it;
  it.w = W_NEW | W_DIRTY;
  it.key = ++g_t.max_pos;
  if (g_t.storage == Storage::IN_APP) {
    const int s = free_slot();
    if (s < 0) return Err::FULL;
    it.w |= uint32_t(s);
  }
  for (int k = g_t.n; k > g_t.open; --k) g_items[k] = g_items[k - 1];
  g_items[g_t.open] = it;
  const int at = g_t.open;
  ++g_t.n;
  ++g_t.open;
  Err e = persist(title);
  if (e == Err::OK && index) *index = at;
  return e;
}

Err todo_edit(int i, const char* title) {
  if (!ready(i)) return Err::NOT_FOUND;
  if (!title_ok(title)) return Err::INVALID;
  g_items[i].w |= W_NEW | W_DIRTY;
  return persist(title);
}

Err todo_delete(int i) {
  if (!ready(i)) return Err::NOT_FOUND;
  park(i);
  return persist(nullptr);
}

Err todo_tick(int i, bool delete_old) {
  if (!ready(i)) return Err::NOT_FOUND;
  if (ticked(g_items[i])) return Err::OK;
  Item& it = g_items[i];
  it.w |= W_TICKED | W_DIRTY;
  it.key = ++g_t.max_seq;
  move_item(i, g_t.open - 1);  // first ticked position
  --g_t.open;
  if (delete_old)
    while (g_t.n > g_t.open + g_visible) park(g_t.n - 1);
  return persist(nullptr);
}

Err todo_untick(int i) {
  if (!ready(i)) return Err::NOT_FOUND;
  if (!ticked(g_items[i])) return Err::OK;
  Item& it = g_items[i];
  it.w = (it.w & ~W_TICKED) | W_DIRTY;
  it.key = ++g_t.max_pos;
  move_item(i, g_t.open);
  ++g_t.open;
  return persist(nullptr);
}

Err todo_move(int i, int dir) {
  if (!ready(i)) return Err::NOT_FOUND;
  const int j = i + (dir < 0 ? -1 : 1);
  if (i >= g_t.open || j < 0 || j >= g_t.open) return Err::INVALID;
  const uint32_t k = g_items[i].key;
  g_items[i].key = g_items[j].key;
  g_items[j].key = k;
  const Item t = g_items[i];
  g_items[i] = g_items[j];
  g_items[j] = t;
  g_items[i].w |= W_DIRTY;
  g_items[j].w |= W_DIRTY;
  return persist(nullptr);
}

Err todo_delete_hidden() {
  if (!g_t.loaded) return Err::NOT_LOADED;
  if (!todo_hidden_count()) return Err::OK;
  while (g_t.n > g_t.open + g_visible) park(g_t.n - 1);
  return persist(nullptr);
}

uint32_t todo_convert_lost_lines() {
  if (!g_t.loaded) return 0;
  return g_t.storage == Storage::TEXT
             ? g_t.report.unknown_lines + g_t.report.limit_lines + g_t.report.comment_lines
             : g_t.report.bad_records;
}

Err todo_convert(Storage target, Progress progress, void* user) {
  if (!g_t.loaded) return Err::NOT_LOADED;
  if (target == g_t.storage) return Err::INVALID;
  g_t.convert_ready = false;
  Err e = ensure_dir(g_t.sys_dir);
  if (e != Err::OK) return e;
  const char* dst = target == Storage::TEXT ? g_t.text_path : g_t.app_path;
  if (target == Storage::IN_APP && (e = remove_file(g_t.app_jrn)) != Err::OK) return e;
  if (target == Storage::TEXT && (e = journal_recover(g_t.app_path, g_t.app_jrn)) != Err::OK) return e;
  SafeSave ss;
  if ((e = safe_begin(ss, dst)) != Err::OK) return e;
  Writer& w = ss.w;
  if (target == Storage::IN_APP) {
    uint8_t h[64];
    header_pack(h);
    w.write(h, 64);
    for (uint32_t k = 64; k < T_HDR; ++k) w.put(0);
  }
  const int nt = g_t.n - g_t.open;
  for (int i = 0; i < g_t.n; ++i) {
    char t[MAX_TITLE_BYTES + 1];
    if ((e = todo_title(i, t, int(sizeof t))) != Err::OK) { safe_abort(ss); return e; }
    if (target == Storage::IN_APP) {
      const uint32_t key = i < g_t.open ? uint32_t(i + 1) : uint32_t(nt - (i - g_t.open));
      slot_pack(g_slot, uint32_t(i), ticked(g_items[i]), key, t, str_len(t));
      w.write(g_slot, SLOT);
    } else {
      w.puts(ticked(g_items[i]) ? "- [x] " : "- [ ] ");
      w.puts(t);
      w.put('\n');
    }
    if (progress && (i & 15) == 0) progress(i * 60 / (g_t.n ? g_t.n : 1), user);
  }
  if (w.err() != Err::OK) { const Err we = w.err(); safe_abort(ss); return we; }
  bool inst = false;
  e = safe_commit(ss, &inst);
  if (!inst) return e == Err::OK ? Err::IO : e;
  // Read the new copy back and compare item by item (ticked state, title, order).
  const Storage from = g_t.storage;
  Hash64 old_h, new_h;
  for (int i = 0; i < g_t.n && e == Err::OK; ++i) e = item_title_hash(from, g_items[i], old_h);
  if (e == Err::OK) {
    const TodoState saved = g_t;
    g_t.n = g_t.open = 0;
    g_t.storage = target;
    TodoReport rep;
    Err le = target == Storage::TEXT ? text_load(rep) : app_load(rep);
    if (le == Err::OK && (g_t.n != saved.n || g_t.open != saved.open)) le = Err::VERIFY;
    for (int i = 0; i < g_t.n && le == Err::OK; ++i) le = item_title_hash(target, g_items[i], new_h);
    if (le == Err::OK && new_h.v != old_h.v) le = Err::VERIFY;
    // Restore the source list.
    g_t = saved;
    g_t.n = g_t.open = 0;
    TodoReport rr;
    const Err re = from == Storage::TEXT ? text_load(rr) : app_load(rr);
    g_t.report = saved.report;
    g_t.loaded = re == Err::OK;
    e = le != Err::OK ? le : re;
  }
  if (progress) progress(100, user);
  if (e != Err::OK) {
    remove_file(dst);
    if (target == Storage::IN_APP) remove_file(g_t.app_jrn);
    return e == Err::CORRUPT ? Err::VERIFY : e;
  }
  g_t.convert_ready = true;
  g_t.convert_target = target;
  return Err::OK;
}

Err todo_convert_finish() {
  if (!g_t.convert_ready) return Err::INVALID;
  g_t.convert_ready = false;
  Err e;
  if (g_t.convert_target == Storage::IN_APP) {
    e = remove_file(g_t.text_path);
  } else {
    e = remove_file(g_t.app_path);
    if (e == Err::OK) e = remove_file(g_t.app_jrn);
  }
  const Err l = todo_load(g_t.convert_target);
  return e != Err::OK ? e : l;
}

Err todo_convert_cancel() {
  if (!g_t.convert_ready) return Err::OK;
  g_t.convert_ready = false;
  return remove_file(g_t.convert_target == Storage::IN_APP ? g_t.app_path : g_t.text_path);
}

size_t todo_ram_bytes() { return sizeof g_items + sizeof g_t + sizeof g_slot; }

}  // namespace planner
