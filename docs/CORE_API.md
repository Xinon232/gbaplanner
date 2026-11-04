# gbaplanner data core — API for the screens (G4)

The core in `src/core/` + `include/core/` holds the calendar and the to-do
list: date math, repeat rules, the RAM index, the queries the screens need,
edits, both storages (text files and the in-app store) and the conversion
between them. It is plain C++20/23 without heap, exceptions, RTTI or iostream;
all memory is static. It talks to the card only through FatFs.

## 1. Building it into the app

- Add `src/core/*.cpp` to the Butano `SOURCES`, `include/` and `src/core/`
  to the include paths. The app's `ff.h`/`ffconf.h` are used (needs
  `FF_FS_MINIMIZE 0` and LFN, for `GBAPLANNER.SYS`; tested with gbawriter's
  ffconf; gbaplanner builds gbaSD's FatFs, `gbasd/fatfs`, with
  `include/fatfs/ffconf.h`, and so do the host tests).
- IWRAM placement in the app (G4, to keep 12 KB of IWRAM for the user
  stack): `cal_marks.bn_iwram.cpp` is compiled as ARM with `-mlong-calls`;
  `cal_marks` (the month marks kernel) carries `PLANNER_IWRAM_CODE`
  (`section(".iwram")`). The parser's `feed_block` / `title_byte`
  (`cal_parse.cpp`) and the index sort's `sift` (`hot.cpp`) are in IWRAM as
  Thumb code. Code marked `PLANNER_IWRAM_LOAD` (the rest of the parser, the
  file hash, `idx_sort`, the marks window setup `prepare`, `cal_day_items`)
  runs from ROM; `-DPLANNER_LOAD_CODE_IWRAM` puts it in IWRAM too. Measured
  on mGBA (exact ROM): 10,000-entry calendar.txt start-up 11.9 s with this
  placement vs. 15.2 s with the whole parser and sort in ROM (10.4 s with
  everything in IWRAM, which leaves too little stack).
- Big tables use `PLANNER_EWRAM` = `section(BN_EWRAM_BSS_SECTION)` (or
  `.sbss`): EWRAM, no ROM image. On the host the macros are empty.
- `planner_config.h` has all limits.

## 2. Start-up

```cpp
#include "core/calendar.h"
#include "core/todo.h"
using namespace planner;

// FatFs volume already mounted by the app (gbaSD).
core_init("/gbaplanner");          // paths only, resets RAM state
core_make_dirs();                  // creates /gbaplanner and GBAPLANNER.SYS
// The app calls core_repair_dir("/gbaplanner/GBAPLANNER.SYS") right after
// mounting, before f_mkdir (exFAT: entry sets a power cut left half written
// in the root, /gbaplanner and GBAPLANNER.SYS, see "Safe save of a whole
// file"), then gbaSD's gbasd_alloc_hint() for GBAPLANNER.SYS, or for
// /gbaplanner when that folder is not there yet (gbasd/fatfs/alloc_hint.h):
// FatFs' search for a free cluster starts after the app's own files, not at
// the start of the card (exFAT has no stored hint: on a nearly full 2 TiB
// card the first save after a start read the whole allocation bitmap).
LoadReport rep;
Err e = cal_load(settings.cal_in_app ? Storage::IN_APP : Storage::TEXT, &rep);
TodoReport trep;
e = todo_load(settings.todo_in_app ? Storage::IN_APP : Storage::TEXT, &trep);
```

`cal_load` / `todo_load` first finish or undo any interrupted save
(recovery), then read the file. A missing file is an empty calendar / list.
`rep.unknown_lines` is the number to report ("N lines not understood");
`rep.limit_lines` are valid lines over a RAM limit (kept in the file, not
shown). On an error nothing is loaded; show the error, the file is untouched.

## 3. Queries

All take day numbers (`int32_t`, days since 1900-01-01; see `date.h`:
`day_from_ymd`, `ymd_from_day`, `weekday` (0 = Monday), `add_months`
(clamped), `format_date(day, DateFormat, buf)`, `format_time(min, TimeFormat, buf)`).

