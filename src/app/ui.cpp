#include "app/ui.h"

#include <cstring>

namespace app::ui {

void title(Canvas& c, const char* text) { c.small_center(0, text, color::BLACK); }

void title_right(Canvas& c, const char* text) { c.small_right(TEXT_RIGHT, 0, text, color::GREY); }

int text_width(const char* text, Font font)
{
    return font == Font::UNSCII ? Canvas::width16(text) : Canvas::body_width(text);
}

void row(Canvas& c, int r, const char* text, bool selected, int skip, Font font, int right, uint8_t ink)
{
    const int y = ROW_Y + r * ROW_H;
    if(selected) c.fill(0, y, SCREEN_W, ROW_H, color::SELECT);
    const int avail = right - TEXT_X;
    const int width = text_width(text, font);
    const auto draw = [&](const char* t, int s) {
        if(font == Font::UNSCII) c.text16(TEXT_X, y, t, ink, s, avail);
        else c.body(TEXT_X, y, t, ink, s, avail);
    };
    if(width <= avail || selected) { draw(text, width <= avail ? 0 : skip); return; }
    // Cut with "..." (UNSCII: whole characters).
    char shown[160];
    int n;
    if(font == Font::UNSCII) {
        const int chars = (avail - Canvas::width16("...")) / 8;
        int i = 0;
        for(int k = 0; k < chars && text[i]; ++k) utf8_next(text, i);
        n = i;
    }
    else n = Canvas::body_fit(text, avail - Canvas::body_width("..."));
    if(n > int(sizeof(shown)) - 4) n = int(sizeof(shown)) - 4;
    std::memcpy(shown, text, size_t(n));
    std::memcpy(shown + n, "...", 4);
    draw(shown, 0);
}

int field_avail(const char* label) { return TEXT_RIGHT - TEXT_X - Canvas::width16(label); }

int field(Canvas& c, int r, const char* label, const char* value, bool selected, int skip, Font value_font, uint8_t ink)
{
    const int y = ROW_Y + r * ROW_H;
    if(selected) c.fill(0, y, SCREEN_W, ROW_H, color::SELECT);
    c.text16(TEXT_X, y, label, color::BLACK);
    const int x = TEXT_X + Canvas::width16(label);
    const int avail = TEXT_RIGHT - x;
    const int width = text_width(value, value_font);
    const auto draw = [&](const char* t, int s) {
        if(value_font == Font::UNSCII) c.text16(x, y, t, ink, s, avail);
        else c.body(x, y, t, ink, s, avail);
    };
    if(width <= avail || selected) { draw(value, width <= avail ? 0 : skip); return x; }
    char shown[160];
    int n;
    if(value_font == Font::UNSCII) {
        const int chars = (avail - Canvas::width16("...")) / 8;
        int i = 0;
        for(int k = 0; k < chars && value[i]; ++k) utf8_next(value, i);
        n = i;
    }
    else n = Canvas::body_fit(value, avail - Canvas::body_width("..."));
    if(n > int(sizeof(shown)) - 4) n = int(sizeof(shown)) - 4;
    std::memcpy(shown, value, size_t(n));
    std::memcpy(shown + n, "...", 4);
    draw(shown, 0);
    return x;
}

int Repeat::steps(const Input& in, unsigned mask)
{
    constexpr int DELAY = 18, SLOW = 4, FAST_AFTER = 70;
    if(in.pressed & mask) {
        key = in.pressed & mask;
        key &= ~key + 1;
        frames = 0;
        return 1;
    }
    if(!key || !in.down(key)) { key = 0; return 0; }
    int n = 0;
    for(unsigned f = 0; f < in.frames; ++f) {
        ++frames;
        if(frames >= FAST_AFTER) ++n;
        else if(frames >= DELAY && (frames - DELAY) % SLOW == 0) ++n;
    }
    return n;
}

Text& Text::operator<<(const char* s)
{
    while(s && *s && n < int(sizeof(buf)) - 1) buf[n++] = *s++;
    buf[n] = 0;
    return *this;
}

Text& Text::operator<<(char ch)
{
    if(n < int(sizeof(buf)) - 1) buf[n++] = ch;
    buf[n] = 0;
    return *this;
}

Text& Text::num(int v, int width)
{
    char d[12];
    int k = 0;
    unsigned u = unsigned(v < 0 ? -v : v);
    do { d[k++] = char('0' + u % 10); u /= 10; } while(u);
    if(v < 0) *this << '-';
    for(int i = k; i < width; ++i) *this << '0';
    while(k) *this << d[--k];
    return *this;
}

void body_fit(Canvas& c, int x, int y, const char* text, int avail, uint8_t ink, bool scroll, int skip)
{
    if(Canvas::body_width(text) <= avail) { c.body(x, y, text, ink, 0, avail); return; }
    if(scroll) { c.body(x, y, text, ink, skip, avail); return; }
    char cut[256];
    int n = Canvas::body_fit(text, avail - Canvas::body_width("..."));
    if(n > int(sizeof(cut)) - 4) n = int(sizeof(cut)) - 4;
    std::memcpy(cut, text, size_t(n));
    std::memcpy(cut + n, "...", 4);
    c.body(x, y, cut, ink, 0, avail);
}

void underline(Canvas& c, int r) { c.hline(0, ROW_Y + (r + 1) * ROW_H - 1, SCREEN_W, color::LINE); }

void note(Canvas& c, const char* text)
{
    if(!text || !text[0]) return;
    c.fill(0, SCREEN_H - 18, SCREEN_W, 18, color::WHITE);
    c.hline(0, SCREEN_H - 18, SCREEN_W, color::LIGHT);
    c.small_center(SCREEN_H - 17, text, color::BLACK);
}

bool Marquee::step(int width, int avail)
{
    ++frame;
    if(width <= avail) return false;
    if(wait) { --wait; return false; }
    if(unsigned(width - avail) <= off) {
        if(++hold < 60) return false;
        reset();
        return true;
    }
    if(frame & 1) return false;
    ++off;
    return true;
}

namespace {
// Tiny formatting without printf (no newlib stdio in the ROM).
struct Out {
    char* p;
    char* end;
    void ch(char c) { if(p + 1 < end) *p++ = c; *p = 0; }
    void str(const char* s) { while(*s) ch(*s++); }
    void num(int v, int width)
    {
        char d[12];
        int n = 0;
        unsigned u = unsigned(v < 0 ? -v : v);
        do { d[n++] = char('0' + u % 10); u /= 10; } while(u);
        if(v < 0) ch('-');
        for(int k = n; k < width; ++k) ch('0');
        while(n) ch(d[--n]);
    }
};
}

void format_date(const Date& d, DateFormat f, char (&out)[16])
{
    Out o{out, out + sizeof(out)};
    out[0] = 0;
    switch(f) {
    case DateFormat::MDY_SLASHES: o.num(d.month, 2); o.ch('/'); o.num(d.day, 2); o.ch('/'); o.num(d.year, 4); break;
    case DateFormat::YMD_DASHES: o.num(d.year, 4); o.ch('-'); o.num(d.month, 2); o.ch('-'); o.num(d.day, 2); break;
    default: o.num(d.day, 2); o.ch('.'); o.num(d.month, 2); o.ch('.'); o.num(d.year, 4); break;
    }
}

void format_hour(int hour, TimeFormat f, char (&out)[12])
{
    Out o{out, out + sizeof(out)};
    out[0] = 0;
    if(f == TimeFormat::H24) { o.num(hour, 2); o.str(":00"); return; }
    o.num(hour % 12 ? hour % 12 : 12, 1);
    o.str(hour < 12 ? ":00 AM" : ":00 PM");
}

void format_int(int v, char (&out)[12])
{
    Out o{out, out + sizeof(out)};
    out[0] = 0;
    o.num(v, 1);
}

}
