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

int
t1 (int i)
{
  int a[] = { 17, 23 };
  struct ci c;
  c.i = i;
  srandom (c.i);
  i = 6;
  c.i = i;
  srandom (c.i);
  STAP_PROBE(test, t1);
  c.i = i;
  srandom (c.i + 4);
  return a[0] + a[1];
}

int
t2 (unsigned int i)
{
  int a[] = { 17, 23 };
  struct cui c;
  c.i = i;
  srandom (c.i);
  i = 0xdeadbeef;
  c.i = i;
  srandom (c.i);
  STAP_PROBE(test, t2);
  c.i = i;
  srandom (c.i + 4);
  return a[0] + a[1];
}

int
t3 (unsigned long long i)
{
  int a[] = { 17, 23 };
  struct cull c;
  c.i = i;
  srandom (c.i);
  i = 0xdeadbeef87654321LL;
  c.i = i;
  srandom (c.i);
  STAP_PROBE(test, t3);
  c.i = i;
  srandom (c.i + 4);
  return a[0] + a[1];
}

int
main (int argc, char **argv)
{
  int i1 = t1 (42);
  int i2 = t2 (42);
  int i3 = t3 (42);
  return 2*i1 - i2 - i3;
}
