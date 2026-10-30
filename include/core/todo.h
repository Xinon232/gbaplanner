// gbaplanner to-do list: todo.txt or the in-app store (GBAPLANNER.SYS/TODO.DAT).
// Items in display order: open items (list order), then ticked items, most
// recently ticked first. The first N ticked items are shown (N = Settings >
// To-do list > Ticked to-dos shown, 0..5, default 2); the others are
// "hidden ticked to-dos". See docs/CORE_API.md.
#ifndef PLANNER_CORE_TODO_H
#define PLANNER_CORE_TODO_H

#include "calendar.h"

namespace planner {

struct TodoReport {
  uint32_t items = 0;
  uint32_t unknown_lines = 0;  // todo.txt lines that are not "- [ ] ..." / "- [x] ..."
  uint32_t limit_lines = 0;    // items over 300 (kept in the file)
  uint32_t comment_lines = 0;  // lines starting with '#' (kept; a conversion drops them)
  uint32_t empty_lines = 0;
  uint32_t bad_records = 0;    // in-app: slots that failed their check
};
// Ticked to-dos shown: 0..TODO_VISIBLE_TICKED_MAX, default 2.
constexpr int TODO_VISIBLE_TICKED_DEFAULT = 2, TODO_VISIBLE_TICKED_MAX = 5;

Err todo_load(Storage s, TodoReport* report = nullptr);
bool todo_loaded();
Storage todo_storage();
int todo_count();          // all items, hidden ones included
int todo_open_count();
int todo_ticked_count();   // visible + hidden
int todo_hidden_count();   // ticked items after the first N (todo_visible_ticked)
bool todo_is_ticked(int i);
bool todo_is_hidden(int i);
Err todo_title(int i, char* out, int cap);  // cap >= 241 holds every title

// Each change is saved before the call returns. Indexes are display indexes.
Err todo_add(const char* title, int* index = nullptr);  // appended to the open items
Err todo_edit(int i, const char* title);
Err todo_delete(int i);
// How many ticked items are shown (RAM only; core_init keeps it). Values
// outside 0..TODO_VISIBLE_TICKED_MAX are clamped.
void todo_set_visible_ticked(int n);
int todo_visible_ticked();
// After lowering the number: with delete_old the ticked items after the
// first N are deleted (saved like the deletions of todo_tick); without it
// nothing is written (they are hidden). Err::OK when nothing is to do.
Err todo_apply_visible(bool delete_old);

// Ticks an open item: it becomes the most recent ticked item. delete_old
// (Settings > Old ticked to-dos: Delete) deletes the ticked items after the
// first N; otherwise they stay as hidden ticked to-dos.
Err todo_tick(int i, bool delete_old);
Err todo_untick(int i);                 // back to the end of the open items
Err todo_move(int i, int dir);          // open items: swap with i + dir (dir = -1 / +1)
Err todo_delete_hidden();

uint32_t todo_convert_lost_lines();     // unknown + over-limit + '#' lines of todo.txt
Err todo_convert(Storage target, Progress progress = nullptr, void* user = nullptr);
Err todo_convert_finish();
Err todo_convert_cancel();

size_t todo_ram_bytes();
void todo_reset(const char* root);  // called by core_init()

}  // namespace planner

#endif
