#include <sys/time.h>

struct foo
{
  int bar;
};

static struct foo foo;

void sub(const char *file)
{
  struct timeval times[2];
  times[0].tv_sec = 1;
  times[0].tv_usec = 2;
  times[1].tv_sec = 3;
  times[1].tv_usec = 4;
  utimes (file, times);
  foo.bar -= 2; /* 40 */
}

int
main (int argc, char **argv)
{
  foo.bar = 41 + argc; /* 42 */
  sub(argv[0]);
  return 0;
}
