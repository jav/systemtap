/* COVERAGE: timer_create timer_gettime timer_settime timer_getoverrun timer_delete */
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>

int main()
{
  timer_t tid=0;
  struct itimerspec val, oval;

  syscall(SYS_timer_create, CLOCK_REALTIME, NULL, &tid);
  //staptest// timer_create (CLOCK_REALTIME, 0x[0]+, XXXX)

  syscall(SYS_timer_gettime, tid, &val);
  //staptest// timer_gettime (NNNN, XXXX)

  syscall(SYS_timer_settime, 0, tid, &val, &oval);
  //staptest// timer_settime (0, NNNN, \[0.000000,0.000000\], XXXX)

  syscall(SYS_timer_getoverrun, tid);
  //staptest// timer_getoverrun (NNNN)

  syscall(SYS_timer_delete, tid);
  //staptest// timer_delete (NNNN)

  return 0;
}
 
