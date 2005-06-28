#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>

typedef unsigned long long uint64;

struct timeval t1;

void start(struct timeval *tv)
{
  gettimeofday (tv, NULL);
}

uint64 time_delta(struct timeval *start, struct timeval *stop)
{
  uint64 secs, usecs;
  
  secs = stop->tv_sec - start->tv_sec;
  usecs = stop->tv_usec - start->tv_usec;
  if (usecs < 0) {
    secs--;
    usecs += 1000000;
  }
  return secs * 1000000 + usecs;
}

uint64 stop(struct timeval *begin)
{
  struct timeval end;
  gettimeofday (&end, NULL);
  return time_delta (begin, &end);
}


int main()
{
  int fd, i;
  char buf[1024];
  uint64 nsecs;

  system ("touch foo");
  fd = open ("foo", O_RDWR);

  start(&t1);
  for (i = 0; i < 1000000; i++) {
    if (read (fd, buf, 0) < 0)
      perror("read");
  }
  nsecs = stop(&t1) / 1000;

  printf("%lld ", nsecs);

  start(&t1);
  for (i = 0; i < 1000000; i++) {
    if (write (fd, buf, 0) < 0)
      perror("write");
  }
  nsecs = stop(&t1) / 1000;
  close (fd);

  printf("%lld\n", nsecs);

  return 0;
}
