#pragma once
// Screens and the screen stack.
//
// Every screen is a Screen object (usually a static one). The stack's top
// screen gets each frame's keys (update) and draws the whole bitmap when it
// asked for a redraw. Opening a screen pushes it; B usually pops it again:
//
//   screens::push(day_screen());   // calendar.leave(), day.enter()
//   screens::pop();                // day.leave(), calendar.enter()
//   screens::replace(other);       // the top screen is exchanged
//
// After every change, keys that are still held are ignored until they are
// let go (a held A that opened a screen does not act in it).
//
// After waking from sleep (sleep.h) screens::guard() ignores every key until
// all keys have been up for GUARD_QUIET frames (a bouncing START does not
// act), and keys held then do not count as released either.
//
// The screens: calendar_screen.cpp (start), day_screen.cpp, entry_screen.cpp
// (category question, form, Custom repeat), todo_screen.cpp, text_edit.cpp
// (typing), question.cpp, settings_screen.cpp; accessors in screens.h.
#include <cstdint>
#include "app/canvas.h"
#include "app/key_latch.h"

namespace app {

// One frame of keys (app::keys::A ...).
struct Input {
    unsigned held = 0, pressed = 0, released = 0;
    // Frames since the previous update (hold timers count these).
    unsigned frames = 1;
    bool down(unsigned k) const { return held & k; }
    bool hit(unsigned k) const { return pressed & k; }
    bool up(unsigned k) const { return released & k; }
};

class Screen {
public:
    virtual ~Screen() = default;
    // Shown: pushed, or uncovered by a pop.
    virtual void enter() {}
    // Covered by a push, or popped / replaced.
    virtual void leave() {}
    virtual void update(const Input& in) = 0;
    virtual void draw(Canvas& c) = 0;

    void redraw() { _redraw = true; }
    bool take_redraw() { const bool r = _redraw; _redraw = false; return r; }

private:
    bool _redraw = true;
};

namespace screens {
constexpr int MAX_DEPTH = 8;
void push(Screen& s);
void pop();
void replace(Screen& s);
Screen* top();
int depth();
// The main loop: keys of this frame to the top screen (masking keys held
// since the last change); true when it must be drawn.
bool frame(unsigned held, unsigned pressed, unsigned frames = 1);
// Every key ignored until all keys have been up for GUARD_QUIET frames
// (gbamp3's unlock guard, UNLOCK_QUIET).
constexpr unsigned GUARD_QUIET = 6;
void guard();
bool guarded();
}

}
