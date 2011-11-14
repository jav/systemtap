#include <sys/sdt.h>

#define MARK(name) STAP_PROBE(implicitptr, name)

int z;

static int
foo (int i)
{
  int *j = &i;
  int **k = &j;
  int ***l = &k;
 l1: MARK (foo_l1);
  z++;		      /* side effect helps the probe placement hit right */
  i++;
  return i;
}

struct S
{
  int *x, y;
};

int u[6];

static inline int
add (struct S *a, struct S *b, int c)
{
  *a->x += *b->x;
  a->y += b->y;
 l1: MARK (add_l1);
  u[c + 0]++;
  a = (struct S *) 0;
  u[c + 1]++;
  a = b;
 l2: MARK (add_l2);
  u[c + 2]++;
  return *a->x + *b->x + a->y + b->y;
}

static int
bar (int i)
{
  int j = i;
  int k;
  struct S p[2] = { { &i, i * 2 }, { &j, j * 2 } };
 l1: MARK (bar_l1);
  k = add (&p[0], &p[1], 0);
 l2: MARK (bar_l2);
  p[0].x = &j;
  p[1].x = &i;
  k += add (&p[0], &p[1], 3);
 l3: MARK (bar_l3);
  return i + j + k;
}

int x = 22;

int
main (void)
{
  x = foo (x);
  x = bar (x);
  return 0;
}
