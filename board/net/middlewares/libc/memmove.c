/*
 * Extracted from newlib libc/string/memmove.c
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
#include <limits.h>
#include "newlib_string_local.h"

void *
__inhibit_loop_to_libcall
memmove(void *dst_void, const void *src_void, size_t length)
{
#if defined(PREFER_SIZE_OVER_SPEED) || defined(__OPTIMIZE_SIZE__)
  char *dst = dst_void;
  const char *src = src_void;

  if (src < dst && dst < src + length)
    {
      src += length;
      dst += length;
      while (length--)
        {
          *--dst = *--src;
        }
    }
  else
    {
      while (length--)
        {
          *dst++ = *src++;
        }
    }

  return dst_void;
#else
  char *dst = dst_void;
  const char *src = src_void;
  long *aligned_dst;
  const long *aligned_src;

  if (src < dst && dst < src + length)
    {
      /* Destructive overlap...have to copy backwards */
      src += length;
      dst += length;

      if (!TOO_SMALL_LITTLE_BLOCK(length) && !UNALIGNED_X_Y(src, dst))
        {
          aligned_dst = (long *)dst;
          aligned_src = (long *)src;

          while (!TOO_SMALL_LITTLE_BLOCK(length))
            {
              *--aligned_dst = *--aligned_src;
              length -= LITTLE_BLOCK_SIZE;
            }

          dst = (char *)aligned_dst;
          src = (char *)aligned_src;
        }

      while (length--)
        {
          *--dst = *--src;
        }
    }
  else
    {
      /* Use optimizing algorithm for a non-destructive copy to closely
         match memcpy. If the size is small or either SRC or DST is unaligned,
         then punt into the byte copy loop.  This should be rare. */
      if (!TOO_SMALL_LITTLE_BLOCK(length) && !UNALIGNED_X_Y(src, dst))
        {
          aligned_dst = (long *)dst;
          aligned_src = (long *)src;

          /* Copy 4X long words at a time if possible. */
          while (!TOO_SMALL_BIG_BLOCK(length))
            {
              *aligned_dst++ = *aligned_src++;
              *aligned_dst++ = *aligned_src++;
              *aligned_dst++ = *aligned_src++;
              *aligned_dst++ = *aligned_src++;
              length -= BIG_BLOCK_SIZE;
            }

          /* Copy one long word at a time if possible. */
          while (!TOO_SMALL_LITTLE_BLOCK(length))
            {
              *aligned_dst++ = *aligned_src++;
              length -= LITTLE_BLOCK_SIZE;
            }

          dst = (char *)aligned_dst;
          src = (char *)aligned_src;
        }

      while (length--)
        {
          *dst++ = *src++;
        }
    }

  return dst_void;
#endif /* not PREFER_SIZE_OVER_SPEED */
}
