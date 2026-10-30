// Settings (Select on the calendar): DESIGN.md "Settings". A top page and
// sub-pages that each fit on the screen (9 rows); a row with ▸ opens a page,
// an option page lists every value with a tick on the current one (A picks
// it). Only the Holidays list (35 choices) scrolls. Then Controls and
// Credits (with Secret Settings), gbawriter style.
#include "app/app_state.h"
#include "app/data.h"
#include "app/help.h"
#include "app/question.h"
#include "app/screens.h"
#include "app/ui.h"

#include <cstring>
#include "holidays/holidays.h"

namespace app {

namespace {

// Pages: the top page, the sub-pages, and the option page of one setting.
enum Page : uint8_t { TOP, GENERAL, COLORS, DAY_VIEW_PAGE, TODO_PAGE, REPEATING, STORAGE, HELP, OPTIONS };

// Rows. OPEN_* open a sub-page; the settings with an option page are
// START_SCREEN, HOLIDAYS, the colors, DAY_VIEW, EVENT_LENGTH, TODO_SHOWS, TASK_WINDOW,
// TICKED_SHOWN, OLD_TICKED, FEB29, SHORT_MONTHS; the others change with Left / Right / A.
enum Row : uint8_t {
    OPEN_GENERAL, HOLIDAYS, OPEN_COLORS, OPEN_DAY_VIEW, OPEN_TODO, OPEN_REPEATING, NOTES, OPEN_STORAGE, OPEN_HELP,
    START_SCREEN, DATE_FORMAT, WEEK_START, TIME_FORMAT,
    EVENT_COLOR, TASK_COLOR, BIRTHDAY_COLOR, HOLIDAY_COLOR,
    DAY_VIEW, DAY_START, EVENT_LENGTH,
    TODO_SHOWS, TASK_WINDOW, TICKED_SHOWN, OLD_TICKED, DELETE_HIDDEN,
    FEB29, SHORT_MONTHS,
    CALENDAR_SAVED, TODO_SAVED,
    TOPIC   // TOPIC + n: Controls topics and Credits
};
constexpr int MAX_ROWS = COUNTRY_COUNT + 1;   // the Holidays list is the longest page
constexpr int DEPTH = 3;                       // top > sub-page > option page
// Left / Right held: a value every VALUE_REPEAT frames after VALUE_DELAY.
constexpr int VALUE_DELAY = 20, VALUE_REPEAT = 6;
// Changed settings are saved after this many frames without a change, and
// when Settings is left.
constexpr int SAVE_IDLE = 60;
// Option rows: the tick, then the text (a color option: its swatch first).
constexpr int TICK_X = ui::TEXT_X, OPTION_X = 22, SWATCH_W = 11, ARROW_X = ui::TEXT_RIGHT - 8;
constexpr int NOTE_LINE_H = 10;

const char* const date_formats[] = {"DD.MM.YYYY", "MM/DD/YYYY", "YYYY-MM-DD"};
const char* const todo_shows[] = {"To-dos only", "To-dos and upcoming tasks", "To-dos and all tasks", "Tasks only"};
const char* const task_windows[] = {"Next open day only", "Open days in the next 7 days",
                                    "Open days in the next 30 days"};
const char* const task_windows_short[] = {"Next open day only", "Open days, next 7 days", "Open days, next 30 days"};

int wrap(int v, int dir, int n) { return ((v + dir) % n + n) % n; }

unsigned list_keys(unsigned held)
{
    using namespace keys;
    unsigned k = 0;
    if(held & UP) k |= list::UP;
    if(held & DOWN) k |= list::DOWN;
    if(held & LEFT) k |= list::LEFT;
    if(held & RIGHT) k |= list::RIGHT;
    if(held & A) k |= list::A;
    if(held & B) k |= list::B;
    return k;
}

using ui::Text;

bool sd_ok() { return data::calendar_ready() || data::todos_ready(); }

// ---- What each row is ----
bool color_row(int row) { return row >= EVENT_COLOR && row <= HOLIDAY_COLOR; }

// The settings that have an option page (and how many options).
int option_count(int row)
{
    switch(row) {
    case DATE_FORMAT: return 3;
    case START_SCREEN: case WEEK_START: case TIME_FORMAT: case DAY_VIEW: case EVENT_LENGTH: case OLD_TICKED: case NOTES:
    case SHORT_MONTHS: case CALENDAR_SAVED: case TODO_SAVED: return 2;
    case HOLIDAYS: return COUNTRY_COUNT + 1;
    case EVENT_COLOR: case TASK_COLOR: case BIRTHDAY_COLOR: case HOLIDAY_COLOR: return color::CATEGORY_COUNT;
    case DAY_START: return 24;
    case TODO_SHOWS: return 4;
    case TICKED_SHOWN: return planner::TODO_VISIBLE_TICKED_MAX + 1;
    case TASK_WINDOW: case FEB29: return 3;
    default: return 0;
    }
}

bool has_page(int row)
{
    return row == START_SCREEN || row == HOLIDAYS || color_row(row) || row == DAY_VIEW || row == EVENT_LENGTH || row == TODO_SHOWS ||
           row == TASK_WINDOW || row == TICKED_SHOWN || row == OLD_TICKED || row == FEB29 || row == SHORT_MONTHS;
}

// Rows with ▸: a sub-page or an option page.
bool arrow(int row) { return (row <= OPEN_HELP && row != NOTES) || has_page(row); }

uint8_t* color_of(Settings& s, int row)
{
    uint8_t* v[] = {&s.event_color, &s.task_color, &s.birthday_color, &s.holiday_color};
    return v[row - EVENT_COLOR];
}

int current(int row)
{
    Settings& s = state::settings();
    switch(row) {
    case START_SCREEN: return int(s.start_screen);
    case DATE_FORMAT: return int(s.date_format);
    case WEEK_START: return int(s.week_start);
    case TIME_FORMAT: return int(s.time_format);
    case HOLIDAYS: return s.holidays;
    case EVENT_COLOR: case TASK_COLOR: case BIRTHDAY_COLOR: case HOLIDAY_COLOR: return *color_of(s, row);
    case DAY_VIEW: return int(s.day_view);
    case DAY_START: return s.day_start_hour;
    case EVENT_LENGTH: return int(s.event_length);
    case TODO_SHOWS: return int(s.todo_shows);
    case TASK_WINDOW: return int(s.task_window);
    case TICKED_SHOWN: return s.ticked_shown;
    case OLD_TICKED: return s.old_ticked_effective() == OldTicked::DELETE ? 0 : 1;
    case NOTES: return s.notes ? 1 : 0;
    case FEB29: return int(s.feb29);
    case SHORT_MONTHS: return int(s.short_months);
    case CALENDAR_SAVED: return int(s.calendar_saved_as);
    case TODO_SAVED: return int(s.todo_saved_as);
    default: return 0;
    }
}

// Ticked to-dos shown: the core shows that many; fewer with "Old ticked
// to-dos: Delete" deletes the extra ones now, as ticks would. Returns an
// error text or nullptr.
const char* apply_ticked_shown()
{
    const Settings& s = state::settings();
    planner::todo_set_visible_ticked(s.ticked_shown);
    if(!data::todos_ready()) return nullptr;
    const planner::Err e = planner::todo_apply_visible(s.old_ticked_effective() == OldTicked::DELETE);
    return data::todo_result(e) ? nullptr : data::error_text(e);
}

// Sets a value (not the storage rows: they convert first). Returns an error
// text or nullptr.
const char* set_value(int row, int v)
{
    Settings& s = state::settings();
    switch(row) {
    case START_SCREEN: s.start_screen = StartScreen(v); break;
    case DATE_FORMAT: s.date_format = DateFormat(v); break;
    case WEEK_START: s.week_start = WeekStart(v); break;
    case TIME_FORMAT: s.time_format = TimeFormat(v); break;
    case HOLIDAYS: s.holidays = uint8_t(v); break;
    case EVENT_COLOR: case TASK_COLOR: case BIRTHDAY_COLOR: case HOLIDAY_COLOR: *color_of(s, row) = uint8_t(v); break;
    case DAY_VIEW: s.day_view = DayView(v); break;
    case DAY_START: s.day_start_hour = uint8_t(v); break;
    case EVENT_LENGTH: s.event_length = EventLength(v); break;
    case TODO_SHOWS: s.todo_shows = TodoShows(v); break;
    case TASK_WINDOW: s.task_window = TaskWindow(v); break;
    case TICKED_SHOWN: s.ticked_shown = uint8_t(v); return apply_ticked_shown();
    // Picking one turns the default (AUTO) into a choice.
    case OLD_TICKED: s.old_ticked = v ? OldTicked::KEEP_HIDDEN : OldTicked::DELETE; break;
    case NOTES: s.notes = v != 0; break;
    // The repeat options change every screen's days at once.
    case FEB29: s.feb29 = Feb29Shown(v); data::apply_repeat_options(); break;
    case SHORT_MONTHS: s.short_months = ShortMonths(v); data::apply_repeat_options(); break;
    default: break;
    }
    return nullptr;
}

// The text of option v. summary: the short form shown on the setting's row.
void option_text(int row, int v, Text& t, bool summary = false)
{
    const Settings& s = state::settings();
    switch(row) {
    case START_SCREEN: t << (v ? "To-do list" : "Calendar"); break;
    case DATE_FORMAT: t << date_formats[v]; break;
    case WEEK_START: t << (v ? "Sunday" : "Monday"); break;
    case TIME_FORMAT: t << (v ? "12 h" : "24 h"); break;
    case HOLIDAYS: t << holidays_country_name(v); break;
    case EVENT_COLOR: case TASK_COLOR: case BIRTHDAY_COLOR: case HOLIDAY_COLOR: t << category_name(v); break;
    case DAY_VIEW: t << (v ? "List" : "Timeline"); break;
    case DAY_START: {
        char h[12];
        ui::format_hour(v, s.time_format, h);
        t << h;
        break;
    }
    case EVENT_LENGTH: t << (v ? "1 h" : "30 min"); break;
    case TODO_SHOWS: t << todo_shows[v]; break;
    case TASK_WINDOW: t << (summary ? task_windows_short : task_windows)[v]; break;
    case TICKED_SHOWN: t.num(v); break;
    case OLD_TICKED: t << (v ? "Keep hidden" : "Delete"); break;
    case NOTES: t << (v ? "On" : "Off"); break;
    case FEB29: t << (v == 0 ? "Skip" : v == 1 ? "28 Feb" : "1 Mar"); break;
    case SHORT_MONTHS: t << (v ? "Last day of the month" : "Skip"); break;
    case CALENDAR_SAVED: case TODO_SAVED: t << (v ? "In-app" : "Text file"); break;
    default: break;
    }
}

const char* row_name(int row)
{
    switch(row) {
    case OPEN_GENERAL: return "General";
    case START_SCREEN: return "Start screen";
    case HOLIDAYS: return "Holidays";
    case OPEN_COLORS: return "Colors";
    case OPEN_DAY_VIEW: case DAY_VIEW: return "Day view";
    case OPEN_TODO: return "To-do list";
    case OPEN_REPEATING: return "Repeating dates";
    case NOTES: return "Notes";
    case OPEN_STORAGE: return "Storage";
    case OPEN_HELP: return "Controls and Credits";
    case DATE_FORMAT: return "Date format";
    case WEEK_START: return "Week starts on";
    case TIME_FORMAT: return "Time format";
    case EVENT_COLOR: return "Event color";
    case TASK_COLOR: return "Task color";
    case BIRTHDAY_COLOR: return "Birthday color";
    case HOLIDAY_COLOR: return "Holiday color";
    case DAY_START: return "Day view starts at";
    case EVENT_LENGTH: return "Default event length";
    case TODO_SHOWS: return "To-do list shows";
    // The page is "To-do list": the short name fits one row.
    case TASK_WINDOW: return "Repeating tasks";
    case TICKED_SHOWN: return "Ticked to-dos shown";
    case OLD_TICKED: return "Old ticked to-dos";
    case DELETE_HIDDEN: return "Delete hidden to-dos";
    case FEB29: return "Feb 29 in other years";
    case SHORT_MONTHS: return "Day 29-31 in short months";
    case CALENDAR_SAVED: return "Calendar saved as";
    case TODO_SAVED: return "To-do list saved as";
    default: return help::topic_name(row - TOPIC);
    }
}

// The option page's title (the full name of the setting).
const char* page_title(int row) { return row == TASK_WINDOW ? "Repeating tasks in to-do list" : row_name(row); }

// A short explanation under the options (small grey text, wrapped).
const char* option_note(int row)
{
    switch(row) {
    case START_SCREEN: return "The screen shown after start-up.";
    case EVENT_LENGTH: return "The length of a new event.";
    case TODO_SHOWS: return "Upcoming: open calendar tasks from the calendar cursor's day on. All tasks: also the "
                            "overdue ones.";
    case TASK_WINDOW: return "With 7 or 30 days, every open day of a repeating task in that window is its own "
                             "row.";
    case TICKED_SHOWN: return "The most recently ticked to-dos stay in the list, grey. With 0 a ticked to-do "
                              "leaves the list at once.";
    case OLD_TICKED: return "Ticked to-dos beyond the ones shown. Until you choose: Delete with in-app "
                            "storage, Keep hidden with a text file.";
    case FEB29: return "Where entries repeating yearly from a Feb 29 (also birthdays) appear in other years.";
    case SHORT_MONTHS: return "Monthly on day 29-31. Short months: February, April, June, September, November. A "
                              "missing 5th weekday becomes the last one.";
    default: return nullptr;
    }
}

const char* page_note(int page)
{
    return page == STORAGE ? "Changing it converts all data." : nullptr;
}

// Small grey text wrapped at spaces into lines from y; returns the lines.
int small_wrapped(Canvas* c, int y, const char* text)
{
    constexpr int CHARS = (ui::TEXT_RIGHT - ui::TEXT_X) / 6;
    int lines = 0;
    const char* p = text;
    while(*p) {
        int n = 0, cut = 0;
        while(p[n] && n <= CHARS) { if(p[n] == ' ') cut = n; ++n; }
        if(!p[n] && n <= CHARS) cut = n;
        else if(!cut) cut = CHARS;
        if(c) {
            char row[CHARS + 2];
            for(int k = 0; k < cut; ++k) row[k] = p[k];
            row[cut] = 0;
            c->small(ui::TEXT_X, y + lines * NOTE_LINE_H, row, color::GREY);
        }
        ++lines;
        p += cut;
        while(*p == ' ') ++p;
    }
    return lines;
}

// An option's text in one or two lines of `avail` pixels (UNSCII, 8 px a
// character): the break goes before " (" when there is one, else at the
// space that gives the most even lines.
int split(const char* text, int avail, char (&a)[40], char (&b)[40])
{
    const int chars = avail / 8;
    const int len = int(std::strlen(text));
    int cut = len;
    if(len > chars) {
        // The space that makes the longer line shortest (both must fit).
        cut = 0;
        int best = len;
        for(int i = 1; i < len && i <= chars; ++i) {
            const int longer = i > len - i - 1 ? i : len - i - 1;
            if(text[i] == ' ' && longer <= chars && longer < best) { best = longer; cut = i; }
        }
        for(int i = 1; i + 1 < len && i <= chars; ++i) if(text[i] == ' ' && text[i + 1] == '(') cut = i;
        if(!cut) cut = chars < 39 ? chars : 39;
    }
    if(cut > 39) cut = 39;
    std::memcpy(a, text, size_t(cut));
    a[cut] = 0;
    const char* rest = text + cut;
    while(*rest == ' ') ++rest;
    int n = int(std::strlen(rest));
    if(n > 39) n = 39;
    std::memcpy(b, rest, size_t(n));
    b[n] = 0;
    return b[0] ? 2 : 1;
}

// ---- Controls and Credits pages ----
class PagesScreen final : public Screen {
public:
    int topic = 0, page = 0;
    void update(const Input& in) override;
    void draw(Canvas& c) override;
};

// ---- Secret Settings ----
class SecretScreen final : public Screen {
public:
    void enter() override { _confirm = false; _note = nullptr; }
    void update(const Input& in) override
    {
        if(in.hit(keys::B)) { screens::pop(); return; }
        if(in.hit(keys::A)) {
            if(!_confirm) _confirm = true;
            else {
                _confirm = false;
                _note = state::delete_configuration() ? "Configuration deleted" : "SD card error";
                apply_ticked_shown();   // back to the default number
            }
            redraw();
        }
    }
    void draw(Canvas& c) override
    {
        c.clear();
        ui::title(c, "Secret Settings");
        ui::row(c, 0, _confirm ? "Delete configuration: Sure?" : "Delete configuration", true);
        c.small(ui::TEXT_X + 2, ui::ROW_Y + 2 * ui::ROW_H, "All settings go back to", color::GREY);
        c.small(ui::TEXT_X + 2, ui::ROW_Y + 3 * ui::ROW_H - 6, "their defaults.", color::GREY);
        if(_note) ui::note(c, _note);
    }

private:
    bool _confirm = false;
    const char* _note = nullptr;
};

PagesScreen pages_screen;
SecretScreen secret_screen;

void PagesScreen::update(const Input& in)
{
    if(in.hit(keys::B)) { screens::pop(); return; }
    const int count = help::topic_pages(topic);
    if(in.hit(keys::LEFT) && page > 0) { --page; redraw(); }
    else if(in.hit(keys::RIGHT) && page + 1 < count) { ++page; redraw(); }
    else if(in.hit(keys::A) && help::secret_page(topic, page)) screens::push(secret_screen);
}

void PagesScreen::draw(Canvas& c)
{
    c.clear();
    const help::Page& p = help::page(topic, page);
    ui::title(c, p.title);
    char number[12], total[12];
    ui::format_int(page + 1, number);
    ui::format_int(help::topic_pages(topic), total);
    Text t;
    t << number << "/" << total;
    ui::title_right(c, t.buf);
    for(int i = 0; i < help::LINES; ++i) {
        const char* line = p.lines[i];
        if(!line) continue;   // a page with fewer lines
        const int y = ui::ROW_Y + i * ui::ROW_H;
        if(line[0] == '#') c.small(8, y, line + 1, color::GREY);
        else if(line[0]) c.body(8, y, line);
    }
}

// The rows of a page (EWRAM: Settings is open rarely).
APP_EWRAM uint8_t page_rows[MAX_ROWS];

// ---- Settings ----
class SettingsScreen final : public Screen {
public:
    void enter() override;
    void leave() override;
    void update(const Input& in) override;
    void draw(Canvas& c) override;

