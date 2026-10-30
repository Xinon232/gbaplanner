#include "cal_codec.h"

namespace planner {
namespace {

enum TState : uint8_t { T_NORMAL, T_SP, T_SPBAR, T_SPSL, T_SPSL2 };
enum RState : uint8_t {
  R_BASE, R_EVERY_N, R_EVERY_UNIT, R_CLAUSE, R_NONE_END, R_WD_LIST, R_MON_ON, R_MON_DAY, R_MON_WD,
  R_UNTIL, R_COUNT, R_SKIP, R_DONE, R_BAD
};
enum Clause : uint8_t { C_ON = 1, C_UNTIL = 2, C_COUNT = 4, C_SKIP = 8, C_DONE = 16 };

inline uint8_t lower(uint8_t c) { return c >= 'A' && c <= 'Z' ? uint8_t(c + 32) : c; }

bool eq(const char* a, int n, const char* word) {
  int i = 0;
  for (; i < n; ++i)
    if (!word[i] || lower(uint8_t(a[i])) != uint8_t(word[i])) return false;
  return word[i] == 0;
}

bool number(const char* s, int n, int maxv, int& v) {
  if (n < 1 || n > 4 || s[0] == '0') return false;
  v = 0;
  for (int i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  return v >= 1 && v <= maxv;
}

const char* const WD[7] = {"mo", "tu", "we", "th", "fr", "sa", "su"};
int weekday_word(const char* s, int n) {
  for (int i = 0; i < 7; ++i)
    if (eq(s, n, WD[i])) return i;
  return -1;
}
const char* const ORD[5] = {"1st", "2nd", "3rd", "4th", "5th"};

int cat_word(const uint8_t* s, int n) {
  static const char* const C[4] = {"holiday", "birthday", "event", "task"};
  for (int i = 0; i < 4; ++i)
    if (eq(reinterpret_cast<const char*>(s), n, C[i])) return i;
  return -1;
}

// Sort + dedupe 3-byte dates in place (n <= 255: insertion sort is fine).
int sort_dates(uint8_t* d, int n) {
  for (int i = 1; i < n; ++i) {
    const uint32_t v = rd24(d + 3 * i);
    int j = i - 1;
    while (j >= 0 && rd24(d + 3 * j) > v) {
      wr24(d + 3 * (j + 1), rd24(d + 3 * j));
      --j;
    }
    wr24(d + 3 * (j + 1), v);
  }
  int m = 0;
  for (int i = 0; i < n; ++i)
    if (m == 0 || rd24(d + 3 * i) != rd24(d + 3 * (m - 1))) wr24(d + 3 * m++, rd24(d + 3 * i));
  return m;
}

}  // namespace

void LineParser::reset(uint32_t offset, uint8_t* scratch, LineSink* sink) {
  sink_ = sink;
  scratch_ = scratch;
  pos_ = offset;
  pending_cr_ = false;
  start_line();
}

void LineParser::start_line() {
  li_.offset = pos_;
  li_.length = 0;
  li_.eol = 0;
  li_.kind = LineKind::EMPTY;
  ParsedEntry& e = li_.e;
  e.cat = Category::EVENT;
  e.day = NO_DAY;
  e.timed = false;
  e.start = e.end = 0;
  e.ticked = false;
  e.has_repeat_part = false;
  e.rule = Rule();
  e.nskip = e.ndone = 0;
  e.skip = scratch_;
  e.done = scratch_ ? scratch_ + 3 * MAX_LIST_DATES : nullptr;
  e.title_bytes = e.notes_bytes = e.title_chars = e.notes_chars = 0;
  phase_ = HEAD;
  first_ = true;
  all_space_ = true;
  comment_ = false;
  head_n_ = 0;
  tstate_ = T_SP;
  tvirt_ = true;
  tesc_ = false;
  bad_text_ = false;
  tok_n_ = 0;
  rstate_ = R_BASE;
  clauses_ = 0;
  last_term_ = 0;
  none_ = false;
  nesc_ = false;
}



bool LineParser::finish() {
  if (pending_cr_) { pending_cr_ = false; content('\r'); }
  if (pos_ == li_.offset) return true;  // no unterminated last line
  return end_line(0);
}


// DATE [TIME] CATEGORY [CHECK] - then the title starts.
void LineParser::head_done() {
  ParsedEntry& e = li_.e;
  const uint8_t* h = head_;
  const int n = head_n_;
  phase_ = BAD;
  if (n < 12 || h[10] != ' ' || !parse_iso(reinterpret_cast<const char*>(h), 10, e.day)) return;
  int p = 11;
  auto token = [&](int at) {
    int q = at;
    while (q < n && h[q] != ' ') ++q;
    return q - at;
  };
  int len = token(p);
  int t1 = -1, t2 = -1;
  bool time_tok = false, all_day = false;
  if (len == 5 && parse_hhmm(reinterpret_cast<const char*>(h + p), 5, t1)) {
    time_tok = true;
  } else if (len == 11 && h[p + 5] == '-' && parse_hhmm(reinterpret_cast<const char*>(h + p), 5, t1) &&
             parse_hhmm(reinterpret_cast<const char*>(h + p + 6), 5, t2)) {
    time_tok = true;
  } else if (eq(reinterpret_cast<const char*>(h + p), len, "all-day")) {
    time_tok = all_day = true;
  }
  if (time_tok) {
    p += len;
    if (p >= n || h[p] != ' ') return;
    ++p;
    len = token(p);
  }
  const int cat = cat_word(h + p, len);
  if (cat < 0) return;
  e.cat = Category(cat);
  p += len;
  if (p >= n || h[p] != ' ') return;
  ++p;
  if (time_tok && !all_day) {
    if (e.cat == Category::EVENT && t2 >= 0 && t2 > t1) {
      e.timed = true; e.start = int16_t(t1); e.end = int16_t(t2);
    } else if (e.cat == Category::TASK && t2 < 0) {
      e.timed = true; e.start = int16_t(t1);
    } else {
      return;
    }
  } else if (all_day && e.cat != Category::EVENT && e.cat != Category::TASK) {
    return;
  }
  if (e.cat == Category::TASK && p + 3 < n + 1 && p + 3 <= n && h[p] == '[' && h[p + 2] == ']' &&
      (p + 3 == n ? false : h[p + 3] == ' ') && (h[p + 1] == ' ' || h[p + 1] == 'x' || h[p + 1] == 'X')) {
    e.ticked = h[p + 1] != ' ';
    p += 4;
  }
  if (p >= n) return;  // empty title
  phase_ = TITLE;
  for (int i = p; i < n && phase_ != BAD; ++i) {
    switch (phase_) {
      case TITLE: title_byte(h[i]); break;
      case REPEAT: repeat_byte(h[i]); break;
      case NOTES: notes_byte(h[i]); break;
      default: break;
    }
  }
}


// Flush a pending space / "|" / "/" / "//" as title text.


bool LineParser::add_date(bool done, int32_t d) {
  ParsedEntry& e = li_.e;
  int& n = done ? e.ndone : e.nskip;
  if (n >= MAX_LIST_DATES) return false;
  if (scratch_) wr24((done ? e.done : e.skip) + 3 * n, uint32_t(d));
  ++n;
  return true;
}

void LineParser::token_end(uint8_t term) {
  // term: ' ', ',', or 0 (end of the repeat part)
  ParsedEntry& e = li_.e;
  Rule& r = e.rule;
  const char* t = tok_;
  const int n = tok_n_;
  tok_n_ = 0;
  if (rstate_ == R_BAD) return;
  if (n == 0 || n > int(sizeof tok_)) { rstate_ = R_BAD; return; }
  const bool list_state = rstate_ == R_WD_LIST || rstate_ == R_SKIP || rstate_ == R_DONE;
  if (term == ',' && !list_state) { rstate_ = R_BAD; return; }
  last_term_ = term;
  int v = 0;
  int32_t d = 0;
  switch (rstate_) {
    case R_BASE:
      if (eq(t, n, "daily")) r.freq = Freq::DAILY;
      else if (eq(t, n, "weekly")) r.freq = Freq::WEEKLY;
      else if (eq(t, n, "monthly")) r.freq = Freq::MONTHLY;
      else if (eq(t, n, "yearly")) r.freq = Freq::YEARLY;
      else if (eq(t, n, "none")) { none_ = true; rstate_ = R_NONE_END; return; }
      else if (eq(t, n, "every")) { rstate_ = R_EVERY_N; return; }
      else { rstate_ = R_BAD; return; }
      r.interval = 1;
      rstate_ = R_CLAUSE;
      return;
    case R_EVERY_N:
      if (!number(t, n, MAX_INTERVAL, v)) { rstate_ = R_BAD; return; }
      r.interval = uint16_t(v);
      rstate_ = R_EVERY_UNIT;
      return;
    case R_EVERY_UNIT:
      if (eq(t, n, "days") || eq(t, n, "day")) r.freq = Freq::DAILY;
      else if (eq(t, n, "weeks") || eq(t, n, "week")) r.freq = Freq::WEEKLY;
      else if (eq(t, n, "months") || eq(t, n, "month")) r.freq = Freq::MONTHLY;
      else if (eq(t, n, "years") || eq(t, n, "year")) r.freq = Freq::YEARLY;
      else { rstate_ = R_BAD; return; }
      rstate_ = R_CLAUSE;
      return;
    case R_CLAUSE: {
      uint8_t c = 0;
      RState next = R_BAD;
      if (eq(t, n, "on")) {
        c = C_ON;
        next = r.freq == Freq::WEEKLY ? R_WD_LIST : r.freq == Freq::MONTHLY ? R_MON_ON : R_BAD;
      } else if (eq(t, n, "until")) { c = C_UNTIL; next = R_UNTIL; }
      else if (eq(t, n, "count")) { c = C_COUNT; next = R_COUNT; }
      else if (eq(t, n, "skip")) { c = C_SKIP; next = R_SKIP; }
      else if (eq(t, n, "done")) { c = C_DONE; next = R_DONE; }
      if (!c || (clauses_ & c) || ((c & (C_UNTIL | C_COUNT)) && (clauses_ & (C_UNTIL | C_COUNT)))) {
        rstate_ = R_BAD;
        return;
      }
      clauses_ |= c;
      rstate_ = uint8_t(next);
      return;
    }
    case R_WD_LIST: {
      const int w = weekday_word(t, n);
      if (w < 0 || (r.weekdays >> w & 1)) { rstate_ = R_BAD; return; }
      r.weekdays = uint8_t(r.weekdays | 1 << w);
      if (term != ',') rstate_ = R_CLAUSE;
      return;
    }
    case R_MON_ON:
      if (eq(t, n, "day")) { rstate_ = R_MON_DAY; return; }
      if (eq(t, n, "last")) { r.mmode = MonthlyMode::LAST_WEEKDAY; rstate_ = R_MON_WD; return; }
      for (int i = 0; i < 5; ++i)
        if (eq(t, n, ORD[i])) { r.mmode = MonthlyMode::NTH_WEEKDAY; r.nth = uint8_t(i + 1); rstate_ = R_MON_WD; return; }
      rstate_ = R_BAD;
      return;
    case R_MON_DAY:
      if (!number(t, n, 31, v)) { rstate_ = R_BAD; return; }
      r.mmode = MonthlyMode::DAY_N;
      r.mday = uint8_t(v);
      rstate_ = R_CLAUSE;
      return;
    case R_MON_WD: {
      const int w = weekday_word(t, n);
      if (w < 0) { rstate_ = R_BAD; return; }
      r.mweekday = uint8_t(w);
      rstate_ = R_CLAUSE;
      return;
    }
    case R_UNTIL:
      if (!parse_iso(t, n, d)) { rstate_ = R_BAD; return; }
      r.end = EndKind::UNTIL;
      r.until = d;
      rstate_ = R_CLAUSE;
      return;
    case R_COUNT:
      if (!number(t, n, MAX_COUNT, v)) { rstate_ = R_BAD; return; }
      r.end = EndKind::COUNT;
      r.count = uint16_t(v);
      rstate_ = R_CLAUSE;
      return;
    case R_SKIP:
    case R_DONE:
      if (!parse_iso(t, n, d) || !add_date(rstate_ == R_DONE, d)) { rstate_ = R_BAD; return; }
      if (term != ',') rstate_ = R_CLAUSE;
      return;
    default:
      rstate_ = R_BAD;
      return;
  }
}




void LineParser::finish_entry() {
  ParsedEntry& e = li_.e;
  Rule& r = e.rule;
  li_.kind = LineKind::UNKNOWN;
  if (bad_text_ || e.title_bytes == 0) return;
  if (e.has_repeat_part) {
    if (rstate_ != R_CLAUSE && rstate_ != R_NONE_END) return;
    if (last_term_ == ',') return;
  } else if (e.cat == Category::BIRTHDAY) {
    r.freq = Freq::YEARLY;  // birthdays repeat yearly by default
    r.interval = 1;
  }
  if (none_) r = Rule();
  if ((e.nskip || e.ndone) && r.freq == Freq::NONE) return;
  if (e.ndone && e.cat != Category::TASK) return;
  if (!rule_valid(r)) return;
  if (scratch_) {
    e.nskip = sort_dates(e.skip, e.nskip);
    e.ndone = sort_dates(e.done, e.ndone);
  }
  li_.kind = LineKind::ENTRY;
}

bool LineParser::end_line(uint8_t eol) {
  li_.eol = eol;
  if (comment_) {
    li_.kind = LineKind::COMMENT;
  } else if (all_space_) {
    li_.kind = LineKind::EMPTY;
  } else {
    if (phase_ == HEAD) head_done();
    if (phase_ == TITLE) {
      if (tesc_) { tesc_ = false; title_emit('\\'); }
      title_flush();
      if (sink_) sink_->title_end();
      finish_entry();
    } else if (phase_ == REPEAT) {
      token_end(0);
      finish_entry();
    } else if (phase_ == NOTES) {
      if (nesc_) { nesc_ = false; notes_emit('\\'); }
      finish_entry();
    } else {
      li_.kind = LineKind::UNKNOWN;
    }
  }
  const bool go = sink_ ? sink_->line(li_) : true;
  start_line();
  return go;
}

Err parse_file(Reader& r, LineParser& p, uint8_t* scratch, LineSink* sink, bool* bom) {
  *bom = false;
  uint8_t head[3];
  int nh = 0;
  while (nh < 3) {
    const int c = r.get();
    if (c < 0) break;
    head[nh++] = uint8_t(c);
  }
  if (r.err() != Err::OK) return r.err();
  int from = 0;
  if (nh == 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) {
    *bom = true;
    from = 3;
  }
  p.reset(uint32_t(from), scratch, sink);
  for (int i = from; i < nh; ++i)
    if (!p.feed(head[i])) return Err::OK;
  for (;;) {
    const uint8_t* b;
    const uint32_t k = r.avail(&b);
    if (!k) break;
    r.advance(k);
    if (!p.feed_block(b, k)) return Err::OK;
  }
  if (r.err() != Err::OK) return r.err();
  p.finish();
  return Err::OK;
}

// ---- writing ----------------------------------------------------------------
namespace {
void put_num(Writer& w, int v) {
  char b[8];
  int n = 0;
  do { b[n++] = char('0' + v % 10); v /= 10; } while (v);
  while (n) w.put(uint8_t(b[--n]));
}
void put_day(Writer& w, int32_t d) {
  char b[11];
  format_iso(d, b);
  w.write(b, 10);
}
void put_hhmm(Writer& w, int m) {
  char b[6];
  format_hhmm(m, b);
  w.write(b, 5);
}
const char* const CAT_WORD[4] = {"holiday", "birthday", "event", "task"};
}  // namespace

void write_head(Writer& w, const EntryOut& e) {
  put_day(w, e.day);
  w.put(' ');
  if (e.timed && e.cat == Category::EVENT) {
    put_hhmm(w, e.start);
    w.put('-');
    put_hhmm(w, e.end);
    w.put(' ');
  } else if (e.timed && e.cat == Category::TASK) {
    put_hhmm(w, e.start);
    w.put(' ');
  }
  w.puts(CAT_WORD[int(e.cat) & 3]);
  w.put(' ');
  if (e.cat == Category::TASK) w.puts(e.ticked ? "[x] " : "[ ] ");
}

void write_repeat(Writer& w, const EntryOut& e) {
  const Rule& r = e.rule;
  const bool plain_yearly = r.freq == Freq::YEARLY && r.interval == 1 && r.end == EndKind::NEVER &&
                            !e.nskip && !e.ndone;
  if (e.cat == Category::BIRTHDAY) {
    if (plain_yearly) return;
    if (r.freq == Freq::NONE) { w.puts(" | none"); return; }
  }
  if (r.freq == Freq::NONE) return;
  w.puts(" | ");
  static const char* const BASE[5] = {"", "daily", "weekly", "monthly", "yearly"};
  static const char* const UNIT[5] = {"", "days", "weeks", "months", "years"};
  if (r.interval == 1) {
    w.puts(BASE[int(r.freq)]);
  } else {
    w.puts("every ");
    put_num(w, r.interval);
    w.put(' ');
    w.puts(UNIT[int(r.freq)]);
  }
  if (r.freq == Freq::WEEKLY && r.weekdays) {
    w.puts(" on ");
    bool first = true;
    for (int i = 0; i < 7; ++i)
      if (r.weekdays >> i & 1) {
        if (!first) w.put(',');
        w.puts(WD[i]);
        first = false;
      }
  } else if (r.freq == Freq::MONTHLY && r.mmode != MonthlyMode::START_DAY) {
    w.puts(" on ");
    if (r.mmode == MonthlyMode::DAY_N) {
      w.puts("day ");
      put_num(w, r.mday);
    } else {
      w.puts(r.mmode == MonthlyMode::LAST_WEEKDAY ? "last" : ORD[(r.nth - 1) % 5]);
      w.put(' ');
      w.puts(WD[r.mweekday % 7]);
    }
  }
  if (r.end == EndKind::UNTIL) { w.puts(" until "); put_day(w, r.until); }
  if (r.end == EndKind::COUNT) { w.puts(" count "); put_num(w, r.count); }
  for (int list = 0; list < 2; ++list) {
    const uint8_t* d = list ? e.done : e.skip;
    const int n = list ? e.ndone : e.nskip;
    if (!n) continue;
    w.puts(list ? " done " : " skip ");
    for (int i = 0; i < n; ++i) {
      if (i) w.put(',');
      put_day(w, int32_t(rd24(d + 3 * i)));
    }
  }
}

void Escaper::push(uint8_t c) {
  q_[n_++] = c;
  drain(false);
}

void Escaper::finish() {
  drain(true);
  prev_space_ = true;
}

void Escaper::drain(bool final) {
  constexpr int END = -1;
  while (n_) {
    const uint8_t c = q_[0];
    int pop = 1;
    if (c == '|') {
      if (n_ < 2 && !final) return;
      const int next = n_ >= 2 ? q_[1] : END;
      if (prev_space_ && (next == ' ' || next == END)) w_.write("\\|", 2);
      else w_.put('|');
    } else if (c == '/' && prev_space_) {
      if (n_ < 2 && !final) return;
      bool done = false;
      if (n_ >= 2 && q_[1] == '/') {
        if (n_ < 3 && !final) return;
        const int after = n_ >= 3 ? q_[2] : END;
        if (after == ' ' || after == END) {
          w_.write("\\/\\/", 4);
          pop = 2;
          done = true;
        }
      }
      if (!done) w_.put('/');
    } else if (c == '\\') {
      w_.write("\\\\", 2);
    } else if (c == '\n') {
      w_.write("\\n", 2);
    } else {
      w_.put(c);
    }
    prev_space_ = c == ' ';
    if (pop == 2) prev_space_ = false;
    for (int i = pop; i < n_; ++i) q_[i - pop] = q_[i];
    n_ -= pop;
  }
}

void hash_fields(Hash64& h, const EntryOut& e) {
  h.byte(uint8_t(e.cat));
  h.u32(uint32_t(e.day));
  h.byte(e.timed);
  h.u32(uint32_t(e.timed ? e.start : 0));
  h.u32(uint32_t(e.timed && e.cat == Category::EVENT ? e.end : 0));
  h.byte(e.cat == Category::TASK && e.ticked);
  const Rule& r = e.rule;
  h.byte(uint8_t(r.freq));
  if (r.freq != Freq::NONE) {
    h.u32(r.interval);
    h.byte(rule_spec(r));
    h.byte(uint8_t(r.end));
    h.u32(r.end == EndKind::UNTIL ? uint32_t(r.until) : r.end == EndKind::COUNT ? r.count : 0);
  }
  h.u32(uint32_t(e.nskip));
  for (int i = 0; i < e.nskip; ++i) h.u32(rd24(e.skip + 3 * i));
  h.u32(uint32_t(e.ndone));
  for (int i = 0; i < e.ndone; ++i) h.u32(rd24(e.done + 3 * i));
}

}  // namespace planner
