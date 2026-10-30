#include "fileio.h"

#include "core/calendar.h"
#include "exfat_repair.h"

namespace planner {

PLANNER_EWRAM FIL g_fil[IO_SLOTS];
PLANNER_EWRAM uint8_t g_iobuf[IO_SLOTS][IO_CHUNK];

Err fr_err(FRESULT r) {
  switch (r) {
    case FR_OK: return Err::OK;
    case FR_NO_FILE:
    case FR_NO_PATH: return Err::NOT_FOUND;
    case FR_DENIED: return Err::DISK_FULL;  // FatFs: no free cluster / directory full
    default: return Err::IO;
  }
}

void path_join(char* out, const char* dir, const char* name) {
  str_copy(out, PATH_LEN, dir);
  str_cat(out, PATH_LEN, "/");
  str_cat(out, PATH_LEN, name);
}

// ---- Reader -----------------------------------------------------------------
Err Reader::open(const char* path, uint32_t* size) {
  close();
  err_ = fr_err(f_open(f_, path, FA_READ));
  if (err_ != Err::OK) return err_;
  open_ = true;
  size_ = uint32_t(f_size(f_));
  pos_ = len_ = idx_ = 0;
  if (size) *size = size_;
  return Err::OK;
}

Err Reader::seek(uint32_t off) {
  if (off >= pos_ && off <= pos_ + len_) {  // inside the buffer
    idx_ = off - pos_;
    return Err::OK;
  }
  const FRESULT r = f_lseek(f_, off);
  if (r != FR_OK) return err_ = fr_err(r);
  pos_ = off;
  len_ = idx_ = 0;
  return Err::OK;
}

int Reader::refill() {
  if (!open_ || err_ != Err::OK) return -1;
  pos_ += len_;
  len_ = idx_ = 0;
  if (pos_ >= size_) return -1;
  if (uint32_t(f_tell(f_)) != pos_) {
    const FRESULT r = f_lseek(f_, pos_);
    if (r != FR_OK) { err_ = fr_err(r); return -1; }
  }
  UINT got = 0;
  uint32_t want = size_ - pos_ < IO_CHUNK ? size_ - pos_ : IO_CHUNK;
  const FRESULT r = f_read(f_, buf_, want, &got);
  if (r != FR_OK) { err_ = fr_err(r); return -1; }
  if (got == 0) { err_ = Err::CORRUPT; return -1; }  // shorter than its size
  len_ = got;
  return buf_[idx_++];
}

Err Reader::read(void* dst, uint32_t n) {
  uint8_t* d = static_cast<uint8_t*>(dst);
  for (uint32_t i = 0; i < n; ++i) {
    const int c = get();
    if (c < 0) return err_ != Err::OK ? err_ : Err::CORRUPT;
    d[i] = uint8_t(c);
  }
  return Err::OK;
}

void Reader::close() {
  if (open_) f_close(f_);
  open_ = false;
  pos_ = len_ = idx_ = size_ = 0;
  err_ = Err::OK;
}

// ---- Writer -----------------------------------------------------------------
Err Writer::create(const char* path) {
  abort();
  err_ = fr_err(f_open(f_, path, FA_WRITE | FA_CREATE_ALWAYS));
  if (err_ != Err::OK) return err_;
  open_ = true;
  n_ = written_ = 0;
  id_ = Identity();
  return Err::OK;
}

Err Writer::open_existing(const char* path) {
  abort();
  err_ = fr_err(f_open(f_, path, FA_WRITE | FA_READ | FA_OPEN_EXISTING));
  if (err_ != Err::OK) return err_;
  open_ = true;
  n_ = written_ = 0;
  id_ = Identity();
  return Err::OK;
}

Err Writer::seek(uint32_t off) {
  if (flush() != Err::OK) return err_;
  const FRESULT r = f_lseek(f_, off);
  if (r != FR_OK) return err_ = fr_err(r);
  if (uint32_t(f_tell(f_)) != off) return err_ = Err::DISK_FULL;  // extension failed
  return Err::OK;
}

void Writer::write(const void* p, uint32_t n) {
  const uint8_t* s = static_cast<const uint8_t*>(p);
  written_ += n;
  if (n) last_ = s[n - 1];
  while (n) {
    if (n_ == IO_CHUNK) flush();
    uint32_t k = IO_CHUNK - n_;
    if (k > n) k = n;
    for (uint32_t i = 0; i < k; ++i) buf_[n_ + i] = s[i];
    n_ += k;
    s += k;
    n -= k;
  }
}

Err Writer::flush() {
  if (!open_) return err_ = err_ == Err::OK ? Err::IO : err_;
  if (n_) id_.add(buf_, n_);
  if (n_ && err_ == Err::OK) {
    UINT done = 0;
    const FRESULT r = f_write(f_, buf_, n_, &done);
    if (r != FR_OK) err_ = fr_err(r);
    else if (done != n_) err_ = Err::DISK_FULL;
  }
  n_ = 0;
  return err_;
}

Err Writer::finish() {
  flush();
  if (open_) {
    if (err_ == Err::OK) err_ = fr_err(f_sync(f_));
    const FRESULT r = f_close(f_);
    if (err_ == Err::OK && r != FR_OK) err_ = fr_err(r);
  }
  open_ = false;
  return err_;
}

void Writer::abort() {
  if (open_) f_close(f_);
  open_ = false;
  n_ = 0;
}

// ---- misc -------------------------------------------------------------------
Err file_size(const char* path, uint32_t* size, bool* exists) {
  FILINFO fi;
  const FRESULT r = f_stat(path, &fi);
  *exists = false;
  if (size) *size = 0;
  if (r == FR_NO_FILE || r == FR_NO_PATH) return Err::OK;
  if (r != FR_OK) return fr_err(r);
  *exists = true;
  if (size) *size = uint32_t(fi.fsize);
  return Err::OK;
}

Err file_identity(const char* path, Identity* id) {
  Reader r(0);
  Err e = r.open(path);
  if (e != Err::OK) return e;
  *id = Identity();
  uint8_t chunk[64];
  uint32_t left = r.size();
  while (left) {
    const uint32_t k = left < sizeof chunk ? left : uint32_t(sizeof chunk);
    if ((e = r.read(chunk, k)) != Err::OK) return e;
    id->add(chunk, k);
    left -= k;
  }
  return Err::OK;
}

Err ensure_dir(const char* path) {
  const FRESULT r = f_mkdir(path);
  return r == FR_OK || r == FR_EXIST ? Err::OK : fr_err(r);
}

Err remove_file(const char* path) {
  const FRESULT r = f_unlink(path);
  return r == FR_OK || r == FR_NO_FILE ? Err::OK : fr_err(r);
}

// ---- exFAT: half-written directory entry sets ---------------------------------
// A power cut between the two sector writes of an entry set that crosses a
// sector boundary leaves it torn and FatFs refuses the folder (FR_INT_ERR);
// exfat_repair (gbasd/fatfs/exfat_repair.c, shared with the other apps) repairs
// it: the root, the folders on the way, then `dir`. It uses fs->win only.
Err core_repair_dir(const char* dir) {
  const int r = exfat_repair_dir(nullptr, dir, 1);
  return r < 0 ? fr_err(FRESULT(-r)) : Err::OK;
}

// exFAT: the recoveries first repair the folder of the file they look at.
static Err repair_folder_of(const char* path) {
  char dir[PATH_LEN];
  str_copy(dir, PATH_LEN, path);
  int cut = str_len(dir);
  while (cut > 0 && dir[cut] != '/') --cut;
  dir[cut] = 0;
  return cut > 0 ? core_repair_dir(dir) : Err::OK;
}

// ---- safe replacement ---------------------------------------------------------
namespace {

constexpr uint8_t MANIFEST_MAGIC[8] = {'G', 'B', 'P', 'M', 'A', 'N', '0', '1'};
constexpr int MANIFEST_SIZE = 8 + 4 + 12 + 12 + 4;

struct Manifest {
  bool had_old = false;
  Identity old_id, new_id;
};

// "calendar.txt" -> "calendar.gpn" / ".gpb" / ".gpj": 8.3 names (same case as
// the extension) need one directory entry each, so FatFs writes every rename
// step as single 32-byte entries (no long-name entries that could be left
// half removed).
void sibling(char* out, const char* target, const char* ext_lower) {
  str_copy(out, PATH_LEN, target);
  int n = str_len(out), dot = -1;
  for (int i = n - 1; i >= 0 && out[i] != '/'; --i)
    if (out[i] == '.') { dot = i; break; }
  const bool upper = dot >= 0 && out[dot + 1] >= 'A' && out[dot + 1] <= 'Z';
  if (dot < 0) dot = n;
  out[dot] = '.';
  for (int i = 0; i < 3; ++i) out[dot + 1 + i] = char(upper ? ext_lower[i] - 32 : ext_lower[i]);
  out[dot + 4] = 0;
}
void names(const char* target, char* tmp, char* bak, char* jrn) {
  sibling(tmp, target, "gpn");
  sibling(bak, target, "gpb");
  sibling(jrn, target, "gpj");
}

void put_id(uint8_t* p, const Identity& id) { wr32(p, id.size); wr32(p + 4, id.h1); wr32(p + 8, id.h2); }
Identity get_id(const uint8_t* p) {
  Identity id;
  id.size = rd32(p); id.h1 = rd32(p + 4); id.h2 = rd32(p + 8);
  return id;
}

Err write_manifest(const char* path, const Manifest& m) {
  uint8_t b[MANIFEST_SIZE];
  for (int i = 0; i < 8; ++i) b[i] = MANIFEST_MAGIC[i];
  wr32(b + 8, m.had_old ? 1 : 0);
  put_id(b + 12, m.old_id);
  put_id(b + 24, m.new_id);
  wr32(b + 36, crc32(b, 36));
  Writer w(1);
  Err e = w.create(path);
  if (e != Err::OK) return e;
  w.write(b, sizeof b);
  return w.finish();
}

// OK + valid, OK + !valid (torn / foreign), or an I/O error.
Err read_manifest(const char* path, Manifest& m, bool& valid) {
  valid = false;
  Reader r(0);
  uint32_t size = 0;
  Err e = r.open(path, &size);
  if (e != Err::OK) return e;
  if (size != MANIFEST_SIZE) return Err::OK;
  uint8_t b[MANIFEST_SIZE];
  if ((e = r.read(b, sizeof b)) != Err::OK) return e;
  for (int i = 0; i < 8; ++i)
    if (b[i] != MANIFEST_MAGIC[i]) return Err::OK;
  if (rd32(b + 36) != crc32(b, 36)) return Err::OK;
  m.had_old = rd32(b + 8) == 1;
  m.old_id = get_id(b + 12);
  m.new_id = get_id(b + 24);
  valid = true;
  return Err::OK;
}

// Two directory entries sharing one cluster chain (left by an interrupted
// f_rename, which writes the new entry before it removes the old one when
// they sit in different directory sectors).
Err same_chain(const char* a, const char* b, bool& alias) {
  alias = false;
  FRESULT r = f_open(&g_fil[0], a, FA_READ);
  if (r != FR_OK) return fr_err(r);
  r = f_open(&g_fil[1], b, FA_READ);
  if (r != FR_OK) { f_close(&g_fil[0]); return fr_err(r); }
  alias = g_fil[0].obj.sclust != 0 && g_fil[0].obj.sclust == g_fil[1].obj.sclust;
  f_close(&g_fil[1]);
  f_close(&g_fil[0]);
  return Err::OK;
}

// Removes one name of an aliased chain without freeing the chain: point the
// directory entry at no cluster (size 0) first, then unlink it.
uint32_t g_detach_count = 0;  // read by the tests

Err detach(const char* name) {
  ++g_detach_count;
  FIL* f = &g_fil[1];
  FRESULT r = f_open(f, name, FA_READ | FA_WRITE | FA_OPEN_EXISTING);
  if (r != FR_OK) return fr_err(r);
  f->obj.sclust = 0;
  f->obj.objsize = 0;
#if FF_FS_EXFAT
  f->obj.stat = 0;
#endif
  f->clust = 0;
  f->sect = 0;
  f->fptr = 0;
  f->flag = BYTE(f->flag | 0x40);  // FA_MODIFIED: f_close writes the entry
  f->flag = BYTE(f->flag & ~0x80); // no dirty data buffer
  r = f_close(f);
  if (r != FR_OK) return fr_err(r);
  return fr_err(f_unlink(name));
}

Err exists(const char* p, bool& e) { return file_size(p, nullptr, &e); }

Err ident_is(const char* p, const Identity& want, bool& match) {
  Identity id;
  Err e = file_identity(p, &id);
  match = e == Err::OK && id == want;
  return e;
}

// Resolve two names that both exist: if they alias, detach `drop`.
Err unalias(const char* keep, const char* drop, bool& was_alias) {
  Err e = same_chain(keep, drop, was_alias);
  if (e != Err::OK) return e;
  if (was_alias) return detach(drop);
  return Err::OK;
}

}  // namespace

uint32_t safe_alias_repairs() { return g_detach_count; }

Err safe_recover(const char* target) {
  char tmp[PATH_LEN], bak[PATH_LEN], jrn[PATH_LEN];
  names(target, tmp, bak, jrn);
  bool o, t, b, j;
  Err e;
  if ((e = repair_folder_of(target)) != Err::OK) return e;
  if ((e = exists(target, o)) != Err::OK || (e = exists(tmp, t)) != Err::OK ||
      (e = exists(bak, b)) != Err::OK || (e = exists(jrn, j)) != Err::OK)
    return e;
  if (!t && !b && !j) return Err::OK;
  Manifest m;
  bool valid = false;
  if (j && (e = read_manifest(jrn, m, valid)) != Err::OK) return e;
  bool alias = false;
  if (!valid) {
    // The manifest is written (and synced) before any rename: without a valid
    // one the original was never moved. The staging copy is incomplete.
    if (b) {
      if (o) return Err::RECOVERY_NEEDED;  // not produced by this code
      if ((e = fr_err(f_rename(bak, target))) != Err::OK) return e;
    }
    if (t) {
      if (o && (e = unalias(target, tmp, alias)) != Err::OK) return e;
      if (!alias && (e = remove_file(tmp)) != Err::OK) return e;
    }
    return j ? remove_file(jrn) : Err::OK;
  }
  // A valid manifest: the staging copy was complete and verified.
  if (o && b && t) {
    // Interrupted rename: original/backup or staging/original alias.
    bool a1 = false;
    if ((e = unalias(target, bak, a1)) != Err::OK) return e;
    if (a1) {
      b = false;  // rename original -> backup had not finished: old copy at target
    } else {
      bool a2 = false;
      if ((e = unalias(target, tmp, a2)) != Err::OK) return e;
      if (!a2) return Err::RECOVERY_NEEDED;
      t = false;  // rename staging -> original had not finished: new copy at target
    }
  }
  if (o && t && !b) {
    // Before the original was moved (or a first save whose final rename was
    // interrupted): drop the staging copy unless it is the target itself.
    if ((e = unalias(target, tmp, alias)) != Err::OK) return e;
    if (!alias && (e = remove_file(tmp)) != Err::OK) return e;
    t = false;
  }
  if (!o && t) {
    bool match = false;
    if ((e = ident_is(tmp, m.new_id, match)) != Err::OK && e != Err::CORRUPT) return e;
    if (match) {
      if ((e = fr_err(f_rename(tmp, target))) != Err::OK) return e;
      o = true;
      t = false;
    } else if (b) {
      if ((e = fr_err(f_rename(bak, target))) != Err::OK) return e;
      b = false;
      o = true;
      if ((e = remove_file(tmp)) != Err::OK) return e;
      t = false;
    } else {
      return Err::RECOVERY_NEEDED;
    }
  }
  if (!o && b) {
    if ((e = fr_err(f_rename(bak, target))) != Err::OK) return e;
    b = false;
    o = true;
  }
  if (o && b) {
    bool match = false;
    if ((e = ident_is(target, m.new_id, match)) != Err::OK && e != Err::CORRUPT) return e;
    if (!match) return Err::RECOVERY_NEEDED;
    if ((e = remove_file(bak)) != Err::OK) return e;
  }
  if (o) {
    bool is_new = false, is_old = false;
    if ((e = ident_is(target, m.new_id, is_new)) != Err::OK && e != Err::CORRUPT) return e;
    if (!is_new && m.had_old && (e = ident_is(target, m.old_id, is_old)) != Err::OK && e != Err::CORRUPT)
      return e;
    if (!is_new && !is_old) return Err::RECOVERY_NEEDED;
  }
  return remove_file(jrn);
}

Err safe_begin(SafeSave& s, const char* target) {
  str_copy(s.target, PATH_LEN, target);
  names(target, s.tmp, s.bak, s.jrn);
  Err e = safe_recover(target);
  if (e != Err::OK) return e;
  return s.w.create(s.tmp);
}

void safe_abort(SafeSave& s) {
  s.w.abort();
  remove_file(s.tmp);
}

Err safe_commit(SafeSave& s, bool* installed, const Identity* old_id) {
  *installed = false;
  Err e = s.w.finish();
  const Identity want = s.w.identity();
  if (e != Err::OK) { remove_file(s.tmp); return e; }
  Identity got;
  e = file_identity(s.tmp, &got);
  if (e != Err::OK || got != want) {
    remove_file(s.tmp);
    return e != Err::OK && e != Err::CORRUPT ? e : Err::VERIFY;
  }
  Manifest m;
  m.new_id = want;
  bool had = false;
  if ((e = exists(s.target, had)) != Err::OK) { remove_file(s.tmp); return e; }
  if (had && old_id) m.old_id = *old_id;
  else if (had && (e = file_identity(s.target, &m.old_id)) != Err::OK) { remove_file(s.tmp); return e; }
  m.had_old = had;
  if ((e = write_manifest(s.jrn, m)) != Err::OK) {
    remove_file(s.jrn);
    remove_file(s.tmp);
    return e;
  }
  // From here on the manifest decides; any failure goes through recovery.
  if (had) e = fr_err(f_rename(s.target, s.bak));
  if (e == Err::OK) e = fr_err(f_rename(s.tmp, s.target));
  if (e == Err::OK) {
    e = file_identity(s.target, &got);
    if (e == Err::OK && got != want) e = Err::VERIFY;
  }
  if (e == Err::OK && had) e = remove_file(s.bak);
  if (e == Err::OK) e = remove_file(s.jrn);
  if (e == Err::OK) {
    *installed = true;
    return Err::OK;
  }
  const Err first = e;
  const Err r = safe_recover(s.target);
  if (r == Err::OK) {
    bool match = false;
    ident_is(s.target, want, match);
    *installed = match;
    return match ? Err::OK : first;
  }
  // Unresolved: find out what the target holds now.
  bool match = false;
  ident_is(s.target, want, match);
  *installed = match;
  return r;
}

// ---- journal ----------------------------------------------------------------
namespace {
constexpr uint8_t JMAGIC[4] = {'G', 'B', 'P', 'J'};
}

Err Journal::begin(const char* jrn_path) {
  str_copy(path_, PATH_LEN, jrn_path);
  body_len_ = 0;
  crc_ = 0;
  Err e = w_.create(path_);
  if (e != Err::OK) return e;
  uint8_t zero[16] = {};
  w_.write(zero, 16);
  return w_.err();
}

void Journal::patch(uint32_t offset, const void* data, uint16_t len) {
  uint8_t h[8];
  wr32(h, offset);
  wr16(h + 4, len);
  wr16(h + 6, 0);
  w_.write(h, 8);
  w_.write(data, len);
  crc_ = crc32_update(crc_, h, 8);
  crc_ = crc32_update(crc_, data, len);
  body_len_ += 8u + len;
}

static Err journal_apply(const char* target_path, const char* jrn_path, uint32_t body_len) {
  Reader r(0);
  Err e = r.open(jrn_path);
  if (e != Err::OK) return e;
  if ((e = r.seek(16)) != Err::OK) return e;
  Writer w(1);
  if ((e = w.open_existing(target_path)) != Err::OK) return e;
  uint32_t done = 0;
  while (done < body_len) {
    uint8_t h[8];
    if ((e = r.read(h, 8)) != Err::OK) return e;
    const uint32_t off = rd32(h);
    uint32_t len = rd16(h + 4);
    if ((e = w.seek(off)) != Err::OK) return e;
    done += 8 + len;
    while (len) {
      const int c = r.get();
      if (c < 0) return r.err() != Err::OK ? r.err() : Err::CORRUPT;
      w.put(uint8_t(c));
      --len;
    }
  }
  r.close();
  return w.finish();
}

Err Journal::commit(const char* target_path) {
  if (w_.err() != Err::OK) { w_.abort(); remove_file(path_); return w_.err(); }
  uint8_t h[16];
  for (int i = 0; i < 4; ++i) h[i] = JMAGIC[i];
  wr32(h + 4, body_len_);
  wr32(h + 8, crc_);
  wr32(h + 12, crc32(h, 12));
  Err e = w_.seek(0);
  if (e == Err::OK) {
    w_.write(h, 16);
    e = w_.finish();
  }
  if (e != Err::OK) { w_.abort(); remove_file(path_); return e; }
  // The journal is complete: from now on a reload replays it.
  e = journal_apply(target_path, path_, body_len_);
  if (e != Err::OK) return Err::NEEDS_RELOAD;
  e = remove_file(path_);
  return e == Err::OK ? Err::OK : Err::NEEDS_RELOAD;
}

Err journal_recover(const char* target_path, const char* jrn_path) {
  bool j = false;
  uint32_t size = 0;
  Err e = repair_folder_of(jrn_path);
  if (e == Err::OK) e = file_size(jrn_path, &size, &j);
  if (e != Err::OK || !j) return e;
  bool valid = false;
  uint32_t body = 0;
  {
    Reader r(0);
    if ((e = r.open(jrn_path)) != Err::OK) return e;
    uint8_t h[16];
    if (size >= 16 && r.read(h, 16) == Err::OK && h[0] == 'G' && h[1] == 'B' && h[2] == 'P' && h[3] == 'J' &&
        rd32(h + 12) == crc32(h, 12) && rd32(h + 4) == size - 16) {
      body = rd32(h + 4);
      uint32_t crc = 0;
      uint8_t chunk[64];
      uint32_t left = body;
      bool ok = true;
      while (left && ok) {
        const uint32_t k = left < sizeof chunk ? left : uint32_t(sizeof chunk);
        ok = r.read(chunk, k) == Err::OK;
        crc = crc32_update(crc, chunk, k);
        left -= k;
      }
      if (!ok && r.err() != Err::OK) return r.err();
      valid = ok && crc == rd32(h + 8);
    } else if (r.err() != Err::OK) {
      return r.err();
    }
  }
  if (valid && (e = journal_apply(target_path, jrn_path, body)) != Err::OK) return e;
  return remove_file(jrn_path);
}

}  // namespace planner
