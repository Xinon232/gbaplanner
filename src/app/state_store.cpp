// The state copies on the card (FatFs), gbawriter's two-slot scheme.
#include "app/app_state.h"
#include "app/sd.h"

#include <cstring>
#include "ff.h"

namespace app::state {

namespace {
constexpr const char* SLOTS[2] = {"/gbaplanner/GBAPLANNER.SYS/STATE0.DAT", "/gbaplanner/GBAPLANNER.SYS/STATE1.DAT"};

Settings current;
// What is on the card (to skip saves that change nothing).
uint8_t saved[FILE_BYTES];
bool have_saved = false;
uint32_t generation_ = 0;
int newest = 1;   // slot of the copy in use; the next save writes the other
__attribute__((section(".sbss"))) FIL file;
__attribute__((section(".sbss"))) uint8_t buffer[MAX_FILE_BYTES];

bool read_slot(int slot, Settings& s, uint32_t& g)
{
    FILINFO info;
    if(f_stat(SLOTS[slot], &info) != FR_OK || (info.fattrib & AM_DIR) || info.fsize > MAX_FILE_BYTES) return false;
    if(f_open(&file, SLOTS[slot], FA_READ) != FR_OK) return false;
    UINT got = 0;
    const bool ok = f_read(&file, buffer, UINT(info.fsize), &got) == FR_OK && got == info.fsize;
    f_close(&file);
    return ok && decode(buffer, int(got), s, g);
}

bool make_sys_dir()
{
    const FRESULT r = f_mkdir(sd::SYS_DIR);
    return r == FR_OK || r == FR_EXIST;
}

// The folder is visible on a PC: earlier versions made it hidden + system;
// those attributes are cleared once (a failure does not matter).
void show_sys_dir()
{
    FILINFO info;
    if(f_stat(sd::SYS_DIR, &info) == FR_OK && (info.fattrib & AM_DIR) && (info.fattrib & (AM_HID | AM_SYS)))
        f_chmod(sd::SYS_DIR, 0, AM_HID | AM_SYS);
}
}

Settings& settings() { return current; }
uint32_t generation() { return generation_; }

void load()
{
    current = Settings();
    have_saved = false;
    generation_ = 0;
    newest = 1;
    if(!sd::ready()) return;
    show_sys_dir();
    Settings s[2];
    uint32_t g[2] = {0, 0};
    const bool valid[2] = {read_slot(0, s[0], g[0]), read_slot(1, s[1], g[1])};
    if(!valid[0] && !valid[1]) return;
    newest = valid[1] && (!valid[0] || g[1] > g[0]) ? 1 : 0;
    current = s[newest];
    generation_ = g[newest];
    encode(current, 0, saved);
    have_saved = true;
}

bool save()
{
    uint8_t data[FILE_BYTES];
    encode(current, 0, data);
    if(have_saved && !std::memcmp(data, saved, FILE_BYTES)) return true;
    if(!sd::ready() || !make_sys_dir()) return false;
    const int slot = newest ^ 1;
    const uint32_t g = generation_ + 1;
    encode(current, g, data);
    FILINFO info;
    const FRESULT exists = f_stat(SLOTS[slot], &info);
    if(exists == FR_OK && f_unlink(SLOTS[slot]) != FR_OK) return false;
    if(exists != FR_OK && exists != FR_NO_FILE) return false;
    if(f_open(&file, SLOTS[slot], FA_WRITE | FA_CREATE_NEW) != FR_OK) return false;
    UINT put = 0;
    bool ok = f_write(&file, data, FILE_BYTES, &put) == FR_OK && put == FILE_BYTES && f_sync(&file) == FR_OK;
    if(f_close(&file) != FR_OK) ok = false;
    if(ok) {
        // Read back: only a copy that checks out counts.
        Settings back;
        uint32_t gb = 0;
        ok = read_slot(slot, back, gb) && gb == g;
    }
    if(!ok) {
        f_unlink(SLOTS[slot]);
        return false;
    }
    generation_ = g;
    newest = slot;
    encode(current, 0, saved);
    have_saved = true;
    return true;
}

bool delete_configuration()
{
    if(!sd::ready()) return false;
    bool ok = true;
    for(const char* slot : SLOTS) {
        FILINFO info;
        const FRESULT r = f_stat(slot, &info);
        if(r == FR_OK) { if(f_unlink(slot) != FR_OK) ok = false; }
        else if(r != FR_NO_FILE) ok = false;
    }
    if(!ok) return false;
    const Settings old = current;
    current = Settings();
    current.has_day = old.has_day;
    current.day = old.day;
    have_saved = false;
    generation_ = 0;
    newest = 1;
    return true;
}

}
