// The to-do list: DESIGN.md "To-do list (Start on the calendar)". START goes
// back to the calendar (B does nothing here); L+R together: sleep (sleep.h).
//
// Rows: New entry (line under it), the open to-dos (SuperFW), the N most
// recently ticked to-dos (Settings > To-do list > Ticked to-dos shown, 0..5;
// grey with a check mark), then a line
// "Upcoming tasks" and the calendar tasks with their date in front (task
// colour), as Settings > To-do list shows says.
#include "app/todo_screen.h"

#include <cstring>
#include "app/app_state.h"
#include "app/data.h"
#include "app/entry_screen.h"
#include "app/sleep.h"
#include "app/text_edit.h"
#include "app/ui.h"

namespace app {

namespace {

constexpr int MAX_TASKS = 200;
constexpr int MAX_ROWS = 1 + planner::MAX_TODOS + 1 + MAX_TASKS + 1;
constexpr int CACHE = 12;
constexpr int TITLE_X = 18, TASK_DATE_X = 12, TASK_TITLE_X = 76;

enum Kind : uint8_t { NEW, TODO, TICKED, HEADER, TASK, MORE };
struct Row {
    Kind kind;
    uint16_t index;   // to-do display index or task index
};

// Titles of rows on screen (read from the card once).
struct Cached { int row; char text[planner::MAX_TITLE_BYTES + 1]; };

unsigned list_keys(unsigned held)
{
    using namespace keys;
    unsigned k = 0;
    if(held & UP) k |= list::UP;
    if(held & DOWN) k |= list::DOWN;
    if(held & LEFT) k |= list::LEFT;
    if(held & RIGHT) k |= list::RIGHT;
    return k;
}

class TodoScreen final : public Screen {
public:
    Date reference{};
    void open(const Date& ref);
    void enter() override;
    void update(const Input& in) override;
    void draw(Canvas& c) override;
    // For the item menu.
    Row selected() const { return _rows[_nav.sel()]; }
    const char* title_of(int row);
    planner::DayItem task(int k) const { return _tasks[k]; }

private:
    Row* _rows = nullptr;
    int _count = 0;
    planner::DayItem* _tasks = nullptr;
    int _ntasks = 0, _tasks_total = 0;
    list::Nav _nav;
    list::Input _input;
    ui::Marquee _marquee;
    uint32_t _marquee_sel = ~0u;
    uint32_t _serial = 0;
    bool _a_down = false, _a_used = false;
    const char* _note = nullptr;
    int _note_frames = 0;
    Cached* _cache = nullptr;
    int _cache_next = 0;

