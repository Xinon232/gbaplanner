#pragma once
// The month grid (start screen). Layout (240x160):
//   y 0-10   small title "October 2026"
//   y 11-21  weekday row (Mo .. Su or Su .. Sa), grey
//   y 22-159 6 week rows x 7 columns, cells 34x23 from x 1
// In a cell: the day number (UNSCII 16, grey for the other months) and under
// it one row of up to 7 dots (3x3, 1 px apart), or 6 dots and a small '+'.
// The cursor day has a light-blue box.
#include "app/day_marks.h"
#include "app/screen.h"

namespace app {

class CalendarScreen final : public Screen {
public:
    static constexpr int TITLE_Y = -3, WEEKDAY_Y = 7;
    static constexpr int GRID_X = 1, GRID_Y = 22, CELL_W = 34, CELL_H = 23;
    static constexpr int NUMBER_DY = 1, DOTS_DY = 16, DOT = 3, DOT_GAP = 1;

    CalendarScreen();
    // After the state is loaded: the cursor on the saved day (or the ROM
    // build date at the first start).
    void start();
    const Date& cursor() const { return _cursor; }
    // The day view went back on another day: the cursor follows it.
    void show_day(const Date& d) { move_to(d); }
    // Before sleeping: the cursor day saved now when it is not yet.
    void flush() { save_day(); }

    void enter() override;
    void leave() override;
    void update(const Input& in) override;
    void draw(Canvas& c) override;

private:
    Date _cursor{}, _saved{};
    bool _saved_known = false;
    int _idle = -1;   // frames since the last move; -1: nothing to save
    unsigned _dpad = 0;
    int _dpad_frames = 0, _dpad_next = 0;
    unsigned _lr = 0;
    int _lr_frames = 0, _lr_next = 0;
    bool _lr_years = false;
    bool _a_down = false;
    int _a_frames = 0;
    CellMarks* _marks;   // GRID_DAYS, in EWRAM
    bool _marks_valid = false;
    int _marks_month = 0;
    uint8_t _marks_week = 0;
    uint32_t _marks_serial = 0;
    uint32_t _marks_data = 0;

    int32_t grid_first() const;
    void compute_marks();
    void move_to(const Date& d);
    void step_dpad(unsigned key);
    void save_day();
    static void draw_dots(Canvas& c, int x, int y, const CellMarks& m, bool on_cursor);
};

CalendarScreen& calendar();

}