    // Steps of a storage change (after each question).
    void conversion_step(int step, int answer);
    void delete_hidden(bool yes);

private:
    struct Place {
        uint8_t page, option_row, sel, top;
    };
    Place _back[DEPTH];
    uint8_t _depth = 0;
    uint8_t _page = TOP;
    uint8_t _option_row = 0;   // OPTIONS: the setting
    uint8_t _count = 0, _sel = 0, _top = 0;
    bool _child = false;       // a screen of ours covers Settings (keep the page)
    bool _conv_calendar = true;
    list::Input _input;
    unsigned _lr = 0;
    int _lr_frames = 0;
    unsigned _held = 0;        // the keys of this frame
    int _idle = -1;
    const char* _note = nullptr;
    int _note_frames = 0;

    void build();
    int height(int i) const;
    int fits_from(int top) const;
    void show_sel();
    void open(uint8_t page, int option_row = 0);
    void back();
    void change(int row, int dir);
    void activate(int i);
    void start_conversion(bool calendar);
    void flash(const char* text) { _note = text; _note_frames = 150; redraw(); }
    void push_child(Screen& s) { _child = true; screens::push(s); }
    void save();
    void draw_row(Canvas& c, int i, int y, bool sel) const;
    void draw_option(Canvas& c, int v, int y, bool sel) const;
};

SettingsScreen settings;
// Texts in EWRAM (IWRAM is kept for the stack; .sbss is not cleared at start).
APP_EWRAM char note_text[48];

void SettingsScreen::build()
{
    int n = 0;
    auto add = [&](int row) { page_rows[n++] = uint8_t(row); };
    switch(_page) {
    case TOP: for(int r = OPEN_GENERAL; r <= OPEN_HELP; ++r) add(r); break;
    case GENERAL: add(START_SCREEN); add(DATE_FORMAT); add(WEEK_START); add(TIME_FORMAT); break;
    case COLORS: for(int r = EVENT_COLOR; r <= HOLIDAY_COLOR; ++r) add(r); break;
    case DAY_VIEW_PAGE: add(DAY_VIEW); add(DAY_START); add(EVENT_LENGTH); break;
    case TODO_PAGE:
        add(TODO_SHOWS); add(TASK_WINDOW); add(TICKED_SHOWN); add(OLD_TICKED);
        if(hidden_todos_exist()) add(DELETE_HIDDEN);
        break;
    case REPEATING: add(FEB29); add(SHORT_MONTHS); break;
    case STORAGE: add(CALENDAR_SAVED); add(TODO_SAVED); break;
    case HELP: for(int t = 0; t < help::TOPICS; ++t) add(TOPIC + t); break;
    default: for(int v = 0; v < option_count(_option_row); ++v) add(v); break;
    }
    _count = uint8_t(n);
    if(_sel >= _count) _sel = uint8_t(_count ? _count - 1 : 0);
}

// Rows (of 16 px) item i takes.
int SettingsScreen::height(int i) const
{
    const int row = page_rows[i];
    if(_page == OPTIONS) {
        Text t;
        option_text(_option_row, row, t);
        char a[40], b[40];
        return split(t.buf, ui::TEXT_RIGHT - OPTION_X - (color_row(_option_row) ? SWATCH_W + 5 : 0), a, b);
    }
    if(_page == TOP || option_count(row) == 0) return 1;
    // "Name: value" on one row when every value fits, else the value on a
    // second row (the row's height does not change with its value).
    int widest = 0;
    for(int v = 0; v < option_count(row); ++v) {
        Text t;
        option_text(row, v, t, true);
        const int w = Canvas::width16(t.buf);
        if(w > widest) widest = w;
    }
    const int avail = (arrow(row) ? ARROW_X - 4 : ui::TEXT_RIGHT) - ui::TEXT_X - (color_row(row) ? SWATCH_W + 4 : 0);
    return Canvas::width16(row_name(row)) + Canvas::width16(": ") + widest <= avail ? 1 : 2;
}

// Items shown from `top` (as many as fit in the rows).
int SettingsScreen::fits_from(int top) const
{
    int rows = 0, n = 0;
    for(int i = top; i < _count; ++i) {
        rows += height(i);
        if(rows > ui::ROWS) break;
        ++n;
    }
    return n;
}

void SettingsScreen::show_sel()
{
    if(_sel < _top) _top = _sel;
    while(_top < _sel && _sel >= _top + fits_from(_top)) ++_top;
}

void SettingsScreen::open(uint8_t page, int option_row)
{
    if(_depth < DEPTH) _back[_depth++] = Place{_page, _option_row, _sel, _top};
    _page = page;
    _option_row = uint8_t(option_row);
    _sel = 0;
    _top = 0;
    build();
    if(page == OPTIONS) {
        // On the current choice, as high up as the list allows.
        _sel = uint8_t(current(option_row));
        int top = _sel;
        while(top > 0 && fits_from(top - 1) == _count - (top - 1)) --top;
        _top = uint8_t(top);
        show_sel();
    }
    _input.reset(uint16_t(list_keys(_held)));
    _lr = 0;
    redraw();
}

void SettingsScreen::back()
{
    if(!_depth) { screens::pop(); return; }
    const Place p = _back[--_depth];
    _page = p.page;
    _option_row = p.option_row;
    _sel = p.sel;
    _top = p.top;
    build();
    show_sel();
    _input.reset(uint16_t(list_keys(_held)));
    _lr = 0;
    redraw();
}

void SettingsScreen::enter()
{
    if(!_child) {
        // Opened from the calendar: the top page, first row.
        _depth = 0;
        _page = TOP;
        _sel = _top = 0;
    }
    _child = false;
    build();
    show_sel();
    _input.reset(0);
    _held = 0;
    _lr = 0;
}

void SettingsScreen::leave() { save(); }

void SettingsScreen::save()
{
    if(_idle < 0) return;
    _idle = -1;
    if(!state::save()) { _note = "SD card error: not saved"; _note_frames = 120; redraw(); }
}

// Left / Right on a row that changes in place.
void SettingsScreen::change(int row, int dir)
{
    // Changing the storage converts all data first (DESIGN.md "Switching
    // storage"); the value changes only when that worked.
    if(row == CALENDAR_SAVED || row == TODO_SAVED) { start_conversion(row == CALENDAR_SAVED); return; }
    set_value(row, wrap(current(row), dir, option_count(row)));
    _idle = 0;
    redraw();
}

void SettingsScreen::activate(int i)
{
    const int row = page_rows[i];
    if(_page == OPTIONS) {
        const char* error = nullptr;
        if(row != current(_option_row) || _option_row == OLD_TICKED) { error = set_value(_option_row, row); _idle = 0; }
        back();
        if(error) flash(error);
        return;
    }
    switch(row) {
    case OPEN_GENERAL: open(GENERAL); return;
    case OPEN_COLORS: open(COLORS); return;
    case OPEN_DAY_VIEW: open(DAY_VIEW_PAGE); return;
    case OPEN_TODO: open(TODO_PAGE); return;
    case OPEN_REPEATING: open(REPEATING); return;
    case OPEN_STORAGE: open(STORAGE); return;
    case OPEN_HELP: open(HELP); return;
    case DELETE_HIDDEN: {
        Text t;
        char n[12];
        ui::format_int(planner::todo_hidden_count(), n);
        t << "Delete " << n << (planner::todo_hidden_count() == 1 ? " hidden to-do?" : " hidden to-dos?");
        std::memcpy(note_text, t.buf, sizeof(note_text) - 1);   // "Delete 300 hidden to-dos?" fits
        note_text[sizeof(note_text) - 1] = 0;
        Question q;
        q.title = "Delete hidden to-dos";
        q.lines[0] = note_text;
        q.done = [](void* self, int answer) { static_cast<SettingsScreen*>(self)->delete_hidden(answer == 0); };
        q.context = this;
        _child = true;
        ask(q);
        return;
    }
    default: break;
    }
    if(row >= TOPIC) {
        pages_screen.topic = row - TOPIC;
        pages_screen.page = 0;
        push_child(pages_screen);
        return;
    }
    if(has_page(row)) { open(OPTIONS, row); return; }
    change(row, 1);
}

// ---- Storage conversion (DESIGN.md "Switching storage") ----
APP_EWRAM char question_line[48];
APP_EWRAM char percent_text[24];
APP_EWRAM Text conversion_note;   // set before each use

void progress(int percent, void* user)
{
    static int last = -1;
    if(percent == last && percent != 0) return;
    last = percent;
    Text t;
    char n[12];
    ui::format_int(percent, n);
    t << n << "%";
    std::strcpy(percent_text, t.buf);
    show_busy(static_cast<const char*>(user), percent_text);
}

void SettingsScreen::start_conversion(bool calendar)
{
    _conv_calendar = calendar;
    if(!(calendar ? data::calendar_ready() : data::todos_ready())) {
        flash(sd_ok() ? (calendar ? "Calendar not loaded" : "To-do list not loaded") : "No SD card");
        return;
    }
    const uint32_t lost = calendar ? planner::cal_convert_lost_lines() : planner::todo_convert_lost_lines();
    if(lost) {
        Text t;
        char n[12];
        ui::format_int(int(lost), n);
        t << n << (lost == 1 ? " line not understood." : " lines not understood.");
        std::strcpy(question_line, t.buf);
        Question q;
        q.title = calendar ? "Calendar saved as" : "To-do list saved as";
        q.lines[0] = question_line;
        q.lines[1] = lost == 1 ? "It will be lost." : "They will be lost.";
        q.answers[0] = "Convert anyway";
        q.answers[1] = "Cancel";
        q.done = [](void* self, int answer) { static_cast<SettingsScreen*>(self)->conversion_step(1, answer); };
        q.context = this;
        _child = true;
        ask(q);
        return;
    }
    conversion_step(1, 0);
}

void SettingsScreen::conversion_step(int step, int answer)
{
    if(answer != 0) return;   // Cancel / No: nothing changes
    const Settings& s = state::settings();
    const bool to_app = _conv_calendar ? s.calendar_saved_as == SavedAs::TEXT_FILE : s.todo_saved_as == SavedAs::TEXT_FILE;
    if(step == 1) {
        Question q;
        q.title = _conv_calendar ? "Calendar saved as" : "To-do list saved as";
        q.lines[0] = _conv_calendar ? (to_app ? "Convert calendar to in-app?" : "Convert calendar to text file?")
                                    : (to_app ? "Convert to-do list to in-app?" : "Convert to-do list to text file?");
        q.done = [](void* self, int a) { static_cast<SettingsScreen*>(self)->conversion_step(2, a); };
        q.context = this;
        _child = true;
        ask(q);
        return;
    }
    // Convert: new copy, read back, compare; then the setting, then the old
    // copy goes (cal_convert_finish).
    const planner::Storage target = to_app ? planner::Storage::IN_APP : planner::Storage::TEXT;
    const char* title = _conv_calendar ? "Converting calendar" : "Converting to-do list";
    progress(0, const_cast<char*>(title));
    planner::Err e = _conv_calendar ? planner::cal_convert(target, progress, const_cast<char*>(title))
                                    : planner::todo_convert(target, progress, const_cast<char*>(title));
    conversion_note.n = 0;
    conversion_note.buf[0] = 0;
    if(e != planner::Err::OK) {
        conversion_note << "Not converted: " << data::error_text(e);
        flash(conversion_note.buf);
        return;
    }
    Settings& w = state::settings();
    const Settings before = w;
    if(_conv_calendar) w.calendar_saved_as = to_app ? SavedAs::IN_APP : SavedAs::TEXT_FILE;
    else w.todo_saved_as = to_app ? SavedAs::IN_APP : SavedAs::TEXT_FILE;
    if(!state::save()) {
        w = before;
        if(_conv_calendar) planner::cal_convert_cancel();
        else planner::todo_convert_cancel();
        flash("SD card error: not converted");
        return;
    }
    e = _conv_calendar ? planner::cal_convert_finish() : planner::todo_convert_finish();
    if(_conv_calendar) data::cal_result(e);
    else data::todo_result(e);
    if(e == planner::Err::OK) conversion_note << (_conv_calendar ? "Calendar converted" : "To-do list converted");
    else conversion_note << "Converted; old copy: " << data::error_text(e);
    flash(conversion_note.buf);
    build();
}

void SettingsScreen::delete_hidden(bool yes)
{
    if(!yes) return;
    const planner::Err e = planner::todo_delete_hidden();
    flash(data::todo_result(e) ? "Hidden to-dos deleted" : data::error_text(e));
    build();
}

void SettingsScreen::update(const Input& in)
{
    using namespace keys;
    _held = in.held;
    if(in.hit(B)) { back(); return; }
    const list::Steps st = _input.frame(uint16_t(list_keys(in.held)));
    const int old_sel = _sel, old_top = _top;
    auto step = [&](int dir, bool fresh) {
        int s = _sel + dir;
        if(s < 0) s = fresh ? _count - 1 : 0;
        if(s >= _count) s = fresh ? 0 : _count - 1;
        _sel = uint8_t(s);
    };
    for(int k = 0; k < st.rows[0]; ++k) step(-1, k == 0 && (st.pressed & list::UP));
    for(int k = 0; k < st.rows[1]; ++k) step(1, k == 0 && (st.pressed & list::DOWN));
    if(_page == OPTIONS && _count > ui::ROWS) {
        // The long list: Left / Right a page.
        for(int k = 0; k < st.pages[0]; ++k) _sel = uint8_t(_sel >= ui::ROWS - 1 ? _sel - (ui::ROWS - 1) : 0);
        for(int k = 0; k < st.pages[1]; ++k)
            _sel = uint8_t(_sel + ui::ROWS - 1 < _count ? _sel + ui::ROWS - 1 : _count - 1);
    }
    show_sel();
    if(_sel != old_sel || _top != old_top) redraw();
    const int row = page_rows[_sel];
    const bool in_place = _page != OPTIONS && option_count(row) && !has_page(row);
    // Left / Right: change the value in place (held: repeat).
    if(in.pressed & (LEFT | RIGHT)) {
        _lr = (in.pressed & LEFT) ? LEFT : RIGHT;
        _lr_frames = 0;
        if(in_place) change(row, _lr == LEFT ? -1 : 1);
    }
    else if(_lr && in.down(_lr)) {
        if(++_lr_frames >= VALUE_DELAY && (_lr_frames - VALUE_DELAY) % VALUE_REPEAT == 0 && in_place &&
           row != CALENDAR_SAVED && row != TODO_SAVED)
            change(row, _lr == LEFT ? -1 : 1);
    }
    else _lr = 0;
    if(in.hit(A)) { activate(_sel); if(screens::top() != this) return; }
    // Saving after a pause; notes fade.
    if(_idle >= 0 && ++_idle >= SAVE_IDLE && !in.held) save();
    if(_note_frames && !--_note_frames) { _note = nullptr; redraw(); }
}

void SettingsScreen::draw_option(Canvas& c, int v, int y, bool sel) const
{
    const int lines = height(v);
    if(sel) c.fill(0, y, SCREEN_W, lines * ui::ROW_H, color::SELECT);
    if(v == current(_option_row)) c.text16(TICK_X, y, "\xe2\x9c\x93");
    int x = OPTION_X;
    if(color_row(_option_row)) {
        c.fill(x, y + 3, SWATCH_W, 10, color::category(v));
        x += SWATCH_W + 5;
    }
    Text t;
    option_text(_option_row, v, t);
    char a[40], b[40];
    split(t.buf, ui::TEXT_RIGHT - x, a, b);
    c.text16(x, y, a);
    if(b[0]) c.text16(x, y + ui::ROW_H, b);
}

void SettingsScreen::draw_row(Canvas& c, int i, int y, bool sel) const
{
    const int row = page_rows[i];
    const int lines = height(i);
    if(sel) c.fill(0, y, SCREEN_W, lines * ui::ROW_H, color::SELECT);
    const Settings& s = state::settings();
    int right = arrow(row) ? ARROW_X - 4 : ui::TEXT_RIGHT;
    if(arrow(row)) c.text16(ARROW_X, y, "\xe2\x96\xb8");
    if(color_row(row)) {
        right -= SWATCH_W + 4;
        c.fill(right + 4, y + 3, SWATCH_W, 10, color::category(*color_of(state::settings(), row)));
    }
    if(row == OPEN_COLORS) {
        // The four category colours.
        const uint8_t v[4] = {s.event_color, s.task_color, s.birthday_color, s.holiday_color};
        for(int k = 0; k < 4; ++k) c.fill(right - 4 * 13 + 2 + k * 13, y + 3, 10, 10, color::category(v[k]));
    }
    Text t;
    t << row_name(row);
    Text value;
    if(option_count(row)) option_text(row, current(row), value, true);
    if(_page == TOP && row == HOLIDAYS) {
        // "Holidays: United Kingdom" when the full name does not fit.
        Text full;
        full << "Holidays: " << value.buf;
        if(Canvas::width16(full.buf) > right - ui::TEXT_X) {
            for(int k = 0; k + 1 < value.n; ++k)
                if(value.buf[k] == ' ' && value.buf[k + 1] == '(') { value.n = k; value.buf[k] = 0; break; }
        }
    }
    if(value.n && lines == 1) {
        Text one;
        one << t.buf << ": " << value.buf;
        // The top page shows the value only where it fits.
        if(Canvas::width16(one.buf) <= right - ui::TEXT_X) t = one;
    }
    c.text16(ui::TEXT_X, y, t.buf, color::BLACK, 0, right - ui::TEXT_X);
    if(lines == 2) c.text16(ui::TEXT_X + 16, y + ui::ROW_H, value.buf, color::BLACK, 0, ui::TEXT_RIGHT - ui::TEXT_X - 16);
}

void SettingsScreen::draw(Canvas& c)
{
    c.clear();
    static const char* const titles[] = {"Settings", "General", "Colors", "Day view", "To-do list",
                                         "Repeating dates", "Storage", "Controls and Credits"};
    ui::title(c, _page == OPTIONS ? page_title(_option_row) : titles[_page]);
    if(_page == OPTIONS && _count > ui::ROWS) {
        Text t;
        t.num(_sel + 1) << "/";
        t.num(_count);
        ui::title_right(c, t.buf);
    }
    int y = ui::ROW_Y, r = 0;
    for(int i = _top; i < _count; ++i) {
        const int h = height(i);
        if(r + h > ui::ROWS) break;
        if(_page == OPTIONS) draw_option(c, page_rows[i], y, i == _sel);
        else draw_row(c, i, y, i == _sel);
        r += h;
        y += h * ui::ROW_H;
        // Lines: above Controls and Credits; above Credits.
        const int row = page_rows[i];
        if((_page == TOP && row == OPEN_STORAGE) || (_page == HELP && row == TOPIC + help::CREDITS - 1))
            ui::underline(c, r - 1);
    }
    const char* note = _page == OPTIONS ? option_note(_option_row) : page_note(_page);
    if(note) {
        c.hline(0, y + 3, SCREEN_W, color::LINE);
        small_wrapped(&c, y + 2, note);
    }
    if(_note) ui::note(c, _note);
}

}

Screen& settings_screen() { return settings; }

}
