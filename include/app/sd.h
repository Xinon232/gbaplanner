#pragma once
// The SD card: gbaSD finds the cart (EverDrive, Omega, else SuperCard; hold
// L+R while starting for the SuperCard driver), then FatFs mounts the card
// and /gbaplanner is created when missing.
namespace app::sd {

// Once at start; true: the card is mounted and /gbaplanner exists.
bool start();
bool ready();
// "SuperCard", "EZ-Flash Omega", "EverDrive", or "" (no card).
const char* cart_name();

constexpr const char* ROOT = "/gbaplanner";
constexpr const char* SYS_DIR = "/gbaplanner/GBAPLANNER.SYS";

}
