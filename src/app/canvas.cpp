#include "app/canvas.h"
#include "app/ui_font_data.h"

#ifndef PLANNER_HOST
extern "C" {
#include "../../references/superfw/src/fonts/font_render.h"
}
#endif

namespace app {

namespace {

constexpr uint16_t rgb(int r, int g, int b) { return uint16_t(r | (g << 5) | (b << 10)); }
// From 8-bit RGB (rounded to 5 bits).
constexpr uint16_t rgb8(int r, int g, int b) { return rgb((r * 31 + 127) / 255, (g * 31 + 127) / 255, (b * 31 + 127) / 255); }
// Category colour mixed with white (amount of white in 1/8).
constexpr uint16_t tint8(int r, int g, int b) { return rgb8(r + (255 - r) * 5 / 8, g + (255 - g) * 5 / 8, b + (255 - b) * 5 / 8); }

#define CAT_COLORS(F) \
    F(0x2f, 0x6f, 0xdf) /* Blue   */ \
    F(0xf0, 0x8a, 0x1c) /* Orange */ \
    F(0x8a, 0x4f, 0xcf) /* Purple */ \
    F(0x2e, 0x9e, 0x44) /* Green  */ \
    F(0xd9, 0x30, 0x30) /* Red    */ \
    F(0x16, 0x9c, 0x9c) /* Teal   */ \
    F(0xe0, 0x5a, 0xa8) /* Pink   */ \
    F(0x8b, 0x5a, 0x2b) /* Brown  */
#define AS_COLOR(r, g, b) rgb8(r, g, b),
#define AS_TINT(r, g, b) tint8(r, g, b),

constexpr uint16_t colors[PALETTE_SIZE] = {
    rgb(31, 31, 31),   // 0 white
    rgb(0, 0, 0),      // 1 black
    rgb(12, 12, 12),   // 2 grey: hints, weekday row (gbamp3 0x4210-like)
    rgb(20, 20, 20),   // 3 light grey: days of the other months
    rgb(21, 26, 31),   // 4 light blue: selected row, cursor day (gbamp3)
    rgb(27, 27, 27),   // 5 pale grey
    rgb(16, 16, 16),   // 6 line under "New ..." rows
    0, 0, 0, 0, 0, 0, 0, 0, 0,
    CAT_COLORS(AS_COLOR)   // 16..23
    CAT_COLORS(AS_TINT)    // 24..31
};

const char* const category_names[color::CATEGORY_COUNT] = {"Blue", "Orange", "Purple", "Green",
                                                            "Red", "Teal", "Pink", "Brown"};

template <int N>
int glyph_index(uint32_t code, const uint16_t (&extra)[N])
{
    if(code >= 33 && code <= 126) return int(code - 33);
    for(int i = 0; i < N; ++i) if(extra[i] == code) return 94 + i;
    return '?' - 33;
}

}

const uint16_t* palette_colors() { return colors; }
const char* category_name(int c) { return category_names[c & 7]; }

uint32_t utf8_next(const char* s, int& i)
{
    const auto b = [&](int k) { return uint32_t(static_cast<unsigned char>(s[i + k])); };
    const uint32_t c = b(0);
    uint32_t code = '?';
    int n = 1;
    if(c < 0x80) code = c;
    else if((c & 0xe0) == 0xc0 && (b(1) & 0xc0) == 0x80) { code = ((c & 0x1f) << 6) | (b(1) & 0x3f); n = 2; }
    else if((c & 0xf0) == 0xe0 && (b(1) & 0xc0) == 0x80 && (b(2) & 0xc0) == 0x80) {
        code = ((c & 0x0f) << 12) | ((b(1) & 0x3f) << 6) | (b(2) & 0x3f);
        n = 3;
    }
    else if((c & 0xf8) == 0xf0 && (b(1) & 0xc0) == 0x80 && (b(2) & 0xc0) == 0x80 && (b(3) & 0xc0) == 0x80) {
        code = ((c & 0x07) << 18) | ((b(1) & 0x3f) << 12) | ((b(2) & 0x3f) << 6) | (b(3) & 0x3f);
        n = 4;
    }
    i += n;
    return code;
}

void Canvas::put(int x, int y, uint8_t c)
{
    auto* p = reinterpret_cast<volatile uint16_t*>(_px + y * SCREEN_W + (x & ~1));
    *p = (x & 1) ? uint16_t((*p & 0x00ff) | (c << 8)) : uint16_t((*p & 0xff00) | c);
}

void Canvas::pixel(int x, int y, uint8_t c)
{
    if(x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H) put(x, y, c);
}

void Canvas::fill(int x, int y, int w, int h, uint8_t c)
{
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > SCREEN_W) w = SCREEN_W - x;
    if(y + h > SCREEN_H) h = SCREEN_H - y;
    if(w <= 0 || h <= 0) return;
    const uint16_t both = uint16_t(c | (c << 8));
    const uint32_t four = both | uint32_t(both) << 16;
    const int end = x + w;
    for(int j = y; j < y + h; ++j) {
        uint8_t* row = _px + j * SCREEN_W;
        int i = x;
        if((i & 1) && i < end) { put(i, j, c); ++i; }
        if((i & 2) && i + 1 < end) { *reinterpret_cast<volatile uint16_t*>(row + i) = both; i += 2; }
        // Whole words (a 240 px row is 60 of them).
        auto* q = reinterpret_cast<volatile uint32_t*>(row + i);
        for(; i + 3 < end; i += 4) *q++ = four;
        if(i + 1 < end) { *reinterpret_cast<volatile uint16_t*>(row + i) = both; i += 2; }
        if(i < end) put(i, j, c);
    }
}

