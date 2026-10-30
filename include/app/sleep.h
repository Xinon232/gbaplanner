#pragma once
// Sleep (low power), after gbamp3's L+R lock and player_sleep(): only in the
// to-do list, L and R pressed together turn the screen off and put the GBA in
// Stop mode until START is held for 1 s (gbamp3's Hold START to unlock): the
// keypad interrupt on START ends the Stop, the display stays off while START
// is held, and START let go (or bouncing) before 60 frames stops again at
// once, nothing redrawn. Before that the calendar's cursor day and the
// settings are saved (every other save is finished when it returns). After
// waking the key latch starts afresh (the held START is no press) and
// screens::guard() ignores every key until all have been up for a moment, so
// that START does nothing else, also when it bounces.
namespace app {

void sleep_until_start();

}
