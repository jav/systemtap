#include "sys/sdt.h"

static int
bar (int i, long j)
{
  return i * j;
}

static int
func (int (*f) ())
{
  volatile int i = 2;
  volatile long j = 21;
  STAP_PROBE (test, constvalues);
  return f(i, j);
}

int
main (int argc, char *argv[], char *envp[])
{
  return func (&bar) - 42;
}
