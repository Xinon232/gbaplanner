// Internal helpers: checksums, little-endian access.
#ifndef PLANNER_CORE_UTIL_H
#define PLANNER_CORE_UTIL_H

#include "core/planner_config.h"

namespace planner {

uint32_t crc32_update(uint32_t crc, const void* data, size_t n);  // start with 0
inline uint32_t crc32(const void* data, size_t n) { return crc32_update(0, data, n); }
uint16_t crc16(const void* data, size_t n, uint16_t seed);

// File identity as gbawriter/gbavocab verify saves: size + two hashes.
struct Identity {
  uint32_t size = 0;
  uint32_t h1 = 2166136261u;  // FNV-1a
  uint32_t h2 = 5381u;        // x33
  void add(const void* data, size_t n);  // hot.cpp
  bool operator==(const Identity& o) const { return size == o.size && h1 == o.h1 && h2 == o.h2; }
  bool operator!=(const Identity& o) const { return !(*this == o); }
};

// 64-bit content hash for entry-by-entry comparison.
struct Hash64 {
  uint64_t v = 1469598103934665603ull;
  void byte(uint8_t c) { v = (v ^ c) * 1099511628211ull; }
  void bytes(const void* p, size_t n) {
    for (size_t i = 0; i < n; ++i) byte(static_cast<const uint8_t*>(p)[i]);
  }
  void u32(uint32_t x) { for (int i = 0; i < 4; ++i) byte(uint8_t(x >> (8 * i))); }
};

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
inline uint32_t rd24(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16; }
inline uint32_t rd32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void wr16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
inline void wr24(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); }
inline void wr32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}

// UTF-8 code points in a byte string (continuation bytes are not counted).
inline int utf8_chars(const char* s, int n) {
  int c = 0;
  for (int i = 0; i < n; ++i) c += (uint8_t(s[i]) & 0xC0) != 0x80;
  return c;
}
int str_len(const char* s);
void str_copy(char* dst, int cap, const char* src);  // always NUL-terminates
void str_cat(char* dst, int cap, const char* src);

}  // namespace planner

#endif
