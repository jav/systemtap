/* COVERAGE: getitimer setitimer */
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <signal.h>

static void
alarm_handler(int signo, siginfo_t *info, void *context)
{
}

int main()
{
  struct sigaction act;
  struct itimerval itv, old_itv;

  memset(&act, 0, sizeof(act));
  act.sa_handler = (void *)alarm_handler;
  sigaction(SIGALRM, &act, NULL);
  sigaction(SIGVTALRM, &act, NULL);
  sigaction(SIGPROF, &act, NULL);


  memset(&itv, 0, sizeof(itv));
  itv.it_interval.tv_sec = 0;
  itv.it_interval.tv_usec = 500000;
  itv.it_value.tv_sec = 1;
  itv.it_value.tv_usec = 0;
  setitimer(ITIMER_REAL, &itv, &old_itv);
  //staptest// setitimer (ITIMER_REAL, \[0.500000,1.000000\], XXXX) = 0

  itv.it_value.tv_sec = 0;
  itv.it_value.tv_usec = 0;
  setitimer(ITIMER_REAL, &itv, NULL);
  //staptest// setitimer (ITIMER_REAL, \[0.500000,0.000000\], 0x[0]+) = 0

  setitimer(ITIMER_VIRTUAL, &itv, NULL);
  //staptest// setitimer (ITIMER_VIRTUAL, \[0.500000,0.000000\], 0x[0]+) = 0
  
  setitimer(ITIMER_PROF, &itv, NULL);
  //staptest// setitimer (ITIMER_PROF, \[0.500000,0.000000\], 0x[0]+) = 0

  getitimer(ITIMER_REAL, &itv);
  //staptest// getitimer (ITIMER_REAL, XXXX) = 0

  getitimer(ITIMER_VIRTUAL, &itv);
  //staptest// getitimer (ITIMER_VIRTUAL, XXXX) = 0

  getitimer(ITIMER_PROF, &itv);
  //staptest// getitimer (ITIMER_PROF, XXXX) = 0
  return 0;
}
 
