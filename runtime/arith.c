#ifndef _ARITH_C_ /* -*- linux-c -*- */
#define _ARITH_C_

/** @file arith.
 * @brief Implements various arithmetic-related helper functions
 */


struct context;
void _stp_divmod64 (unsigned *errorcount, int64_t x, int64_t y,
                    int64_t *quo, int64_t *rem);


/** Divide x by y.  In case of overflow or division-by-zero,
 * increment context errorcount, and return any old value.
 */
inline int64_t _stp_div64 (unsigned *errorcount, int64_t x, int64_t y)
{
  if (likely ((x >= LONG_MIN && x <= LONG_MAX) &&
              (y >= LONG_MIN && y <= LONG_MAX)))
    {
      long xx = (long) x;
      long yy = (long) y;
      // check for division-by-zero and overflow
      if (unlikely (yy == 0 || (xx == LONG_MIN && yy == -1)))
        {
          (*errorcount) ++;
          return 0;
        }
      return xx / yy;
    }
  else
    {
      int64_t quo = 0;
      _stp_divmod64 (errorcount, x, y, &quo, NULL);
      return quo;
    }
}


/** Modulo x by y.  In case of overflow or division-by-zero,
 * increment context errorcount, and return any old value.
 */
inline int64_t _stp_mod64 (unsigned *errorcount, int64_t x, int64_t y)
{
  if (likely ((x >= LONG_MIN && x <= LONG_MAX) &&
              (y >= LONG_MIN && y <= LONG_MAX)))
    {
      long xx = (long) x;
      long yy = (long) y;
      // check for division-by-zero and overflow
      if (unlikely (yy == 0 || (xx == LONG_MIN && yy == -1)))
        {
          (*errorcount) ++;
          return 0;
        }
      return xx % yy;
    }
  else
    {
      int64_t rem = 0;
      _stp_divmod64 (errorcount, x, y, NULL, &rem);
      return rem;
    }
}


/** Perform general long division/modulus. */
void _stp_divmod64 (unsigned *errorcount, int64_t x, int64_t y,
                    int64_t *quo, int64_t *rem)
{
  // XXX: wimp out for now
  (*errorcount) ++;
  if (quo) *quo = 0;
  if (rem) *rem = 0;
}


/** Return a random integer between -n and n.
 * @param n how far from zero to go.  Make it positive but less than a million or so.
 */
int _stp_random_pm (int n)
{
  static unsigned long seed;
  static int initialized_p = 0;

  if (unlikely (! initialized_p))
    {
      seed = (unsigned long) jiffies;
      initialized_p = 1;
    }

  /* from glibc rand man page */
  seed = seed * 1103515245 + 12345;

  return (seed % (2*n+1)-n);
}



#endif /* _ARITH_C_ */
