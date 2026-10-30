#pragma once
// The app's own state: all settings and the last calendar day, in two checked
// copies /gbaplanner/GBAPLANNER.SYS/STATE0.DAT and STATE1.DAT (gbawriter's
// GBAWRITER.SYS mechanism). A save writes the older copy (removed first,
// written, synced, closed); at start the valid copy with the higher
// generation is used, so a save cut off by power loss leaves the other copy.
//
// File layout (little endian):
//   0  "GBPLSTAT"            magic
//   8  u16 version            VERSION of the app that wrote it
//  10  u16 payload bytes      later versions append fields; older apps
//                             ignore what they do not know, newer apps use
//                             defaults for fields an older file lacks
//  12  u32 generation
//  16  payload                (PAYLOAD_V1 below)
//   …  u32 FNV-1a of everything before
#include <cstdint>
#include "app/settings.h"

namespace app::state {

constexpr uint16_t VERSION = 1;
// Version 1 payload: one byte per setting in the order of Settings, then
// has_day, day, month, year (u16), then Feb 29 in other years, Day 29-31 in
// short months, Repeating tasks in to-do list (bytes 21-23; files written
// before they existed have 0 there = the defaults), then Ticked to-dos
// shown (byte 24; a shorter file gives the default 2), then Start screen
// (byte 25; a shorter file gives Calendar).
constexpr int PAYLOAD_V1 = 26;
constexpr int HEADER = 16;
constexpr int FILE_BYTES = HEADER + PAYLOAD_V1 + 4;
constexpr int MAX_FILE_BYTES = 512;   // larger files are not read

// Pure encoding (host-tested): returns the bytes written (FILE_BYTES).
int encode(const Settings& s, uint32_t generation, uint8_t (&out)[FILE_BYTES]);
// false: bad magic, size or checksum. Unknown or invalid values: defaults.
bool decode(const uint8_t* data, int size, Settings& out, uint32_t& generation);
uint32_t fnv1a(uint32_t hash, const uint8_t* data, int n);
constexpr uint32_t FNV_START = 2166136261u;

// ---- On the card (FatFs) ----
// The settings in RAM (defaults until load()).
Settings& settings();
// Reads both copies; the newer valid one wins. No valid copy: defaults.
void load();
// Writes the state when it differs from the last saved one. true: saved or
// nothing to save; false: the card failed (the RAM state stays).
bool save();
// Secret Settings > Delete configuration: removes both copies, settings back
// to their defaults (the calendar day stays where it is in RAM).
bool delete_configuration();
// Generation of the copy in use (0: none yet).
uint32_t generation();

}
