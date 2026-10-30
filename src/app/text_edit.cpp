// The text editor: gbavocab's entry editor (src/entry_editor.cpp,
// src/entry_render.cpp) reduced to one field.
#include "app/text_edit.h"

#include <cstring>
#include "app/data.h"
#include "app/ui.h"
#include "app/writer_layout.h"

namespace app {

namespace {
constexpr int TEXT_X = 8, TEXT_Y = 20, PITCH = writer::TEXT_PITCH, WIDTH = 220;
constexpr int STATUS_Y = 126, HINT_Y = 144;
constexpr int VIEW_ROWS = (STATUS_Y - TEXT_Y) / PITCH;   // 5

int measure(const char* glyph) { return Canvas::body_width(glyph); }

uint16_t writer_keys(unsigned held)
{
    using B = writer::Button;
    const auto bit = [](B b) { return uint16_t(1u << unsigned(b)); };
    uint16_t k = 0;
    if(held & keys::UP) k |= bit(B::UP);
    if(held & keys::DOWN) k |= bit(B::DOWN);
    if(held & keys::LEFT) k |= bit(B::LEFT);
    if(held & keys::RIGHT) k |= bit(B::RIGHT);
    if(held & keys::A) k |= bit(B::A);
    if(held & keys::B) k |= bit(B::B);
    if(held & keys::L) k |= bit(B::L);
    if(held & keys::R) k |= bit(B::R);
    if(held & keys::START) k |= bit(B::START);
    if(held & keys::SELECT) k |= bit(B::SELECT);
    return k;
}

class TextEditScreen final : public Screen {
public:
    TextEditRequest req;
    struct Big { writer::TextModel text; writer::Layout layout; };
    writer::TextModel& text = ewram<Big>().text;
    writer::Layout& layout = ewram<Big>().layout;
    writer::InputState input;
    writer::CaretClock clock;
    int viewport = 0;
    const char* message = "";
    bool finished = false;
    unsigned _held = 0;
    bool provisional = false;
    std::size_t provisional_end = 0;

