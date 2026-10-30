// Byte-level hot path of the calendar.txt parser. In the app the two
// hottest functions (feed_block, title_byte) are in IWRAM as Thumb code
// (PLANNER_IWRAM_CODE), the rest runs from ROM (PLANNER_IWRAM_LOAD, see
// planner_config.h). Per line work (head, tokens, checks) stays in
// cal_codec.cpp.
#include "cal_codec.h"

namespace planner {
namespace {
enum TState : uint8_t { T_NORMAL, T_SP, T_SPBAR, T_SPSL, T_SPSL2 };
}  // namespace

PLANNER_IWRAM_LOAD bool LineParser::feed(uint8_t c) {
  if (pending_cr_) {
    pending_cr_ = false;
    if (c == '\n') { ++pos_; return end_line(2); }
    content('\r');
  }
  if (c == '\r') { pending_cr_ = true; ++pos_; return true; }
  if (c == '\n') { ++pos_; return end_line(1); }
  ++pos_;
  content(c);
  return true;
}

PLANNER_IWRAM_CODE bool LineParser::feed_block(const uint8_t* p, uint32_t n) {
  const uint8_t* const end = p + n;
  while (p < end) {
    if (!pending_cr_) {
      // Fast paths: runs of bytes that only need counting / copying.
      uint32_t k = 0;
      const uint32_t left = uint32_t(end - p);
      if (comment_ || phase_ == BAD) {
        bool space = true;
        while (k < left && p[k] != '\n' && p[k] != '\r') {
          if (p[k] != ' ' && p[k] != '\t') space = false;
          ++k;
        }
        if (!space) all_space_ = false;
        if (k && first_) first_ = false;
      } else if (phase_ == HEAD && !first_) {
        uint32_t room = uint32_t(sizeof head_) - uint32_t(head_n_);
        if (room > 1) room -= 1;  // the byte that fills the buffer goes the slow way
        else room = 0;
        while (k < left && k < room && p[k] != '\n' && p[k] != '\r') {
          const uint8_t c = p[k];
          if (c != ' ' && c != '\t') all_space_ = false;
          head_[head_n_++] = c;
          ++k;
        }
      } else if (phase_ == TITLE && tstate_ == T_NORMAL && !tesc_ && !(sink_ && sink_->wants_text)) {
        ParsedEntry& e = li_.e;
        uint32_t chars = 0;
        while (k < left) {
          const uint8_t c = p[k];
          if (c == ' ' || c == '\\' || c == '\n' || c == '\r' || c == 0) break;
          chars += (c & 0xC0) != 0x80;
          ++k;
        }
        e.title_bytes = uint16_t(e.title_bytes + k);
        e.title_chars = uint16_t(e.title_chars + chars);
        if (e.title_chars > MAX_TITLE_CHARS) bad_text_ = true;
      } else if (phase_ == NOTES && !nesc_ && !(sink_ && sink_->wants_text)) {
        ParsedEntry& e = li_.e;
        uint32_t chars = 0;
        while (k < left) {
          const uint8_t c = p[k];
          if (c == '\\' || c == '\n' || c == '\r' || c == 0) break;
          chars += (c & 0xC0) != 0x80;
          ++k;
        }
        e.notes_bytes = uint16_t(e.notes_bytes + (k > 0xFFFF ? 0xFFFF : k));
        e.notes_chars = uint16_t(e.notes_chars + (chars > 0xFFFF ? 0xFFFF : chars));
        if (e.notes_chars > MAX_NOTES_CHARS || k > 0xFFFF) bad_text_ = true;
      }
      if (k) {
        li_.length += k;
        pos_ += k;
        p += k;
        continue;
      }
    }
    if (!feed(*p++)) return false;
  }
  return true;
}

PLANNER_IWRAM_LOAD void LineParser::content(uint8_t c) {
  ++li_.length;
  if (first_) {
    first_ = false;
    if (c == '#') comment_ = true;
  }
  if (c != ' ' && c != '\t') all_space_ = false;
  if (comment_) return;
  switch (phase_) {
    case HEAD:
      head_[head_n_++] = c;
      if (head_n_ == int(sizeof head_)) head_done();
      break;
    case TITLE: title_byte(c); break;
    case REPEAT: repeat_byte(c); break;
    case NOTES: notes_byte(c); break;
    case BAD: break;
  }
}

PLANNER_IWRAM_LOAD void LineParser::title_emit(uint8_t c) {
  ParsedEntry& e = li_.e;
  if (c == '\n' || c == '\r' || c == 0) bad_text_ = true;
  ++e.title_bytes;
  if ((c & 0xC0) != 0x80) ++e.title_chars;
  if (e.title_chars > MAX_TITLE_CHARS) bad_text_ = true;
  if (sink_) sink_->title_char(c);
}

PLANNER_IWRAM_LOAD void LineParser::title_flush() {
  if (tstate_ == T_NORMAL) return;
  if (!tvirt_) title_emit(' ');
  if (tstate_ == T_SPBAR) title_emit('|');
  if (tstate_ == T_SPSL) title_emit('/');
  if (tstate_ == T_SPSL2) { title_emit('/'); title_emit('/'); }
  tstate_ = T_NORMAL;
  tvirt_ = false;
}

PLANNER_IWRAM_CODE void LineParser::title_byte(uint8_t c) {
  if (tesc_) {
    tesc_ = false;
    switch (c) {
      case 'n': title_emit('\n'); return;
      case '\\': title_emit('\\'); return;
      case '|': title_emit('|'); return;
      case '/': title_emit('/'); return;
      default: title_emit('\\'); break;  // unknown escape: literal backslash
    }
  }
  switch (tstate_) {
    case T_SP:
      if (c == '|') { tstate_ = T_SPBAR; return; }
      if (c == '/') { tstate_ = T_SPSL; return; }
      if (c == ' ') { if (!tvirt_) title_emit(' '); tvirt_ = false; return; }
      title_flush();
      break;
    case T_SPBAR:
      if (c == ' ') {  // " | "
        if (li_.e.title_bytes == 0) bad_text_ = true;
        if (sink_) sink_->title_end();
        phase_ = REPEAT;
        li_.e.has_repeat_part = true;
        return;
      }
      title_flush();
      break;
    case T_SPSL:
      if (c == '/') { tstate_ = T_SPSL2; return; }
      title_flush();
      break;
    case T_SPSL2:
      if (c == ' ') {  // " // "
        if (li_.e.title_bytes == 0) bad_text_ = true;
        if (sink_) sink_->title_end();
        phase_ = NOTES;
        return;
      }
      title_flush();
      break;
    default: break;
  }
  if (c == '\\') { tesc_ = true; return; }
  if (c == ' ') { tstate_ = T_SP; tvirt_ = false; return; }
  title_emit(c);
}

PLANNER_IWRAM_LOAD void LineParser::repeat_byte(uint8_t c) {
  if (c == ' ' || c == ',') {
    if (c == ' ' && tok_n_ == 2 && tok_[0] == '/' && tok_[1] == '/') {  // " // "
      tok_n_ = 0;
      phase_ = NOTES;
      return;
    }
    token_end(c);
    return;
  }
  if (tok_n_ < int(sizeof tok_)) tok_[tok_n_] = char(c);
  ++tok_n_;
}

PLANNER_IWRAM_LOAD void LineParser::notes_emit(uint8_t c) {
  ParsedEntry& e = li_.e;
  if (c == '\r' || c == 0) bad_text_ = true;
  ++e.notes_bytes;
  if ((c & 0xC0) != 0x80) ++e.notes_chars;
  if (e.notes_chars > MAX_NOTES_CHARS) bad_text_ = true;
  if (sink_) sink_->notes_char(c);
}

PLANNER_IWRAM_LOAD void LineParser::notes_byte(uint8_t c) {
  if (nesc_) {
    nesc_ = false;
    switch (c) {
      case 'n': notes_emit('\n'); return;
      case '\\': notes_emit('\\'); return;
      case '|': notes_emit('|'); return;
      case '/': notes_emit('/'); return;
      default: notes_emit('\\'); break;
    }
  }
  if (c == '\\') { nesc_ = true; return; }
  notes_emit(c);
}

}  // namespace planner