    void build();
    void act(const Row& r);
    void message(const char* m) { _note = m; _note_frames = 150; redraw(); }
    static bool skip(void* self, uint32_t row);
};
TodoScreen screen;
struct TodoBuffers {
    Row rows[MAX_ROWS];
    planner::DayItem tasks[MAX_TASKS];
    Cached cache[CACHE];
};

bool TodoScreen::skip(void* self, uint32_t row)
{
    const Kind k = static_cast<TodoScreen*>(self)->_rows[row].kind;
    return k == HEADER || k == MORE;
}

void TodoScreen::open(const Date& ref)
{
    _rows = ewram<TodoBuffers>().rows;
    _tasks = ewram<TodoBuffers>().tasks;
    _cache = ewram<TodoBuffers>().cache;
    reference = ref;
    _count = 0;
    build();
    _nav.reset(uint32_t(_count), skip, this);
    _nav.set_rows(ui::ROWS);
}

void TodoScreen::build()
{
    const TodoShows shows = state::settings().todo_shows;
    const uint32_t keep = _count ? _nav.sel() : 0;
    _count = 0;
    _serial = data::serial();
    for(int k = 0; k < CACHE; ++k) _cache[k].row = -1;
    const bool todos = shows != TodoShows::TASKS_ONLY;
    if(todos) {
        _rows[_count++] = {NEW, 0};
        if(data::todos_ready()) {
            const int open = planner::todo_open_count();
            for(int i = 0; i < open; ++i) _rows[_count++] = {TODO, uint16_t(i)};
            const int all = planner::todo_count();
            for(int i = open; i < all; ++i)
                if(!planner::todo_is_hidden(i)) _rows[_count++] = {TICKED, uint16_t(i)};
        }
    }
    _ntasks = 0;
    _tasks_total = 0;
    if(shows != TodoShows::TODOS_ONLY && data::calendar_ready()) {
        planner::TaskCursor cur;
        const planner::TaskList mode = shows == TodoShows::TODOS_ALL_TASKS ? planner::TaskList::ALL : planner::TaskList::UPCOMING;
        // The core returns up to 64 rows per call: page with the cursor.
        while(_ntasks < MAX_TASKS) {
            const int got = planner::cal_tasks(data::to_day(reference), mode, &cur, _tasks + _ntasks,
                                               MAX_TASKS - _ntasks, &_tasks_total, data::task_window());
            if(got <= 0) break;
            _ntasks += got;
        }
        _rows[_count++] = {HEADER, 0};
        for(int k = 0; k < _ntasks; ++k) _rows[_count++] = {TASK, uint16_t(k)};
        if(_tasks_total > _ntasks) _rows[_count++] = {MORE, 0};
    }
    if(!_count) _rows[_count++] = {HEADER, 0};
    _nav.reset(uint32_t(_count), skip, this);
    _nav.set_rows(ui::ROWS);
    _nav.select(keep);
    _marquee.reset();
    redraw();
}

void TodoScreen::enter()
{
    if(_serial != data::serial()) build();
    _a_down = false;
    redraw();
}

const char* TodoScreen::title_of(int row)
{
    for(int k = 0; k < CACHE; ++k) if(_cache[k].row == row) return _cache[k].text;
    Cached& c = _cache[_cache_next];
    _cache_next = (_cache_next + 1) % CACHE;
    c.row = row;
    c.text[0] = 0;
    const Row& r = _rows[row];
    if(r.kind == TODO || r.kind == TICKED) {
        if(planner::todo_title(r.index, c.text, sizeof(c.text)) != planner::Err::OK) std::strcpy(c.text, "?");
    }
    else if(r.kind == TASK) {
        if(planner::cal_title(_tasks[r.index].id, c.text, sizeof(c.text)) != planner::Err::OK) std::strcpy(c.text, "?");
    }
    return c.text;
}

const char* add_done(void*, const char* text)
{
    const planner::Err e = planner::todo_add(text);
    if(!data::todo_result(e)) return data::error_text(e);
    return nullptr;
}

void TodoScreen::act(const Row& r)
{
    switch(r.kind) {
    case NEW: {
        if(!data::todos_ready()) { message(data::error_text(planner::Err::NOT_LOADED)); return; }
        TextEditRequest req;
        req.heading = "New to-do";
        req.max_chars = planner::MAX_TITLE_CHARS;
        req.done = add_done;
        open_text_edit(req, "");
        return;
    }
    case TODO: {
        const bool del = state::settings().old_ticked_effective() == OldTicked::DELETE;
        const planner::Err e = planner::todo_tick(r.index, del);
        if(!data::todo_result(e)) message(data::error_text(e));
        build();
        return;
    }
    case TICKED: {
        const planner::Err e = planner::todo_untick(r.index);
        if(!data::todo_result(e)) message(data::error_text(e));
        build();
        return;
    }
    case TASK: {
        const planner::DayItem& t = _tasks[r.index];
        const planner::Err e = planner::cal_set_done(t.id, t.day, true, nullptr);
        if(!data::cal_result(e)) message(data::error_text(e));
        build();
        return;
    }
    default: return;
    }
}

// ---- Select on an item: Edit / Delete ----
class ItemMenu final : public Screen {
public:
    Row row{};
    planner::DayItem task{};
    char* title = nullptr;   // EWRAM
    int sel = 0;
    bool confirm = false;
    const char* note = nullptr;
    void enter() override { redraw(); }
    void update(const Input& in) override;
    void draw(Canvas& c) override
    {
        c.clear();
        // The item's title in SuperFW (any letter), centred like a title.
        const int w = Canvas::body_width(title);
        c.body(w < 228 ? (SCREEN_W - w) / 2 : 6, 0, title, color::BLACK, 0, 228);
        ui::row(c, 0, "Edit", sel == 0);
        ui::row(c, 1, confirm ? "Delete: Sure?" : "Delete", sel == 1);
        if(note) ui::note(c, note);
    }
};
ItemMenu item_menu;
APP_EWRAM char item_title[planner::MAX_TITLE_BYTES + 1];

int edit_index = 0;
const char* edit_done(void*, const char* text)
{
    const planner::Err e = planner::todo_edit(edit_index, text);
    if(!data::todo_result(e)) return data::error_text(e);
    return nullptr;
}

void ItemMenu::update(const Input& in)
{
    using namespace keys;
    if(in.hit(B) || in.hit(SELECT)) { screens::pop(); return; }
    if(in.hit(UP) || in.hit(DOWN)) { sel ^= 1; confirm = false; redraw(); }
    if(!in.hit(A)) return;
    if(sel == 0) {
        if(row.kind == TASK) { screens::pop(); open_entry(task.id, data::to_date(task.day)); return; }
        edit_index = row.index;
        TextEditRequest req;
        req.heading = "Edit to-do";
        req.max_chars = planner::MAX_TITLE_CHARS;
        req.done = edit_done;
        screens::pop();
        open_text_edit(req, title);
        return;
    }
    if(!confirm) { confirm = true; redraw(); return; }
    const planner::Err e = row.kind == TASK ? planner::cal_delete(task.id) : planner::todo_delete(row.index);
    const bool ok = row.kind == TASK ? data::cal_result(e) : data::todo_result(e);
    if(ok) screens::pop();
    else { confirm = false; note = data::error_text(e); redraw(); }
}

void TodoScreen::update(const Input& in)
{
    using namespace keys;
    // L and R pressed together (a fresh chord: keys held when the list
    // opened, e.g. L+R at start-up for the SuperCard driver, are masked):
    // sleep until START is held 1 s, which does nothing else.
    if((in.held & (L | R)) == (L | R) && (in.pressed & (L | R))) {
        _a_down = false;
        sleep_until_start();
        return;
    }
    if(in.hit(START)) { screens::pop(); return; }
    const Row cur = _rows[_nav.sel()];
    // Hold A + Up/Down: move an open to-do.
    if(in.hit(A)) { _a_down = true; _a_used = false; }
    if(_a_down && in.down(A) && cur.kind == TODO && (in.hit(UP) || in.hit(DOWN))) {
        const int dir = in.hit(UP) ? -1 : 1;
        const int to = cur.index + dir;
        if(to >= 0 && to < planner::todo_open_count()) {
            const planner::Err e = planner::todo_move(cur.index, dir);
            if(data::todo_result(e)) { build(); _nav.select(_nav.sel() + uint32_t(dir)); }
            else message(data::error_text(e));
        }
        _a_used = true;
        _input.frame(uint16_t(list_keys(in.held)));
        redraw();
        return;
    }
    if(_a_down && in.up(A)) {
        _a_down = false;
        if(!_a_used) { act(cur); if(screens::top() != this) return; }
    }
    if(!in.down(A)) {
        const list::Steps st = _input.frame(uint16_t(list_keys(in.held)));
        bool moved = false;
        for(int k = 0; k < st.rows[0]; ++k) moved |= _nav.step(-1, k == 0 && (st.pressed & list::UP));
        for(int k = 0; k < st.rows[1]; ++k) moved |= _nav.step(1, k == 0 && (st.pressed & list::DOWN));
        for(int k = 0; k < st.pages[0]; ++k) moved |= _nav.page(-1);
        for(int k = 0; k < st.pages[1]; ++k) moved |= _nav.page(1);
        if(moved) redraw();
    }
    if(in.hit(SELECT)) {
        const Row r = _rows[_nav.sel()];
        if(r.kind == TODO || r.kind == TICKED || r.kind == TASK) {
            item_menu.row = r;
            if(r.kind == TASK) item_menu.task = _tasks[r.index];
            item_menu.title = item_title;
            std::strncpy(item_title, title_of(int(_nav.sel())), sizeof(item_title) - 1);
            item_menu.sel = 0;
            item_menu.confirm = false;
            item_menu.note = nullptr;
            screens::push(item_menu);
            return;
        }
    }
    if(_note_frames && !--_note_frames) { _note = nullptr; redraw(); }
    if(_nav.sel() != _marquee_sel) { _marquee_sel = _nav.sel(); _marquee.reset(); }
    const Row r = _rows[_nav.sel()];
    if(r.kind == TODO || r.kind == TICKED || r.kind == TASK) {
        const int x = r.kind == TASK ? TASK_TITLE_X : TITLE_X;
        if(_marquee.step(Canvas::body_width(title_of(int(_nav.sel()))), ui::TEXT_RIGHT - x)) redraw();
    }
}

void TodoScreen::draw(Canvas& c)
{
    c.clear();
    char date[16];
    ui::format_date(reference, state::settings().date_format, date);
    ui::Text head;
    head << "To-do \xc2\xb7 from " << date;
    ui::title(c, head.buf);
    const TodoShows shows = state::settings().todo_shows;
    for(int k = 0; k < _nav.rows(); ++k) {
        const uint32_t i = _nav.top() + uint32_t(k);
        if(i >= _nav.count()) break;
        const Row& r = _rows[i];
        const bool sel = i == _nav.sel();
        const int y = ui::ROW_Y + k * ui::ROW_H;
        const int skip = sel ? int(_marquee.off) : 0;
        switch(r.kind) {
        case NEW:
            ui::row(c, k, "New entry", sel);
            ui::underline(c, k);
            break;
        case TODO:
        case TICKED: {
            if(sel) c.fill(0, y, SCREEN_W, ui::ROW_H, color::SELECT);
            const bool ticked = r.kind == TICKED;
            if(ticked) c.text16(ui::TEXT_X, y, "\xe2\x9c\x93", color::GREY);
            ui::body_fit(c, TITLE_X, y, title_of(int(i)), ui::TEXT_RIGHT - TITLE_X, ticked ? color::GREY : color::BLACK, sel, skip);
            break;
        }
        case HEADER: {
            const char* label = !data::calendar_ready() && shows != TodoShows::TODOS_ONLY ? "Calendar not loaded"
                              : shows == TodoShows::TODOS_ALL_TASKS ? "All tasks" : "Upcoming tasks";
            if(_count == 1) label = "Nothing here";
            c.small(ui::TEXT_X, y, label, color::GREY);
            const int x = ui::TEXT_X + Canvas::small_width(label) + 4;
            c.hline(x, y + 8, ui::TEXT_RIGHT - x, color::LINE);
            break;
        }
        case TASK: {
            if(sel) c.fill(0, y, SCREEN_W, ui::ROW_H, color::SELECT);
            const planner::DayItem& t = _tasks[r.index];
            if(t.flags & planner::ITEM_OVERDUE) c.small(ui::TEXT_X - 2, y, "!", color::BLACK);
            char d[16];
            ui::format_date(data::to_date(t.day), state::settings().date_format, d);
            c.small(TASK_DATE_X, y, d, data::category_color(planner::Category::TASK));
            ui::body_fit(c, TASK_TITLE_X, y, title_of(int(i)), ui::TEXT_RIGHT - TASK_TITLE_X, color::BLACK, sel, skip);
            break;
        }
        case MORE: {
            ui::Text t;
            t << "\xe2\x80\xa6 ";
            t.num(_tasks_total - _ntasks) << " more tasks";
            c.small(ui::TEXT_X, y, t.buf, color::GREY);
            break;
        }
        default: break;
        }
    }
    if(_note) ui::note(c, _note);
}

}

Screen& todo_screen(const Date& reference)
{
    screen.open(reference);
    return screen;
}

bool hidden_todos_exist() { return data::todos_ready() && planner::todo_hidden_count() > 0; }

}
