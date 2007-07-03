/* COVERAGE: utimes futimes futimesat */
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/time.h>
#include <fcntl.h>

int main()
{
  int fd;
  struct timeval tv[2];

  fd = creat("foobar", 0666);

  /* access time */
  tv[0].tv_sec = 1000000000;
  tv[0].tv_usec = 1234;
  tv[1].tv_sec = 2000000000;
  tv[1].tv_usec = 5678;
  
  utimes("foobar", tv);
  // utimes ("foobar", \[1000000000.001234\]\[2000000000.005678\])

  futimes(fd, tv);
  // futimesat (-100, "foobar", \[1000000000.001234\]\[2000000000.005678\])

  futimesat(7, "foobar", tv);
  // futimesat (7, "foobar", \[1000000000.001234\]\[2000000000.005678\])

  futimesat(AT_FDCWD, "foobar", tv);
  // futimesat (-100, "foobar", \[1000000000.001234\]\[2000000000.005678\])

  return 0;
}
