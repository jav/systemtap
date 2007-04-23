/* COVERAGE: gettimeofday settimeofday clock_gettime clock_settime clock_getres clock_nanosleep time */
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/syscall.h>

int main()
{
  int t;
  struct timeval tv;
  struct timespec ts;
  time_t tt;

#ifdef SYS_time
  syscall(SYS_time, &tt);
  // time (XXXX) = NNNN
  
  syscall(SYS_time, NULL);
  // time (0x[0]+) = NNNN
#endif

  t = syscall(SYS_gettimeofday, &tv, NULL);
  // gettimeofday (XXXX, 0x[0]+) = 0

  settimeofday(&tv, NULL);
  // settimeofday (\[NNNN.NNNN\], NULL) =

  syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
  // clock_gettime (CLOCK_REALTIME, XXXX) = 0

  syscall(SYS_clock_settime, CLOCK_REALTIME, &ts);
  // clock_settime (CLOCK_REALTIME, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_REALTIME, &ts);
  // clock_getres (CLOCK_REALTIME, XXXX) = 0

  syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
  // clock_gettime (CLOCK_MONOTONIC, XXXX) = 0

  syscall(SYS_clock_settime, CLOCK_MONOTONIC, &ts);
  // clock_settime (CLOCK_MONOTONIC, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_MONOTONIC, &ts);
  // clock_getres (CLOCK_MONOTONIC, XXXX) = 0

  syscall(SYS_clock_gettime, CLOCK_PROCESS_CPUTIME_ID, &ts);
  // clock_gettime (CLOCK_PROCESS_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_settime, CLOCK_PROCESS_CPUTIME_ID, &ts);
  // clock_settime (CLOCK_PROCESS_CPUTIME_ID, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_PROCESS_CPUTIME_ID, &ts);
  // clock_getres (CLOCK_PROCESS_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_gettime, CLOCK_THREAD_CPUTIME_ID, &ts);
  // clock_gettime (CLOCK_THREAD_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_settime, CLOCK_THREAD_CPUTIME_ID, &ts);
  // clock_settime (CLOCK_THREAD_CPUTIME_ID, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_THREAD_CPUTIME_ID, &ts);
  // clock_getres (CLOCK_THREAD_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
  // clock_gettime (CLOCK_REALTIME, XXXX) = 0

  ts.tv_sec++;
  syscall(SYS_clock_nanosleep, CLOCK_REALTIME, TIMER_ABSTIME, &ts);
  // clock_nanosleep (CLOCK_REALTIME, TIMER_ABSTIME, \[NNNN.NNNN\], XXXX) = 0

  ts.tv_sec = 0;   ts.tv_nsec = 10000;  
  syscall(SYS_clock_nanosleep, CLOCK_REALTIME, 0, &ts);
  // clock_nanosleep (CLOCK_REALTIME, 0, \[NNNN.NNNN\], XXXX) = 0
  
  return 0;
}

