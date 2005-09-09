/* test of 64-bit division */
#include "runtime.h"
#define LLONG_MAX    9223372036854775807LL
#define LLONG_MIN    (-LLONG_MAX - 1LL)

/* This tests a lot of edge conditions.*/
/* Then it does 10 million random divisions, comparing the result */
/* with the results from glibc */

int main()
{
  int64_t x, y, div1, mod1, div2, mod2;
  const char *error;
  int i;

  x = 0;
  y = 0;
  div1 = _stp_div64(&error, x, y);
  if (div1 != 0 || *error != 'd') {
    printf("Failed 0/0 test\n");
    exit(-1);
  }
  error = "";

  mod1 = _stp_mod64(&error, x, y);
  if (mod1 != 0 || *error != 'd') {
    printf("Failed 0%0 test\n");
    exit(-1);
  }
  error = "";

  x = 1;
  y = 0;
  div1 = _stp_div64(&error, x, y);
  if (div1 != 0 || *error != 'd') {
    printf("Failed 1/0 test\n");
    exit(-1);
  }
  error = "";

  mod1 = _stp_mod64(&error, x, y);
  if (mod1 != 0 || *error != 'd') {
    printf("Failed 1%0 test\n");
    exit(-1);
  }
  error = "";

  x = 0;
  y = 1;

  div1 = _stp_div64(&error, x, y);
  if (*error || div1 != 0) {
    printf("Failed 0/1 test\n");
    exit(-1);
  }

  mod1 = _stp_mod64(&error, x, y);
  if (*error || mod1 != 0) {
    printf("Failed 0%1 test\n");
    exit(-1);
  }

  x = -1;
  y = -1;

  div1 = _stp_div64(&error, x, y);
  if (*error || div1 != 1) {
    printf("Failed -1/-1 test\n");
    exit(-1);
  }

  mod1 = _stp_mod64(&error, x, y);
  if (*error || mod1 != 0) {
    printf("Failed -1%-1 test\n");
    exit(-1);
  }


  for (y = -1; y < 2; y++) {
    if (y == 0)
      continue;

    for (x = LONG_MIN - 1LL; x < LONG_MIN + 2LL; x++ ) {
      div1 = _stp_div64(&error, x, y);
      mod1 = _stp_mod64(&error, x, y);
      div2 = x/y;
      mod2 = x%y;
      if (div1 != div2) {
	printf ("%lld/%lld was %lld and should have been %lld\n", x,y,div1,div2);
	exit (-1);
      }
      if (mod1 != mod2) {
	printf ("%lld\%%lld was %lld and should have been %lld\n", x,y,mod1,mod2);
	exit (-1);
      }
    }

    for (x = LONG_MAX - 1LL; x < LONG_MAX + 2LL; x++ ) {
      div1 = _stp_div64(&error, x, y);
      mod1 = _stp_mod64(&error, x, y);
      div2 = x/y;
      mod2 = x%y;
      if (div1 != div2) {
	printf ("%lld/%lld was %lld and should have been %lld\n", x,y,div1,div2);
	exit (-1);
      }
      if (mod1 != mod2) {
	printf ("%lld\%%lld was %lld and should have been %lld\n", x,y,mod1,mod2);
	exit (-1);
      }
    }

    for (x = LLONG_MIN; x <= LLONG_MIN + 1LL; x++ ) {
      div1 = _stp_div64(&error, x, y);
      mod1 = _stp_mod64(&error, x, y);
      div2 = x/y;
      mod2 = x%y;
      if (div1 != div2) {
	printf ("%lld/%lld was %lld and should have been %lld\n", x,y,div1,div2);
	exit (-1);
      }
      if (mod1 != mod2) {
	printf ("%lld\%%lld was %lld and should have been %lld\n", x,y,mod1,mod2);
	exit (-1);
      }
    }

    for (x = LONG_MAX - 1; x <= LONG_MAX; x++ ) {
      div1 = _stp_div64(&error, x, y);
      mod1 = _stp_mod64(&error, x, y);
      div2 = x/y;
      mod2 = x%y;
      if (div1 != div2) {
	printf ("%lld/%lld was %lld and should have been %lld\n", x,y,div1,div2);
	exit (-1);
      }
      if (mod1 != mod2) {
	printf ("%lld\%%lld was %lld and should have been %lld\n", x,y,mod1,mod2);
	exit (-1);
      }
    }
  }

  /* now do ten million random divisions and mods */
  for (i = 0; i < 10000000; i++)  {
    x = mrand48();
    y = mrand48();
    if (y == 0) {
      i--;
      continue;
    }

    div1 = _stp_div64(NULL, x, y);
    mod1 = _stp_mod64(NULL, x, y);
    div2 = x/y;
    mod2 = x%y;

    if (div1 != div2) {
      printf ("%lld/%lld was %lld and should have been %lld\n", x,y,div1,div2);
      exit (-1);
    }
    if (mod1 != mod2) {
      printf ("%lld\%%lld was %lld and should have been %lld\n", x,y,mod1,mod2);
      exit (-1);
    }
  }
  printf("OK\n");
  return 0;
}
