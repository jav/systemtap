/* COVERAGE: signalfd */
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

int main()
{
  sigset_t mask;
  int sfd;

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGQUIT);
  sfd = signalfd(-1, &mask, 0);
  //staptest// signalfd (-1, XXXX, NNNN) = NNNN

  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGUSR2);
  sfd = signalfd(sfd, &mask, 0);
  //staptest// signalfd (NNNN, XXXX, NNNN) = NNNN

#if defined(SFD_NONBLOCK) && defined(SFD_CLOEXEC)
  sfd = signalfd(-1, &mask, SFD_NONBLOCK);
  //staptest// signalfd4 (-1, XXXX, NNNN, SFD_NONBLOCK) = NNNN
  sfd = signalfd(-1, &mask, SFD_CLOEXEC);
  //staptest// signalfd4 (-1, XXXX, NNNN, SFD_CLOEXEC) = NNNN
  sfd = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
  //staptest// signalfd4 (-1, XXXX, NNNN, SFD_NONBLOCK|SFD_CLOEXEC) = NNNN
#endif

  return 0;
}
