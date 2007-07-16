/* COVERAGE: select pselect6 pselect7 */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>

int main()
{
  int fd;
  struct timespec tim = {0, 200000000};
  sigset_t sigs;
  fd_set rfds;
  struct timeval tv = {0, 117};

  sigemptyset(&sigs);
  sigaddset(&sigs,SIGUSR2);

  select( 1, &rfds, NULL, NULL, &tv);
  // select (1, XXXX, 0x[0]+, 0x[0]+, \[0.000117\])

  tv.tv_sec = 0;
  tv.tv_usec = 113;

  select( 1, NULL, NULL, NULL, &tv);
  // select (1, 0x[0]+, 0x[0]+, 0x[0]+, \[0.000113\])

#ifdef SYS_pselect6
  pselect( 1, &rfds, NULL, NULL, &tim, &sigs);
  //pselect[67] (1, XXXX, 0x[0]+, 0x[0]+, \[0.200000000\], XXXX)

  pselect( 0, NULL, NULL, NULL, &tim, &sigs);
  // pselect[67] (0, 0x[0]+, 0x[0]+, 0x[0]+, \[0.200000000\], XXXX) =
#endif

  return 0;
}
