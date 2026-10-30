#pragma once
// The app's side of the data core (include/core/, docs/CORE_API.md) and the
// country holidays (include/holidays/holidays.h): loading at start, the
// error handling every edit needs (NEEDS_RELOAD), a change counter the
// screens watch, category colours from Settings and the holiday country.
#include <cstdint>
#include <new>
#include "app/date.h"
#include "core/calendar.h"
#include "core/todo.h"

#ifdef __arm__
#define APP_EWRAM __attribute__((section(".sbss")))
#else
#define APP_EWRAM
#endif

namespace app {
// One object of type T in EWRAM (.sbss only takes zero-initialised data, so
// it is built there on first use). For the large buffers of the screens;
// IWRAM is kept for the stack.
template <class T> T& ewram()
{
    APP_EWRAM alignas(T) static unsigned char raw[sizeof(T)];
    static T* made = nullptr;
    if(!made) made = new(raw) T();
    return *made;
}
}

namespace app::data {

// After the card is mounted and the state loaded: core_init, the folders,
// calendar and to-do list from the storages Settings name. false: nothing
// could be loaded (no card, or an error; message() says why).
bool start();
bool calendar_ready();
// Settings > Feb 29 in other years / Day 29-31 in short months to the core
// (at start before loading, and whenever they change; moves the counter).
void apply_repeat_options();
// Settings > Repeating tasks in to-do list: 0, 7 or 30 days.
int task_window();
bool todos_ready();
// Lines of calendar.txt / todo.txt that were not understood at the last load.
uint32_t unknown_lines();
// The note to show once after loading ("3 lines not understood", or a load
// error); nullptr when there is nothing to say.
const char* start_note();

// Every edit goes through here: OK -> true and the change counter moves.
// NEEDS_RELOAD -> the calendar / list is loaded again (the card holds the
// old or the new state). Any error: false, error_text() says what happened.
bool cal_result(planner::Err e);
bool todo_result(planner::Err e);
const char* error_text(planner::Err e);
// Moves on every change of entries or to-dos (screens refresh their copies).
uint32_t serial();
void touch();

// Settings > Event / Task / Birthday / Holiday color: palette index.
uint8_t category_color(planner::Category c);
uint8_t category_tint(planner::Category c);
const char* category_label(planner::Category c);   // "Event" ...
// Settings > Holidays (0: none) and the country holidays of one day.
int country();
int country_holidays(const Date& d, const char** names, int max);

// planner day numbers <-> app dates.
inline int32_t to_day(const Date& d) { return day_number(d); }
inline Date to_date(int32_t day) { return from_day_number(day); }

// "09:00" / "9:00 AM" (Settings > Time format).
void format_time(int minutes, char (&out)[12]);

}
