#pragma once
// The app's screens. The calendar is the bottom of the stack; the others
// are opened from it with screens::push() (the to-do list also at start-up
// when Settings > General > Start screen is To-do list).
#include "app/date.h"
#include "app/day_screen.h"
#include "app/entry_screen.h"
#include "app/screen.h"
#include "app/todo_screen.h"

namespace app {

Screen& calendar_screen();
Screen& settings_screen();

}
