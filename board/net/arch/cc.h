#pragma once

// lwIP port for panda: no libc beyond the string functions panda and
// net_libc.c provide, no printf.

#include <stdint.h>

#define LWIP_NO_CTYPE_H 1

void net_platform_assert(const char *msg, int line, const char *file) __attribute__((noreturn));
uint32_t net_rand(void);

#define LWIP_PLATFORM_DIAG(x) do {} while (0)
#define LWIP_PLATFORM_ASSERT(x) net_platform_assert((x), __LINE__, __FILE__)
#define LWIP_RAND() net_rand()
