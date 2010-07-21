#include <stdlib.h>
#include "sys/sdt.h"

struct ci
{
  int i;
};

struct cui
{
  unsigned int i;
};

struct cull
{
  unsigned long long i;
};

void
t1 (int i)
{
  struct ci c;
  c.i = i;
  srandom (c.i);
  i = 6;
  c.i = i;
  srandom (c.i);
  STAP_PROBE(test, t1);
  c.i = i;
  srandom (c.i + 4);
}

void
t2 (unsigned int i)
{
  struct cui c;
  c.i = i;
  srandom (c.i);
  i = 0xdeadbeef;
  c.i = i;
  srandom (c.i);
  STAP_PROBE(test, t2);
  c.i = i;
  srandom (c.i + 4);
}

void
t3 (unsigned long long i)
{
  struct cull c;
  c.i = i;
  srandom (c.i);
  i = 0xdeadbeef87654321LL;
  c.i = i;
  srandom (c.i);
  STAP_PROBE(test, t3);
  c.i = i;
  srandom (c.i + 4);
}

int
main (int argc, char **argv)
{
  t1 (42);
  t2 (42);
  t3 (42);
  return 0;
}
