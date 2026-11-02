/*
 * Extracted from newlib libc/string/strlen.c
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

size_t
strlen(const char *str)
{
  const char *start = str;

#if !defined(PREFER_SIZE_OVER_SPEED) && !defined(__OPTIMIZE_SIZE__)
  unsigned long *aligned_addr;

  /* Align the pointer, so we can search a word at a time. */
  while (UNALIGNED_X(str))
    {
      if (!*str)
        return str - start;
      str++;
    }

  /* If the string is word-aligned, we can check for the presence of
     a null in each word-sized block. */
  aligned_addr = (unsigned long *)str;
  while (!DETECT_NULL(*aligned_addr))
    aligned_addr++;

  /* Once a null is detected, we check each byte in that block for a
     precise position of the null. */
  str = (char *)aligned_addr;

#endif /* not PREFER_SIZE_OVER_SPEED */

  while (*str)
    str++;
  return str - start;
}
