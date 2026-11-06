/*
 * Copyright (c) 2000 by Joerg Wunsch <joerg@FreeBSD.org>
 * Extracted from newlib libc/string/memcpy.c
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
memcpy(void *__restrict dst0, const void *__restrict src0, size_t len0)
{
#if defined(PREFER_SIZE_OVER_SPEED) || defined(__OPTIMIZE_SIZE__)
  char *dst = (char *)dst0;
  char *src = (char *)src0;

  void *save = dst0;

  while (len0--)
    {
      *dst++ = *src++;
    }

  return save;
#else
  char *dst = dst0;
  const char *src = src0;
  long *aligned_dst;
  const long *aligned_src;

  /* If the size is small, or either SRC or DST is unaligned,
     then punt into the byte copy loop.  This should be rare. */
  if (!TOO_SMALL_LITTLE_BLOCK(len0) && !UNALIGNED_X_Y(src, dst))
    {
      aligned_dst = (long *)dst;
      aligned_src = (long *)src;

      /* Copy 4X long words at a time if possible. */
      while (!TOO_SMALL_BIG_BLOCK(len0))
        {
          *aligned_dst++ = *aligned_src++;
          *aligned_dst++ = *aligned_src++;
          *aligned_dst++ = *aligned_src++;
          *aligned_dst++ = *aligned_src++;
          len0 -= BIG_BLOCK_SIZE;
        }

      /* Copy one long word at a time if possible. */
      while (!TOO_SMALL_LITTLE_BLOCK(len0))
        {
          *aligned_dst++ = *aligned_src++;
          len0 -= LITTLE_BLOCK_SIZE;
        }

      /* Pick up any residual with a byte copier. */
      dst = (char *)aligned_dst;
      src = (char *)aligned_src;
    }

  while (len0--)
    *dst++ = *src++;

  return dst0;
#endif /* not PREFER_SIZE_OVER_SPEED */
}
