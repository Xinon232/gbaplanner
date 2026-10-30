#pragma once
// The gbawriter letter engine as gbavocab's entry editor uses it (gbavocab
// include/writer_core.h, from gbawriter): a UTF-8 text with a caret and the
// typing input (letter groups on the D-pad with B / A / R, L for the second
// layer, R: Shift, hold R: Caps, SELECT: symbols, digits and accents,
// A: space, B: delete, Start+D-pad: move, Start+A / Start+B: done / back).
// Copied without the diary-name helpers; same behaviour.
#include <cstddef>
#include <cstdint>

namespace writer {
// gbaplanner: one field at a time; notes are the longest (500 characters,
// up to 4 bytes each in UTF-8 as the data core allows).
constexpr std::size_t TEXT_CAPACITY = 2000;
bool valid_utf8(const char *s, std::size_t n);
class TextModel {
public:
  TextModel();
  bool set_text(const char *utf8);
  bool insert(const char *utf8);
  bool replace_before_caret(const char *utf8);
  bool backspace();
  void move_left();
  void move_right();
  void move_home();
  void move_end();
  void set_caret(std::size_t position);
  const char *data();
  std::size_t bytes() const;
  std::size_t caret_byte() const;
  bool dirty() const;
  void mark_saved();

private:
  char _storage[TEXT_CAPACITY + 1];
  std::size_t _gap_begin, _gap_end, _size;
  bool _dirty;
  void move_gap(std::size_t position);
  static std::size_t previous_utf8(const char *s, std::size_t p);
  static std::size_t next_utf8(const char *s, std::size_t n, std::size_t p);
};

enum class Button : uint8_t {
  UP,
  DOWN,
  LEFT,
  RIGHT,
  A,
  B,
  L,
  R,
  START,
  SELECT
};
enum class EventKind : uint8_t {
  NONE,
  INSERT,
  REPLACE,
  BACKSPACE,
  MOVE_LEFT,
  MOVE_RIGHT,
  MOVE_UP,
  MOVE_DOWN,
  PAGE_PREV,
  PAGE_NEXT,
  SAVE,
  SAVE_MENU,
  TOGGLE_STATUS
};
struct InputEvent {
  EventKind kind;
  const char *text;
};
class InputState {
public:
  InputState();
  static constexpr int NAV_REPEAT_DELAY=24, NAV_REPEAT_INTERVAL=5;
  static constexpr int CAPS_HOLD_DELAY=2*NAV_REPEAT_DELAY;
  using Consumer=void(*)(void*,InputEvent);
  // Events and snapshots share held-session edges. A fresh press is time zero;
  // each subsequent update advances one elapsed frame (events do not tick).
  void update(uint16_t held,Consumer consume,void* context);
  InputEvent press(Button button, bool just_pressed);
  InputEvent release(Button button);
  bool shift_armed() const { return _shift; }
  bool caps() const { return _caps; }
  // Host provisional ownership: an existing-letter session owns no insertion
  // to cancel on Start+Select, and must bypass the provisional replacement gate.
  bool select_active() const {return _select && !_select_existing;}
  const char* active_group() const;
  bool toggle_latched() const { return _toggle_latched; }
  void reset_transient(){bool shift=_shift,caps=_caps;*this=InputState();_shift=shift;_caps=caps;}
  // After a screen change made with a Start chord: Start is still held and
  // already used, so the next Start+A / Start+B counts at once and releasing
  // Start does not type anything.
  void keep_start_held(){reset_transient();_held=_previous=uint16_t(1u<<unsigned(Button::START));_start_used=true;}
  void reject_edit(){_letter_keys=0;_group_r_count=0;_rejected=true;}

private:
  uint16_t _letter_keys=0;
  char _letter_base=0;
  bool _letter_upper=false, _select_existing=false;
  bool _toggle_latched=false;
  uint16_t _held;
  uint16_t _previous=0;
  bool _r_pending=false,_group_upper=false,_rejected=false;
  int _r_frames=0;
  uint16_t _repeat_keys=0;
  int _repeat_frames=0;
  bool _shift, _caps, _start_used, _select;
  int _group_r_count;
  char _select_value[5];
  char _alternate_base;
  int _alternate_index = 0;
  bool _select_alpha = false;
  char _output[5] = {};
  bool held(Button b) const;
  void set(Button b, bool on);
  InputEvent letter_event(char base, bool select);
  const char *case_text(const char *lower, char base);
};
const char *alternate_letter(char base, int index);
} // namespace writer
