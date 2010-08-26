#include <stdio.h>

static void
foo (int *p)
{
 l1:
  printf ("%d, %d\n", p[0], p[1]);
}

int main (void)
{
  int a[] = { 17, 23 };
 l1:
  foo (a);
  return 0;
}
