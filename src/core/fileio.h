// Internal FatFs helpers: buffered reader/writer, safe whole-file
// replacement with recovery (the gbawriter way), and a small redo journal for
// in-place updates of the in-app stores.
#ifndef PLANNER_CORE_FILEIO_H
#define PLANNER_CORE_FILEIO_H

#include "ff.h"
#include "util.h"

namespace planner {

constexpr int PATH_LEN = 96;
constexpr int IO_CHUNK = 128;  // FatFs keeps the sector in the FIL; this is a small copy buffer
constexpr int IO_SLOTS = 3;

// The core owns three FatFs file objects with a small buffer each.
// Slot 0: main reader. Slot 1: writer (or a second reader). Slot 2: a second
// reader of the same or another file, so that two streams never seek back
// and forth on one FIL (a backward f_lseek walks the cluster chain).
extern FIL g_fil[IO_SLOTS];
extern uint8_t g_iobuf[IO_SLOTS][IO_CHUNK];

Err fr_err(FRESULT r);
void path_join(char* out, const char* dir, const char* name);

class Reader {
 public:
  explicit Reader(int slot) : f_(&g_fil[slot]), buf_(g_iobuf[slot]) {}
  ~Reader() { close(); }
  Err open(const char* path, uint32_t* size = nullptr);
  Err seek(uint32_t off);
  // Next byte, or -1 at the end of file or on an error (see err()).
  int get() {
    if (idx_ < len_) return buf_[idx_++];
    return refill();
  }
  Err read(void* dst, uint32_t n);  // exactly n bytes or an error (CORRUPT at EOF)
  // Buffered bytes available now (refills when empty); 0 at end or error.
  uint32_t avail(const uint8_t** p) {
    if (idx_ >= len_) {
      if (refill() < 0) return 0;
      --idx_;  // refill() consumed the first byte
    }
    *p = buf_ + idx_;
    return len_ - idx_;
  }
  void advance(uint32_t n) { idx_ += n; }
  uint32_t tell() const { return pos_ + idx_; }
  uint32_t size() const { return size_; }
  Err err() const { return err_; }
  void close();
  bool is_open() const { return open_; }

 private:
  int refill();
  FIL* f_;
  uint8_t* buf_;
  uint32_t pos_ = 0, len_ = 0, idx_ = 0, size_ = 0;
  Err err_ = Err::OK;
  bool open_ = false;
};

class Writer {
 public:
  explicit Writer(int slot) : f_(&g_fil[slot]), buf_(g_iobuf[slot]) {}
  ~Writer() { abort(); }
  Err create(const char* path);              // new file (truncates)
  Err open_existing(const char* path);       // read/write, no truncation
  Err seek(uint32_t off);                    // flushes first
  void put(uint8_t c) {
    if (n_ == IO_CHUNK) flush();
    buf_[n_++] = c;
    ++written_;
    last_ = c;
  }
  uint8_t last() const { return last_; }  // last byte written
  void write(const void* p, uint32_t n);
  void puts(const char* s) { write(s, uint32_t(str_len(s))); }
  Err flush();
  Err finish();  // flush + f_sync + close
  void abort();  // close, ignore errors
  Err err() const { return err_; }
  // Of the bytes written since create (complete after flush / finish).
  const Identity& identity() const { return id_; }
  uint32_t written() const { return written_; }
  bool is_open() const { return open_; }

 private:
  FIL* f_;
  uint8_t* buf_;
  uint32_t n_ = 0, written_ = 0;
  uint8_t last_ = 0;
  Identity id_;
  Err err_ = Err::OK;
  bool open_ = false;
};

// exists = false and OK when the file is missing.
Err file_size(const char* path, uint32_t* size, bool* exists);
Err file_identity(const char* path, Identity* id);  // uses reader slot 0
Err ensure_dir(const char* path);
Err remove_file(const char* path);  // OK when missing

// ---- safe whole-file replacement ------------------------------------------
// Same name with extension .gpn: staging copy, .gpb: backup of the original,
// .gpj: manifest (identity of the new and of the old copy). Steps:
//   write staging, sync, re-read and compare identity; write manifest, sync;
//   rename original -> backup; rename staging -> original; re-read and compare;
//   remove backup, remove manifest.
// safe_recover() resolves every intermediate state (also two directory
// entries left on one cluster chain by an interrupted FatFs rename): after it
// the target holds either the old or the new complete copy.
struct SafeSave {
  char target[PATH_LEN], tmp[PATH_LEN], bak[PATH_LEN], jrn[PATH_LEN];
  Writer w{1};
};
Err safe_recover(const char* target);
uint32_t safe_alias_repairs();  // how often recovery detached an aliased name (tests)
Err safe_begin(SafeSave& s, const char* target);  // recovery first, then opens the staging writer
// Completes the save. *installed tells whether the new copy is now the
// target (also when an error happened after the switch).
// old_id: identity of the current target when the caller already read it
// completely while writing the staging copy (saves one full read).
Err safe_commit(SafeSave& s, bool* installed, const Identity* old_id = nullptr);
void safe_abort(SafeSave& s);  // removes the staging copy

// ---- redo journal ----------------------------------------------------------
// Journal file: 16-byte header {"GBPJ", body length, body CRC32, header CRC32}
// then patches {u32 offset, u16 length, u16 0, bytes}. Written and synced
// completely before the target is touched; replayed (idempotent) on load.
class Journal {
 public:
  Err begin(const char* jrn_path);
  void patch(uint32_t offset, const void* data, uint16_t len);
  Err commit(const char* target_path);  // sync journal, apply, sync target, remove journal
 private:
  Writer w_{1};
  char path_[PATH_LEN];
  uint32_t body_len_ = 0, crc_ = 0;
};
// Replays a complete journal left by an interrupted update, drops a torn one.
Err journal_recover(const char* target_path, const char* jrn_path);

}  // namespace planner

#endif
