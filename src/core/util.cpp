#include "util.h"

namespace planner {

uint32_t crc32_update(uint32_t crc, const void* data, size_t n) {
  static const uint32_t T[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                 0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  const uint8_t* p = static_cast<const uint8_t*>(data);
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    crc = (crc >> 4) ^ T[crc & 15];
    crc = (crc >> 4) ^ T[crc & 15];
  }
  return ~crc;
}

uint16_t crc16(const void* data, size_t n, uint16_t seed) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint16_t c = uint16_t(0xFFFF ^ seed);
  for (size_t i = 0; i < n; ++i) {
    c ^= uint16_t(p[i] << 8);
    for (int b = 0; b < 8; ++b) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
  }
  return c;
}

int str_len(const char* s) {
  int n = 0;
  while (s[n]) ++n;
  return n;
}

void str_copy(char* dst, int cap, const char* src) {
  int i = 0;
  for (; i < cap - 1 && src[i]; ++i) dst[i] = src[i];
  dst[i] = 0;
}

void str_cat(char* dst, int cap, const char* src) {
  const int n = str_len(dst);
  str_copy(dst + n, cap - n, src);
}

}  // namespace planner
