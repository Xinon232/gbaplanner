// The calendar (start screen): DESIGN.md "Calendar (start screen)".
#include "app/calendar_screen.h"

#include "app/app_state.h"
#include "app/data.h"
#include "app/day_screen.h"
#include "app/entry_screen.h"
#include "app/todo_screen.h"
#include "app/screens.h"
#include "app/ui.h"

namespace app {

namespace {
// D-pad: one step per press; held: the first repeat at DPAD_DELAY frames,
// then a step every DPAD_REPEAT frames; held past DPAD_FAST frames the
// interval shrinks by one frame every DPAD_RAMP frames down to DPAD_MIN
// (7.5 steps/s, then up to 10 steps/s).
constexpr int DPAD_DELAY = 16, DPAD_REPEAT = 8, DPAD_FAST = 120, DPAD_RAMP = 60, DPAD_MIN = 6;
// L / R: let go before LR_HOLD frames (1 s) = one month; held = one year at
// LR_HOLD, then every LR_REPEAT frames (0.5 s); from LR_RAMP1 frames (2 s)
// the interval shrinks step by step to LR_MID frames at LR_RAMP2 (3 s, about
// 5 years/s), then to LR_MIN frames at LR_RAMP3 (5 s, 15 years/s).
constexpr int LR_HOLD = 60, LR_REPEAT = 30, LR_RAMP1 = 120, LR_RAMP2 = 180, LR_MID = 12, LR_RAMP3 = 300,
              LR_MIN = 4;
// Hold A this long: new entry (else A opens the day when let go).
constexpr int A_HOLD = 30;
// The cursor day is saved after this many frames without a move (~3 s).
constexpr int SAVE_IDLE = 180;

// Frames to the next D-pad step after a step at `held` frames.
int dpad_interval(int held)
{
    if(held < DPAD_FAST) return DPAD_REPEAT;
    const int i = DPAD_REPEAT - 1 - (held - DPAD_FAST) / DPAD_RAMP;
    return i < DPAD_MIN ? DPAD_MIN : i;
}

// Frames to the next year after a year step at `held` frames (a smooth ramp).
int year_interval(int held)
{
    if(held < LR_RAMP1) return LR_REPEAT;
    if(held < LR_RAMP2) return LR_REPEAT - (LR_REPEAT - LR_MID) * (held - LR_RAMP1) / (LR_RAMP2 - LR_RAMP1);
    if(held < LR_RAMP3) return LR_MID - (LR_MID - LR_MIN) * (held - LR_RAMP2) / (LR_RAMP3 - LR_RAMP2);
    return LR_MIN;
}

}

namespace {
__attribute__((section(".sbss"))) CellMarks grid_marks[GRID_DAYS];
}

CalendarScreen::CalendarScreen() : _marks(grid_marks) {}

void CalendarScreen::start()
{
    const Settings& s = state::settings();
    _cursor = s.has_day ? s.day : build_date();
    _saved = _cursor;
    _saved_known = s.has_day;
    _marks_valid = false;
}

void CalendarScreen::enter()
{
    // Back from another screen: data or settings may have changed.
    _marks_valid = false;
    _dpad = 0;
    _lr = 0;
    _a_down = false;
    redraw();
}

void CalendarScreen::leave() { save_day(); }

void CalendarScreen::save_day()
{
    _idle = -1;
    if(_saved_known && _saved == _cursor) return;
    Settings& s = state::settings();
    s.day = _cursor;
    s.has_day = true;
    if(state::save()) { _saved = _cursor; _saved_known = true; }
}

int32_t CalendarScreen::grid_first() const
{
    const Date first{_cursor.year, _cursor.month, 1};
    const bool sunday = state::settings().week_start == WeekStart::SUNDAY;
    const int offset = (weekday(first) + (sunday ? 1 : 0)) % 7;
    return day_number(first) - offset;
}

void CalendarScreen::compute_marks()
{
    const int32_t base = grid_first();
    for(int i = 0; i < GRID_DAYS; ++i) _marks[i] = CellMarks();
    int32_t start = base < 0 ? 0 : base, end = base + GRID_DAYS - 1;
    if(end > LAST_DAY) end = LAST_DAY;
    if(start <= end) day_marks(from_day_number(start), int(end - start + 1), _marks + (start - base));
    _marks_valid = true;
    _marks_month = _cursor.year * 12 + _cursor.month;
    _marks_week = uint8_t(state::settings().week_start);
    _marks_serial = test_marks_serial();
    _marks_data = data::serial();
}

void CalendarScreen::move_to(const Date& d)
{
    const Date c = clamp_date(d);
    if(c == _cursor) return;
    const bool month_changed = c.year != _cursor.year || c.month != _cursor.month;
    _cursor = c;
    _idle = 0;
    if(month_changed) _marks_valid = false;
    redraw();
}

void CalendarScreen::update(const Input& in)
{
    using namespace keys;
    // ---- D-pad: days and weeks ----
    constexpr unsigned DPAD = UP | DOWN | LEFT | RIGHT;
    if(in.pressed & DPAD) {
        _dpad = in.pressed & DPAD;
        _dpad = _dpad & (~_dpad + 1);   // one key
        _dpad_frames = 0;
        _dpad_next = DPAD_DELAY;
        step_dpad(_dpad);
    }
    else if(_dpad && in.down(_dpad)) {
        for(unsigned f = 0; f < in.frames; ++f) {
            if(++_dpad_frames < _dpad_next) continue;
            _dpad_next = _dpad_frames + dpad_interval(_dpad_frames);
            step_dpad(_dpad);
        }
    }
    else _dpad = 0;

    // ---- L / R: months (tap) and years (hold) ----
    if(!_lr && (in.pressed & (L | R))) {
        _lr = (in.pressed & L) ? L : R;
        _lr_frames = 0;
        _lr_next = LR_HOLD;
        _lr_years = false;
    }
    else if(_lr && in.down(_lr)) {
        const int dir = _lr == L ? -1 : 1;
        for(unsigned f = 0; f < in.frames; ++f) {
            if(++_lr_frames >= _lr_next) {
                _lr_next = _lr_frames + year_interval(_lr_frames);
                _lr_years = true;
                // Years stop at 1900 and 2200 (the month stays).
                const int year = _cursor.year + dir;
                if(year >= YEAR_MIN && year <= YEAR_MAX) move_to(add_months(_cursor, 12 * dir));
            }
        }
    }
    else if(_lr) {
        if(!_lr_years) move_to(add_months(_cursor, _lr == L ? -1 : 1));
        _lr = 0;
        _lr_years = false;
        redraw();   // the dots come back
    }

    // ---- A: open the day (on release) or hold: new entry ----
    if(in.hit(A)) { _a_down = true; _a_frames = 0; }
    else if(_a_down && in.down(A)) {
        _a_frames += int(in.frames);
        if(_a_frames >= A_HOLD) {
            _a_down = false;
            screens::push(new_entry_screen(_cursor));
            return;
        }
    }
    else if(_a_down && in.up(A)) {
        _a_down = false;
        screens::push(day_screen(_cursor));
        return;
    }
    if(in.hit(START)) { screens::push(todo_screen(_cursor)); return; }
    if(in.hit(SELECT)) { screens::push(settings_screen()); return; }

    // ---- Marks: when the month changed (not while years scroll) ----
    const bool scrolling = _lr && _lr_years;
    if(!scrolling) {
        if(!_marks_valid || _marks_month != _cursor.year * 12 + _cursor.month ||
           _marks_week != uint8_t(state::settings().week_start) || _marks_serial != test_marks_serial() || _marks_data != data::serial()) {
            compute_marks();
            redraw();
        }
    }

    // ---- Save the cursor day after a few seconds without a move ----
    if(_idle >= 0) {
        _idle += int(in.frames);
        if(_idle >= SAVE_IDLE && !in.held) save_day();
    }
}

void CalendarScreen::step_dpad(unsigned key)
{
    using namespace keys;
    const int step = key == LEFT ? -1 : key == RIGHT ? 1 : key == UP ? -7 : 7;
    const int32_t n = day_number(_cursor) + step;
    if(n < 0 || n > LAST_DAY) return;
    move_to(from_day_number(n));
}

void CalendarScreen::draw(Canvas& c)
{
    c.clear();
    // Title: month and year, small, at the very top.
    char title[24];
    {
        const char* m = month_name(_cursor.month);
        int n = 0;
        while(m[n] && n < 14) { title[n] = m[n]; ++n; }
        title[n++] = ' ';
        int y = _cursor.year;
        title[n + 3] = char('0' + y % 10); y /= 10;
        title[n + 2] = char('0' + y % 10); y /= 10;
        title[n + 1] = char('0' + y % 10); y /= 10;
        title[n] = char('0' + y % 10);
        title[n + 4] = 0;
    }
    c.small_center(TITLE_Y, title);
    // Weekday row.
    const bool sunday = state::settings().week_start == WeekStart::SUNDAY;
    for(int col = 0; col < 7; ++col) {
        const char* wd = weekday_short(sunday ? (col + 6) % 7 : col);
        const int x = GRID_X + col * CELL_W + (CELL_W - Canvas::small_width(wd)) / 2;
        c.small(x, WEEKDAY_Y, wd, color::GREY);
    }
    // Days.
    const int32_t base = grid_first();
    const int32_t cursor = day_number(_cursor);
    const bool dots = _marks_valid && !(_lr && _lr_years);
    for(int i = 0; i < GRID_DAYS; ++i) {
        const int32_t n = base + i;
        if(n < 0 || n > LAST_DAY) continue;
        const Date d = from_day_number(n);
        const int x = GRID_X + (i % 7) * CELL_W, y = GRID_Y + (i / 7) * CELL_H;
        const bool inside = d.month == _cursor.month;
        if(n == cursor) c.fill(x + 1, y, CELL_W - 2, CELL_H - 1, color::SELECT);
        char num[3] = {0, 0, 0};
        if(d.day >= 10) { num[0] = char('0' + d.day / 10); num[1] = char('0' + d.day % 10); }
        else num[0] = char('0' + d.day);
        const int w = Canvas::width16(num);
        c.text16(x + (CELL_W - w) / 2, y + NUMBER_DY, num, inside ? color::BLACK : color::LIGHT);
        if(dots) draw_dots(c, x, y + DOTS_DY, _marks[i], n == cursor);
    }
}

void CalendarScreen::draw_dots(Canvas& c, int x, int y, const CellMarks& m, bool on_cursor)
{
    if(!m.count) return;
    const bool more = m.count > DAY_MARK_DOTS;
    const int dots = more ? DAY_MARK_DOTS - 1 : m.count;
    const int shown = dots + (more ? 1 : 0);
    const int width = shown * DOT + (shown - 1) * DOT_GAP;
    int px = x + (CELL_W - width) / 2;
    for(int k = 0; k < dots; ++k, px += DOT + DOT_GAP) {
        const uint8_t col = color::category(m.color[k]);
        if(m.hollow_mask & (1u << k)) {
            c.frame(px, y, DOT, DOT, col);
            c.pixel(px + 1, y + 1, color::WHITE);
        }
        else c.fill(px, y, DOT, DOT, col);
    }
    if(more) {
        // A small '+' (3x3) in black.
        c.hline(px, y + 1, 3, color::BLACK);
        c.vline(px + 1, y, 3, color::BLACK);
    }
    (void)on_cursor;
}

Screen& calendar_screen()
{
    static CalendarScreen screen;
    return screen;
}

CalendarScreen& calendar()
{
    return static_cast<CalendarScreen&>(calendar_screen());
}

}
