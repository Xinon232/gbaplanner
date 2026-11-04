# gbaplanner 11.0

A calendar and to-do list for the Game Boy Advance. Months with a coloured dot for every item, a day view (timeline or list), a to-do list with your calendar's upcoming tasks (START switches between the two; L+R there puts the GBA to sleep, Start held 1 s wakes it), country holidays for 34 countries, repeating entries, notes, and everything saved on the SD card — by default as plain text files you can also read and edit on a PC. No real-time clock is used: the calendar opens on the day you left it on.

Butano screens in the look of gbawriter and gbamp3 (white screen, small title, light-blue selection), UNSCII 16 and the SuperFW text renderer, typing with the gbawriter letters, and SD card access through [gbaSD](https://github.com/Xinon232/gbaSD). The data core is described in [docs/CORE_API.md](docs/CORE_API.md), the holiday table in [docs/HOLIDAYS.md](docs/HOLIDAYS.md).

> **Physical hardware remains unverified.** Host tests and exact-ROM emulator runs on modeled SD carts pass; they are not proof on a real GBA. Back up your SD card and try it with copies of your files first.

This repository holds the complete source code of gbaplanner 11.0 ([github.com/Xinon232/gbaplanner](https://github.com/Xinon232/gbaplanner)); build the ROM as described under [Building](#building). The [full-controls PDF](gbaplanner-full-controls.pdf) has every screen and the file formats.

## Flashcarts

One ROM runs on the **SuperCard SD**, the **EZ-Flash Omega** (also the Definitive Edition; start the ROM from the SD card) and the **EverDrive GBA**. The cart is detected at every start; hold **L+R** while it starts to use the SuperCard driver (with the to-do list as start screen this does not put it to sleep: only a fresh L+R there does). Without a card the driver takes a while to give up; the calendar then says `No SD card: nothing is saved`. Tested on FAT16, FAT32 and exFAT cards (modeled: host tests and emulator runs, files contiguous and fragmented), also large cards up to the largest SDXC card (2 TiB − 8 MiB; exFAT with 128 KiB, 512 KiB, 1 MiB and 32 MiB clusters, FAT32 32 GiB) with the files at the far end of the card. On a nearly full exFAT card the first save after a start takes well under a second (measured on the modeled carts: at most 18 frames, 0.3 s; with 10.0 it took over a minute on 2 TiB): gbaSD's free-space search reads the allocation bitmap a word at a time and starts at the app's own files (`gbasd_alloc_hint`). On a card with very large clusters the first start, which makes `/gbaplanner` and `GBAPLANNER.SYS`, is slow: FatFs clears each new folder's cluster (32 MiB clusters: about 2 to 5 minutes); later starts are not. A calendar.txt or todo.txt of 4 GiB or more (exFAT) is reported as `File too large` and left unchanged. FatFs (R0.15) and the exFAT entry-set repair come from gbaSD (`gbasd/fatfs`, shared by the GBA apps; settings in `include/fatfs/ffconf.h`; see `gbasd/docs/fatfs.md`): its exFAT changes keep a folder readable after a power cut while it grew and fix long names on 512-byte-cluster exFAT cards, and on FAT a growing folder's new cluster is now cleared before the folder is linked to it, so a power cut then no longer leaves garbage entries; since gbaSD 9cc52f8 a power cut while a file with a long name is deleted or renamed on FAT no longer leaves it visible under an 8.3 alias name (`NAME~1.TXT`); since gbaSD 3ee9d37 exFAT cards with 32 MiB clusters mount, the free-space search is fast and a new folder's cluster is cleared 4 sectors at a time (a 2 KiB buffer in EWRAM).

## Files on the SD card

```
/gbaplanner/                      created at start if missing
/gbaplanner/calendar.txt          the calendar (Calendar saved as: Text file, the default)
/gbaplanner/todo.txt              the to-dos (To-do list saved as: Text file)
/gbaplanner/GBAPLANNER.SYS/       folder, the app's own saves (visible on a PC)
    STATE0.DAT / STATE1.DAT       settings + last calendar day (two checked copies)
    CAL.DAT / CAL.JRN             the calendar when saved in-app
    TODO.DAT / TODO.JRN           the to-dos when saved in-app (the default)
```

No `.sav` files; country holidays are never written to any file. Older versions made `GBAPLANNER.SYS` hidden and system; the app clears both attributes at start, so the folder shows on a PC. Text files are saved the gbawriter way: a staging copy (`.gpn`) is written and read back, then it replaces the old file; a power cut at any moment leaves either the old or the new file, and the next start finishes or undoes the save. Leftover `.gpn`, `.gpb`, `.gpj` files belong to that recovery: do not delete them by hand.

### calendar.txt

One line per entry, UTF-8, LF or CRLF line ends:

```
2026-10-23 09:00-10:30 event Dentist // Dr. Meier, bring X-rays\n2nd floor
2026-10-25 event Trip to Hamburg | weekly on sa,su until 2026-12-31
1990-03-14 birthday Anna
2026-12-24 holiday Christmas Eve | yearly
2026-10-30 17:00 task [ ] Pay rent | monthly done 2026-10-30
2026-10-28 task [x] Send letter
2026-11-02 08:00-08:30 event Gym | every 2 weeks on mo,th count 10 skip 2026-11-16
```

- Fields: `DATE [TIME] CATEGORY [CHECK] TITLE [ | REPEAT] [ // NOTES]`.
- DATE is always `YYYY-MM-DD` (years 1900–2200), whatever date format the screens use. A birthday's year is the birth year.
- TIME: `HH:MM-HH:MM` for an event (end after start, same day), `HH:MM` for a task, always 24 h; no time (or `all-day`) = all day. Birthdays and holidays have no time.
- CATEGORY: `event`, `task`, `birthday`, `holiday`. CHECK (tasks only): `[ ]` or `[x]`.
- REPEAT: `daily`, `weekly`, `monthly`, `yearly`, or `every N days|weeks|months|years`; weekly `on mo,tu,we,th,fr,sa,su`; monthly `on day 23`, `on 4th fr` or `on last fr`; ends `until DATE` or `count N` (else never); `skip DATE,DATE` (days removed with Delete only this day); `done DATE,...` (ticked days of a repeating task). Birthdays repeat yearly without a repeat part (`| none` = once); holidays you enter are written `| yearly`.
- NOTES after ` // `: `\n` is a line break, `\\` a backslash; ` | ` and ` // ` inside a title or notes are written `\|` and `\/\/`.
- Lines starting with `#` and empty lines are comments. Lines the app does not understand are kept unchanged when it rewrites the file, and the app tells you at start how many there are (`3 lines not understood`). Only a conversion to in-app storage drops them, after asking.
- A save keeps the original line order and all other bytes; new entries go to the end.
- The settings Repeating dates > Feb 29 in other years and Day 29-31 in short months change only where such entries are shown (and which day "Delete only this day" and ticking use); the file stays the same.

### todo.txt

```
- [ ] Buy milk
- [ ] Call Tom
- [x] Water plants
```

Open to-dos first in list order, then ticked ones, most recently ticked first. The first N ticked lines are shown in the app (Settings > To-do list > Ticked to-dos shown, default 2); the others are hidden ticked to-dos. Other lines are kept unchanged.

## Calendar (the start screen by default)

| Key | Action |
|---|---|
| D-pad | One day / one week; hold: after a moment 7.5 steps a second, after 2 s faster, up to 10 a second; crosses into the next or previous month |
| L / R | Previous / next month (the day number is kept, clamped); let go within 1 second |
| Hold L / R | After 1 s a year, then a year every 0.5 s; held longer the years go faster and faster (about 5 a second after 3 s, 15 a second after 5 s). Dots appear when you let go |
| A (let go) | Day view of the cursor day |
| Hold A | New entry for the cursor day |
| Start | To-do list |
| Select | Settings |

One dot per item under the day number, up to 7 (more: six dots and a small `+`), in this order: holidays (country, then your own), birthdays, then events and tasks by time (all-day first). Dot colours come from Settings; a ticked task is a ring. The calendar opens on the day you left it on (saved when you leave the calendar and after about 3 seconds without moving); the first time on the day the ROM was built.

## Day view (A on a day)

| Key | Action |
|---|---|
| Up / Down | New entry (selected when the day opens) → all-day items → timed items in time order (the timeline scrolls along) |
| A | New entry: the entry form for this day. An entry: its form (edit). A country holiday: a read-only note |
| L / R | Previous / next day |
| B | Back to the calendar (its cursor is on the day you ended on) |

Timeline (default): all-day items as coloured bars (two rows; `+N` when there are more), hours on the left (24 h or 12 h), 1 hour = one 16 px row, events as blocks in a light tint of their colour with a colour edge, overlapping events side by side, timed tasks as a half-hour block, ticked tasks grey. A bar at the bottom shows the selected item's time and full title (scrolling when long) and `≡` when it has notes (Settings > Notes On). Birthdays show the age, e.g. `Anna (36)`. The selection starts on New entry; the timeline opens on the first entry's hour, or on the hour of Settings > Day view > Day view starts at. List (Settings > Day view > Day view): one row per item with time, colour bar and title, same keys. A very full day lists its first 40 items.

## New entry / edit entry

First the category: **Event / Task / Birthday / Holiday**. Then the form:

| Row | |
|---|---|
| Category | Left/Right change it (also when editing) |
| Title | A: type (up to 60 characters) |
| Notes | Only with Settings > Notes On (the default); shows the first line, A: multi-line editor (up to 500 characters) |
| Date / Birth date | A, or L / R (the previous / next part in the order of the date format), chooses day, month or year (underlined); Left/Right change it |
| All day | On / Off (events and tasks) |
| Start / End (event), Time (task) | 5-minute steps, hold Left/Right: fast. A new event lasts Settings > Day view > Default event length |
| Repeat | Does not repeat / Every day / week / month / year / Custom… (A opens it) |
| Save | Saves and closes |
| Mark as done / not done | Tasks, when editing: ticks this day's occurrence |
| Delete | Asks `Sure?` first |
| Delete only this day | Repeating entries, when editing: skips this occurrence (asks first) |

B leaves the form without saving. Birthdays and your own holidays repeat every year by default, events and tasks do not. Editing a repeating entry changes the whole series.

Custom…: Repeats every N, Unit days / weeks / months / years; weekly: Repeats on S M T W T F S (order of Settings > General > Week starts on; Left/Right choose a day, A switches it on or off); monthly: on day N, on the Nth weekday or on the last weekday (from the start date); Ends: Never / On date / After X occurrences (End date: A or L / R choose day, month or year); OK takes it, B goes back unchanged.

### Typing (gbawriter letters)

| Keys | Types |
|---|---|
| Hold a direction + B / A / R | Letter 1 / 2 / 3 of its group: Up `abc`, Right `hij`, Down `nop`, Left `tuw` |
| Hold L + direction + B / A / R | Up `def`, Right `klm`, Down `qrs`, Left `xyz` |
| Hold Right, press R twice / hold Left, press R twice | `g` / `v` |
| A alone / B alone | Space / delete (hold: repeat) |
| R short / hold R | Shift / Caps |
| Select alone | A period; keep Select held: Up/Down digits, Left/Right `( ) / ; @ # % & _ + = -`, R/L `. , ' " : ! ?` |
| Letter keys held + Select | Accents (á ä à â ã å æ, ç č ć, é è ë ê, í ï ì î, ñ ń, ó ö ô ò õ ø œ, ß š ś, ü ú ù û, ý ÿ, ž ź ż) |
| Start + D-pad / L / R | Move the caret / a page |
| Start alone (notes) | New line |
| Start+A / Start+B | Done / back without a change |

## To-do list (Start on the calendar, or the start screen)

| Key | Action |
|---|---|
| Up / Down | Choose a row (Left/Right: a page) |
| A | New entry: type a to-do. An open to-do: tick it. A ticked one: untick it. A calendar task: done for that day |
| Hold A + Up / Down | Move an open to-do |
| Select | Edit / Delete (Delete asks `Sure?`) |
| Start | Back to the calendar, on the reference day |
| B | Nothing (B goes back only in the Select menu and the editor) |
| L+R together | Sleep: the screen goes off and the GBA uses almost no power until **Start** is held 1 s |

The title says the reference day, `To-do · from 23.10.2026`: the calendar cursor's day (when the list is the start screen: the day the calendar remembered; Start then opens the calendar on it). Open to-dos come first, then the most recently ticked ones (grey with ✓): as many as Settings > To-do list > Ticked to-dos shown says (0–5, default 2). Ticking one more removes the oldest shown one from the list — deleted, or kept hidden (Settings > To-do list > Old ticked to-dos); with 0 a ticked to-do leaves the list at once. Lowering the number removes the extra ones the same way at once. Below a line `Upcoming tasks` come the calendar's tasks with their date in the task colour: one row per task, its first open day on or after the reference day (All tasks: also overdue ones first, marked `!`). A ticked task day leaves the list and stays in the calendar grey, with a ring. With Settings > To-do list > Repeating tasks set to 7 or 30 days, every open day of a repeating task in that window is its own row (tick them one by one; a task with no open day in the window shows its next one), and All tasks lists each overdue day too.

**Sleep** (only in the to-do list): press L and R together and the screen goes off; the GBA stops (Stop mode, almost no power) until you hold **Start** for 1 second: Start held 1 s wakes it, and the to-do list comes back exactly as it was while Start is still down. A shorter press (let go, or the contact bouncing, before 1 s) puts it straight back to sleep: the screen stays off and nothing happens. The Start press that wakes it does nothing else (it does not switch to the calendar, also when the button bounces when let go), and every button is ignored until all of them have been up for a moment (6 frames), so press Start again for the calendar. Everything is saved before the GBA sleeps (also the calendar day), and the SD card works as before after waking. L+R held while starting (SuperCard driver) does not count: only a fresh L+R in the list.

## Settings (Select on the calendar)

Settings opens on its top page; each page fits on the screen (only the Holidays list scrolls). A row with ▸ opens a page and shows its value where it fits (`Holidays: Germany`, the four colours). On an option page every value is listed with a tick on the current one: A picks it and goes back. Rows without ▸ change with Left / Right (or A). B goes back one page, with the cursor on the row you came from; on the top page it leaves Settings. Values are saved when you leave Settings or after a second.

| Page and row | Values (default first) |
|---|---|
| **General ▸** | |
| Start screen ▸ | Calendar / To-do list: the screen shown after start-up (settings saved before this version load as Calendar). The calendar still opens on the day you left it on; the to-do list starts from that day |
| Date format | DD.MM.YYYY / MM/DD/YYYY / YYYY-MM-DD |
| Week starts on | Monday / Sunday |
| Time format | 24 h / 12 h |
| **Holidays ▸** | No country / Austria … Japan (34 countries, national holidays; a list of all 35 choices) |
| **Colors ▸** | Event color, Task color, Birthday color, Holiday color ▸ — each a page of 8 colours: Blue, Orange, Purple, Green by default; also Red, Teal, Pink, Brown |
| **Day view ▸** | |
| Day view ▸ | Timeline / List |
| Day view starts at | 00:00 … 23:00 (default 08:00; Left / Right) |
| Default event length ▸ | 1 h / 30 min |
| **To-do list ▸** | |
| To-do list shows ▸ | To-dos and upcoming tasks / To-dos and all tasks / Tasks only / To-dos only |
| Repeating tasks ▸ | Next open day only / Open days in the next 7 days / Open days in the next 30 days |
| Ticked to-dos shown ▸ | 2 / 0 / 1 / 3 / 4 / 5: how many ticked to-dos stay in the list, grey (settings saved before this version load as 2) |
| Old ticked to-dos ▸ | Ticked to-dos beyond the ones shown: Delete with in-app storage, Keep hidden with a text file (until you choose) |
| Delete hidden to-dos | Only when there are hidden ticked to-dos; asks first |
| **Repeating dates ▸** | |
| Feb 29 in other years ▸ | Skip / 28 Feb / 1 Mar: where entries repeating yearly from a Feb 29 (birthdays too) appear in other years |
| Day 29-31 in short months ▸ | Skip / Last day of the month: monthly on day 29–31 in shorter months (February, April, June, September, November); a missing 5th weekday becomes the last one |
| **Notes** | On / Off. On is the default since this version; a state saved before keeps its value (Off stays Off) |
| **Storage ▸** | |
| Calendar saved as | Text file / In-app |
| To-do list saved as | In-app / Text file |
| **Controls and Credits ▸** | Controls: Calendar, Day view, Entry form, To-do list, Settings; Credits (the last Credits page opens **Secret Settings > Delete configuration**) |

The top page has nine rows, so Start screen, Date format, Week starts on and Time format share the General page, and the Controls topics and Credits share one page.

Changing **Storage > Calendar saved as** or **To-do list saved as** converts all data at once: if the text file has lines the app does not understand (or `#` comments) it warns first (`3 lines not understood. They will be lost. Convert anyway? / Cancel`), then asks `Convert calendar to in-app? Yes / No`, shows the percent, reads the new copy back and compares every entry, and only then removes the old copy and keeps the new setting. On any error nothing changes and the error is shown. A small calendar converts in well under a second; the time grows with the number of entries (measured on mGBA, SuperCard model: 500 entries about 5 s to in-app, 10,000 entries about a minute and a half).

## Limits

- 10,000 calendar entries, 300 to-dos; titles up to 60 characters, notes up to 500; years 1900–2200.
- Loading and saving time grow with the number of entries. A full 10,000-entry text calendar takes several seconds to load (measured on mGBA: about 9–11 s, depending on the cart) and each save rewrites and verifies the whole file; the in-app store loads faster (10,000 entries: about 5 s) and changes only the affected entry; its file grows with the number of entries. Month changes take a few frames even then; opening a very full day takes up to about 0.7 s.
- The day view lists the first 40 items of a day; the to-do list shows the first 200 calendar tasks.
- Country holidays: national holidays only; future years follow today's rules; China's make-up working days are not shown.
- Physical hardware is unverified (see above).

## Building

Toolchain: devkitPro devkitARM (GCC 16.1.0; the devkitpro/devkitarm image of 2026-06-10), Butano at commit
77dcbcb3d8783596a9f333c64eedbccec77b05dc, Python 3, make and git.

```
git clone --recursive https://github.com/Xinon232/gbaplanner.git
cd gbaplanner
git submodule update --init                   # gbaSD (only needed after a clone without --recursive)
git clone https://github.com/GValiente/butano.git ../butano
git -C ../butano checkout 77dcbcb3d8783596a9f333c64eedbccec77b05dc
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
make LIBBUTANO=../butano/butano
```

The output is `gbaplanner.gba` in the repository root (and `gbaplanner.elf`, `build/`). The ROM contains the build date
(`__DATE__`, the calendar's first day), so ROMs built on different days differ in those bytes.

Generated sources are committed, so the ROM build needs no Python packages. To regenerate them:
`python3 tools/build_ui_fonts.py` (Pillow) writes `include/app/ui_font_data.h` from `references/fonts/`;
`tools/holidays/` (packages in `tools/holidays/requirements.txt`, see `docs/HOLIDAYS.md`) writes
`src/holidays/holiday_data.c/.h`; `python3 tools/build_controls_pdf.py` (ReportLab, DejaVu Sans) writes the
controls PDF from this README.

## About / license

gbaplanner 11.0 — made by Halim Jarrar, (c) 2026 — halimj.itch.io — gba@halim-jarrar.de

gbaplanner is free software, licensed under the GNU General Public License version 3 or later (GPL-3.0-or-later, see `LICENSE`). It comes with no warranty. Source code: [github.com/Xinon232/gbaplanner](https://github.com/Xinon232/gbaplanner).

Parts made by others: SuperFW text renderer and SuperCard driver by David Guillen Fandos (GPL-3.0-or-later); UNSCII by Viznut (GPL); GNU Unifont (GPL-2.0-or-later); UI font from gbamp3 (Halim Jarrar, GPL-3.0-or-later); gbawriter letter engine (Halim Jarrar, GPL); Omega and EverDrive drivers (gbaSD, GPL, after the Omega kernel, Apache-2.0, and edio-gba by krikzz, MIT); FatFs by ChaN (BSD style); Butano by Gustavo Valiente (zlib). Holidays: python-holidays (MIT), generated at build time; Chinese calendar and equinox dates computed at build time with Skyfield (MIT) and the JPL DE440 ephemeris (public domain).
