/*
 * Extracted from newlib libc/string/strncmp.c
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 */

#include <string.h>
#include <limits.h>
#include "newlib_string_local.h"

int
strncmp(const char *s1, const char *s2, size_t n)
{
#if defined(PREFER_SIZE_OVER_SPEED) || defined(__OPTIMIZE_SIZE__)
  if (n == 0)
    return 0;

  while (n-- != 0 && *s1 == *s2)
    {
      if (n == 0 || *s1 == '\0')
        break;
      s1++;
      s2++;
    }

  return (*(unsigned char *)s1) - (*(unsigned char *)s2);
#else
  unsigned long *a1;
  unsigned long *a2;

  if (n == 0)
    return 0;

  /* If s1 or s2 are unaligned, then compare bytes. */
  if (!UNALIGNED_X_Y(s1, s2))
    {
      /* If s1 and s2 are word-aligned, compare them a word at a time. */
      a1 = (unsigned long *)s1;
      a2 = (unsigned long *)s2;
      while (n >= sizeof(long) && *a1 == *a2)
        {
          n -= sizeof(long);

          /* If we've run out of bytes or hit a null, return zero
             since we already know *a1 == *a2. */
          if (n == 0 || DETECT_NULL(*a1))
            return 0;

          a1++;
          a2++;
        }

      /* A difference was detected in last few bytes of s1, so search bytewise */
      s1 = (char *)a1;
      s2 = (char *)a2;
    }

  while (n-- > 0 && *s1 == *s2)
    {
      /* If we've run out of bytes or hit a null, return zero
         since we already know *s1 == *s2. */
      if (n == 0 || *s1 == '\0')
        return 0;
      s1++;
      s2++;
    }
  return (*(unsigned char *)s1) - (*(unsigned char *)s2);
#endif /* not PREFER_SIZE_OVER_SPEED */
}
