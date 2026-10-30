#pragma once
// Every press counts (gbareader V3.3, include/reader_key_latch.h): the keys
// are read at each VBlank (an interrupt, also while the main loop is busy with
// the SD card) and each change is kept in order. The main loop takes them one
// press at a time, so quick presses during card access are not lost.
#include <cstdint>

namespace app::keys {

// Keys as KEYINPUT has them (1 = held).
constexpr unsigned A = 1, B = 2, SELECT = 4, START = 8, RIGHT = 16, LEFT = 32, UP = 64, DOWN = 128,
                   R = 256, L = 512, ALL = 1023;

// What the main loop sees in a frame.
struct Frame {
    unsigned held = 0, pressed = 0;
    // VBlanks since the previous next() (1 when the loop keeps up): hold
    // timers count real time, also when drawing or the card took longer.
    unsigned frames = 1;
};

struct KeyLatch {
    static constexpr unsigned SIZE = 256;
    volatile uint16_t changes[SIZE];
    volatile uint16_t head;   // written by the interrupt
    volatile uint16_t tail;   // written by the main loop
    uint16_t sampled;         // the interrupt's last reading
    uint16_t seen;            // the keys as the main loop saw them last
    volatile uint32_t vblanks;   // counted by the interrupt
    uint32_t taken;              // vblanks at the previous next()

    // The keys held now (at start: not counted as presses).
    void start(unsigned keys)
    {
        head = 0;
        tail = 0;
        sampled = seen = uint16_t(keys);
        vblanks = taken = 0;
    }

    // At each VBlank: the keys held now.
    void sample(unsigned keys)
    {
        vblanks = vblanks + 1;
        if(keys == sampled) return;
        sampled = uint16_t(keys);
        const unsigned at = head, next = (at + 1) % SIZE;
        // Full: the newest change is replaced.
        if(next == tail) changes[(at + SIZE - 1) % SIZE] = uint16_t(keys);
        else { changes[at] = uint16_t(keys); head = uint16_t(next); }
    }

    // Once a main loop: the changes up to the next press (releases go with it).
    Frame next()
    {
        Frame f;
        while(tail != head) {
            const unsigned keys = changes[tail];
            const unsigned pressed = keys & ~unsigned(seen);
            seen = uint16_t(keys);
            tail = uint16_t((tail + 1) % SIZE);
            if(pressed) { f.pressed = pressed; break; }
        }
        f.held = seen;
        const uint32_t now = vblanks;
        const uint32_t n = now - taken;
        taken = now;
        f.frames = n < 1 ? 1 : n > 30 ? 30 : n;
        return f;
    }
};

#ifdef __DEVKITARM__
// The latch filled by the VBlank interrupt (src/platform/keys.iwram.cpp).
KeyLatch& latch();
// After bn::core::init(): the keys are read at each VBlank from now on.
void start_latch();
// The keys held right now (KEYINPUT), e.g. L+R at boot.
unsigned held_now();
#endif

}
