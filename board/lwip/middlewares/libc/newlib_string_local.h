/*
 * Extracted from newlib libc/string/local.h
 * Optimization macros for word-at-a-time string/memory operations.
 */

#ifndef NEWLIB_STRING_LOCAL_H
#define NEWLIB_STRING_LOCAL_H

#include <limits.h>

/*
 * GCC attribute to prevent the compiler from transforming loops back
 * into calls to memset/memcpy/memmove (which would cause infinite recursion).
 */
#ifdef __GNUC__
#define __inhibit_loop_to_libcall \
  __attribute__((__optimize__("-fno-tree-loop-distribute-patterns")))
#else
#define __inhibit_loop_to_libcall
#endif

/* Nonzero if X is not aligned on a "long" boundary. */
#define UNALIGNED_X(X) ((long)(X) & (sizeof(long) - 1))

/* Nonzero if either X or Y is not aligned on a "long" boundary. */
#define UNALIGNED_X_Y(X, Y) \
  (((long)(X) | (long)(Y)) & (sizeof(long) - 1))

/* How many bytes are copied each iteration of the word copy loop. */
#define LITTLE_BLOCK_SIZE (sizeof(long))

/* How many bytes are copied each iteration of the 4X unrolled loop. */
#define BIG_BLOCK_SIZE (sizeof(long) << 2)

/* Threshold for punting to the byte copier (word-size). */
#define TOO_SMALL_LITTLE_BLOCK(LEN) ((LEN) < LITTLE_BLOCK_SIZE)

/* Threshold for punting to the byte copier (4x word-size). */
#define TOO_SMALL_BIG_BLOCK(LEN) ((LEN) < BIG_BLOCK_SIZE)

/* Nonzero if X (a long int) contains a NULL byte. */
#if LONG_MAX == 2147483647L
#define DETECT_NULL(X) (((X) - 0x01010101) & ~(X) & 0x80808080)
#elif LONG_MAX == 9223372036854775807L
#define DETECT_NULL(X) (((X) - 0x0101010101010101) & ~(X) & 0x8080808080808080)
#else
#error long int is not a 32bit or 64bit type.
#endif

/* Returns nonzero if (long)X contains the byte used to fill (long)MASK. */
#define DETECT_CHAR(X, MASK) (DETECT_NULL((X) ^ (MASK)))

#endif /* NEWLIB_STRING_LOCAL_H */