| Call | Use |
|---|---|
| `cal_marks(first_day, ndays, DayMarks out[])` | Calendar grid (ndays ≤ 56, use 42). Per day: `total`, `shown` (≤ 8), `item[i]` = category (bits 0-1) + `MARK_TICKED`. Order: holidays, birthdays, events/tasks (all-day first, then by start time, ties in file order). Country holidays (G3) are not included: put them in front and reduce the shown items yourself. Rule: 7 dots, or 6 + `+` when total > 7. |
| `cal_day_items(day, DayItem out[], max, skip)` | Day view, same order; returns the total; page with `skip`. `DayItem`: id, cat, flags (`ITEM_TIMED`, `ITEM_TICKED`, `ITEM_REPEATS`), start, end (minutes), day. |
| `cal_tasks(ref, TaskList::UPCOMING/ALL, &cursor, out, max, &total, window = 0)` | To-do screen "Upcoming tasks": per task its first open occurrence ≥ ref; `ALL` adds per task its first open occurrence < ref (`ITEM_OVERDUE`, listed first). With `window` 7 / 30 (Settings > Repeating tasks in to-do list) a repeating task gives one row per open occurrence in ref .. ref+window-1 (none there: its next open day), and `ALL` one row per open overdue day (at most `TASK_OVERDUE_MAX` = 200 per task). Sorted by (overdue, day, display order); at most 64 per call — keep the `TaskCursor` to get the next page. |
| `cal_set_repeat_options(RepeatOptions)` | Settings > Feb 29 in other years (`Feb29::SKIP/FEB28/MAR1`) and Day 29–31 in short months (`ShortMonth::SKIP/LAST_DAY`). Global, read by every query (`g_repeat`, repeat.h); also recomputes the last day of "after N occurrences" series. Call before `cal_load` and on every change. |
| `cal_get(id, &EntryInfo)` | Fields + rule for the entry form. |
| `cal_title(id, buf, 241, &has_notes)` | Title from the card (4-entry cache); `has_notes` for the `≡`. |
| `cal_notes(id, buf, 2001)` | Notes, `\n` = line break. |
| `cal_dates(id, done, out, max)` | Skipped / done days. |
| `cal_occurs(id, day)`, `cal_is_done(id, day)` | Single checks. |

`EntryId` is the position in the RAM index. It stays valid until the next
call that changes the calendar; every changing call returns the new id of
the entry it touched (other ids may shift). Re-query after an edit.

Birthday age: `year(shown day) - year(EntryInfo.day)` when ≥ 0.

## 4. Edits (each one is on the card when the call returns)

```cpp
EntryInput in;            // cat, day, timed, start, end, ticked, rule, title, notes
in.cat = Category::EVENT; in.day = d; in.timed = true; in.start = 9*60; in.end = 10*60;
in.rule.freq = Freq::WEEKLY; in.rule.weekdays = 1<<5 | 1<<6;   // sa, su
in.rule.end = EndKind::UNTIL; in.rule.until = day_from_ymd(2026,12,31);
in.title = "Trip"; in.notes = nullptr;
EntryId id; Err e = cal_add(in, &id);
```

| Call | Notes |
|---|---|
| `cal_validate(in)` | Check before saving (title 1–60 chars, notes ≤ 500, end > start, times 0..1439, rule). |
| `cal_add(in, &id)` | `title` required, `notes` may be null. Appended to calendar.txt. |
| `cal_update(id, in, &new_id)` | Whole series. `title`/`notes` null = keep the stored text (so the form need not load notes when Notes is Off). Skipped/done days are kept (done only for tasks; dropped when the rule becomes "does not repeat"). |
| `cal_delete(id)` | |
| `cal_skip_day(id, day, &new_id)` | "Delete only this day" (repeating entries; `day` must be an occurrence). |
| `cal_set_done(id, day, done, &new_id)` | Tick/untick a task occurrence; non-repeating: the `[x]`. |

