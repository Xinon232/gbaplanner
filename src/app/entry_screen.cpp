// The entry form: DESIGN.md "New entry / edit entry".
//
// New: the category question (Event / Task / Birthday / Holiday), then the
// form. Rows: Category, Title, Notes (Settings > Notes On), Date (day /
// month / year; A or L / R pick the part, Left / Right change it), All day
// (event, task), Start / End (event) or Time (task) in 5-minute steps, Repeat
// (Does not repeat / Every day / week / month / year / Custom...), then
// Save, Done (tasks, when editing), Delete and Delete only this day
// (repeating, when editing). B leaves without saving.
#include "app/entry_screen.h"

#include <cstring>
#include "app/app_state.h"
#include "app/data.h"
#include "app/text_edit.h"
#include "app/ui.h"

namespace app {

using planner::Category;
using planner::EndKind;
using planner::Freq;
using planner::MonthlyMode;
using planner::Rule;

namespace {

constexpr Category ORDER[4] = {Category::EVENT, Category::TASK, Category::BIRTHDAY, Category::HOLIDAY};
int order_of(Category c)
{
    for(int i = 0; i < 4; ++i) if(ORDER[i] == c) return i;
    return 0;
}

const char* const weekday_letters_mon[7] = {"M", "T", "W", "T", "F", "S", "S"};
const char* const wd2[7] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
const char* const ordinals[6] = {"", "1st", "2nd", "3rd", "4th", "5th"};

bool sunday_first() { return state::settings().week_start == WeekStart::SUNDAY; }

void date_text(const Date& d, ui::Text& t)
{
    char buf[16];
    ui::format_date(d, state::settings().date_format, buf);
    t << buf;
}

// Character columns of day / month / year in the formatted date.
void part_columns(int part, int& col, int& len)
{
    const DateFormat f = state::settings().date_format;
    static const int dmy[3][2] = {{0, 2}, {3, 2}, {6, 4}};
    static const int mdy[3][2] = {{3, 2}, {0, 2}, {6, 4}};
    static const int ymd[3][2] = {{8, 2}, {5, 2}, {0, 4}};
    const int (*t)[2] = f == DateFormat::MDY_SLASHES ? mdy : f == DateFormat::YMD_DASHES ? ymd : dmy;
    col = t[part][0];
    len = t[part][1];
}

// Date part step: day within the month, month (day clamped), year.
Date step_date(const Date& d, int part, int dir)
{
    Date r = d;
    if(part == 0) {
        const int last = days_in_month(d.year, d.month);
        r.day = int8_t((d.day - 1 + dir + last * 4) % last + 1);
    }
    else if(part == 1) {
        r.month = int8_t((d.month - 1 + dir + 48) % 12 + 1);
        r = clamp_date(r);
    }
    else {
        int y = d.year + dir;
        if(y < YEAR_MIN) y = YEAR_MIN;
        if(y > YEAR_MAX) y = YEAR_MAX;
        r.year = int16_t(y);
        r = clamp_date(r);
    }
    return r;
}

// L / R: the date part left / right of `part` in the date format's order
// (stops at the first and last part).
int next_part(int part, int dir)
{
    const DateFormat f = state::settings().date_format;
    static const int dmy[3] = {0, 1, 2}, mdy[3] = {1, 0, 2}, ymd[3] = {2, 1, 0};
    const int* order = f == DateFormat::MDY_SLASHES ? mdy : f == DateFormat::YMD_DASHES ? ymd : dmy;
    int pos = 0;
    while(pos < 2 && order[pos] != part) ++pos;
    pos += dir;
    return order[pos < 0 ? 0 : pos > 2 ? 2 : pos];
}

// Underlines the selected date part of a field drawn at value x.
void mark_part(Canvas& c, int r, int value_x, int part)
{
    int col, len;
    part_columns(part, col, len);
    const int y = ui::ROW_Y + r * ui::ROW_H + 14;
    c.fill(value_x + col * 8, y, len * 8 - 1, 2, color::BLACK);
}

bool is_preset(const Rule& r)
{
    return r.interval == 1 && r.weekdays == 0 && r.mmode == MonthlyMode::START_DAY && r.end == EndKind::NEVER;
}

void rule_text(const Rule& r, int32_t start, ui::Text& t)
{
    static const char* const unit1[5] = {"", "day", "week", "month", "year"};
    static const char* const units[5] = {"", "days", "weeks", "months", "years"};
    if(r.freq == Freq::NONE) { t << "Does not repeat"; return; }
    t << "Every ";
    if(r.interval == 1) t << unit1[int(r.freq)];
    else t.num(r.interval) << ' ' << units[int(r.freq)];
    if(r.freq == Freq::WEEKLY && r.weekdays) {
        t << " on ";
        bool first = true;
        for(int k = 0; k < 7; ++k) {
            const int wd = sunday_first() ? (k + 6) % 7 : k;
            if(!(r.weekdays & (1 << wd))) continue;
            if(!first) t << ",";
            t << wd2[wd];
            first = false;
        }
    }
    if(r.freq == Freq::MONTHLY) {
        if(r.mmode == MonthlyMode::DAY_N) { t << " on day "; t.num(r.mday); }
        else if(r.mmode == MonthlyMode::NTH_WEEKDAY) t << " on the " << ordinals[r.nth] << ' ' << weekday_name(r.mweekday);
        else if(r.mmode == MonthlyMode::LAST_WEEKDAY) t << " on the last " << weekday_name(r.mweekday);
    }
    if(r.end == EndKind::UNTIL) { t << ", until "; date_text(data::to_date(r.until), t); }
    else if(r.end == EndKind::COUNT) { t << ", "; t.num(r.count) << (r.count == 1 ? " time" : " times"); }
    (void)start;
}

Rule preset(int k)
{
    Rule r;
    r.freq = Freq(k);
    return r;
}

// ---- Shared form state ----
struct Form {
    bool editing = false;
    planner::EntryId id = planner::NO_ENTRY;
    Date occurrence{};
    Category cat = Category::EVENT;
    bool notes_loaded = false, notes_changed = false;
    Date date{};
    bool timed = true;
    int start = 9 * 60, end = 10 * 60;
    Rule rule;
    bool custom = false;      // Repeat shows "Custom..." (a rule beyond the presets)
    bool repeats_stored = false;
    bool done = false;        // the occurrence is ticked (tasks)
};
Form form;
// The texts of the form (EWRAM).
APP_EWRAM char form_title[planner::MAX_TITLE_BYTES + 1];
APP_EWRAM char form_notes[planner::MAX_NOTES_BYTES + 1];

// ---- Custom repeat ----
class RepeatScreen final : public Screen {
public:
    void open();
    void update(const Input& in) override;
    void draw(Canvas& c) override;

private:
    enum Row : uint8_t { EVERY, UNIT, ON_DAYS, MONTHLY, ENDS, UNTIL, COUNT, OK };
    Rule _r;
    uint8_t _mask = 0;   // weekly: days in the Monday = bit 0 order
    Date _until{};
    int _rows[8], _count = 0, _sel = 0, _day_cursor = 0, _part = 0;
    Rule _monthly[4];
    int _monthly_count = 0, _monthly_sel = 0;
    ui::Repeat _repeat;
    ui::Marquee _marquee;
    void build();
    void change(int row, int dir);
    void label(int row, ui::Text& t) const;
};
RepeatScreen repeat_screen;

void RepeatScreen::open()
{
    _r = form.rule;
    if(_r.freq == Freq::NONE) _r.freq = Freq::WEEKLY;
    const int32_t start = data::to_day(form.date);
    _mask = _r.weekdays ? _r.weekdays : uint8_t(1u << planner::weekday(start));
    _until = _r.end == EndKind::UNTIL ? data::to_date(_r.until) : add_months(form.date, 1);
    if(_r.end != EndKind::COUNT) _r.count = 10;
    // Monthly choices from the start date (+ the stored one when different).
    const Date d = form.date;
    const int wd = weekday(d);
    _monthly_count = 0;
    Rule m;
    m.freq = Freq::MONTHLY;
    _monthly[_monthly_count++] = m;
    if((d.day - 1) / 7 + 1 <= 4) {
        m.mmode = MonthlyMode::NTH_WEEKDAY;
        m.nth = uint8_t((d.day - 1) / 7 + 1);
        m.mweekday = uint8_t(wd);
        _monthly[_monthly_count++] = m;
    }
    if(d.day + 7 > days_in_month(d.year, d.month)) {
        Rule l;
        l.freq = Freq::MONTHLY;
        l.mmode = MonthlyMode::LAST_WEEKDAY;
        l.mweekday = uint8_t(wd);
        _monthly[_monthly_count++] = l;
    }
    _monthly_sel = 0;
    if(_r.freq == Freq::MONTHLY && _r.mmode != MonthlyMode::START_DAY) {
        bool found = false;
        for(int i = 0; i < _monthly_count; ++i)
            if(_monthly[i].mmode == _r.mmode && _monthly[i].nth == _r.nth && _monthly[i].mweekday == _r.mweekday &&
               (_r.mmode != MonthlyMode::DAY_N || _monthly[i].mday == _r.mday)) { _monthly_sel = i; found = true; }
        if(!found && _monthly_count < 4) {
            Rule o;
            o.freq = Freq::MONTHLY;
            o.mmode = _r.mmode; o.mday = _r.mday; o.nth = _r.nth; o.mweekday = _r.mweekday;
            _monthly_sel = _monthly_count;
            _monthly[_monthly_count++] = o;
        }
    }
    _sel = 0;
    _day_cursor = 0;
    _part = 0;
    build();
    screens::push(*this);
}

void RepeatScreen::build()
{
    _count = 0;
    _rows[_count++] = EVERY;
    _rows[_count++] = UNIT;
    if(_r.freq == Freq::WEEKLY) _rows[_count++] = ON_DAYS;
    if(_r.freq == Freq::MONTHLY) _rows[_count++] = MONTHLY;
    _rows[_count++] = ENDS;
    if(_r.end == EndKind::UNTIL) _rows[_count++] = UNTIL;
    if(_r.end == EndKind::COUNT) _rows[_count++] = COUNT;
    _rows[_count++] = OK;
    if(_sel >= _count) _sel = _count - 1;
}

void RepeatScreen::change(int row, int dir)
{
    switch(row) {
    case EVERY: {
        int v = _r.interval + dir;
        if(v < 1) v = 1;
        if(v > planner::MAX_INTERVAL) v = planner::MAX_INTERVAL;
        _r.interval = uint16_t(v);
        break;
    }
    case UNIT: _r.freq = Freq((int(_r.freq) - 1 + dir + 4) % 4 + 1); build(); break;
    case ON_DAYS: _day_cursor = (_day_cursor + dir + 7) % 7; break;
    case MONTHLY: _monthly_sel = (_monthly_sel + dir + _monthly_count) % _monthly_count; break;
    case ENDS: _r.end = EndKind((int(_r.end) + dir + 3) % 3); build(); break;
    case UNTIL: _until = step_date(_until, _part, dir); break;
    case COUNT: {
        int v = _r.count + dir;
        if(v < 1) v = 1;
        if(v > planner::MAX_COUNT) v = planner::MAX_COUNT;
        _r.count = uint16_t(v);
        break;
    }
    default: return;
    }
    _marquee.reset();
    redraw();
}

void RepeatScreen::update(const Input& in)
{
    using namespace keys;
    if(in.hit(B)) { screens::pop(); return; }
    if(in.hit(UP) && _sel > 0) { --_sel; _marquee.reset(); redraw(); }
    if(in.hit(DOWN) && _sel + 1 < _count) { ++_sel; _marquee.reset(); redraw(); }
    const int row = _rows[_sel];
    const int n = _repeat.steps(in, LEFT | RIGHT);
    for(int k = 0; k < n; ++k) change(row, _repeat.dir());
    // L / R on the end date: the previous / next date part.
    if(row == UNTIL && (in.pressed & (L | R))) { _part = next_part(_part, in.hit(L) ? -1 : 1); redraw(); }
    if(in.hit(A)) {
        if(row == ON_DAYS) {
            const int wd = sunday_first() ? (_day_cursor + 6) % 7 : _day_cursor;
            const uint8_t next = uint8_t(_mask ^ (1u << wd));
            if(next) _mask = next;   // at least one day
            redraw();
        }
        else if(row == UNTIL) { _part = (_part + 1) % 3; redraw(); }
        else if(row == OK) {
            Rule r;
            r.freq = _r.freq;
            r.interval = _r.interval;
            if(r.freq == Freq::WEEKLY) {
                const uint8_t own = uint8_t(1u << weekday(form.date));
                r.weekdays = _mask == own ? 0 : _mask;
            }
            if(r.freq == Freq::MONTHLY) {
                const Rule& m = _monthly[_monthly_sel];
                r.mmode = m.mmode; r.mday = m.mday; r.nth = m.nth; r.mweekday = m.mweekday;
            }
            r.end = _r.end;
            if(r.end == EndKind::UNTIL) r.until = data::to_day(_until);
            if(r.end == EndKind::COUNT) r.count = _r.count;
            form.rule = r;
            form.custom = !is_preset(r);
            screens::pop();
            return;
        }
    }
    ui::Text t;
    label(row, t);
    if(_marquee.step(Canvas::width16(t.buf), ui::TEXT_RIGHT - ui::TEXT_X)) redraw();
}

void RepeatScreen::label(int row, ui::Text& t) const
{
    static const char* const units[5] = {"", "days", "weeks", "months", "years"};
    switch(row) {
    case EVERY: t << "Repeats every: "; t.num(_r.interval); break;
    case UNIT: t << "Unit: " << units[int(_r.freq)]; break;
    case ON_DAYS: t << "Repeats on:"; break;
    case MONTHLY: {
        const Rule& m = _monthly[_monthly_sel];
        if(m.mmode == MonthlyMode::START_DAY) { t << "Monthly: on day "; t.num(form.date.day); }
        else if(m.mmode == MonthlyMode::DAY_N) { t << "Monthly: on day "; t.num(m.mday); }
        else if(m.mmode == MonthlyMode::NTH_WEEKDAY) t << "Monthly: on the " << ordinals[m.nth] << ' ' << weekday_name(m.mweekday);
        else t << "Monthly: on the last " << weekday_name(m.mweekday);
        break;
    }
    case ENDS:
        t << "Ends: " << (_r.end == EndKind::NEVER ? "Never" : _r.end == EndKind::UNTIL ? "On date" : "After X occurrences");
        break;
    case UNTIL: t << "End date: "; date_text(_until, t); break;
    case COUNT: t << "Occurrences: "; t.num(_r.count); break;
    case OK: t << "OK"; break;
    default: break;
    }
}

void RepeatScreen::draw(Canvas& c)
{
    c.clear();
    ui::title(c, "Custom repeat");
    for(int i = 0; i < _count; ++i) {
        const int row = _rows[i];
        ui::Text t;
        label(row, t);
        const bool sel = i == _sel;
        ui::row(c, i, t.buf, sel, int(_marquee.off));
        const int y = ui::ROW_Y + i * ui::ROW_H;
        if(row == ON_DAYS) {
            // S M T W T F S (or M .. S): filled = repeats on that day.
            for(int k = 0; k < 7; ++k) {
                const int wd = sunday_first() ? (k + 6) % 7 : k;
                const int x = 102 + k * 18;
                const bool on = _mask & (1u << wd);
                if(on) c.fill(x - 2, y + 1, 13, 14, color::category(state::settings().event_color));
                c.text16(x, y, weekday_letters_mon[wd], on ? color::WHITE : color::BLACK);
                if(sel && k == _day_cursor) c.fill(x - 2, y + 15, 13, 1, color::BLACK);
            }
        }
        if(row == UNTIL && sel) mark_part(c, i, ui::TEXT_X + Canvas::width16("End date: "), _part);
    }
    const int hint_row = _rows[_sel];
    const char* hint = hint_row == ON_DAYS ? "Left/Right: day   A: on / off"
                     : hint_row == UNTIL   ? "L/R/A: date part  Left/Right: change"
                     : hint_row == OK      ? "A: use this repeat   B: back"
                                           : "Left/Right: change   B: back";
    c.hline(0, SCREEN_H - 16, SCREEN_W, color::LIGHT);
    c.small(ui::TEXT_X, SCREEN_H - 17, hint, color::GREY);
}

// ---- The form ----
class FormScreen final : public Screen {
public:
    void open_new(Category cat, const Date& day);
    bool open_edit(planner::EntryId id, const Date& day);
    void enter() override;
    void update(const Input& in) override;
    void draw(Canvas& c) override;

private:
    enum Row : uint8_t { CATEGORY, TITLE, NOTES, DATE, ALLDAY, START, END, TIME, REPEAT, SAVE, DONE, DELETE, DELETE_DAY };
    int _rows[13], _count = 0, _sel = 0, _top = 0, _part = 0;
    int _confirm = -1;
    int _repeat_choice = 0;   // 0..4 presets, 5 custom
    ui::Repeat _repeat;
    ui::Marquee _marquee;
    int _marquee_sel = -1;
    const char* _note = nullptr;
    int _note_frames = 0;
    void build();
    void change(int row, int dir);
    void activate(int row);
    void label(int row, ui::Text& t, ui::Font& font) const;
    void save();
    void message(const char* m) { _note = m; _note_frames = 150; redraw(); }
    void sync_choice();
};
FormScreen form_screen;

void FormScreen::sync_choice()
{
    if(form.custom || !is_preset(form.rule)) { _repeat_choice = 5; form.custom = true; }
    else _repeat_choice = int(form.rule.freq);
}

void default_times()
{
    const Settings& s = state::settings();
    form.start = s.day_start_hour * 60;
    const int len = s.event_length == EventLength::MIN30 ? 30 : 60;
    form.end = form.start + len;
    if(form.end > 23 * 60 + 55) { form.start = 23 * 60 + 55 - len; form.end = 23 * 60 + 55; }
}

void FormScreen::open_new(Category cat, const Date& day)
{
    form = Form();
    form_title[0] = 0;
    form_notes[0] = 0;
    form.cat = cat;
    form.date = day;
    form.timed = cat == Category::EVENT;
    default_times();
    if(cat == Category::BIRTHDAY || cat == Category::HOLIDAY) form.rule = preset(int(Freq::YEARLY));
    _sel = 1;   // Title
    _top = 0;
    _confirm = -1;
    _note = nullptr;
    sync_choice();
    build();
}

bool FormScreen::open_edit(planner::EntryId id, const Date& day)
{
    planner::EntryInfo info;
    if(planner::cal_get(id, &info) != planner::Err::OK) return false;
    form = Form();
    form_title[0] = 0;
    form_notes[0] = 0;
    form.editing = true;
    form.id = id;
    form.occurrence = day;
    form.cat = info.cat;
    form.date = data::to_date(info.day);
    form.timed = info.timed;
    form.start = info.start;
    form.end = info.end;
    if(!form.timed) default_times();
    else if(info.cat == Category::TASK) form.end = form.start + 30;
    form.rule = info.rule;
    form.repeats_stored = info.rule.freq != Freq::NONE;
    form.done = planner::cal_is_done(id, data::to_day(day));
    if(planner::cal_title(id, form_title, sizeof(form_title)) != planner::Err::OK) form_title[0] = 0;
    if(state::settings().notes) {
        form.notes_loaded = planner::cal_notes(id, form_notes, sizeof(form_notes)) == planner::Err::OK;
        if(!form.notes_loaded) form_notes[0] = 0;
    }
    _sel = 1;
    _top = 0;
    _confirm = -1;
    _note = nullptr;
    sync_choice();
    build();
    return true;
}

void FormScreen::build()
{
    _count = 0;
    _rows[_count++] = CATEGORY;
    _rows[_count++] = TITLE;
    if(state::settings().notes) _rows[_count++] = NOTES;
    _rows[_count++] = DATE;
    const bool timed_cat = form.cat == Category::EVENT || form.cat == Category::TASK;
    if(timed_cat) {
        _rows[_count++] = ALLDAY;
        if(form.timed) {
            if(form.cat == Category::EVENT) { _rows[_count++] = START; _rows[_count++] = END; }
            else _rows[_count++] = TIME;
        }
    }
    _rows[_count++] = REPEAT;
    _rows[_count++] = SAVE;
    if(form.editing && form.cat == Category::TASK) _rows[_count++] = DONE;
    if(form.editing) _rows[_count++] = DELETE;
    if(form.editing && form.repeats_stored) _rows[_count++] = DELETE_DAY;
    if(_sel >= _count) _sel = _count - 1;
}

void FormScreen::enter()
{
    sync_choice();
    build();
    _marquee.reset();
    redraw();
}

int clamp_time(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void FormScreen::change(int row, int dir)
{
    constexpr int LAST = 23 * 60 + 55;
    switch(row) {
    case CATEGORY: {
        const Category old = form.cat;
        form.cat = ORDER[(order_of(form.cat) + dir + 4) % 4];
        // Defaults of the new category while the repeat is still a default.
        const bool default_rule = !form.custom && (form.rule.freq == Freq::NONE ||
                                                   (form.rule.freq == Freq::YEARLY && (old == Category::BIRTHDAY || old == Category::HOLIDAY)));
        if(default_rule) form.rule = preset(int(form.cat == Category::BIRTHDAY || form.cat == Category::HOLIDAY ? Freq::YEARLY : Freq::NONE));
        if(form.cat == Category::EVENT && old != Category::EVENT && !form.editing) form.timed = true;
        if(form.cat == Category::TASK && form.end <= form.start) form.end = form.start + 30;
        sync_choice();
        build();
        break;
    }
    case DATE: form.date = step_date(form.date, _part, dir); break;
    case ALLDAY:
        form.timed = !form.timed;
        if(form.timed && form.end <= form.start) default_times();
        build();
        break;
    case START: {
        const int len = form.end - form.start;
        form.start = clamp_time(form.start + 5 * dir, 0, LAST - 5);
        form.end = clamp_time(form.start + (len > 0 ? len : 5), form.start + 5, LAST);
        break;
    }
    case END: form.end = clamp_time(form.end + 5 * dir, form.start + 5, LAST); break;
    case TIME: form.start = clamp_time(form.start + 5 * dir, 0, LAST); break;
    case REPEAT:
        _repeat_choice = (_repeat_choice + dir + 6) % 6;
        if(_repeat_choice < 5) { form.rule = preset(_repeat_choice); form.custom = false; }
        else form.custom = true;
        break;
    default: return;
    }
    _confirm = -1;
    _marquee.reset();
    redraw();
}

const char* title_done(void*, const char* text)
{
    std::strncpy(form_title, text, sizeof(form_title) - 1);
    form_title[sizeof(form_title) - 1] = 0;
    return nullptr;
}

const char* notes_done(void*, const char* text)
{
    std::strncpy(form_notes, text, sizeof(form_notes) - 1);
    form_notes[sizeof(form_notes) - 1] = 0;
    form.notes_loaded = true;
    form.notes_changed = true;
    return nullptr;
}

void FormScreen::activate(int row)
{
    switch(row) {
    case TITLE: {
        TextEditRequest r;
        r.heading = "Title";
        r.max_chars = planner::MAX_TITLE_CHARS;
        r.done = title_done;
        open_text_edit(r, form_title);
        return;
    }
    case NOTES: {
        TextEditRequest r;
        r.heading = "Notes";
        r.max_chars = planner::MAX_NOTES_CHARS;
        r.multiline = true;
        r.allow_empty = true;
        r.done = notes_done;
        open_text_edit(r, form_notes);
        return;
    }
    case DATE: _part = (_part + 1) % 3; redraw(); return;
    case REPEAT:
        if(_repeat_choice == 5) repeat_screen.open();
        else change(row, 1);
        return;
    case SAVE: save(); return;
    case DONE: {
        planner::EntryId nid;
        const planner::Err e = planner::cal_set_done(form.id, data::to_day(form.occurrence), !form.done, &nid);
        if(data::cal_result(e)) screens::pop();
        else message(data::error_text(e));
        return;
    }
    case DELETE:
    case DELETE_DAY: {
        if(_confirm != row) { _confirm = row; redraw(); return; }
        const planner::Err e = row == DELETE ? planner::cal_delete(form.id)
                                             : planner::cal_skip_day(form.id, data::to_day(form.occurrence), nullptr);
        if(data::cal_result(e)) screens::pop();
        else { _confirm = -1; message(data::error_text(e)); }
        return;
    }
    default: change(row, 1); return;
    }
}

void FormScreen::save()
{
    bool blank = true;
    for(const char* p = form_title; *p; ++p) if(*p != ' ') blank = false;
    if(blank) { message("Type a title first (A on Title)"); return; }
    planner::EntryInput in;
    in.cat = form.cat;
    in.day = data::to_day(form.date);
    const bool timed_cat = form.cat == Category::EVENT || form.cat == Category::TASK;
    in.timed = timed_cat && form.timed;
    in.start = int16_t(in.timed ? form.start : 0);
    in.end = int16_t(in.timed && form.cat == Category::EVENT ? form.end : 0);
    in.ticked = form.cat == Category::TASK && form.rule.freq == Freq::NONE && form.done;
    in.rule = form.rule;
    if(in.rule.end == EndKind::UNTIL && in.rule.until < in.day) { message("The end date is before the date"); return; }
    in.title = form_title;
    in.notes = form.notes_loaded && (form.notes_changed || !form.editing) ? form_notes : nullptr;
    if(form.editing && !form.notes_changed) in.notes = nullptr;
    const planner::Err v = planner::cal_validate(in);
    if(v != planner::Err::OK) { message(data::error_text(v)); return; }
    const planner::Err e = form.editing ? planner::cal_update(form.id, in, nullptr) : planner::cal_add(in, nullptr);
    if(data::cal_result(e)) screens::pop();
    else message(data::error_text(e));
}

void FormScreen::label(int row, ui::Text& t, ui::Font& font) const
{
    font = ui::Font::UNSCII;
    char tm[12];
    switch(row) {
    case CATEGORY: t << data::category_label(form.cat); break;
    case TITLE: font = ui::Font::SUPERFW; t << form_title; break;
    case NOTES: {
        font = ui::Font::SUPERFW;
        int i = 0;
        while(form_notes[i] && form_notes[i] != '\n' && i < 120) t << form_notes[i++];
        if(!form.notes_loaded && form.editing) t << "";
        break;
    }
    case DATE: date_text(form.date, t); break;
    case ALLDAY: t << (form.timed ? "Off" : "On"); break;
    case START: data::format_time(form.start, tm); t << tm; break;
    case END: data::format_time(form.end, tm); t << tm; break;
    case TIME: data::format_time(form.start, tm); t << tm; break;
    case REPEAT:
        if(_repeat_choice == 5 && is_preset(form.rule)) t << "Custom\xe2\x80\xa6";
        else rule_text(form.rule, 0, t);
        break;
    default: break;
    }
}

const char* row_label(int row, bool birthday)
{
    switch(row) {
    case 0: return "Category: ";
    case 1: return "Title: ";
    case 2: return "Notes: ";
    case 3: return birthday ? "Birth date: " : "Date: ";
    case 4: return "All day: ";
    case 5: return "Start: ";
    case 6: return "End: ";
    case 7: return "Time: ";
    case 8: return "Repeat: ";
    default: return "";
    }
}

void FormScreen::update(const Input& in)
{
    using namespace keys;
    if(in.hit(B)) { screens::pop(); return; }
    bool moved = false;
    if(in.hit(UP) && _sel > 0) { --_sel; moved = true; }
    if(in.hit(DOWN) && _sel + 1 < _count) { ++_sel; moved = true; }
    if(moved) { _confirm = -1; _marquee.reset(); redraw(); }
    if(_sel < _top) _top = _sel;
    if(_sel >= _top + 8) _top = _sel - 7;
    const int row = _rows[_sel];
    const int n = _repeat.steps(in, LEFT | RIGHT);
    for(int k = 0; k < n; ++k) change(row, _repeat.dir());
    // L / R on the date: the previous / next date part.
    if(row == DATE && (in.pressed & (L | R))) { _part = next_part(_part, in.hit(L) ? -1 : 1); redraw(); }
    if(in.hit(A)) { activate(row); if(screens::top() != this) return; }
    if(_note_frames && !--_note_frames) { _note = nullptr; redraw(); }
    if(row <= REPEAT) {
        ui::Text t;
        ui::Font f;
        label(row, t, f);
        if(_marquee.step(ui::text_width(t.buf, f), ui::field_avail(row_label(row, form.cat == Category::BIRTHDAY)))) redraw();
    }
}

void FormScreen::draw(Canvas& c)
{
    c.clear();
    ui::Text head;
    head << (form.editing ? "Edit " : "New ") << data::category_label(form.cat);
    ui::title(c, head.buf);
    for(int r = 0; r < 8 && _top + r < _count; ++r) {
        const int i = _top + r;
        const int row = _rows[i];
        const bool sel = i == _sel;
        if(row >= SAVE) {
            const char* text = row == SAVE ? "Save"
                             : row == DONE ? (form.done ? "Mark as not done" : "Mark as done")
                             : row == DELETE ? (_confirm == DELETE ? "Delete: Sure?" : "Delete")
                                             : (_confirm == DELETE_DAY ? "Delete only this day: Sure?" : "Delete only this day");
            ui::row(c, r, text, sel);
            continue;
        }
        ui::Text t;
        ui::Font f;
        label(row, t, f);
        const uint8_t ink = row == CATEGORY ? data::category_color(form.cat) : color::BLACK;
        const int x = ui::field(c, r, row_label(row, form.cat == Category::BIRTHDAY), t.buf, sel, int(_marquee.off), f, ink);
        if(row == DATE && sel) mark_part(c, r, x, _part);
        if(row == REPEAT) ui::underline(c, r);
    }
    const int row = _rows[_sel];
    const char* hint = row == TITLE || row == NOTES ? "A: type"
                     : row == DATE ? "L/R/A: date part  Left/Right: change"
                     : row == REPEAT ? (_repeat_choice == 5 ? "A: set the custom repeat" : "Left/Right: change")
                     : row >= SAVE ? "A: do it   B: back without saving"
                                   : "Left/Right: change (hold: fast)";
    c.hline(0, SCREEN_H - 16, SCREEN_W, color::LIGHT);
    c.small(ui::TEXT_X, SCREEN_H - 17, hint, color::GREY);
    if(_note) ui::note(c, _note);
}

// ---- The category question ----
class CategoryScreen final : public Screen {
public:
    Date day{};
    int sel = 0;
    void enter() override { redraw(); }
    void update(const Input& in) override
    {
        using namespace keys;
        if(in.hit(B)) { screens::pop(); return; }
        if(in.hit(UP)) { sel = (sel + 3) % 4; redraw(); }
        if(in.hit(DOWN)) { sel = (sel + 1) % 4; redraw(); }
        if(in.hit(A)) {
            form_screen.open_new(ORDER[sel], day);
            screens::replace(form_screen);
        }
    }
    void draw(Canvas& c) override
    {
        c.clear();
        char date[16];
        ui::format_date(day, state::settings().date_format, date);
        ui::Text t;
        t << "New entry \xc2\xb7 " << date;
        ui::title(c, t.buf);
        for(int i = 0; i < 4; ++i) {
            ui::row(c, i, data::category_label(ORDER[i]), i == sel);
            c.fill(ui::TEXT_RIGHT - 11, ui::ROW_Y + i * ui::ROW_H + 3, 11, 10, data::category_color(ORDER[i]));
        }
    }
};
CategoryScreen category_screen;

}

Screen& new_entry_screen(const Date& day)
{
    category_screen.day = day;
    category_screen.sel = 0;
    return category_screen;
}

void open_entry(planner::EntryId id, const Date& day)
{
    if(form_screen.open_edit(id, day)) screens::push(form_screen);
}

}
