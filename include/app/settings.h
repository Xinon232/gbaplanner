#pragma once
// Every setting of DESIGN.md (Settings table) with its default, plus the last
// calendar day. Kept in /gbaplanner/GBAPLANNER.SYS/STATE0.DAT / STATE1.DAT
// (app_state.h).
#include <cstdint>
#include "app/date.h"

namespace app {

enum class DateFormat : uint8_t { DMY_DOTS, MDY_SLASHES, YMD_DASHES };   // DD.MM.YYYY / MM/DD/YYYY / YYYY-MM-DD
enum class WeekStart : uint8_t { MONDAY, SUNDAY };
enum class TimeFormat : uint8_t { H24, H12 };
enum class DayView : uint8_t { TIMELINE, LIST };
enum class EventLength : uint8_t { MIN30, HOUR1 };
enum class TodoShows : uint8_t { TODOS_ONLY, TODOS_UPCOMING, TODOS_ALL_TASKS, TASKS_ONLY };
// AUTO: the default of DESIGN.md (Delete with in-app storage, Keep hidden
// with a text file) until the user picks one.
enum class OldTicked : uint8_t { AUTO, DELETE, KEEP_HIDDEN };
enum class SavedAs : uint8_t { TEXT_FILE, IN_APP };
// Repeat options (planner::RepeatOptions) and the to-do task window.
enum class Feb29Shown : uint8_t { SKIP, FEB28, MAR1 };          // Feb 29 in other years
enum class ShortMonths : uint8_t { SKIP, LAST_DAY };            // Day 29-31 in short months
enum class TaskWindow : uint8_t { NEXT_OPEN, DAYS7, DAYS30 };    // Repeating tasks in to-do list
// Settings > General > Start screen: the screen shown after start-up.
enum class StartScreen : uint8_t { CALENDAR, TODO };

constexpr int COUNTRY_COUNT = 34;   // Holidays: 0 = No country, 1..34 below

struct Settings {
    DateFormat date_format = DateFormat::DMY_DOTS;
    WeekStart week_start = WeekStart::MONDAY;
    TimeFormat time_format = TimeFormat::H24;
    uint8_t holidays = 0;                  // 0 = No country, else country_name(holidays)
    uint8_t event_color = 0;               // Blue (color::Category)
    uint8_t task_color = 1;                // Orange
    uint8_t birthday_color = 2;            // Purple
    uint8_t holiday_color = 3;             // Green
    DayView day_view = DayView::TIMELINE;
    uint8_t day_start_hour = 8;            // Day view starts at 00:00 .. 23:00
    EventLength event_length = EventLength::HOUR1;
    TodoShows todo_shows = TodoShows::TODOS_UPCOMING;
    OldTicked old_ticked = OldTicked::AUTO;
    bool notes = true;                     // new default On; a saved state keeps its value
    SavedAs calendar_saved_as = SavedAs::TEXT_FILE;
    SavedAs todo_saved_as = SavedAs::IN_APP;
    Feb29Shown feb29 = Feb29Shown::SKIP;
    ShortMonths short_months = ShortMonths::SKIP;
    TaskWindow task_window = TaskWindow::NEXT_OPEN;
    uint8_t ticked_shown = 2;              // Ticked to-dos shown: 0..5
    StartScreen start_screen = StartScreen::CALENDAR;   // state files from before: Calendar
    // The last calendar day (cursor); has_day false: the ROM build date.
    bool has_day = false;
    Date day{};

    // The value Old ticked to-dos really has (AUTO resolved).
    OldTicked old_ticked_effective() const
    {
        if(old_ticked != OldTicked::AUTO) return old_ticked;
        return todo_saved_as == SavedAs::IN_APP ? OldTicked::DELETE : OldTicked::KEEP_HIDDEN;
    }
    bool same_options(const Settings& o) const;   // everything but the day
};

// "Austria" .. "Japan" (index 1..34, DESIGN.md order); 0: "No country".
const char* country_name(int index);

}
