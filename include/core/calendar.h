// gbaplanner calendar: entries, RAM index, queries for the screens, edits,
// storage (calendar.txt or the in-app store) and conversion between them.
// See docs/CORE_API.md.
#ifndef PLANNER_CORE_CALENDAR_H
#define PLANNER_CORE_CALENDAR_H

#include "repeat.h"

namespace planner {

enum class Category : uint8_t { HOLIDAY = 0, BIRTHDAY = 1, EVENT = 2, TASK = 3 };
enum class Storage : uint8_t { TEXT = 0, IN_APP = 1 };

// Index of an entry in the RAM index. Valid until the next call that changes
// the calendar (add/update/delete/skip/done/load/convert); those return the
// new id of the entry they touched.
using EntryId = uint16_t;
constexpr EntryId NO_ENTRY = 0xFFFF;

// Sets the folder ("/gbaplanner"); creates nothing. The FatFs volume must be
// mounted by the app. Resets calendar and to-do RAM state.
void core_init(const char* root = "/gbaplanner");
// Creates the folder and GBAPLANNER.SYS if missing.
Err core_make_dirs();
// exFAT: repairs directory entries a power cut left half written (FatFs
// writes a file's entry set sector by sector) in `dir` and its parents, so
// FatFs can read the folder again; FAT: nothing. Loads and saves do this for
// their own folder; the app calls it for GBAPLANNER.SYS at start.
Err core_repair_dir(const char* dir);

// ---- loading ---------------------------------------------------------------
struct LoadReport {
  uint32_t entries = 0;        // entries in RAM
  uint32_t unknown_lines = 0;  // lines not understood (kept in the file, lost by a conversion)
  uint32_t limit_lines = 0;    // valid lines over a RAM limit (10,000 entries, rule tables); kept
  uint32_t comment_lines = 0;  // lines starting with '#' (kept; a conversion drops them)
  uint32_t empty_lines = 0;    // empty / blank lines
  uint32_t bad_records = 0;    // in-app: records that failed their check (skipped)
  uint32_t bytes = 0;          // file size
};
// Runs the recovery of interrupted saves, then loads. A missing file is an
// empty calendar. On error the calendar is empty and not loaded.
Err cal_load(Storage s, LoadReport* report = nullptr);
bool cal_loaded();
Storage cal_storage();
int cal_count();
const LoadReport& cal_report();  // of the last load

// ---- queries ---------------------------------------------------------------
// Month view: for each day, up to 8 marks in display order (holidays,
// birthdays, then events and tasks by time with all-day first; same time:
// file order) and the total. Country holidays are not included.
constexpr uint8_t MARK_CATEGORY = 3;  // bits 0-1: Category
constexpr uint8_t MARK_TICKED = 4;    // bit 2: ticked task (hollow ring)
struct DayMarks {
  uint16_t total;
  uint8_t shown;    // min(total, 8)
  uint8_t item[8];
};
// first_day .. first_day + ndays - 1, ndays <= 56 (a 6-week grid is 42).
void cal_marks(int32_t first_day, int ndays, DayMarks* out);

constexpr uint8_t ITEM_TIMED = 1;    // has a start time (event: start-end; task: start)
constexpr uint8_t ITEM_TICKED = 2;   // ticked task occurrence
constexpr uint8_t ITEM_REPEATS = 4;  // entry repeats
constexpr uint8_t ITEM_OVERDUE = 8;  // task list: open occurrence before the reference day
struct DayItem {
  EntryId id;
  Category cat;
  uint8_t flags;
  int16_t start, end;  // minutes; end only for events
  int32_t day;         // occurrence day
};
// All items of one day in display order; writes up to max items starting at
// item `skip`; returns the total number of items that day.
int cal_day_items(int32_t day, DayItem* out, int max, int skip = 0);

// To-do list tasks. Upcoming: per task its first open occurrence on or after
// `ref`. All: also, per task, its first open occurrence before `ref`
// (ITEM_OVERDUE, listed first). Sorted by (overdue first, day, display order).
// Paging: pass the same cursor again to get the next page (cursor->next = 0
// starts at the beginning). Returns the number written; *total gets the size
// of the whole list.
enum class TaskList : uint8_t { UPCOMING = 0, ALL = 1 };
struct TaskCursor { uint64_t next = 0; };
// window > 0 (Settings > Repeating tasks in to-do list: 7 or 30 days): a
// repeating task gives one row per open occurrence in ref .. ref+window-1
// (or, when there is none in the window, its next open day); with ALL also
// one row per open occurrence before ref (oldest first, at most
// TASK_OVERDUE_MAX per task). window 0: one row per task as above.
constexpr int TASK_OVERDUE_MAX = 200;
int cal_tasks(int32_t ref, TaskList mode, TaskCursor* cursor, DayItem* out, int max, int* total, int window = 0);

// Settings > Feb 29 in other years / Day 29-31 in short months (repeat.h).
// Takes effect at once for every query; updates the last days of series
// that end after N occurrences. Call before loading too.
void cal_set_repeat_options(const RepeatOptions& o);
RepeatOptions cal_repeat_options();

struct EntryInfo {
  Category cat;
  int32_t day;      // start date (birthday: birth date)
  bool timed;
  int16_t start, end;
  bool ticked;      // non-repeating task "[x]"
  Rule rule;
  uint8_t nskip, ndone;
};
Err cal_get(EntryId id, EntryInfo* out);
// Title (UTF-8, NUL-terminated; cap >= 241 holds every title). Cached for the
// last few entries shown. *has_notes (optional) = entry has notes.
Err cal_title(EntryId id, char* out, int cap, bool* has_notes = nullptr);
// Notes (cap >= 2001 holds all notes); '\n' = line break.
Err cal_notes(EntryId id, char* out, int cap);
// Skip (done = false) or done (done = true) days of an entry, sorted.
int cal_dates(EntryId id, bool done, int32_t* out, int max);
// Does the entry occur on `day` (rule and skipped days)? Is it ticked there?
bool cal_occurs(EntryId id, int32_t day);
bool cal_is_done(EntryId id, int32_t day);

// ---- edits (each one is saved before the call returns) ---------------------
struct EntryInput {
  Category cat = Category::EVENT;
  int32_t day = 0;
  bool timed = false;      // event: start-end; task: start; birthday/holiday: false
  int16_t start = 0, end = 0;
  bool ticked = false;     // non-repeating task
  Rule rule;               // birthday: Freq::YEARLY by default (Freq::NONE = once)
  const char* title = nullptr;  // required for add; nullptr in update = keep
  const char* notes = nullptr;  // nullptr = keep (add: none)
};
// Checks an input without saving (title length, times, rule, ...).
Err cal_validate(const EntryInput& in);
Err cal_add(const EntryInput& in, EntryId* id = nullptr);
// Changes the whole series; skipped and done days are kept.
Err cal_update(EntryId id, const EntryInput& in, EntryId* new_id = nullptr);
Err cal_delete(EntryId id);
// "Delete only this day": adds a skipped day to a repeating entry.
Err cal_skip_day(EntryId id, int32_t day, EntryId* new_id = nullptr);
// Tick / untick a task occurrence (non-repeating: the [x] state).
Err cal_set_done(EntryId id, int32_t day, bool done, EntryId* new_id = nullptr);

// ---- conversion text <-> in-app ---------------------------------------------
// Lines that a conversion would lose: unknown + over-limit + '#' comment
// lines of the loaded text file (in-app: over-limit + bad records). Ask the
// user when > 0.
uint32_t cal_convert_lost_lines();
using Progress = void (*)(int percent, void* user);
// Writes the calendar in the other storage, reads it back and compares entry
// by entry. On any error the new copy is removed and nothing changed. On
// success both copies exist: save the new setting, then call
// cal_convert_finish() (removes the old copy and loads the new one), or
// cal_convert_cancel() if the setting could not be saved.
Err cal_convert(Storage target, Progress progress = nullptr, void* user = nullptr);
Err cal_convert_finish();
Err cal_convert_cancel();

// RAM used by the calendar core (all static), bytes.
size_t cal_ram_bytes();

}  // namespace planner

#endif
