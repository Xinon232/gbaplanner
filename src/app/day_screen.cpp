// The day view: DESIGN.md "Day view (A on a day)".
//
// Layout (240 x 160):
//   y   0-15   small title "Friday, 23.10.2026"
//   y  16-31   "New entry" (line under it)
//   timeline:  up to two all-day bars (16 px each), then the hours, 1 h =
//              16 px, labels on the left; blocks at their time, overlapping
//              events side by side, timed tasks as a short block
//   list:      agenda rows (time, colour bar, title)
//   y 144-159  the selected item: time, full title (scrolls), "≡" = notes
#include "app/day_screen.h"

#include <cstring>
#include "app/app_state.h"
#include "app/calendar_screen.h"
#include "app/data.h"
#include "app/entry_screen.h"
#include "app/ui.h"
#include "holidays/holidays.h"

namespace app {

using planner::Category;

namespace {

constexpr int MAX_ITEMS = 40;
constexpr int POOL = 6144;
constexpr int NEW_Y = 16, BODY_Y = 32, BAR_Y = 144;
constexpr int HOUR_H = 16, LABEL_W = 34, BLOCK_X = 36, BLOCK_W = 202;
constexpr int MAX_ALLDAY_SHOWN = 2;
constexpr int PAD = 7;            // the first hour line and its label below the bars
constexpr int CONTENT_H = 24 * HOUR_H + PAD + 4;
constexpr int TASK_MINUTES = 30;   // a timed task is drawn as a half-hour block

struct Item {
    bool country = false;
    planner::DayItem it{};
    Category cat = Category::EVENT;
    bool timed = false, ticked = false, notes = false;
    int16_t start = 0, end = 0;
    uint16_t title = 0;      // offset in the pool
    uint8_t col = 0, cols = 1;
};

class DayScreen final : public Screen {
public:
    void open(const Date& d);
    void enter() override;
    void leave() override;
    void update(const Input& in) override;
    void draw(Canvas& c) override;

private:
    Date _day{};
    Item* _items = nullptr;
    int _count = 0, _allday = 0, _total = 0;
    char* _pool = nullptr;
    int _pool_used = 0;
    int _sel = 0;          // 0 = New entry, i + 1 = _items[i]
    int _scroll = 0;       // timeline, pixels from 00:00
    int _list_top = 0;     // list mode, first row shown
    int _allday_top = 0;
    uint32_t _serial = 0;
    ui::Marquee _marquee;
    ui::Repeat _ud;
    const char* _note = nullptr;
    int _note_frames = 0;

