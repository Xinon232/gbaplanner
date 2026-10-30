#include "app/screen.h"

namespace app::screens {

namespace {
Screen* stack[MAX_DEPTH];
int count = 0;
unsigned masked = 0, previous = 0;
bool changed = false;
// EWRAM (initialised data): IWRAM is kept for the stack.
#ifdef __arm__
#define SCREEN_EWRAM __attribute__((section(".ewram")))
#else
#define SCREEN_EWRAM
#endif
SCREEN_EWRAM bool guarding = false;
SCREEN_EWRAM unsigned quiet = 0;

void covered() { changed = true; }
}

void push(Screen& s)
{
    if(count && stack[count - 1]) stack[count - 1]->leave();
    if(count < MAX_DEPTH) stack[count++] = &s;
    else stack[count - 1] = &s;
    s.redraw();
    s.enter();
    covered();
}

void pop()
{
    if(count <= 1) return;
    stack[count - 1]->leave();
    --count;
    stack[count - 1]->redraw();
    stack[count - 1]->enter();
    covered();
}

void replace(Screen& s)
{
    if(!count) { push(s); return; }
    stack[count - 1]->leave();
    stack[count - 1] = &s;
    s.redraw();
    s.enter();
    covered();
}

Screen* top() { return count ? stack[count - 1] : nullptr; }
int depth() { return count; }

void guard()
{
    guarding = true;
    quiet = 0;
}

bool guarded() { return guarding; }

bool frame(unsigned held, unsigned pressed, unsigned frames)
{
    if(guarding) {
        // Nothing acts, not even a release: a key down (or a press that was
        // let go within the frame) starts the quiet time again. At the end
        // every key is up, so nothing stays masked.
        previous = held;
        masked = 0;
        if(held || pressed) quiet = 0;
        else if((quiet += frames ? frames : 1) >= GUARD_QUIET) guarding = false;
        return false;
    }
    if(changed) { masked = previous | held; changed = false; }
    const unsigned was_masked = masked;
    masked &= held;   // let go: counts again from the next press
    Input in;
    in.held = held & ~masked;
    in.pressed = pressed & ~masked;
    in.frames = frames ? frames : 1;
    // A masked key that is let go does not count as released.
    in.released = previous & ~held & ~was_masked;
    previous = held;
    Screen* s = top();
    if(!s) return false;
    s->update(in);
    s = top();   // update may have changed the screen
    return s && s->take_redraw();
}

}
