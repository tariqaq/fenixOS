// Force-included into every simulator source (/FI): what the board's GCC + newlib give and MSVC
// lacks.
#pragma once
#include <string.h>
#ifdef _MSC_VER
#define __attribute__(x)
#endif
#ifdef __cplusplus
extern "C" {
#endif
static inline size_t strlcpy(char* dst, const char* src, size_t cap) {
  size_t n = strlen(src);
  if (cap) {
    size_t k = n < cap - 1 ? n : cap - 1;
    memcpy(dst, src, k);
    dst[k] = 0;
  }
  return n;
}
static inline size_t strlcat(char* dst, const char* src, size_t cap) {
  size_t d = strnlen(dst, cap);
  return d == cap ? cap + strlen(src) : d + strlcpy(dst + d, src, cap - d);
}
#ifdef __cplusplus
}
#endif
