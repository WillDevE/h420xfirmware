/*
 * Minimal freestanding C runtime support.
 *
 * The image is linked with -nostdlib, so only the routines TinyUSB and the
 * application actually reference are provided here. No newlib is used.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (d < s) {
    while (n--) {
      *d++ = *s++;
    }
  } else if (d > s) {
    d += n;
    s += n;
    while (n--) {
      *--d = *--s;
    }
  }
  return dst;
}

void *memset(void *dst, int value, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  while (n--) {
    *d++ = (uint8_t)value;
  }
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  while (n--) {
    if (*pa != *pb) {
      return (int)*pa - (int)*pb;
    }
    pa++;
    pb++;
  }
  return 0;
}

size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') {
    n++;
  }
  return n;
}

/* ARM EABI runtime entry points that GCC may emit for structure copies. */
void __aeabi_memcpy(void *dst, const void *src, size_t n) {
  (void)memcpy(dst, src, n);
}

void __aeabi_memcpy4(void *dst, const void *src, size_t n) {
  (void)memcpy(dst, src, n);
}

void __aeabi_memcpy8(void *dst, const void *src, size_t n) {
  (void)memcpy(dst, src, n);
}

void __aeabi_memmove(void *dst, const void *src, size_t n) {
  (void)memmove(dst, src, n);
}

void __aeabi_memset(void *dst, size_t n, int value) {
  (void)memset(dst, value, n);
}

void __aeabi_memclr(void *dst, size_t n) {
  (void)memset(dst, 0, n);
}

void __aeabi_memclr4(void *dst, size_t n) {
  (void)memset(dst, 0, n);
}
