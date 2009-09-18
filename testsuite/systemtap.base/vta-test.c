#include <stdlib.h>
#include "sdt.h"

void
t1 (int i)
{
  srandom (i);
  i = 6;
  srandom (i);
  STAP_PROBE(test, t1);
  srandom (i + 4);
}

void
t2 (unsigned int i)
{
  srandom (i);
  i = 0xdeadbeef;
  srandom (i);
  STAP_PROBE(test, t2);
  srandom (i + 4);
}

void
t3 (unsigned long long i)
{
  srandom (i);
  i = 0xdeadbeef87654321LL;
  srandom (i);
  STAP_PROBE(test, t3);
  srandom (i + 4);
}

int
main (int argc, char **argv)
{
  t1 (42);
  t2 (42);
  t3 (42);
  return 0;
}
