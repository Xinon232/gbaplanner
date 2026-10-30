// gbaplanner country holidays: lookup into the generated table.
// See include/holidays/holidays.h and docs/HOLIDAYS.md.

#include "holidays/holidays.h"

#include "holiday_data.h"

static_assert(HOL_FIRST_YEAR == HOLIDAYS_FIRST_YEAR, "year range");
static_assert(HOL_LAST_YEAR == HOLIDAYS_LAST_YEAR, "year range");

namespace
{

// Days before the first of each month in a common year.
const uint16_t k_month_start[13] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};

// The code avoids divisions: the GBA's CPU has none, and in Thumb mode even
// a division by a constant is a library call. Weekday of January 1st, Easter
// and leap years come from hol_year_info.

// x % 7 for 0 <= x < 13000 without a division ((x * 9363) >> 16 == x / 7).
inline int mod7(int x)
{
    return x - 7 * ((x * 9363) >> 16);
}

struct YearInfo
{
    int jan1_wd;   // 0 = Monday
    int leap;      // 1 in leap years
    int easter;    // day of year of Western Easter Sunday
    int orthodox;  // day of year of Orthodox Easter Sunday (Gregorian)
};

YearInfo year_info(int year)
{
    unsigned v = hol_year_info[year - HOL_FIRST_YEAR];
    YearInfo yi;
    yi.jan1_wd = v & 7;
    yi.leap = (v >> 15) & 1;
    int march22 = 31 + 28 + yi.leap + 22;
    yi.easter = march22 + ((v >> 3) & 63);
    yi.orthodox = march22 + ((v >> 9) & 63);
    return yi;
}

// Day of year (1-based) of month/day.
inline int day_of_year(const YearInfo& yi, int m, int d)
{
    return k_month_start[m - 1] + d + (m > 2 ? yi.leap : 0);
}

inline int days_in_month(const YearInfo& yi, int m)
{
    return k_month_start[m] - k_month_start[m - 1] + (m == 2 ? yi.leap : 0);
}

// Day of year of a rule in a year, 0 if the rule gives no day that year.
int rule_doy(const HolRule& r, const YearInfo& yi)
{
    switch(r.kind)
    {
    case HOL_K_FIXED:
        if(r.p0 == 2 && r.p1 == 29 && !yi.leap)
        {
            return 0;
        }
        return day_of_year(yi, r.p0, r.p1);
    case HOL_K_FIXED_NOT_SUNDAY:
    {
        if(r.p0 == 2 && r.p1 == 29 && !yi.leap)
        {
            return 0;
        }
        int doy = day_of_year(yi, r.p0, r.p1);
        return mod7(yi.jan1_wd + doy - 1) == 6 ? 0 : doy;
    }
    case HOL_K_EASTER:
        return yi.easter + static_cast<int8_t>(r.p0);
    case HOL_K_ORTHODOX:
        return yi.orthodox + static_cast<int8_t>(r.p0);
    case HOL_K_WEEKDAY:
    {
        int a = day_of_year(yi, r.p0, r.p1);
        int ahead = r.p2 - mod7(yi.jan1_wd + a - 1);
        return a + (ahead < 0 ? ahead + 7 : ahead);
    }
    default:
        return 0;
    }
}

// Could a rule fall into [first, last] (days of year)? Cheap pre-check that
// avoids the date arithmetic for most rules of a month query.
bool rule_may_hit(const HolRule& r, int month)
{
    switch(r.kind)
    {
    case HOL_K_FIXED:
    case HOL_K_FIXED_NOT_SUNDAY:
        return r.p0 == month;
    case HOL_K_WEEKDAY:
        return r.p0 == month || (r.p0 + 1 == month && r.p1 >= 22);
    default:
        return true;
    }
}

bool valid(int country, int year)
{
    return country >= 1 && country <= HOL_NCOUNTRIES && year >= HOL_FIRST_YEAR &&
           year <= HOL_LAST_YEAR;
}

