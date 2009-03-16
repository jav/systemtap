#include "sdt.h" /* Really <sys/sdt.h>, but pick current source version. */

static void call1(int a)
{
  STAP_PROBE1(test, mark_a, a);
}

static void call2(int a, int b)
{
  STAP_PROBE2(test, mark_b, a, b);
}

static void call3(int a, int b, int c)
{
  STAP_PROBE3(test, mark_c, a, b, c);
}

static void call4(int a, int b, int c, int d)
{
  STAP_PROBE4(test, mark_d, a, b, c, d);
}

static void call5(int a, int b, int c, int d, int e)
{
  STAP_PROBE5(test, mark_e, a, b, c, d, e);
}

static void call6(int a, int b, int c, int d, int e, int f)
{
  STAP_PROBE6(test, mark_f, a, b, c, d, e, f);
}

static void call7(int a, int b, int c, int d, int e, int f, int g)
{
  STAP_PROBE7(test, mark_g, a, b, c, d, e, f, g);
}

static void call8(int a, int b, int c, int d, int e, int f, int g, int h)
{
  STAP_PROBE8(test, mark_h, a, b, c, d, e, f, g, h);
}

static void call9(int a, int b, int c, int d, int e, int f, int g, int h, int i)
{
  STAP_PROBE9(test, mark_i, a, b, c, d, e, f, g, h, i);
}

static void call10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j)
{
  STAP_PROBE10(test, mark_j, a, b, c, d, e, f, g, h, i, j);
}

int
main (int argc, char **argv)
{
  int a, b, c, d, e, f, g, h, i, j;
  a = 1; b = 2; c = 3; d = 4; e = 5; f = 6; g = 7; h = 8; i = 9; j = 10;
  call1(a);
  call2(a, b);
  call3(a, b, c);
  call4(a, b, c, d);
  call5(a, b, c, d, e);
  call6(a, b, c, d, e, f);
  call7(a, b, c, d, e, f, g);
  call8(a, b, c, d, e, f, g, h);
  call9(a, b, c, d, e, f, g, h, i);
  call10(a, b, c, d, e, f, g, h, i, j);
  (void) argv;
  (void) argc;
  return 0;
}
