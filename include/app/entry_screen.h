#pragma once
// New entry / edit entry (DESIGN.md "New entry / edit entry"): the category
// question, the form, the Custom repeat screen.
#include "app/date.h"
#include "app/screen.h"
#include "core/calendar.h"

namespace app {

// New entry for `day`: asks Event / Task / Birthday / Holiday first.
Screen& new_entry_screen(const Date& day);
// Edit an entry, opened from its occurrence on `day` (Delete only this day,
// Done of a task occurrence). Pushes the form.
void open_entry(planner::EntryId id, const Date& day);

}
