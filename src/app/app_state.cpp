// Encoding of the state copies (pure; tests/app/test_state.cpp) and the
// country names of Settings > Holidays.
#include "app/app_state.h"

#include <cstring>

namespace app {

namespace {
// DESIGN.md order; index 0 is "No country". The holiday table (G3) uses the
// same order: Settings > Holidays stores this index.
constexpr const char* countries[COUNTRY_COUNT + 1] = {
    "No country", "Austria", "Belgium", "Bulgaria", "Croatia", "Cyprus", "Czechia", "Denmark", "Estonia",
    "Finland", "France", "Germany", "Greece", "Hungary", "Ireland", "Italy", "Latvia", "Lithuania",
    "Luxembourg", "Malta", "Netherlands", "Poland", "Portugal", "Romania", "Slovakia", "Slovenia", "Spain",
    "Sweden", "United Kingdom (England & Wales)", "Canada", "USA", "Australia", "New Zealand", "China",
    "Japan"};
constexpr char MAGIC[8] = {'G', 'B', 'P', 'L', 'S', 'T', 'A', 'T'};

void put16(uint8_t* p, unsigned v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void put32(uint8_t* p, uint32_t v) { for(int k = 0; k < 4; ++k) p[k] = uint8_t(v >> (8 * k)); }
unsigned get16(const uint8_t* p) { return p[0] | unsigned(p[1]) << 8; }
uint32_t get32(const uint8_t* p) { return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

template <typename E>
E pick(const uint8_t* payload, int len, int at, int count, E fallback)
{
    return at < len && payload[at] < count ? E(payload[at]) : fallback;
}
}

const char* country_name(int index) { return index >= 0 && index <= COUNTRY_COUNT ? countries[index] : countries[0]; }

bool Settings::same_options(const Settings& o) const
{
    Settings a = *this, b = o;
    a.has_day = b.has_day = false;
    a.day = b.day = Date{};
    uint8_t x[state::FILE_BYTES], y[state::FILE_BYTES];
    state::encode(a, 0, x);
    state::encode(b, 0, y);
    return !std::memcmp(x, y, sizeof(x));
}

namespace state {

uint32_t fnv1a(uint32_t hash, const uint8_t* data, int n)
{
    for(int i = 0; i < n; ++i) { hash ^= data[i]; hash *= 16777619u; }
    return hash;
}

int encode(const Settings& s, uint32_t generation, uint8_t (&out)[FILE_BYTES])
{
    std::memset(out, 0, sizeof(out));
    std::memcpy(out, MAGIC, 8);
    put16(out + 8, VERSION);
    put16(out + 10, PAYLOAD_V1);
    put32(out + 12, generation);
    uint8_t* p = out + HEADER;
    p[0] = uint8_t(s.date_format);
    p[1] = uint8_t(s.week_start);
    p[2] = uint8_t(s.time_format);
    p[3] = s.holidays;
    p[4] = s.event_color;
    p[5] = s.task_color;
    p[6] = s.birthday_color;
    p[7] = s.holiday_color;
    p[8] = uint8_t(s.day_view);
    p[9] = s.day_start_hour;
    p[10] = uint8_t(s.event_length);
    p[11] = uint8_t(s.todo_shows);
    p[12] = uint8_t(s.old_ticked);
    p[13] = s.notes ? 1 : 0;
    p[14] = uint8_t(s.calendar_saved_as);
    p[15] = uint8_t(s.todo_saved_as);
    p[16] = s.has_day ? 1 : 0;
    p[17] = uint8_t(s.day.day);
    p[18] = uint8_t(s.day.month);
    put16(p + 19, unsigned(s.day.year));
    p[21] = uint8_t(s.feb29);
    p[22] = uint8_t(s.short_months);
    p[23] = uint8_t(s.task_window);
    p[24] = s.ticked_shown;
    p[25] = uint8_t(s.start_screen);
    put32(out + HEADER + PAYLOAD_V1, fnv1a(FNV_START, out, HEADER + PAYLOAD_V1));
    return FILE_BYTES;
}

bool decode(const uint8_t* data, int size, Settings& out, uint32_t& generation)
{
    if(size < HEADER + 4 || size > MAX_FILE_BYTES || std::memcmp(data, MAGIC, 8)) return false;
    const unsigned version = get16(data + 8);
    const int len = int(get16(data + 10));
    if(!version || HEADER + len + 4 != size) return false;
    if(get32(data + HEADER + len) != fnv1a(FNV_START, data, HEADER + len)) return false;
    generation = get32(data + 12);
    const uint8_t* p = data + HEADER;
    const Settings d;
    Settings s;
    s.date_format = pick(p, len, 0, 3, d.date_format);
    s.week_start = pick(p, len, 1, 2, d.week_start);
    s.time_format = pick(p, len, 2, 2, d.time_format);
    s.holidays = pick<uint8_t>(p, len, 3, COUNTRY_COUNT + 1, d.holidays);
    s.event_color = pick<uint8_t>(p, len, 4, 8, d.event_color);
    s.task_color = pick<uint8_t>(p, len, 5, 8, d.task_color);
    s.birthday_color = pick<uint8_t>(p, len, 6, 8, d.birthday_color);
    s.holiday_color = pick<uint8_t>(p, len, 7, 8, d.holiday_color);
    s.day_view = pick(p, len, 8, 2, d.day_view);
    s.day_start_hour = pick<uint8_t>(p, len, 9, 24, d.day_start_hour);
    s.event_length = pick(p, len, 10, 2, d.event_length);
    s.todo_shows = pick(p, len, 11, 4, d.todo_shows);
    s.old_ticked = pick(p, len, 12, 3, d.old_ticked);
    s.notes = pick<uint8_t>(p, len, 13, 2, d.notes ? 1 : 0) != 0;
    s.calendar_saved_as = pick(p, len, 14, 2, d.calendar_saved_as);
    s.todo_saved_as = pick(p, len, 15, 2, d.todo_saved_as);
    s.feb29 = pick(p, len, 21, 3, d.feb29);
    s.short_months = pick(p, len, 22, 2, d.short_months);
    s.task_window = pick(p, len, 23, 3, d.task_window);
    s.ticked_shown = pick<uint8_t>(p, len, 24, 6, d.ticked_shown);
    s.start_screen = pick(p, len, 25, 2, d.start_screen);
    if(len >= 21 && p[16] == 1) {
        Date day{int16_t(get16(p + 19)), int8_t(p[18]), int8_t(p[17])};
        if(valid_date(day)) { s.has_day = true; s.day = day; }
    }
    out = s;
    return true;
}

}
}
