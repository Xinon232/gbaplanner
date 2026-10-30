#pragma once
// The look of the other apps (gbawriter Home / gbamp3): white screen, small
// title at the top, 16 px rows, a light-blue bar on the selected row, a line
// under the "New ..." row, long names of the selected row scrolling sideways.
#include "app/canvas.h"
#include "app/list_nav.h"
#include "app/screen.h"
#include "app/settings.h"

namespace app::ui {

constexpr int ROW_Y = list::ROW_Y, ROW_H = list::ROW_H, ROWS = list::ROWS;
constexpr int TEXT_X = 6, TEXT_RIGHT = 234;

// Small title, centred in the 16 px header.
void title(Canvas& c, const char* text);
// A grey number such as "2/5" at the right end of the header.
void title_right(Canvas& c, const char* text);

enum class Font : uint8_t { UNSCII, SUPERFW };
// One list row at screen row r (0 = first under the title). Selected: the
// light-blue bar; a text wider than the row is cut with "..." unless it is
// selected (then `skip` pixels have scrolled out on the left).
// right: where the text ends (room for a swatch or icon).
void row(Canvas& c, int r, const char* text, bool selected, int skip = 0, Font font = Font::UNSCII,
         int right = TEXT_RIGHT, uint8_t ink = color::BLACK);
// SuperFW text in `avail` pixels: cut with "..." unless `scroll` (then
// `skip` pixels scrolled out on the left, the marquee of a selected row).
void body_fit(Canvas& c, int x, int y, const char* text, int avail, uint8_t ink, bool scroll = false, int skip = 0);
// The grey line under a row ("New ..." rows, the last setting).
void underline(Canvas& c, int r);
// Width of a row's text in its font.
int text_width(const char* text, Font font);
// A short note in the bottom row (grey line above it).
void note(Canvas& c, const char* text);

// gbamp3 marquee: wait one second, move one pixel every second frame to the
// end, hold one second, start again.
struct Marquee {
    unsigned off = 0, wait = 60, hold = 0, frame = 0;
    void reset() { off = 0; wait = 60; hold = 0; }
    // true when the text moved (redraw).
    bool step(int width, int avail);
};

// A form row: label in UNSCII, then the value (UNSCII or SuperFW), cut
// with "..." unless selected (then `skip` pixels scrolled out).
// Returns the value's x.
int field(Canvas& c, int r, const char* label, const char* value, bool selected, int skip = 0,
          Font value_font = Font::UNSCII, uint8_t ink = color::BLACK);
// Width a field's value has (for the marquee).
int field_avail(const char* label);

// Left / Right (or any key) held: a first step at once, then after a short
// delay repeated steps, faster the longer it is held. steps() returns the
// steps of this frame for one key of `mask` (-1 / +1 per step for Left /
// Right: dir()).
struct Repeat {
    unsigned key = 0;
    int frames = 0;
    int steps(const Input& in, unsigned mask);
    int dir() const { return key == keys::LEFT || key == keys::UP || key == keys::L ? -1 : 1; }
};

// Text without printf.
struct Text {
    char buf[160];
    int n = 0;
    Text() { buf[0] = 0; }
    Text& operator<<(const char* s);
    Text& operator<<(char ch);
    Text& num(int v, int width = 1);
};

// DD.MM.YYYY / MM/DD/YYYY / YYYY-MM-DD.
void format_date(const Date& d, DateFormat f, char (&out)[16]);
// "08:00" or "8:00 AM".
void format_hour(int hour, TimeFormat f, char (&out)[12]);
void format_int(int v, char (&out)[12]);

}
