#include "app/sd.h"

#include "core/calendar.h"
#include "ff.h"
#include "cart_io.h"

namespace app::sd {

namespace {
// .sbss: EWRAM (Butano / devkitARM linker script), not IWRAM.
__attribute__((section(".sbss"))) FATFS fatfs;
bool mounted = false;
}

bool start()
{
    if(mounted) return true;
    if(cart_sd_init() || f_mount(&fatfs, "0:", 1) != FR_OK) return false;
    // exFAT: entry sets a power cut left half written in the root,
    // /gbaplanner and GBAPLANNER.SYS (the app's state), before anything
    // reads or creates there (exfat_repair, via the core).
    planner::core_repair_dir(SYS_DIR);
    const FRESULT r = f_mkdir(ROOT);
    if(r != FR_OK && r != FR_EXIST) return false;
    mounted = true;
    return true;
}

bool ready() { return mounted; }

const char* cart_name()
{
    switch(cart_kind) {
    case CART_SUPERCARD: return mounted ? "SuperCard" : "";
    case CART_EZFO: return "EZ-Flash Omega";
    case CART_EDGBA: return "EverDrive";
    default: return "";
    }
}

}
