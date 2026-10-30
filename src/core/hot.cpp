// Small hot loops: the file identity hash used to verify every save, and
// the in-place index sort run after loading. In the app the sort's inner
// sift is in IWRAM (PLANNER_IWRAM_CODE, Thumb), the rest runs from ROM
// (PLANNER_IWRAM_LOAD; see planner_config.h).
#include "cal_internal.h"

namespace planner {

PLANNER_IWRAM_LOAD void Identity::add(const void* data, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t a = h1, b = h2;
  for (size_t i = 0; i < n; ++i) {
    a = (a ^ p[i]) * 16777619u;
    b = b * 33u + p[i];
  }
  h1 = a;
  h2 = b;
  size += uint32_t(n);
}

namespace {
// (display key, location) without 64-bit arithmetic: k = group/timed/start.
[[gnu::always_inline]] inline uint32_t key_hi(const Rec& r) {
  const uint32_t c = (r.w0 >> 20) & 3;
  const uint32_t group = c > 2 ? 2 : c;
  return group << 12 | ((r.w0 >> 23 & 1) ? (1u << 11 | (r.w1 & 0x7FF)) : 0u);
}
[[gnu::always_inline]] inline bool less(const Rec& a, const Rec& b) {
  const uint32_t ka = key_hi(a), kb = key_hi(b);
  if (ka != kb) return ka < kb;
  return (a.w2 & 0x7FFFFF) < (b.w2 & 0x7FFFFF);
}
}  // namespace

namespace {
PLANNER_IWRAM_CODE void sift(Rec* v, int i, int n) {
  const Rec x = v[i];
  for (;;) {
    int c = 2 * i + 1;
    if (c >= n) break;
    if (c + 1 < n && less(v[c], v[c + 1])) ++c;
    if (!less(x, v[c])) break;
    v[i] = v[c];
    i = c;
  }
  v[i] = x;
}
}  // namespace

// Heap sort in place (no extra RAM).
PLANNER_IWRAM_LOAD void idx_sort() {
  Rec* const v = g_idx;
  const int n0 = g_n;
  for (int i = n0 / 2 - 1; i >= 0; --i) sift(v, i, n0);
  for (int n = n0 - 1; n > 0; --n) {
    const Rec t = v[0];
    v[0] = v[n];
    v[n] = t;
    sift(v, 0, n);
  }
}

}  // namespace planner
