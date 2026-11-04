#include "app/help.h"

namespace app::help {

namespace {
// Controls: plain words, the keys you need most first.
constexpr Page calendar[] = {
    {"Calendar", {"#Moving", "D-pad: one day or one week.", "Hold: 7.5 steps a second, after", "2 s up to 10; across months.",
                  "#Months and years", "L / R: previous / next month.", "Hold L / R 1 s: year by year;",
                  "held longer: faster and faster."}},
    {"Calendar", {"#Days", "A: open the day.", "Hold A: new entry for the day.", "#Other screens",
                  "Start: to-do list.", "Select: Settings.", "#Dots", "One dot per item of the day."}},
    {"Calendar", {"#Dots", "Order: holidays, birthdays,", "then events and tasks by time.",
                  "A ring: a ticked task.", "More than 7: six dots and +.", "#Remembered",
                  "The calendar opens on the day", "you left it on."}}};
constexpr Page days[] = {
    {"Day view", {"#A on a day", "Opens on New entry. Up/Down:", "all-day items, then by time.", "A: open the entry form.",
                  "L / R: previous / next day.", "B: back to the calendar.", "#Bottom line: full title, time,",
                  "and a mark when it has notes."}},
    {"Day view", {"#Timeline (Settings > Day view)", "1 hour = one row. Blocks at", "their time, side by side when",
                  "they overlap. Grey: done task.", "#List", "One row per item: time,", "colour, title.",
                  "Country holidays: read-only."}}};
constexpr Page form[] = {
    {"Entry form", {"#New entry", "Hold A on a day, or A on", "New entry: Event, Task,", "Birthday or Holiday.",
                    "#Rows", "Up/Down: choose a row.", "Left/Right: change (hold: fast).",
                    "A on Title / Notes: type."}},
    {"Entry form", {"#Date", "A or L / R: day, month or year;", "Left/Right: change it.", "Birthday: the birth year.",
                    "#Time", "All day Off: start and end", "(task: one time), 5 minutes.",
                    "B: back without saving."}},
    {"Repeat", {"#Repeat", "Every day / week / month / year", "or Custom\xe2\x80\xa6 (A): every N, days of",
                "the week, monthly day or", "weekday, ends never / on a date", "/ after N times.",
                "Editing changes the series;", "Delete only this day skips one."}},
    {"Repeat options", {"#Settings > Repeating dates", "Feb 29 in other years: Skip,", "28 Feb or 1 Mar (yearly from",
                        "a Feb 29, also birthdays).", "Day 29-31 in short months:", "Skip or the last day (monthly",
                        "on day 29-31; a missing 5th", "weekday: the last one)."}},
    {"Typing: letters", {"Hold a direction, then", "B / A / R: letter 1 / 2 / 3.", "Up: abc    Right: hij",
                         "Down: nop  Left: tuw", "Hold L too: def klm qrs xyz", "Right + R twice: g",
                         "Left + R twice: v", "A: space.  B: delete."}},
    {"Typing: more", {"R: Shift.  Hold R: Caps.", "Select: . then Up/Down 1-0,", "Left/Right ( ) / ; @ # % & ...,",
                      "R / L: . , ' \" : ! ?", "Letter keys + Select: accents.", "Start+D-pad: move the caret.",
                      "Start alone: new line (notes).", "Start+A: done.  Start+B: back."}}};
constexpr Page todos[] = {
    {"To-do list", {"#Start on the calendar", "A: tick or untick.", "Hold A + Up/Down: move a to-do.",
                    "Select: Edit or Delete.", "Start: back to the calendar.", "#Ticked", "The last ones stay grey (Settings:",
                    "Ticked to-dos shown, 0-5)."}},
    {"To-do list", {"#Sleep (saves power)", "L+R together: the screen goes", "off and the GBA sleeps.",
                    "Start held 1 s wakes it, back", "to the list (nothing else).", "#Start screen",
                    "Settings > General > Start", "screen: Calendar or To-do list."}},
    {"To-do list", {"#Upcoming tasks", "Calendar tasks from the day", "of the calendar cursor.",
                    "A: done for that day.", "#Settings > To-do list", "What it shows, how many ticked",
                    "stay; older ticked ones: Delete", "or Keep hidden."}},
    {"To-do list", {"#Repeating tasks", "Settings > To-do list >", "Repeating tasks: the next open",
                    "day, or every open day of the", "next 7 or 30 days, one row each", "(tick them one by one).",
                    "All tasks: each overdue day", "gets its own row too."}}};
constexpr Page settings[] = {
    {"Settings", {"#Select on the calendar", "Up/Down: choose a row.", "Left/Right: change a value.",
                  "A: open \xe2\x96\xb8, or pick an option", "(the tick marks the current one).",
                  "B: back to the page before.", "#Saved", "When you leave, or after 1 s."}}};
// Credits: the personal page first, then the licence and each part made by
// others with its licence (gbawriter / gbavocab order).
constexpr Page credits[] = {
    {"About", {"gbaplanner 11.0", "(c) 2026 Halim Jarrar", "GPL-3.0-or-later", "No warranty.", "#Source",
               "github.com/Xinon232/gbaplanner", "halimj.itch.io", "gba@halim-jarrar.de"}},
    {"License", {"gbaplanner is free software:", "you can share and change it", "under the GNU GPL, version 3",
                 "or (at your option) later.", "It comes with no warranty.", "#Parts made by others",
                 "Their names and licenses are", "on the next pages."}},
    {"Text and fonts", {"#Text renderer", "SuperFW by David Guillen Fandos", "License: GPL 3.0 or later", "#Fonts",
                        "UNSCII by Viznut: GPL", "GNU Unifont (also Hangul):", "GPL 2.0 or later",
                        "UI font: gbamp3 (GPL 3.0+)"}},
    {"SD card and files", {"#SD card driver: SuperCard", "SuperFW by David Guillen Fandos", "License: GPL 3.0 or later",
                           "#Omega and EverDrive drivers", "Halim Jarrar (GPL), after the",
                           "Omega kernel (Apache 2.0) and", "edio-gba by krikzz (MIT)",
                           "Files: FatFs by ChaN (BSD style)"}},
    {"Holidays", {"#Country holidays", "python-holidays (MIT),", "generated at build time.", "#Astronomy",
                  "Chinese calendar and equinox", "dates: Skyfield (MIT) and the", "JPL DE440 ephemeris",
                  "(public domain), at build time."}},
    {"Typing and engine", {"#Typing", "gbawriter letter engine", "(Halim Jarrar, GPL)", "#Engine",
                           "Butano by Gustavo Valiente", "License: zlib", "#Built with", "devkitPro / devkitARM"}},
    {"Secret Settings", {"Press A to open", "Secret Settings.", "", "", "", "", "", ""}}};

struct Topic {
    const char* name;
    const Page* pages;
    int count;
};
template <int N> constexpr Topic topic(const char* name, const Page (&p)[N]) { return {name, p, N}; }
constexpr Topic topics[TOPICS] = {topic("Controls: Calendar", calendar), topic("Controls: Day view", days),
                                  topic("Controls: Entry form", form), topic("Controls: To-do list", todos),
                                  topic("Controls: Settings", settings), topic("Credits", credits)};
}

const char* topic_name(int t) { return topics[t].name; }
int topic_pages(int t) { return topics[t].count; }
const Page& page(int t, int p) { return topics[t].pages[p]; }
bool secret_page(int t, int p) { return t == CREDITS && p == topics[t].count - 1; }

}
