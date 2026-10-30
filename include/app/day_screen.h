#pragma once
// Day view (DESIGN.md "Day view"): timeline (default) or list.
#include "app/date.h"
#include "app/screen.h"

namespace app {

Screen& day_screen(const Date& day);

}