Rule fields (`repeat.h`): `freq` NONE/DAILY/WEEKLY/MONTHLY/YEARLY,
`interval` 1..999, `weekdays` (weekly; bit 0 = Monday, 0 = start's weekday),
`mmode` START_DAY / DAY_N (`mday`) / NTH_WEEKDAY (`nth` 1..5, `mweekday`) /
LAST_WEEKDAY (`mweekday`), `end` NEVER/UNTIL (`until`)/COUNT (`count` 1..9999).
Birthdays: pass `Freq::YEARLY` for the default; `Freq::NONE` = once.

Text storage rewrites and verifies the whole file per edit (10,000 entries:
~585 KB written + read back); in-app storage writes one data record and two
small patches.

On `Err::NEEDS_RELOAD` call `cal_load` again (it runs the recovery; the card
then holds the old or the new state). All other errors: nothing changed.

## 5. To-do list (`todo.h`)

Display index `i`: `0 .. todo_open_count()-1` open (list order), then ticked
(most recent first). `todo_is_hidden(i)` = ticked beyond the first N, N =
`todo_visible_ticked()` (Settings > To-do list > Ticked to-dos shown, 0–5,
default `TODO_VISIBLE_TICKED_DEFAULT` = 2; `todo_set_visible_ticked(n)` sets
it in RAM, clamped to 0..`TODO_VISIBLE_TICKED_MAX`; `core_init` keeps it).

`todo_add(title)` (end of the open items), `todo_edit(i, title)`,
`todo_delete(i)`, `todo_tick(i, delete_old)` (delete_old = setting "Old
ticked to-dos: Delete"), `todo_untick(i)` (to the end of the open items),
`todo_move(i, ±1)` (open items), `todo_delete_hidden()`, `todo_title(i, buf, 241)`,
`todo_hidden_count()`. `todo_tick(i, true)` deletes the ticked items after
the first N. `todo_apply_visible(delete_old)` after lowering N: with
delete_old the ticked items after the first N are deleted and saved exactly
as the same number of ticks would delete them; without it nothing is
written (they are hidden). Titles 1–60 characters. Max 300 items.

## 6. Conversion (Settings > … saved as)

```cpp
uint32_t lost = cal_convert_lost_lines();    // > 0: "N lines not understood. They will be lost."
Err e = cal_convert(Storage::IN_APP, progress_cb, user);   // percent 0..100
if (e == Err::OK) {
  if (save_setting(new_value)) cal_convert_finish();      // removes the old copy, loads the new one
  else cal_convert_cancel();                              // removes the new copy
} // else: show err, setting unchanged, nothing changed on the card
```

`cal_convert` writes the new copy (safe save), reads it back and compares
every entry (fields, rule, skip/done days, title, notes) with the source;
only then returns OK. Lost lines = unknown + over-limit + `#` comment lines
(comments are not entries, a conversion cannot keep them). Same for
`todo_convert` / `todo_convert_finish` / `todo_convert_cancel` (lost =
unknown + over-300 + `#` lines). Hidden ticked to-dos are carried over.
If the power fails between `cal_convert` and `cal_convert_finish`, both
copies exist and the saved setting decides; the stale copy is overwritten by
the next conversion.

## 7. Error codes (`Err`, `err_name(e)` gives a short name)

`OK`, `IO` (FatFs error), `DISK_FULL`, `NOT_FOUND`, `CORRUPT` (check failed),
`VERIFY` (read-back differed; nothing changed), `RECOVERY_NEEDED`
(leftovers of an interrupted save that cannot be resolved automatically —
not produced by any state the tests reach), `FULL` (RAM table full: 10,000
entries, 580 rule extensions, 1,200 skip/done days, 255 per list, 300
to-dos), `INVALID`, `TOO_LARGE` (text file > 8 MB), `NOT_LOADED`,
`NEEDS_RELOAD`.

## 8. File formats

### calendar.txt / todo.txt
Exactly DESIGN.md. Details of this implementation:
- Keywords, categories and weekdays are accepted in any case; written lower case.
- `all-day` accepted in the time position; events need `HH:MM-HH:MM`
  (end > start, 23:59 max), tasks `HH:MM`, birthdays/holidays no time.
- `[ ]`/`[x]`/`[X]` only for tasks, optional (missing = open).
- Repeat clauses in any order, each once; `until` and `count` exclusive;
  `skip`/`done` need a repeat; `done` only for tasks; lists are sorted and
  de-duplicated in RAM (a rewritten line writes them sorted).
- Birthday lines without a repeat part are yearly; a non-repeating birthday is
  written `| none` (extension of the grammar; `none` is accepted for every
  category).
- Escapes: `\n`, `\\`, `\|`, `\/`; other `\x` are a literal backslash. The
  writer escapes `|` when it stands between spaces (or at the start / end of
  the text) and `//` likewise, i.e. exactly where it could be taken for a
  separator.
- Lines with a title over 60 or notes over 500 characters, or a control
  character in the title, are "not understood" (kept verbatim).
- Only the changed line is regenerated; all other bytes (BOM, CRLF, comments,
  unknown lines, missing final newline) are copied. New lines use the file's
  first line ending (LF for a new file) and go to the end.
- todo.txt: item lines `- [ ] text` / `- [x] text`; a save fills the item
  lines of the old file, in order, with the items in display order (unknown
  lines stay at their place), extra items are appended, unused item lines
  are dropped.

### Safe save of a whole file (text files, in-app compaction / conversion)
gbawriter's scheme with 8.3 sibling names (one directory entry each):
`calendar.gpn` staging copy → sync → read back, compare size + 2 hashes →
`calendar.gpj` manifest (identities of old and new copy) → rename
`calendar.txt` → `calendar.gpb` → rename staging → `calendar.txt` → read back
→ remove backup and manifest. Recovery (on every load and before every save)
resolves every intermediate state, including an interrupted FatFs rename
that leaves two names on one cluster chain (the extra name is detached:
its directory entry is pointed at no cluster, then unlinked, so the chain is
not freed).

On exFAT a name is an entry set (file + stream + name entries, 96 bytes for
these names) that FatFs writes sector by sector, so a power cut can leave a
set that crosses a sector boundary half written; FatFs then refuses the
whole folder (FR_INT_ERR). Recovery (`safe_recover`, `journal_recover`)
first repairs the folder with `core_repair_dir()`, a thin wrapper of the
standalone C module `exfat_repair_dir()` (gbaSD's
`gbasd/fatfs/exfat_repair.c` / `.h`, shared by the GBA apps; it
repairs the root, the folders on the way, then the folder): a set whose
later entries were never written (unfinished create / rename) is freed, a
complete set with a wrong checksum (unfinished update of its stream entry)
gets the matching checksum, stream / name entries left without their file
entry (unfinished delete) are freed; anything else is left alone. The app
also calls it for GBAPLANNER.SYS right after mounting. On FAT it does
nothing.

### In-app calendar: `GBAPLANNER.SYS/CAL.DAT` + `CAL.JRN`
```
0        header 64 B: "GBPCAL01", version 1, capacity (record numbers,
         1..10000), meta offset 512, meta size 20, data offset
         (512 + 20 x capacity, rounded up to 512), hw (records used),
         data_end, garbage bytes, live count, CRC32
512      meta records, 20 B each, record r at 512 + 20 r:
         w0 day17|cat2|timed|ticked|start11, w1 end11|freq3|spec8|endkind2|used|lists,
         w2 interval10|until-or-count17, u32 data offset, u16 data length, CRC16(seed r)
DATA     data records (appended): title, 0, notes, 0, skip days, done days
         (3 B each), nskip, ndone, 0, 0, CRC32(r + bytes)
```
An edit appends the new data record after `data_end` (invisible until the
header moves), syncs, then writes meta record + header through the redo
journal `CAL.JRN` (16-byte header with CRC, patches `{offset, len, bytes}`;
written and synced before the target is touched, replayed on load, then
removed). Delete = meta `used = 0` (journaled). Load reads header + 20 B per
record (10,000 entries: 200 KB) and the data records of entries with
skip/done days only. The capacity follows the number of entries: a new or
converted or compacted file gets `cap_for(live)` = 1.5 x live + 64,
rounded up to a multiple of 128, at most 10,000 (20 entries: 128 record
numbers, a 3.6 KB file; before, every file reserved 200 KB of meta
records). When the record numbers run out the file is compacted (safe save)
into a larger capacity and reloaded, so adding grows the file on demand
with the usual crash safety; at 10,000 live entries the add is FULL. On load
it is compacted when garbage > 512 KB and > live data. Files written with
the fixed capacity 10,000 are the same format and load unchanged.

Measured on mGBA, SuperCard model, exact ROM (typical mix; times from the
answer Yes to Settings again, start-up to the calendar incl. 0.5 s SD
check):

| entries | text -> in-app | in-app -> text | start-up in-app | start-up text |
|---|---|---|---|---|
| 20 | 0.6 s | 0.4 s | 0.5 s | 0.5 s |
| 500 | 4.9 s | 3.5 s | 0.75 s | 1.0 s |
| 10,000 | 94 s | 71 s | 4.8 s | 11.8 s |

A conversion parses the text several times, writes, reads back and compares
(500 entries: about 510 sector reads and 90 writes); the SD card time
dominates. TODO.DAT needs no such change: it holds only the used slots
(header + 256 B per to-do, written on demand; 2 to-dos = 1 KB, the first
to-do takes about 0.17 s).

### In-app to-dos: `GBAPLANNER.SYS/TODO.DAT` + `TODO.JRN`
512-byte header ("GBPTODO1", CRC), then up to 300 slots × 256 B (only as
many as used; the file grows by journaled slot writes): CRC32 (slot +
bytes 4..255), flags (used, ticked), title length, order key, title. Open
items by key ascending, ticked by key descending (tick sequence). Each
change writes the touched slots through the journal.

## 9. RAM (all static; GBA sizes)

| Item | Bytes |
|---|---|
| index 10,000 × 12 B (EWRAM) | 120,000 |
| rule extensions 580 × 10 B (EWRAM) | 5,800 |
| skip/done date pool (1,200 + 510 parse scratch) × 3 B (EWRAM) | 5,130 |
| month-marks table (EWRAM) | 2,048 |
| title cache 4 × 252 B (EWRAM) | 1,008 |
| to-do items 301 × 8 B (EWRAM) | 2,408 |
| 3 FatFs FIL × 592 B + 3 × 128 B buffers (EWRAM) | 2,160 |
| state, paths, slot buffer (IWRAM .bss) | 1,260 |
| **total** | **139,814** (EWRAM 138,556 + IWRAM 1,260), limit 143,360 |

Stack: `cal_marks` uses ~1 KB, the rest less. The FATFS object is the app's.

## 10. Performance

mGBA (16.78 MHz, WAITCNT 0x4317 as Butano), 10,000 entries:

| | ms |
|---|---|
| month marks, typical mix (30 % birthdays, 8 % holidays, 47 % one-off, 15 % weekly) | 64 (≈ 6 ms per 1,000 entries) |
| month marks, all daily / weekly 3 days / monthly 2nd Fr / yearly / monthly day 31 | 86 / 109 / 101 / 95 / 103 |
| with the repeat options (G4 bench, `prepare` from ROM): all monthly 5th Fr → last / day 31 → last day | 107 / 100 |
| all 10,000 yearly from Feb 29, one grid with that day: Feb 28 option (common year) / no option (leap year) | 156 / 152 (every entry lands on the same day: the pathological case, about the same with or without the option) |
| day items (one day) | 60 |
| task list page | 20 |
| index sort after loading | 1,300 |
| parser | 179 cycles per byte (≈ 6 s CPU for a 585 KB calendar.txt, plus card reads) |

Host (RAM disk) sector counts for 10,000 entries: text load 1,163 sectors
read; text edit 3,479 written / 5,799 read; in-app load 1,351 read; in-app
edit 16 written / 51 read; text → in-app conversion 3,350 written.

Compute the marks once when the shown month changes (DESIGN: "dots are
computed when scrolling stops"), not per frame.

## 11. Decisions on points DESIGN.md leaves open

- Repeat semantics = python-dateutil `rrule` (WKST = Monday always, so the
  "week starts on" setting changes only the display; the start date counts
  only if it matches; monthly day 29–31 and 5th weekday skip months; Feb 29
  yearly and Feb 29 birthdays only in leap years; skipped days still use up
  `count`). Verified on 8,000 random rules.
- Repeat options (G4 feature round): with Feb 29 = 28 Feb / 1 Mar a yearly
  series from Feb 29 occurs on that day in common years; with Day 29–31 =
  last day, monthly on day N (also START_DAY from day 29–31) uses the last
  day of a shorter month and a missing 5th weekday becomes the last one.
  Per period (year / month of the interval) the Skip candidate is computed,
  a missing one is replaced, then days before the start are dropped and
  until / count apply — so `count` counts moved days and no period gains a
  second occurrence (a single-day rule cannot collide; Mar 1 never meets a
  Feb 29 occurrence of the same series). A moved day may lie on the start
  date's other side only when the start itself is a moved day's candidate
  (e.g. monthly day 31 starting Apr 30 occurs on Apr 30). Verified against an
  independent brute-force reference (tests/core/gen_fallback_cases.py,
  itself equal to dateutil in skip mode) for all six option sets, and
  test_options.cpp (day items, marks, task windows, skip/done, reload).
  In the month-marks kernel the options only change the tables built per
  query (`prepare`, from ROM); the per-entry loop is unchanged.
- Task list: one line per task series (its first open occurrence on/after
  the reference day; with "all tasks" also its first open overdue one).
- `[x]` on a repeating task is kept but has no effect (done days decide).
- Ticked to-dos: untick puts the item at the end of the open list; new
  to-dos go to the end of the open list.
- Lines over the RAM limits are kept in the file and counted as
  `limit_lines`; a later delete may make them load.
- A conversion warns about (and drops) `#` comment lines too.
- Editing the date of a repeating entry keeps its skip/done days.
- Text-file size limit 8 MB (23-bit offsets).

## 12. Known gaps

- Month marks worst case is ~100 ms (target "well under 100 ms") and the
  10,000-entry typical case ~4 frames (target ~1 frame); small calendars are
  proportionally faster (500 entries ≈ 3 ms).
- Loading 10,000 entries from calendar.txt costs ~6 s CPU + card time;
  in-app load is much faster (no parsing).
- exFAT: tested on modeled cards only (tests/exfat: RAM disk and mGBA).
- Only modelled (RAM disk / mGBA) – no real hardware.

## 13. Tests

`tests/core/run.sh` (`quick` skips fault injection and the timing ROM):
date math (all 109,938 days), dateutil oracle (2 × 4,000 rules), calendar.txt
grammar/escapes/byte-exact rewrites, calendar behaviour on both storages vs
brute force, to-dos, conversions, 10,000-entry load/save/convert, fault
injection (power cut at every sector write, failing write/read at every step,
full card; fsck after every recovery), GBA cross-build (C++20 + C++23) and
the mGBA timing ROM (`tests/core/gba/`).

`tests/exfat/run.py` runs the same core tests (and the fault injection, with
fsck.exfat after every recovery) on an exFAT RAM disk made by FatFs' own
`f_mkfs` (FM_EXFAT), once with contiguous files and once with the free space
fragmented (FAT chains), plus `tests/core/test_exfat.cpp` (torn exFAT entry
sets repaired) and gbaSD's `gbasd/tests/fatfs/stretch_cut.py` (a power cut at
each sector write while /gbaplanner grows by a cluster, fragmented and
contiguous: the folder reads, fsck.exfat is clean, the listing matches
`exfat_read.py`, the folder grows again) and `growth_cut.py` (the same on
FAT12/16/32 and exFAT), both with this app's ffconf.h; `--emu` adds the
emulator tests on exFAT cards (see tests/emu/README.md). FatFs is gbaSD's
(`gbasd/fatfs`, R0.15 with its changes marked `gbaSD:`, see
`gbasd/docs/fatfs.md`): on exFAT a new fragment of a chain is ended in the
FAT before it is linked, a sub-directory ends at its data length, and a new
entry set that takes two new clusters of its folder extends the folder's data
length by both (R0.15 counted one); on FAT a folder's new cluster is cleared
before the folder is linked to it (a cut in between left garbage entries),
and a delete or rename marks the 8.3 short-name entry deleted before the
long-name entries (gbaSD 9cc52f8; R0.15's order let a cut leave the file
visible under its 8.3 alias), tested by gbaSD's `remove_cut.py` with this
app's ffconf.h (FAT12/16/32 and exFAT).

gbaSD's `tests/fatfs/exfat_repair/run.sh` (in gbaSD's CI, with the settings
of this app's ffconf.h; here: `FFCONF=include/fatfs/ffconf.h sh
gbasd/tests/fatfs/exfat_repair/run.sh`) tests the exfat_repair module on its
own (C, its own RAM disk, ASan + UBSan): torn sets built byte-exactly for create, append,
rewrite, delete, rename and mkdir, sets of 3, 4, 10 and 19 entries (names up
to 255 characters) crossing a sector boundary at every offset, in a folder in
a FAT chain (512-byte clusters) and a contiguous one (4 KB), with a power
cut after every sector write of the operation: after each repair the card is
fsck.exfat clean, every file that was there reads back, a second repair
changes nothing, and the repair changes only entry type bytes (freed) and set
checksums; a folder's own set torn in the root; healthy folders and FAT
volumes byte-identical with no writes; and the `exFAT fix:` regression (a
set taking two new clusters). fsck.exfat 1.2.2 cannot check a set spread over
three clusters (512-byte clusters, 18-19 entries) and reports it as a bad
checksum; the test accepts exactly those reports for such (valid) sets.
