#pragma once
// To-do list (DESIGN.md "To-do list"), Start on the calendar (or the start
// screen: Settings > General > Start screen); Start goes back to it.
#include "app/date.h"
#include "app/screen.h"

namespace app {

// The list from `reference` (the calendar cursor's day; there is no clock).
Screen& todo_screen(const Date& reference);
// Settings > Delete hidden to-dos is shown only when there are any.
bool hidden_todos_exist();

}
