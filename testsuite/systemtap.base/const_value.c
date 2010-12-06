#include "sys/sdt.h"

struct foo
{
  const int i;
  const long j;
};

typedef struct foo fooer;

static int
bar (const int i, const long j)
{
  return i * j;
}

static int
func (int (*f) ())
{
  const int a[] = { 17, 23 };
  const fooer baz = { .i = 2, .j = 21 };
  STAP_PROBE (test, constvalues);
  return f(baz.i, baz.j);
}

int
main (int argc, char *argv[], char *envp[])
{
  return func (&bar) - 42;
}
