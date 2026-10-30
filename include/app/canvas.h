#pragma once
// The whole screen is one 240x160 bitmap with 8 bits per pixel (mode 4,
// gbawriter's renderer): every screen is drawn in software into the back page,
// then the pages flip. No sprites: dots, blocks, lines and the three fonts all
// go into the same bitmap with any palette colour.
//
// Fonts:
//   text16()  UNSCII 16 (8 px per character): menus, settings, calendar grid
//   small()   gbamp3 5x7 in 16 px cells (6 px): small titles, hints, weekdays
//   body()    SuperFW renderer (gbawriter): entry titles, notes, to-dos
//
// GBA VRAM has no byte writes: every write is a halfword (also on the host,
// where the canvas draws into plain memory for tests).
#include <cstdint>

namespace app {

// Palette indices (see palette_colors() for the colours).
namespace color {
constexpr uint8_t WHITE = 0, BLACK = 1, GREY = 2, LIGHT = 3, SELECT = 4, PALE = 5, LINE = 6;
// The eight category colours (Settings: event / task / birthday / holiday
// colour), in the order of the Settings values.
constexpr uint8_t CATEGORY = 16;
constexpr int CATEGORY_COUNT = 8;
enum Category : uint8_t { BLUE, ORANGE, PURPLE, GREEN, RED, TEAL, PINK, BROWN };
// Light tints of the same colours (blocks, bars): TINT + category.
constexpr uint8_t TINT = 24;
inline constexpr uint8_t category(int c) { return uint8_t(CATEGORY + (c & 7)); }
inline constexpr uint8_t tint(int c) { return uint8_t(TINT + (c & 7)); }
}

// 32 colours as 15-bit BGR (index -> colour), white first.
const uint16_t* palette_colors();
constexpr int PALETTE_SIZE = 32;
const char* category_name(int c);

constexpr int SCREEN_W = 240, SCREEN_H = 160;

class Canvas {
public:
    explicit Canvas(uint8_t* pixels = nullptr) : _px(pixels) {}
    void bind(uint8_t* pixels) { _px = pixels; }
    uint8_t* pixels() const { return _px; }

    void clear(uint8_t c = color::WHITE) { fill(0, 0, SCREEN_W, SCREEN_H, c); }
    void fill(int x, int y, int w, int h, uint8_t c);
    void pixel(int x, int y, uint8_t c);
    void hline(int x, int y, int w, uint8_t c) { fill(x, y, w, 1, c); }
    void vline(int x, int y, int h, uint8_t c) { fill(x, y, 1, h, c); }
    // 1 px outline.
    void frame(int x, int y, int w, int h, uint8_t c);

    // UNSCII 16 text. y is the top of the 16 px cell (ink on rows 1-12,
    // descenders to 14). skip: pixels scrolled out on the left (marquee);
    // nothing is drawn right of x + max_width. Returns the full text width.
    int text16(int x, int y, const char* s, uint8_t c = color::BLACK, int skip = 0, int max_width = SCREEN_W);
    static int width16(const char* s);
    void text16_center(int y, const char* s, uint8_t c = color::BLACK);

    // gbamp3 5x7 text, same conventions; y is the top of a 16 px cell
    // (ink on rows 5-11, descenders to 13).
    int small(int x, int y, const char* s, uint8_t c = color::BLACK, int max_width = SCREEN_W);
    static int small_width(const char* s);
    void small_center(int y, const char* s, uint8_t c = color::BLACK);
    void small_right(int right, int y, const char* s, uint8_t c = color::GREY);

    // SuperFW text (gbawriter renderer), y is the top of the 16 px line.
    void body(int x, int y, const char* s, uint8_t c = color::BLACK, int skip = 0, int max_width = 224);
    static int body_width(const char* s);
    // UTF-8 bytes of s that fit in width pixels.
    static int body_fit(const char* s, int width);

private:
    uint8_t* _px;
    void put(int x, int y, uint8_t c);
    void glyph(int x, int y, const uint8_t* rows, uint8_t c, int clip_left, int clip_right);
};

// UTF-8: the next code point of s at i (i advances; bad bytes give '?').
uint32_t utf8_next(const char* s, int& i);

}
