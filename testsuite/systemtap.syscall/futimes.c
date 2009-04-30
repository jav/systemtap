/* COVERAGE: utimes futimes futimesat utimensat */
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <linux/utime.h>

#ifndef UTIME_NOW
#define UTIME_NOW       ((1l << 30) - 1l)
#define UTIME_OMIT      ((1l << 30) - 2l)
#endif

int main()
{
  int fd;
  struct timeval tv[2];
  struct timespec ts[2];
  struct utimbuf times;

  fd = creat("foobar", 0666);

  /* access time */
  tv[0].tv_sec = 1000000000;
  tv[0].tv_usec = 1234;
  tv[1].tv_sec = 2000000000;
  tv[1].tv_usec = 5678;


#ifdef __NR_utime
 times.actime = 1000000000;
 times.modtime = 2000000000;
 syscall(__NR_utime, "foobar", &times );
 //staptest// utime ("foobar", \[Sun Sep  9 01:46:40 2001, Wed May 18 03:33:20 2033])
#endif /* __NR_utimes */
  
#ifdef __NR_utimes
  syscall(__NR_utimes, "foobar", tv);
  //staptest// utimes ("foobar", \[1000000000.001234\]\[2000000000.005678\])
#endif /* __NR_utimes */

#ifdef __NR_futimesat
  syscall(__NR_futimesat, 7, "foobar", tv);
  //staptest// futimesat (7, "foobar", \[1000000000.001234\]\[2000000000.005678\])

  syscall(__NR_futimesat, AT_FDCWD, "foobar", tv);
  //staptest// futimesat (AT_FDCWD, "foobar", \[1000000000.001234\]\[2000000000.005678\])
#endif /* __NR_futimesat */

#ifdef __NR_utimensat
  ts[0].tv_sec = 1000000000;
  ts[0].tv_nsec = 123456789;
  ts[1].tv_sec = 2000000000;
  ts[1].tv_nsec = 56780000;  
  syscall(__NR_utimensat, AT_FDCWD, "foobar", ts, 0);
  //staptest// utimensat (AT_FDCWD, "foobar", \[1000000000.123456789\]\[2000000000.056780000\], 0x0)

  ts[0].tv_sec = 0;
  ts[0].tv_nsec = UTIME_NOW;
  ts[1].tv_sec = 0;
  ts[1].tv_nsec = UTIME_OMIT;
  syscall(__NR_utimensat, AT_FDCWD, "foobar", ts, AT_SYMLINK_NOFOLLOW);
  //staptest// utimensat (AT_FDCWD, "foobar", \[UTIME_NOW\]\[UTIME_OMIT\], AT_SYMLINK_NOFOLLOW)

  syscall(__NR_utimensat, 22, "foobar", ts, 0x42);
  //staptest// utimensat (22, "foobar", \[UTIME_NOW\]\[UTIME_OMIT\], 0x42)

#endif 

  return 0;
}