// Calls f(doy, name_index) for every holiday of the country-year whose day
// of year lies in [first, last]. month (1..12) enables the pre-check, 0 = off.
template<typename F>
void for_each(int country, int year, int first, int last, int month, F&& f)
{
    int c = country - 1;
    YearInfo yi = year_info(year);
    for(int k = hol_rule_start[c], end = hol_rule_start[c + 1]; k < end; ++k)
    {
        const HolRule& r = hol_rules[k];
        if(year < r.first || year > r.last || (month && !rule_may_hit(r, month)))
        {
            continue;
        }
        int doy = rule_doy(r, yi);
        if(doy >= first && doy <= last)
        {
            f(doy, r.name);
        }
    }
    static_assert((HOL_BLOCK & (HOL_BLOCK - 1)) == 0, "block size is a power of two");
    unsigned y = static_cast<unsigned>(year - HOL_FIRST_YEAR);
    int pos = hol_x_block[c * HOL_NBLOCKS + y / HOL_BLOCK];
    const uint8_t* counts = hol_x_count + c * HOL_NYEARS;
    for(unsigned i = y & ~static_cast<unsigned>(HOL_BLOCK - 1); i < y; ++i)
    {
        pos += counts[i];
    }
    for(int k = pos, end = pos + counts[y]; k < end; ++k)
    {
        int doy = hol_x_doy[k] | ((hol_x_name[k] >> 15) << 8);
        if(doy > last)
        {
            break;  // sorted by day of year
        }
        if(doy >= first)
        {
            f(doy, hol_x_name[k] & 0x7fff);
        }
    }
}

}  // namespace

int holidays_country_count()
{
    return HOL_NCOUNTRIES + 1;
}

const char* holidays_country_name(int country)
{
    if(country < 0 || country > HOL_NCOUNTRIES)
    {
        return nullptr;
    }
    return hol_country_names[country];
}

const char* holidays_country_code(int country)
{
    if(country < 0 || country > HOL_NCOUNTRIES)
    {
        return nullptr;
    }
    return hol_country_codes[country];
}

int holidays_on_day(int country, int year, int month, int day, HolidayItem* out, int max)
{
    if(!valid(country, year) || month < 1 || month > 12 || day < 1)
    {
        return 0;
    }
    YearInfo yi = year_info(year);
    if(day > days_in_month(yi, month))
    {
        return 0;
    }
    int doy = day_of_year(yi, month, day);
    uint16_t found[HOL_MAX_PER_DAY];
    int n = 0;
    for_each(country, year, doy, doy, month, [&](int, int name) {
        if(n < HOL_MAX_PER_DAY)
        {
            // Insertion sort by name index: a stable order for the day view.
            int i = n++;
            while(i > 0 && found[i - 1] > name)
            {
                found[i] = found[i - 1];
                --i;
            }
            found[i] = static_cast<uint16_t>(name);
        }
    });
    for(int i = 0; i < n && i < max; ++i)
    {
        out[i].name = hol_name_chars + hol_name_off[found[i]];
    }
    return n;
}

int holidays_in_month(int country, int year, int month, uint8_t counts[31], uint32_t* day_mask)
{
    for(int i = 0; i < 31; ++i)
    {
        counts[i] = 0;
    }
    if(day_mask)
    {
        *day_mask = 0;
    }
    if(!valid(country, year) || month < 1 || month > 12)
    {
        return 0;
    }
    YearInfo yi = year_info(year);
    int first = day_of_year(yi, month, 1);
    int last = first + days_in_month(yi, month) - 1;
    int total = 0;
    uint32_t mask = 0;
    for_each(country, year, first, last, month, [&](int doy, int) {
        int d = doy - first;
        ++counts[d];
        mask |= 1u << d;
        ++total;
    });
    if(day_mask)
    {
        *day_mask = mask;
    }
    return total;
}
