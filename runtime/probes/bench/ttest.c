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

void start()
{
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
  uint64 utime = usecs(&rend.ru_utime) - usecs(&rstart.ru_utime);
  uint64 stime = usecs(&rend.ru_stime) - usecs(&rstart.ru_stime);
  return utime + stime;
}

void usage(char *name)
{
    printf ("Usage %s [time]\nWhere \"time\" is millions of times to loop.\n", name);
    exit(1);
}

int main(int argc, char *argv[])
{
  int fd, i, n = 1;
  char buf[1024];
  uint64 nsecs;

  if (argc > 2)
    usage(argv[0]);

  if (argc == 2) {
    n = strtol(argv[1], NULL, 10);
    if (n == 0)
      usage(argv[0]);
  }

  fd = open ("foo", O_CREAT | O_RDWR);

  /* large warmup time */
  for (i = 0; i < n * 1000000; i++) {
    if (write (fd, buf, 0) < 0)
      perror("write");
  }

  start();
  for (i = 0; i < n * 1000000; i++) {
    if (read (fd, buf, 0) < 0)
      perror("read");
  }
  nsecs = stop() / (n * 1000);

  printf("%lld ", nsecs);

  start();
  for (i = 0; i < n * 1000000; i++) {
    if (write (fd, buf, 0) < 0)
      perror("write");
  }
  nsecs = stop() / (n * 1000);
  close (fd);

  printf("%lld\n", nsecs);
  unlink("foo");

  return 0;
}
