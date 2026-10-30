#pragma once
// What the month grid shows under each day number: one dot per item, in the
// DESIGN.md order (holidays (country and own), birthdays, then events and
// tasks by time, all-day first). The calendar asks for the 42 days of its
// grid at once (also the grey days of the other months).
//
// src/app/day_marks.cpp fills it from the data core's month marks
// (planner::cal_marks) and the country holidays of Settings > Holidays.
// (app::CellMarks is the screen's form; planner::DayMarks the core's.)
#include <cstdint>
#include "app/date.h"

namespace app {

constexpr int DAY_MARK_COLORS = 8;   // colours kept per day (7 dots shown)
constexpr int DAY_MARK_DOTS = 7;     // more items: 6 dots and a small '+'
constexpr int GRID_DAYS = 42;        // 6 weeks x 7 days

struct CellMarks {
    // All items of the day (may be more than DAY_MARK_COLORS).
    uint8_t count = 0;
    // Category colour (color::Category, 0 Blue .. 7 Brown, already resolved
    // through Settings > Event / Task / Birthday / Holiday color) of the first
    // items, in display order.
    uint8_t color[DAY_MARK_COLORS] = {};
    // Bit i: item i is a ticked task (hollow ring instead of a dot).
    uint8_t hollow_mask = 0;
};

// Marks of `count` consecutive days starting at `first` (out[0] = first).
// Called when the shown month changes (not while years scroll with L/R held)
// and when the calendar comes back from another screen.
void day_marks(const Date& first, int count, CellMarks* out);

// ---- Test hook (emulator tests) ----
// The emulator tests write marks into this table (address from the ELF,
// symbol planner_test_marks) while the ROM runs. When magic is MAGIC, they
// are added after the real items; a new `serial` makes the calendar ask again.
// Never set by the app itself.
struct TestMark {
    int16_t year;
    uint8_t month, day;
    uint8_t color;     // 0..7
    uint8_t hollow;    // 1: ticked task
};
struct TestMarks {
    static constexpr uint32_t MAGIC = 0x4b52414d;   // "MARK"
    static constexpr int MAX = 64;
    volatile uint32_t magic;
    volatile uint32_t serial;
    volatile uint32_t count;
    TestMark marks[MAX];
};
// Serial of the test table (0 when unused); the calendar polls it each frame.
uint32_t test_marks_serial();

}
