#include "string.h"

void *memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  if (((uintptr_t)d & 7) == 0 && ((uintptr_t)s & 7) == 0) {
    uint64_t *d64 = (uint64_t *)d;
    const uint64_t *s64 = (const uint64_t *)s;
    while (n >= 8) {
      *d64++ = *s64++;
      n -= 8;
    }
    d = (uint8_t *)d64;
    s = (const uint8_t *)s64;
  }

  while (n--) {
    *d++ = *s++;
  }

  return dest;
}

void *memset(void *s, int c, size_t n) {
  uint8_t *p = (uint8_t *)s;
  uint8_t val = (uint8_t)c;

  if (((uintptr_t)p & 7) == 0 && n >= 8) {
    uint64_t val64 = (uint64_t)val * 0x0101010101010101ULL;
    uint64_t *p64 = (uint64_t *)p;
    while (n >= 8) {
      *p64++ = val64;
      n -= 8;
    }
    p = (uint8_t *)p64;
  }

  while (n--) {
    *p++ = val;
  }

  return s;
}

void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dest;
  const uint8_t *s = (const uint8_t *)src;

  if (d < s) {
    return memcpy(dest, src, n);
  }

  if (d > s) {
    d += n;
    s += n;
    while (n--) {
      *--d = *--s;
    }
  }

  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = (const uint8_t *)s1;
  const uint8_t *p2 = (const uint8_t *)s2;

  for (size_t i = 0; i < n; i++) {
    if (p1[i] != p2[i]) {
      return (int)p1[i] - (int)p2[i];
    }
  }

  return 0;
}
