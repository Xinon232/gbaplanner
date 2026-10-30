// calendar.txt line grammar (DESIGN.md "calendar.txt"): streaming parser and
// writer. The parser takes one byte at a time, so lines of any length are
// handled without a line buffer; long notes and day lists never sit in RAM.
#ifndef PLANNER_CORE_CAL_CODEC_H
#define PLANNER_CORE_CAL_CODEC_H

#include "core/calendar.h"
#include "fileio.h"

namespace planner {

enum class LineKind : uint8_t { EMPTY, COMMENT, ENTRY, UNKNOWN };

struct ParsedEntry {
  Category cat;
  int32_t day;
  bool timed;
  int16_t start, end;
  bool ticked;           // "[x]"
  bool has_repeat_part;  // " | ..." present
  Rule rule;
  int nskip, ndone;      // dates (3 bytes each, sorted, no duplicates) in the scratch area
  uint8_t* skip;
  uint8_t* done;
  uint16_t title_bytes, notes_bytes;  // decoded sizes
  uint16_t title_chars, notes_chars;
};

struct LineInfo {
  uint32_t offset;  // first byte of the line
  uint32_t length;  // without the line ending
  uint8_t eol;      // 0: end of file, 1: LF, 2: CRLF
  LineKind kind;
  ParsedEntry e;    // valid for ENTRY
};

class LineSink {
 public:
  // false: the sink ignores title_char / notes_char (lets the parser skip
  // runs of plain text quickly).
  bool wants_text = true;
  virtual void title_char(uint8_t) {}
  virtual void title_end() {}
  virtual void notes_char(uint8_t) {}
  // Return false to stop parsing.
  virtual bool line(const LineInfo& li) = 0;
};

class LineParser {
 public:
  // scratch: room for 2 * MAX_LIST_DATES dates (3 bytes each), or nullptr
  // when the day lists are not needed (they are then only counted).
  void reset(uint32_t offset, uint8_t* scratch, LineSink* sink);
  bool feed(uint8_t c);  // false when the sink stopped the parse
  // Same as feeding the bytes one by one, with fast paths for plain text.
  bool feed_block(const uint8_t* p, uint32_t n);
  bool finish();         // end of input
  uint32_t offset() const { return pos_; }

 private:
  enum Phase : uint8_t { HEAD, TITLE, REPEAT, NOTES, BAD };
  void start_line();
  bool end_line(uint8_t eol);
  void content(uint8_t c);
  void head_done();
  void title_byte(uint8_t c);
  void title_emit(uint8_t c);
  void title_flush();
  void repeat_byte(uint8_t c);
  void token_end(uint8_t term);
  void notes_byte(uint8_t c);
  void notes_emit(uint8_t c);
  bool add_date(bool done, int32_t d);
  void finish_entry();

  LineSink* sink_ = nullptr;
  uint8_t* scratch_ = nullptr;
  LineInfo li_;
  uint32_t pos_ = 0;
  bool pending_cr_ = false;
  Phase phase_ = HEAD;
  bool first_ = true, all_space_ = true, comment_ = false;
  uint8_t head_[48];
  int head_n_ = 0;
  // title scanner
  uint8_t tstate_ = 0;
  bool tvirt_ = false, tesc_ = false, bad_text_ = false;
  // repeat tokenizer
  char tok_[16];
  int tok_n_ = 0;
  uint8_t rstate_ = 0, clauses_ = 0, last_term_ = 0;
  bool none_ = false;
  // notes
  bool nesc_ = false;
};

// Parses a whole file: skips a UTF-8 BOM, feeds every byte. Returns the
// reader error, or OK. *bom = whether a BOM was found.
Err parse_file(Reader& r, LineParser& p, uint8_t* scratch, LineSink* sink, bool* bom);

// ---- writing ----------------------------------------------------------------
struct EntryOut {
  Category cat;
  int32_t day;
  bool timed;
  int16_t start, end;
  bool ticked;
  Rule rule;
  const uint8_t* skip; int nskip;  // 3-byte dates
  const uint8_t* done; int ndone;
};
void write_head(Writer& w, const EntryOut& e);    // "DATE [TIME] CATEGORY [CHECK] "
void write_repeat(Writer& w, const EntryOut& e);  // " | ..." (or nothing)

// Escapes title / notes text: \\ \n, and \| \/\/ for " | " and " // ".
class Escaper {
 public:
  explicit Escaper(Writer& w) : w_(w) {}
  void push(uint8_t c);
  void finish();
  void text(const char* s, int n) { for (int i = 0; i < n; ++i) push(uint8_t(s[i])); }
 private:
  void drain(bool final);
  Writer& w_;
  uint8_t q_[3];
  int n_ = 0;
  bool prev_space_ = true;
};

// Canonical content hash of an entry (used to compare copies entry by entry).
void hash_fields(Hash64& h, const EntryOut& e);

}  // namespace planner

#endif
