// gbaplanner: calendar and to-do list for the Game Boy Advance.
// Start: white screen at once, the SD card (gbaSD + FatFs), the state, then
// the calendar (with the to-do list on top when Settings > General > Start
// screen says so). Every frame: the keys as Butano's keypad read them in the
// last bn::core::update to the top screen, which redraws the whole bitmap
// when it changed. A key pressed and let go while the app was busy (SD card)
// is not seen: presses are never queued and replayed.
#include "bn_bg_palette_item.h"
#include "bn_bg_palettes.h"
#include "bn_color.h"
#include "bn_core.h"
#include "bn_keypad.h"
#include "bn_palette_bitmap_bg_painter.h"
#include "bn_palette_bitmap_bg_ptr.h"

#include "app/app_state.h"
#include "app/calendar_screen.h"
#include "app/canvas.h"
#include "app/data.h"
#include "app/question.h"
#include "app/screens.h"
#include "app/sd.h"
#include "app/ui.h"

namespace {

bn::color colors[app::PALETTE_SIZE];

const bn::bg_palette_item& palette()
{
    for(int i = 0; i < app::PALETTE_SIZE; ++i) {
        const uint16_t v = app::palette_colors()[i];
        colors[i] = bn::color(v & 31, (v >> 5) & 31, (v >> 10) & 31);
    }
    static const bn::bg_palette_item item(bn::span<const bn::color>(colors), bn::bpp_mode::BPP_8);
    return item;
}

struct Display {
    bn::palette_bitmap_bg_ptr bg = bn::palette_bitmap_bg_ptr::create(palette());
    bn::palette_bitmap_bg_painter painter{bg};
    app::Canvas canvas;
    app::Canvas& begin()
    {
        canvas.bind(reinterpret_cast<uint8_t*>(painter.page().data()));
        return canvas;
    }
    void end() { painter.flip_page_later(); }
};

Display* display_ptr = nullptr;

// The keys of this frame (app::keys bits, KEYINPUT order) from bn::keypad.
struct Keys {
    unsigned held = 0, pressed = 0;
    // Frames since the previous update (1 when the loop keeps up), so that
    // hold timers count real time also when drawing or the card took longer.
    unsigned frames = 1;
};

Keys read_keys()
{
    using bn::keypad::key_type;
    constexpr key_type order[] = {key_type::A,     key_type::B,    key_type::SELECT, key_type::START,
                                  key_type::RIGHT, key_type::LEFT, key_type::UP,     key_type::DOWN,
                                  key_type::R,     key_type::L};
    Keys k;
    for(unsigned i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        if(bn::keypad::held(order[i])) k.held |= 1u << i;
        if(bn::keypad::pressed(order[i])) k.pressed |= 1u << i;
    }
    const int missed = bn::core::last_missed_frames();
    k.frames = missed <= 0 ? 1 : missed >= 29 ? 30 : unsigned(missed) + 1;
    return k;
}

void busy(Display& d, const char* title, const char* text)
{
    app::Canvas& c = d.begin();
    c.clear();
    app::ui::title(c, title);
    c.small_center(64, text);
    d.end();
    bn::core::update();
}

}

void app::show_busy(const char* title, const char* text)
{
    if(display_ptr) busy(*display_ptr, title, text);
}

int main()
{
    bn::core::init();
    // White at once: starting the SD card takes a while on a real cart, and
    // the engine's default backdrop is black.
    bn::bg_palettes::set_transparent_color(bn::color(31, 31, 31));
    bn::core::update();

    Display display;
    display_ptr = &display;
    busy(display, "gbaplanner", "Checking SD...");
    const bool card = app::sd::start();
    app::state::load();
    // The calendar and the to-do list (calendar.txt with 10,000 entries
    // takes several seconds).
    const char* note = "No SD card: nothing is saved";
    if(card) {
        busy(display, "gbaplanner", "Loading\xe2\x80\xa6");
        app::data::start();
        note = app::data::start_note();
    }
    app::calendar().start();
    app::screens::push(app::calendar_screen());
    // Start screen To-do list: the list from the remembered day (the
    // calendar stays under it; START goes back to it on that day).
    if(app::state::settings().start_screen == app::StartScreen::TODO)
        app::screens::push(app::todo_screen(app::calendar().cursor()));
    int note_frames = note ? 180 : 0;

    while(true) {
        const Keys k = read_keys();
        bool draw = app::screens::frame(k.held, k.pressed, k.frames);
        if(note_frames) {
            if(note_frames == 180 || note_frames == 1) draw = true;
            --note_frames;
        }
        if(draw) {
            app::Canvas& c = display.begin();
            app::screens::top()->draw(c);
            if(note_frames) app::ui::note(c, note);
            display.end();
        }
        bn::core::update();
    }
}
