#include "app/sd.h"

#include "core/calendar.h"
#include "ff.h"
#include "alloc_hint.h"
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
    // The first new cluster is looked for after the app's own files, not
    // from the start of the card (exFAT keeps no free-space hint: large,
    // nearly full cards). GBAPLANNER.SYS holds what every save rewrites
    // (STATE0/1.DAT, TODO.DAT, CAL.DAT); before the first start made it,
    // /gbaplanner (calendar.txt, todo.txt). The second call does nothing
    // once the first set the hint.
    gbasd_alloc_hint(&fatfs, SYS_DIR);
    gbasd_alloc_hint(&fatfs, ROOT);
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
