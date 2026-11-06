// C library functions lwIP needs beyond panda's memset/memcpy/memcmp
// (board/libc.h). Firmware links with -nostdlib.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

size_t strlen(const char *s) {
  size_t n = 0U;
  while (s[n] != '\0') {
    n++;
  }
  return n;
}

int strncmp(const char *a, const char *b, size_t n) {
  int ret = 0;
  for (size_t i = 0U; i < n; i++) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];
    if ((ca != cb) || (ca == 0U)) {
      ret = (int)ca - (int)cb;
      break;
    }
  }
  return ret;
}

void *memmove(void *dest, const void *src, size_t n) {
  uint8_t *d = dest;
  const uint8_t *s = src;
  if (d < s) {
    for (size_t i = 0U; i < n; i++) {
      d[i] = s[i];
    }
  } else if (d > s) {
    for (size_t i = n; i > 0U; i--) {
      d[i - 1U] = s[i - 1U];
    }
  } else {
    // same buffer
  }
  return dest;
}

// Used by netif_find() to parse interface numbers ("en0")
int atoi(const char *s) {
  int value = 0;
  bool negative = (*s == '-');
  if (negative || (*s == '+')) {
    s++;
  }
  while ((*s >= '0') && (*s <= '9')) {
    value = (value * 10) + (*s - '0');
    s++;
  }
  return negative ? -value : value;
}

// lwIP's MEMCPY (see lwipopts.h). Frame copies are almost never 4-byte aligned
// on both ends (14 byte Ethernet header), where panda's memcpy falls back to
// bytes. The Cortex-M7 handles unaligned word accesses to normal memory, so
// copy 32 bits at a time regardless of alignment.
typedef struct __attribute__((packed)) { uint32_t v; } net_u32_unaligned_t;

void *net_memcpy(void *dest, const void *src, size_t n) {
  uint8_t *d = dest;
  const uint8_t *s = src;
  while (n >= 16U) {
    ((net_u32_unaligned_t *)d)[0].v = ((const net_u32_unaligned_t *)s)[0].v;
    ((net_u32_unaligned_t *)d)[1].v = ((const net_u32_unaligned_t *)s)[1].v;
    ((net_u32_unaligned_t *)d)[2].v = ((const net_u32_unaligned_t *)s)[2].v;
    ((net_u32_unaligned_t *)d)[3].v = ((const net_u32_unaligned_t *)s)[3].v;
    d += 16; s += 16; n -= 16U;
  }
  while (n >= 4U) {
    ((net_u32_unaligned_t *)d)->v = ((const net_u32_unaligned_t *)s)->v;
    d += 4; s += 4; n -= 4U;
  }
  while (n > 0U) {
    *d++ = *s++;
    n--;
  }
  return dest;
}
