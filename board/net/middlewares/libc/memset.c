/*
 * Copyright (c) 2000 by Joerg Wunsch <joerg@FreeBSD.org>
 * Extracted from newlib libc/string/memset.c
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 */

#include <string.h>
#include <stddef.h>
#include "newlib_string_local.h"

void *
__inhibit_loop_to_libcall
memset(void *m, int c, size_t n)
{
  char *s = (char *)m;

#if !defined(PREFER_SIZE_OVER_SPEED) && !defined(__OPTIMIZE_SIZE__)
  unsigned int i;
  unsigned long buffer;
  unsigned long *aligned_addr;
  unsigned int d = c & 0xff;

  while (UNALIGNED_X(s))
    {
      if (n--)
        *s++ = (char)c;
      else
        return m;
    }

  if (!TOO_SMALL_LITTLE_BLOCK(n))
    {
      aligned_addr = (unsigned long *)s;

      /* Store D into each char sized location in BUFFER so that
         we can set large blocks quickly. */
      buffer = (d << 8) | d;
      buffer |= (buffer << 16);
      for (i = 32; i < sizeof(buffer) * 8; i <<= 1)
        buffer = (buffer << i) | buffer;

      while (!TOO_SMALL_BIG_BLOCK(n))
        {
          *aligned_addr++ = buffer;
          *aligned_addr++ = buffer;
          *aligned_addr++ = buffer;
          *aligned_addr++ = buffer;
          n -= BIG_BLOCK_SIZE;
        }

      while (!TOO_SMALL_LITTLE_BLOCK(n))
        {
          *aligned_addr++ = buffer;
          n -= LITTLE_BLOCK_SIZE;
        }
      s = (char *)aligned_addr;
    }

#endif /* not PREFER_SIZE_OVER_SPEED */

  while (n--)
    *s++ = (char)c;

  return m;
}
