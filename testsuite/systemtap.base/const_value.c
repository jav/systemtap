#include "sdt.h"

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

// Because of PR10726 we don't want to get this function inlined.
// We do need -O2 to get the const_value encodings in dwarf.
static __attribute__((__noinline__)) int
func (int (*f) ())
{
  const fooer baz = { .i = 2, .j = 21 };
  STAP_PROBE (test, constvalues);
  return f(baz.i, baz.j);
}

int
main (int argc, char *argv[], char *envp[])
{
  return func (&bar) - 42;
}