    bool timeline() const { return state::settings().day_view == DayView::TIMELINE; }
    void load(bool keep_selection);
    void layout_columns();
    void opening_view();
    int timeline_y() const { return BODY_Y + (_allday < MAX_ALLDAY_SHOWN ? _allday : MAX_ALLDAY_SHOWN) * 16; }
    int timeline_h() const { return BAR_Y - timeline_y(); }
    void follow();
    void select(int s);
    void title_of(int i, ui::Text& t) const;
    void time_of(int i, ui::Text& t) const;
    void draw_timeline(Canvas& c);
    void draw_list(Canvas& c);
    void draw_bar(Canvas& c);
    uint8_t ink(const Item& it) const { return it.ticked ? color::GREY : color::BLACK; }
};
DayScreen screen;
APP_EWRAM char title_pool[POOL];
struct Items { Item item[MAX_ITEMS]; };

void DayScreen::open(const Date& d)
{
    _items = ewram<Items>().item;
    _pool = title_pool;
    _note = nullptr;
    _note_frames = 0;
    _day = d;
    load(false);
    opening_view();
}

void DayScreen::load(bool keep_selection)
{
    const int old_sel = _sel;
    _count = 0;
    _pool_used = 0;
    _serial = data::serial();
    const auto add_title = [&](const char* s) {
        const int n = int(std::strlen(s));
        const int room = POOL - _pool_used - 1;
        const int k = n < room ? n : (room > 0 ? room : 0);
        const uint16_t at = uint16_t(_pool_used);
        std::memcpy(_pool + _pool_used, s, size_t(k));
        _pool[_pool_used + k] = 0;
        _pool_used += k + 1;
        return at;
    };
    // Country holidays first (read-only).
    const char* names[4];
    const int nh = data::country_holidays(_day, names, 4);
    for(int k = 0; k < nh && _count < MAX_ITEMS; ++k) {
        Item& it = _items[_count++];
        it = Item();
        it.country = true;
        it.cat = Category::HOLIDAY;
        it.title = add_title(names[k]);
    }
    _total = nh;
    if(data::calendar_ready()) {
        static planner::DayItem items[MAX_ITEMS] APP_EWRAM;
        const int32_t dn = data::to_day(_day);
        const int total = planner::cal_day_items(dn, items, MAX_ITEMS - _count, 0);
        const int n = total < MAX_ITEMS - _count ? total : MAX_ITEMS - _count;
        _total += total;
        for(int k = 0; k < n; ++k) {
            Item& it = _items[_count++];
            it = Item();
            it.it = items[k];
            it.cat = items[k].cat;
            it.timed = items[k].flags & planner::ITEM_TIMED;
            it.ticked = items[k].flags & planner::ITEM_TICKED;
            it.start = items[k].start;
            it.end = it.cat == Category::TASK ? int16_t(items[k].start + TASK_MINUTES) : items[k].end;
            char title[planner::MAX_TITLE_BYTES + 1];
            bool notes = false;
            if(planner::cal_title(items[k].id, title, sizeof(title), &notes) != planner::Err::OK) std::strcpy(title, "?");
            it.notes = notes;
            if(it.cat == Category::BIRTHDAY) {
                planner::EntryInfo info;
                if(planner::cal_get(items[k].id, &info) == planner::Err::OK) {
                    const int age = _day.year - planner::ymd_from_day(info.day).y;
                    if(age >= 0) {
                        ui::Text t;
                        t << title << " (";
                        t.num(age) << ")";
                        std::strncpy(title, t.buf, sizeof(title) - 1);
                        title[sizeof(title) - 1] = 0;
                    }
                }
            }
            it.title = add_title(title);
        }
    }
    _allday = 0;
    while(_allday < _count && !_items[_allday].timed) ++_allday;
    layout_columns();
    _sel = keep_selection ? (old_sel <= _count ? old_sel : _count) : 0;
    _marquee.reset();
    redraw();
}

void DayScreen::layout_columns()
{
    // Clusters of overlapping timed items; first free column in each.
    int i = _allday;
    while(i < _count) {
        int end = _items[i].end, j = i;
        int col_end[8] = {};
        int cols = 0;
        for(; j < _count && (j == i || _items[j].start < end); ++j) {
            Item& it = _items[j];
            int c = 0;
            while(c < cols && col_end[c] > it.start) ++c;
            if(c >= 8) c = 7;
            if(c == cols && cols < 8) ++cols;
            col_end[c] = it.end;
            it.col = uint8_t(c);
            if(it.end > end) end = it.end;
        }
        for(int k = i; k < j; ++k) _items[k].cols = uint8_t(cols ? cols : 1);
        i = j;
    }
}

void DayScreen::opening_view()
{
    // The selection starts on New entry; the timeline shows the first
    // entry's hour (or Day view starts at).
    _sel = 0;
    _list_top = 0;
    _allday_top = 0;
    const int max = CONTENT_H - timeline_h();
    _scroll = (_allday < _count ? _items[_allday].start / 60 : state::settings().day_start_hour) * HOUR_H;
    if(_scroll > max) _scroll = max;
    if(_scroll < 0) _scroll = 0;
    follow();
}

void DayScreen::follow()
{
    if(_sel >= 1 && _sel - 1 < _allday) {
        const int k = _sel - 1;
        if(k < _allday_top) _allday_top = k;
        if(k >= _allday_top + MAX_ALLDAY_SHOWN) _allday_top = k - MAX_ALLDAY_SHOWN + 1;
    }
    if(_sel >= 1 && _sel - 1 >= _allday) {
        const Item& it = _items[_sel - 1];
        const int top = PAD + it.start * HOUR_H / 60, bottom = PAD + it.end * HOUR_H / 60;
        const int h = timeline_h();
        if(top < _scroll + 4) _scroll = top - 4;
        else if(bottom > _scroll + h - 4) _scroll = bottom - h + 4 > top - 4 ? top - 4 : bottom - h + 4;
        const int max = CONTENT_H - h;
        if(_scroll > max) _scroll = max;
        if(_scroll < 0) _scroll = 0;
    }
    // List mode: 8 rows (New entry + items).
    constexpr int ROWS = 8;
    if(_sel < _list_top) _list_top = _sel;
    if(_sel >= _list_top + ROWS) _list_top = _sel - ROWS + 1;
}

void DayScreen::select(int s)
{
    if(s < 0 || s > _count || s == _sel) return;
    _sel = s;
    follow();
    _marquee.reset();
    redraw();
}

void DayScreen::enter()
{
    if(_serial != data::serial()) load(true);
    follow();
    redraw();
}

void DayScreen::leave() {}

void DayScreen::title_of(int i, ui::Text& t) const { t << _pool + _items[i].title; }

void DayScreen::time_of(int i, ui::Text& t) const
{
    const Item& it = _items[i];
    if(!it.timed) { t << "All day"; return; }
    char a[12];
    data::format_time(it.start, a);
    t << a;
    if(it.cat == Category::EVENT) {
        char b[12];
        data::format_time(it.end, b);
        t << "-" << b;
    }
}

void DayScreen::update(const Input& in)
{
    using namespace keys;
    if(in.hit(B)) {
        calendar().show_day(_day);
        screens::pop();
        return;
    }
    if(in.hit(L) || in.hit(R)) {
        _day = add_days(_day, in.hit(L) ? -1 : 1);
        load(false);
        opening_view();
        return;
    }
    const int n = _ud.steps(in, UP | DOWN);
    for(int k = 0; k < n; ++k) {
        const int s = _sel + _ud.dir();
        if(s >= 0 && s <= _count) select(s);
    }
    if(in.hit(A)) {
        if(_sel == 0) { screens::push(new_entry_screen(_day)); return; }
        const Item& it = _items[_sel - 1];
        if(it.country) {
            // The note row holds 38 small characters.
            static ui::Text note;
            note = ui::Text();
            note << "Public holiday (" << holidays_country_name(data::country()) << "), read-only";
            if(note.n > 38) { note = ui::Text(); note << "Public holiday, read-only"; }
            _note = note.buf;
            _note_frames = 150;
            redraw();
        }
        else { open_entry(it.it.id, _day); return; }
    }
    if(_note_frames && !--_note_frames) { _note = nullptr; redraw(); }
    if(_sel >= 1) {
        ui::Text t;
        title_of(_sel - 1, t);
        ui::Text tm;
        time_of(_sel - 1, tm);
        const int avail = 228 - (ui::TEXT_X + Canvas::small_width(tm.buf) + 6) - (_items[_sel - 1].notes ? 10 : 0);
        if(_marquee.step(Canvas::body_width(t.buf), avail)) redraw();
    }
}

void DayScreen::draw(Canvas& c)
{
    c.clear();
    char date[16];
    ui::format_date(_day, state::settings().date_format, date);
    ui::Text head;
    head << weekday_name(weekday(_day)) << ", " << date;
    ui::title(c, head.buf);
    if(timeline()) draw_timeline(c);
    else draw_list(c);
    draw_bar(c);
    if(_note) ui::note(c, _note);
}

void DayScreen::draw_timeline(Canvas& c)
{
    // New entry.
    ui::row(c, 0, "New entry", _sel == 0);
    ui::underline(c, 0);
    // All-day bars.
    const int shown = _allday < MAX_ALLDAY_SHOWN ? _allday : MAX_ALLDAY_SHOWN;
    for(int r = 0; r < shown; ++r) {
        const int i = _allday_top + r;
        const Item& it = _items[i];
        const int y = BODY_Y + r * 16;
        const uint8_t col = data::category_color(it.cat);
        c.fill(2, y + 1, SCREEN_W - 4, 14, it.ticked ? color::PALE : data::category_tint(it.cat));
        c.fill(2, y + 1, 3, 14, it.ticked ? color::GREY : col);
        if(_sel == i + 1) c.frame(1, y, SCREEN_W - 2, 16, color::BLACK);
        int right = SCREEN_W - 8;
        if(r == shown - 1 && _allday > shown) {
            // More all-day items than rows: how many are out of view.
            ui::Text t;
            t << "+";
            t.num(_allday - shown);
            c.small_right(SCREEN_W - 6, y, t.buf, color::GREY);
            right -= Canvas::small_width(t.buf) + 4;
        }
        ui::Text t;
        title_of(i, t);
        ui::body_fit(c, 9, y, t.buf, right - 9, ink(it));
    }
    // Hours.
    const int ty = timeline_y(), th = timeline_h();
    const bool h12 = state::settings().time_format == TimeFormat::H12;
    for(int h = 0; h <= 24; ++h) {
        const int y = ty + PAD + h * HOUR_H - _scroll;
        if(y < ty || y >= ty + th) continue;
        c.hline(LABEL_W, y, SCREEN_W - LABEL_W, color::PALE);
        if(h == 24) continue;
        char label[12];
        if(h12) {
            ui::Text t;
            t.num(h % 12 ? h % 12 : 12) << (h < 12 ? " AM" : " PM");
            std::strcpy(label, t.buf);
        }
        else ui::format_hour(h, TimeFormat::H24, label);
        if(y - 1 >= ty && y + 6 <= ty + th) c.small(2, y - 6, label, color::GREY);
    }
    // Blocks.
    for(int i = _allday; i < _count; ++i) {
        const Item& it = _items[i];
        const int w = BLOCK_W / it.cols;
        const int x = BLOCK_X + it.col * w;
        int y0 = ty + PAD + it.start * HOUR_H / 60 - _scroll;
        int y1 = ty + PAD + it.end * HOUR_H / 60 - _scroll;
        if(y1 - y0 < 5) y1 = y0 + 5;
        const int full0 = y0, full1 = y1;
        if(y1 <= ty || y0 >= ty + th) continue;
        if(y0 < ty) y0 = ty;
        if(y1 > ty + th) y1 = ty + th;
        const uint8_t col = data::category_color(it.cat);
        c.fill(x + 1, y0, w - 2, y1 - y0 - 1, it.ticked ? color::PALE : data::category_tint(it.cat));
        c.fill(x + 1, y0, 2, y1 - y0 - 1, it.ticked ? color::GREY : col);
        if(full1 - full0 >= 14 && full0 >= ty - 1 && full0 + 15 <= ty + th) {
            ui::Text t;
            title_of(i, t);
            ui::body_fit(c, x + 5, full0 - 1 < 0 ? 0 : full0 - 1, t.buf, w - 8, ink(it));
        }
        if(_sel == i + 1) c.frame(x, y0 - (y0 == full0 ? 1 : 0), w, y1 - y0 + (y0 == full0 ? 1 : 0), color::BLACK);
    }
}

void DayScreen::draw_list(Canvas& c)
{
    constexpr int ROWS = 8;
    for(int r = 0; r < ROWS; ++r) {
        const int s = _list_top + r;
        if(s > _count) break;
        const int y = ui::ROW_Y + r * ui::ROW_H;
        if(s == 0) {
            ui::row(c, r, "New entry", _sel == 0);
            ui::underline(c, r);
            continue;
        }
        const Item& it = _items[s - 1];
        if(_sel == s) c.fill(0, y, SCREEN_W, ui::ROW_H, color::SELECT);
        ui::Text tm;
        if(it.timed) { char a[12]; data::format_time(it.start, a); tm << a; }
        else tm << "All day";
        c.small(ui::TEXT_X, y, tm.buf, it.ticked ? color::GREY : color::BLACK);
        c.fill(56, y + 2, 3, 12, it.ticked ? color::GREY : data::category_color(it.cat));
        ui::Text t;
        title_of(s - 1, t);
        ui::body_fit(c, 63, y, t.buf, ui::TEXT_RIGHT - 63, ink(it));
    }
    if(_count + 1 > ROWS) {
        ui::Text t;
        t.num(_sel + 1) << "/";
        t.num(_count + 1);
        ui::title_right(c, t.buf);
    }
}

void DayScreen::draw_bar(Canvas& c)
{
    c.fill(0, BAR_Y, SCREEN_W, SCREEN_H - BAR_Y, color::WHITE);
    c.hline(0, BAR_Y, SCREEN_W, color::LIGHT);
    if(_sel == 0) {
        if(_total > _count) {
            // A very full day: only the first MAX_ITEMS items are listed.
            ui::Text t;
            t.num(_count) << " of ";
            t.num(_total) << " items shown";
            c.small(ui::TEXT_X, BAR_Y, t.buf, color::GREY);
            return;
        }
        const char* text = _total ? "A: new entry   L/R: other day" : "Nothing on this day   A: new entry";
        c.small(ui::TEXT_X, BAR_Y, text, color::GREY);
        return;
    }
    const int i = _sel - 1;
    ui::Text tm;
    time_of(i, tm);
    c.small(ui::TEXT_X, BAR_Y, tm.buf, color::BLACK);
    const int x = ui::TEXT_X + Canvas::small_width(tm.buf) + 6;
    const bool notes = _items[i].notes && state::settings().notes;
    const int right = notes ? 226 : 234;
    ui::Text t;
    title_of(i, t);
    c.body(x, BAR_Y, t.buf, color::BLACK, int(_marquee.off), right - x);
    if(notes) c.text16(228, BAR_Y, "\xe2\x89\xa1", color::BLACK);
}

}

Screen& day_screen(const Date& day)
{
    screen.open(day);
    return screen;
}

}