    void open(const TextEditRequest& r, const char* initial)
    {
        req = r;
        if(!text.set_text(initial ? initial : "")) text.set_text("");
        input = writer::InputState();
        message = "";
        finished = false;
        provisional = false;
        viewport = 0;
        reflow();
    }
    void reflow()
    {
        layout.reflow(text, WIDTH, measure);
        const int row = layout.position(text, text.caret_byte()).row;
        if(row < viewport) viewport = row;
        if(row >= viewport + VIEW_ROWS) viewport = row - VIEW_ROWS + 1;
    }
    void enter() override { input.reset_transient(); redraw(); }
    void update(const Input& in) override
    {
        const bool before = clock.visible();
        clock.tick(in.pressed != 0);
        // Shift / Caps and the letter group follow the keys held.
        if(in.held != _held) { _held = in.held; redraw(); }
        input.update(writer_keys(in.held), [](void* p, writer::InputEvent e) { static_cast<TextEditScreen*>(p)->consume(e); },
                     this);
        if(finished) return;
        if(before != clock.visible()) redraw();
    }
    void consume(writer::InputEvent e);
    void draw(Canvas& c) override;
};

TextEditScreen editor;

int count_chars(const char* s, std::size_t n)
{
    int k = 0;
    for(std::size_t i = 0; i < n; ++i) if((static_cast<unsigned char>(s[i]) & 0xc0) != 0x80) ++k;
    return k;
}

void TextEditScreen::consume(writer::InputEvent e)
{
    if(finished) return;
    using K = writer::EventKind;
    clock.tick(true);
    bool edit = false, ok = true;
    const int chars = count_chars(text.data(), text.bytes());
    switch(e.kind) {
    case K::SAVE: {
        bool blank = true;
        for(std::size_t i = 0; i < text.bytes(); ++i) if(text.data()[i] != ' ' && text.data()[i] != '\n') blank = false;
        if(blank && !req.allow_empty) { message = "Type something first"; redraw(); return; }
        const char* r = req.done ? req.done(req.context, text.data()) : nullptr;
        if(r) { message = r; redraw(); return; }
        finished = true;
        if(screens::top() == this) screens::pop();
        return;
    }
    case K::SAVE_MENU:
        finished = true;
        screens::pop();
        return;
    case K::TOGGLE_STATUS:
        provisional = false;
        break;
    case K::INSERT:
        if(std::strchr(e.text, '\r') || std::strchr(e.text, '\t')) { ok = false; break; }
        if(std::strchr(e.text, '\n') && !req.multiline) return;   // Start alone: nothing in one-line fields
        if(chars + count_chars(e.text, std::strlen(e.text)) > req.max_chars) { ok = false; break; }
        ok = text.insert(e.text);
        edit = ok;
        if(input.select_active()) { provisional = ok; provisional_end = text.caret_byte(); }
        break;
    case K::REPLACE:
        if(input.select_active() && (!provisional || text.caret_byte() != provisional_end)) { ok = false; break; }
        ok = text.replace_before_caret(e.text);
        edit = ok;
        if(input.select_active() && ok) provisional_end = text.caret_byte();
        break;
    case K::BACKSPACE: edit = text.backspace(); break;
    case K::MOVE_LEFT: text.move_left(); layout.reset_column(); break;
    case K::MOVE_RIGHT: text.move_right(); layout.reset_column(); break;
    case K::MOVE_UP: layout.move(text, -1); break;
    case K::MOVE_DOWN: layout.move(text, 1); break;
    case K::PAGE_PREV: layout.move(text, -VIEW_ROWS); break;
    case K::PAGE_NEXT: layout.move(text, VIEW_ROWS); break;
    default: break;
    }
    if(!ok) { input.reject_edit(); message = chars >= req.max_chars ? "Limit reached" : "Not possible here"; }
    else if(edit) message = "";
    reflow();
    redraw();
}

void TextEditScreen::draw(Canvas& c)
{
    c.clear();
    ui::title(c, req.heading);
    char count[24];
    {
        char a[12], b[12];
        ui::format_int(count_chars(text.data(), text.bytes()), a);
        ui::format_int(req.max_chars, b);
        std::strcpy(count, a);
        std::strcat(count, "/");
        std::strcat(count, b);
    }
    ui::title_right(c, count);
    const char* s = text.data();
    int last = viewport + VIEW_ROWS;
    if(last > layout.rows()) last = layout.rows();
    for(int row = viewport; row < last; ++row) {
        int x = TEXT_X;
        const int y = TEXT_Y + (row - viewport) * PITCH;
        const std::size_t end = row + 1 < layout.rows() ? layout.row_start(row + 1) : text.bytes();
        for(std::size_t p = layout.row_content_start(text, row); p < end;) {
            char ch[5];
            p = writer::Layout::character(s, p, ch);
            if(ch[0] == '\n') break;
            const int w = layout.width(ch);
            if(w && ch[0] != ' ') c.body(x, y, ch, color::BLACK, 0, 232 - x);
            x += w;
        }
    }
    const writer::VisualPosition caret = layout.position(text, text.caret_byte());
    if(clock.visible() && caret.row >= viewport && caret.row < last)
        c.vline(TEXT_X + caret.x, TEXT_Y + (caret.row - viewport) * PITCH, 16, color::BLACK);
    // Status line: the letter group held, Shift / Caps, or a message.
    c.hline(0, STATUS_Y - 2, SCREEN_W, color::LIGHT);
    if(message[0]) c.small(TEXT_X, STATUS_Y, message, color::BLACK);
    else {
        const char* group = input.active_group();
        if(group[0]) c.body(TEXT_X, STATUS_Y - 2, group);
        if(input.caps()) c.small_right(ui::TEXT_RIGHT, STATUS_Y, "Caps", color::BLACK);
        else if(input.shift_armed()) c.small_right(ui::TEXT_RIGHT, STATUS_Y, "Shift", color::BLACK);
    }
    c.small(TEXT_X, HINT_Y, "Start+A: Done   Start+B: Back",
            color::GREY);
}
}

int utf8_chars(const char* s) { return count_chars(s, std::strlen(s)); }

void open_text_edit(const TextEditRequest& request, const char* initial)
{
    editor.open(request, initial);
    screens::push(editor);
}

}