void Canvas::frame(int x, int y, int w, int h, uint8_t c)
{
    fill(x, y, w, 1, c);
    fill(x, y + h - 1, w, 1, c);
    fill(x, y, 1, h, c);
    fill(x + w - 1, y, 1, h, c);
}

void Canvas::glyph(int x, int y, const uint8_t* rows, uint8_t c, int clip_left, int clip_right)
{
    if(clip_left < 0) clip_left = 0;
    if(clip_right > SCREEN_W) clip_right = SCREEN_W;
    for(int r = 0; r < 16; ++r) {
        const int py = y + r;
        unsigned bits = rows[r];
        if(!bits || py < 0 || py >= SCREEN_H) continue;
        for(int k = 0; bits; ++k, bits = (bits << 1) & 0xff) {
            if(!(bits & 0x80)) continue;
            const int px = x + k;
            if(px >= clip_left && px < clip_right) put(px, py, c);
        }
    }
}

int Canvas::text16(int x, int y, const char* s, uint8_t c, int skip, int max_width)
{
    int pen = x - skip, i = 0;
    const int right = x + max_width;
    while(s[i]) {
        const uint32_t code = utf8_next(s, i);
        if(code != ' ' && pen + 8 > x && pen < right)
            glyph(pen, y, fontdata::unscii16_rows[glyph_index(code, fontdata::unscii16_extra)], c, x, right);
        pen += fontdata::unscii16_advance;
    }
    return pen - (x - skip);
}

int Canvas::width16(const char* s)
{
    int n = 0, i = 0;
    while(s[i]) { utf8_next(s, i); ++n; }
    return n * fontdata::unscii16_advance;
}

void Canvas::text16_center(int y, const char* s, uint8_t c) { text16((SCREEN_W - width16(s)) / 2, y, s, c); }

int Canvas::small(int x, int y, const char* s, uint8_t c, int max_width)
{
    int pen = x, i = 0;
    const int right = x + max_width;
    while(s[i]) {
        const uint32_t code = utf8_next(s, i);
        if(code != ' ' && pen < right)
            glyph(pen, y, fontdata::small_rows[glyph_index(code, fontdata::small_extra)], c, x, right);
        pen += fontdata::small_advance;
    }
    return pen - x;
}

int Canvas::small_width(const char* s)
{
    int n = 0, i = 0;
    while(s[i]) { utf8_next(s, i); ++n; }
    // The last character's empty column does not count.
    return n ? n * fontdata::small_advance - 1 : 0;
}

void Canvas::small_center(int y, const char* s, uint8_t c) { small((SCREEN_W - small_width(s)) / 2, y, s, c); }

void Canvas::small_right(int right, int y, const char* s, uint8_t c) { small(right - small_width(s), y, s, c); }

#ifndef PLANNER_HOST
void Canvas::body(int x, int y, const char* s, uint8_t c, int skip, int max_width)
{
    if(x < 0 || y < 0 || y + 16 > SCREEN_H || max_width <= 0) return;
    if(x + max_width > SCREEN_W) max_width = SCREEN_W - x;
    draw_text_idx8_bus16_range(s, _px + y * SCREEN_W + x, unsigned(skip), unsigned(max_width), SCREEN_W, c);
}
int Canvas::body_width(const char* s) { return int(font_width(s)); }
int Canvas::body_fit(const char* s, int width) { return int(font_width_cap(s, unsigned(width))); }
#else
// Host: SuperFW text is measured as 8 px per character and not drawn.
void Canvas::body(int, int, const char*, uint8_t, int, int) {}
int Canvas::body_width(const char* s) { return width16(s); }
int Canvas::body_fit(const char* s, int width)
{
    int i = 0, w = 0;
    while(s[i]) { int j = i; utf8_next(s, j); if(w + 8 > width) break; w += 8; i = j; }
    return i;
}
#endif

}
