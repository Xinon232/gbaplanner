// gbaplanner data core: build configuration, limits, error codes.
// Builds for the GBA (devkitARM, -fno-exceptions -fno-rtti, no heap) and for
// the host (tests). Everything the core allocates is static; see
// docs/CORE_API.md for the RAM budget.
#ifndef PLANNER_CORE_CONFIG_H
#define PLANNER_CORE_CONFIG_H

#include <stdint.h>
#include <stddef.h>

// Large zero-initialised buffers go to EWRAM (uninitialised EWRAM section, so
// they take no ROM space). Butano defines BN_EWRAM_BSS_SECTION (".sbss" with
// devkitARM, ".ewram_bss" with Wonderful); without Butano ".sbss" is used,
// which is devkitARM's gba_cart.ld NOLOAD EWRAM section.
#if defined(__arm__) && !defined(PLANNER_HOST)
#  ifdef BN_EWRAM_BSS_SECTION
#    define PLANNER_EWRAM __attribute__((section(BN_EWRAM_BSS_SECTION)))
#  else
#    define PLANNER_EWRAM __attribute__((section(".sbss")))
#  endif
// Hot code (month marks) in IWRAM. The file holding it is named
// *.bn_iwram.cpp so Butano compiles it as ARM code with -mlong-calls.
#  define PLANNER_IWRAM_CODE __attribute__((section(".iwram"), noinline))
// Code that is hot only while loading / saving / opening a day (parser byte
// loop, file hash, index sort, day items). In the app it runs from ROM to
// keep IWRAM for the stack (docs/CORE_API.md "IWRAM gets tight");
// -DPLANNER_LOAD_CODE_IWRAM puts it in IWRAM (the core's timing ROM).
#  ifdef PLANNER_LOAD_CODE_IWRAM
#    define PLANNER_IWRAM_LOAD PLANNER_IWRAM_CODE
#  else
#    define PLANNER_IWRAM_LOAD
#  endif
#else
#  define PLANNER_EWRAM
#  define PLANNER_IWRAM_CODE
#  define PLANNER_IWRAM_LOAD
#endif

namespace planner {

// ---- limits (DESIGN.md "Limits") -------------------------------------------
constexpr int MAX_ENTRIES = 10000;      // calendar entries
constexpr int MAX_TODOS = 300;          // to-do items
constexpr int MAX_TITLE_CHARS = 60;     // UTF-8 code points
constexpr int MAX_NOTES_CHARS = 500;    // UTF-8 code points (a line break counts as 1)
constexpr int MAX_TITLE_BYTES = 4 * MAX_TITLE_CHARS;   // 240
constexpr int MAX_NOTES_BYTES = 4 * MAX_NOTES_CHARS;   // 2000
// Repeat-rule limits.
constexpr int MAX_INTERVAL = 999;       // every N days/weeks/months/years
constexpr int MAX_COUNT = 9999;         // count N
constexpr int MAX_LIST_DATES = 255;     // skip dates per entry, and done dates per entry
// Shared tables (RAM): entries whose rule has an end (until/count), skipped or
// done days, or an interval above 256 use one "rule extension" record; their
// skip/done dates live in one shared date pool.
constexpr int MAX_RULE_EXT = 580;
constexpr int DATE_POOL = 1200;         // dates in all skip/done lists together
// Text files larger than this are refused (index offsets are 23 bits).
constexpr uint32_t MAX_TEXT_FILE_BYTES = (1u << 23) - 1;

enum class Err : uint8_t {
  OK = 0,
  IO,               // FatFs reported an error (read/write/open...)
  DISK_FULL,        // no space left on the card
  NOT_FOUND,        // file (or entry id) does not exist
  CORRUPT,          // stored data failed its check (CRC / hash / format)
  VERIFY,           // a written copy did not read back identically
  RECOVERY_NEEDED,  // leftovers of an interrupted save that cannot be resolved
                    // automatically (data is readable, saving is blocked)
  FULL,             // a RAM table is full (entries, rule extensions, date pool, to-dos)
  INVALID,          // the request is invalid (bad date, title too long, ...)
  TOO_LARGE,        // file too large for the index
  NOT_LOADED,       // call needs a loaded calendar / to-do list
  NEEDS_RELOAD,     // a failed save left RAM and card possibly out of step:
                    // call load() again (it runs the recovery)
};
const char* err_name(Err e);

}  // namespace planner

#endif
