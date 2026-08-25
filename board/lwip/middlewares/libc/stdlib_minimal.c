/*
 * Standalone implementations of atoi, rand, and srand.
 *
 * Algorithms are equivalent to newlib's but without dependencies on
 * reent structures, locale, or ctype.
 *
 * atoi: same behavior as strtol(s, NULL, 10) for valid base-10 input.
 * rand/srand: Knuth LCG (same multiplier as newlib), 64-bit state,
 *             returns upper 31 bits.
 */

#include <stdlib.h>

/*
 * atoi -- convert string to integer (base 10).
 * Handles leading whitespace and optional +/- sign.
 */
int
atoi(const char *s)
{
  int n = 0;
  int neg = 0;

  /* Skip whitespace (space, \t, \n, \v, \f, \r). */
  while (*s == ' ' || ((unsigned char)(*s - '\t') <= ('\r' - '\t')))
    s++;

  if (*s == '-')
    {
      neg = 1;
      s++;
    }
  else if (*s == '+')
    {
      s++;
    }

  while (*s >= '0' && *s <= '9')
    n = n * 10 + (*s++ - '0');

  return neg ? -n : n;
}

/*
 * rand / srand -- pseudo-random number generator.
 *
 * Uses the same 64-bit Knuth LCG multiplier as newlib:
 *   next = next * 6364136223846793005 + 1
 * Returns bits [62:32] (masked with RAND_MAX = 0x7fffffff).
 *
 * NOT thread-safe: uses a file-scope static. If you call rand() from
 * ISR context, protect with a critical section.
 */
static unsigned long long _rand_next = 1;

void
srand(unsigned int seed)
{
  _rand_next = seed;
}

int
rand(void)
{
  _rand_next = _rand_next * __extension__ 6364136223846793005ULL + 1;
  return (int)((_rand_next >> 32) & 0x7fffffff);
}
