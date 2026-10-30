// Loading, edits and colours for the screens (include/app/data.h).
#include "app/data.h"

#include "app/app_state.h"
#include "app/canvas.h"
#include "app/sd.h"
#include "holidays/holidays.h"

namespace app::data {

using namespace planner;

namespace {
bool cal_ok = false, todo_ok = false;
uint32_t unknown = 0;
uint32_t changes = 1;
char note[48];
const char* note_ptr = nullptr;

Storage cal_storage_setting() { return state::settings().calendar_saved_as == SavedAs::IN_APP ? Storage::IN_APP : Storage::TEXT; }
Storage todo_storage_setting() { return state::settings().todo_saved_as == SavedAs::IN_APP ? Storage::IN_APP : Storage::TEXT; }

void put(char*& p, char* end, const char* s) { while(*s && p + 1 < end) *p++ = *s++; *p = 0; }
void put_num(char*& p, char* end, uint32_t v)
{
    char d[12];
    int n = 0;
    do { d[n++] = char('0' + v % 10); v /= 10; } while(v);
    while(n && p + 1 < end) *p++ = d[--n];
    *p = 0;
}
}

bool start()
{
    note_ptr = nullptr;
    if(!sd::ready()) return false;
    core_init(sd::ROOT);
    apply_repeat_options();
    todo_set_visible_ticked(state::settings().ticked_shown);
    Err e = core_make_dirs();
    LoadReport rep;
    Err ce = e == Err::OK ? cal_load(cal_storage_setting(), &rep) : e;
    cal_ok = ce == Err::OK;
    TodoReport trep;
    Err te = e == Err::OK ? todo_load(todo_storage_setting(), &trep) : e;
    todo_ok = te == Err::OK;
    unknown = (cal_ok ? rep.unknown_lines : 0) + (todo_ok ? trep.unknown_lines : 0);
    char* p = note;
    char* end = note + sizeof(note);
    note[0] = 0;
    if(!cal_ok) { put(p, end, "Calendar not loaded: "); put(p, end, error_text(ce)); }
    else if(!todo_ok) { put(p, end, "To-do list not loaded: "); put(p, end, error_text(te)); }
    else if(unknown) {
        put_num(p, end, unknown);
        put(p, end, unknown == 1 ? " line not understood" : " lines not understood");
    }
    note_ptr = note[0] ? note : nullptr;
    ++changes;
    return cal_ok && todo_ok;
}

void apply_repeat_options()
{
    const Settings& s = state::settings();
    RepeatOptions o;
    o.feb29 = Feb29(uint8_t(s.feb29));
    o.short_month = ShortMonth(uint8_t(s.short_months));
    cal_set_repeat_options(o);
    ++changes;
}

int task_window()
{
    switch(state::settings().task_window) {
    case TaskWindow::DAYS7: return 7;
    case TaskWindow::DAYS30: return 30;
    default: return 0;
    }
}

bool calendar_ready() { return cal_ok && cal_loaded(); }
bool todos_ready() { return todo_ok && todo_loaded(); }
uint32_t unknown_lines() { return unknown; }
const char* start_note() { return note_ptr; }
uint32_t serial() { return changes; }
void touch() { ++changes; }

bool cal_result(Err e)
{
    ++changes;
    if(e == Err::OK) return true;
    if(e == Err::NEEDS_RELOAD) cal_ok = cal_load(cal_storage(), nullptr) == Err::OK;
    return false;
}

bool todo_result(Err e)
{
    ++changes;
    if(e == Err::OK) return true;
    if(e == Err::NEEDS_RELOAD) todo_ok = todo_load(todo_storage(), nullptr) == Err::OK;
    return false;
}

const char* error_text(Err e)
{
    switch(e) {
    case Err::OK: return "OK";
    case Err::IO: return "SD card error";
    case Err::DISK_FULL: return "SD card full";
    case Err::NOT_FOUND: return "Not found";
    case Err::CORRUPT: return "File damaged";
    case Err::VERIFY: return "Check after saving failed";
    case Err::RECOVERY_NEEDED: return "Unfinished save on the card";
    case Err::FULL: return "Too many entries";
    case Err::INVALID: return "Invalid entry";
    case Err::TOO_LARGE: return "File too large";
    case Err::NOT_LOADED: return sd::ready() ? "Not loaded" : "No SD card";
    case Err::NEEDS_RELOAD: return "Not saved: card error";
    default: break;
    }
    return "Error";
}

uint8_t category_color(Category c)
{
    const Settings& s = state::settings();
    switch(c) {
    case Category::HOLIDAY: return color::category(s.holiday_color);
    case Category::BIRTHDAY: return color::category(s.birthday_color);
    case Category::TASK: return color::category(s.task_color);
    default: return color::category(s.event_color);
    }
}

uint8_t category_tint(Category c) { return uint8_t(category_color(c) - color::CATEGORY + color::TINT); }

const char* category_label(Category c)
{
    switch(c) {
    case Category::HOLIDAY: return "Holiday";
    case Category::BIRTHDAY: return "Birthday";
    case Category::TASK: return "Task";
    default: return "Event";
    }
}

int country() { return state::settings().holidays; }

int country_holidays(const Date& d, const char** names, int max)
{
    HolidayItem items[4];
    const int n = holidays_on_day(country(), d.year, d.month, d.day, items, 4);
    int k = 0;
    for(; k < n && k < 4 && k < max; ++k) names[k] = items[k].name;
    return k;
}

void format_time(int minutes, char (&out)[12])
{
    planner::format_time(minutes, state::settings().time_format == TimeFormat::H12 ? planner::TimeFormat::H12 : planner::TimeFormat::H24, out);
}

}
