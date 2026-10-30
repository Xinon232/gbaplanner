// The calendar's dots: country holidays first, then the data core's month
// marks (own holidays, birthdays, events and tasks by time), resolved to the
// colours of Settings. Called once per shown month (never while years
// scroll); the core needs up to ~100 ms for 10,000 entries.
#include "app/day_marks.h"

#include "app/canvas.h"
#include "app/data.h"
#include "holidays/holidays.h"

extern "C" {
// EWRAM .sbss: zero at start; only the emulator tests write it.
__attribute__((section(".sbss"), used)) app::TestMarks planner_test_marks;
}

namespace app {

namespace {
APP_EWRAM planner::DayMarks core_marks[planner::MAX_WINDOW];

void add(CellMarks& m, uint8_t colour, bool hollow)
{
    if(m.count < DAY_MARK_COLORS) {
        m.color[m.count] = uint8_t(colour & 7);
        if(hollow) m.hollow_mask = uint8_t(m.hollow_mask | (1u << m.count));
    }
    if(m.count < 255) ++m.count;
}
}

uint32_t test_marks_serial()
{
    return planner_test_marks.magic == TestMarks::MAGIC ? planner_test_marks.serial : 0;
}

void day_marks(const Date& first, int count, CellMarks* out)
{
    for(int i = 0; i < count; ++i) out[i] = CellMarks();
    if(count > planner::MAX_WINDOW) count = planner::MAX_WINDOW;
    const int32_t base = day_number(first);
    // Country holidays, month by month over the window.
    const int country = data::country();
    if(country) {
        const uint8_t hc = uint8_t(data::category_color(planner::Category::HOLIDAY) - color::CATEGORY);
        Date d = first;
        for(int at = 0; at < count;) {
            uint8_t counts[31];
            holidays_in_month(country, d.year, d.month, counts, nullptr);
            const int last = days_in_month(d.year, d.month);
            for(int day = d.day; day <= last && at < count; ++day, ++at)
                for(int k = 0; k < counts[day - 1]; ++k) add(out[at], hc, false);
            if(d.month == 12) { if(d.year >= YEAR_MAX) break; d = Date{int16_t(d.year + 1), 1, 1}; }
            else d = Date{d.year, int8_t(d.month + 1), 1};
        }
    }
    // Entries.
    if(data::calendar_ready()) {
        planner::cal_marks(base, count, core_marks);
        for(int i = 0; i < count; ++i) {
            const planner::DayMarks& m = core_marks[i];
            CellMarks& c = out[i];
            for(int k = 0; k < m.shown; ++k) {
                const auto cat = planner::Category(m.item[k] & planner::MARK_CATEGORY);
                add(c, uint8_t(data::category_color(cat) - color::CATEGORY), m.item[k] & planner::MARK_TICKED);
            }
            // Items beyond the core's 8 still count (6 dots and '+').
            const int more = int(m.total) - int(m.shown);
            for(int k = 0; k < more && c.count < 255; ++k) ++c.count;
        }
    }
    // Test hook.
    if(planner_test_marks.magic != TestMarks::MAGIC) return;
    uint32_t n = planner_test_marks.count;
    if(n > uint32_t(TestMarks::MAX)) n = TestMarks::MAX;
    for(uint32_t k = 0; k < n; ++k) {
        const TestMark& m = planner_test_marks.marks[k];
        const Date d{m.year, int8_t(m.month), int8_t(m.day)};
        if(!valid_date(d)) continue;
        const int32_t at = day_number(d) - base;
        if(at < 0 || at >= count) continue;
        add(out[at], m.color, m.hollow);
    }
}

}
