// Sleep: sleep.h. ROM code (IWRAM is kept for the stack).
#include "app/sleep.h"

#include <cstdint>

#include "bn_core.h"
#include "bn_core_lock.h"
#include "bn_keypad.h"

#include "app/app_state.h"
#include "app/calendar_screen.h"
#include "app/key_latch.h"
#include "app/screen.h"

extern "C" {
// libugba (Butano's bn_hw_irq.h): IE bits with IME saved and restored.
void IRQ_Enable(int index);
void IRQ_Disable(int index);
}

namespace app {

namespace {

constexpr int IRQ_KEYPAD = 12;
// START held this long wakes it: 60 frames of 228 lines (1 s), less a few
// lines for the time from the press to the count (the keypad interrupt
// ending the Stop), so that START down for 60 frames is enough.
constexpr unsigned HOLD_LINES = 60 * 228 - 8;

volatile uint16_t& reg16(uintptr_t at) { return *reinterpret_cast<volatile uint16_t*>(at); }

// KEYINPUT (0 = down) read here: keys::held_now() lives in IWRAM.
bool start_down() { return !(reg16(0x04000130) & keys::START); }

// START stays down for HOLD_LINES scanlines (VCOUNT keeps counting with
// the display blanked); up for any moment (a bounce too) ends it.
bool start_held_1s()
{
    unsigned line = reg16(0x04000006), lines = 0;
    while(start_down()) {
        const unsigned now = reg16(0x04000006);
        if(now != line) {
            line = now;
            if(++lines >= HOLD_LINES) return true;
        }
    }
    return false;
}

}

void sleep_until_start()
{
    // Nothing may be half done: the cursor day the calendar would save
    // after ~3 s, and the settings (both no-ops when already saved). Every
    // other save (entries, to-dos, conversions) ends before it returns, its
    // file synced and closed.
    calendar().flush();
    state::save();
    // As bn::core::sleep(START), but START must be held 1 s before the
    // display comes back (gbamp3's Hold START to unlock, player_sleep): one
    // more frame, then START up first.
    bn::core::update();
    while(bn::keypad::held(bn::keypad::key_type::START)) bn::core::update();
    {
        // Butano's interrupts (VBlank and the key latch with it, audio,
        // HDMA) off; the display blanked; the keypad interrupt on START.
        bn::core_lock lock;
        reg16(0x04000000) |= 0x80;
        reg16(0x04000132) = 0x4000 | keys::START;
        for(;;) {
            // Stop until START is down: the keypad interrupt ends it on the
            // GBA; anything else (an emulator without Stop) stops again.
            reg16(0x04000202) = 1u << IRQ_KEYPAD;   // nothing stale pending
            IRQ_Enable(IRQ_KEYPAD);
            do {
                __asm__ volatile("swi 0x03" ::: "r0", "r1", "r2", "r3", "memory");
            } while(!start_down());
            // No keypad interrupts while START is held; the screen stays
            // off. Released (or bounced) before 1 s: Stop again at once.
            IRQ_Disable(IRQ_KEYPAD);
            if(start_held_1s()) break;
        }
        reg16(0x04000132) = 0;
        reg16(0x04000202) = 1u << IRQ_KEYPAD;
        // ~core_lock: interrupts back, the next VBlank, the display on.
    }
    // The latch afresh: what is held now (START) is no press, and the
    // changes queued around the sleep (L+R let go) are dropped.
    keys::start_latch();
    screens::guard();
}

}
