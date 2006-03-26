#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

typedef unsigned long long uint64;
struct timeval tstart, tstop;

void start()
{
  gettimeofday (&tstart, NULL);
}

uint64 usecs (struct timeval *tv)
{
  return tv->tv_sec * 1000000 + tv->tv_usec;
}

uint64 stop()
{
  gettimeofday (&tstop, NULL);
  return  usecs(&tstop) - usecs(&tstart);
}

void usage(char *name)
{
    printf ("Usage %s [num_threads]\nitest will call sys_getuid() 1000000/num_threads times.\n", name);
    exit(1);
}

int main(int argc, char *argv[])
{
  int i, n = 1;
  uint64 nsecs;

  if (argc > 2)
    usage(argv[0]);

  if (argc == 2) {
    n = strtol(argv[1], NULL, 10);
    if (n <= 0)
      usage(argv[0]);
  }

  start();
  for (i = 0; i < 1000000/n; i++)
    getuid();

  nsecs = stop() * n / 1000;

  /* returns nanosecs per call  */
  printf("%lld\n", nsecs);
  return 0;
}
