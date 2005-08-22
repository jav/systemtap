#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

typedef unsigned long long uint64;
struct rusage rstart;
struct timeval tstart, tstop;
uint64 ttime = 0;

void start()
{
  gettimeofday (&tstart, NULL);
  getrusage (RUSAGE_SELF, &rstart);
}

uint64 usecs (struct timeval *tv)
{
  return tv->tv_sec * 1000000 + tv->tv_usec;
}

uint64 stop()
{
  struct rusage rend;
  getrusage (RUSAGE_SELF, &rend);
  gettimeofday (&tstop, NULL);
  uint64 utime = usecs(&rend.ru_utime) - usecs(&rstart.ru_utime);
  uint64 stime = usecs(&rend.ru_stime) - usecs(&rstart.ru_stime);
  ttime = usecs(&tstop) - usecs(&tstart);
  return utime + stime;
}

void usage(char *name)
{
    printf ("Usage %s [time]\nWhere \"time\" is millions of times to loop.\n", name);
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
    if (n == 0)
      usage(argv[0]);
  }


  start();
  for (i = 0; i < n * 1000000; i++)
    getuid();

  nsecs = stop() / (n * 1000);

  /* returns 
     nanosecs per call (user + system time)
     elapsed usecs (real time)
  */
  printf("%lld %.2f\n", nsecs, ttime/1000000.0);

  return 0;
}
