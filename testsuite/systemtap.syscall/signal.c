/* COVERAGE: signal kill tgkill sigprocmask sigaction getpid */
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>

static void 
sig_act_handler(int signo)
{
}

int main()
{
  sigset_t mask;
  struct sigaction sa;
  pid_t pid;

#ifdef SYS_signal
  syscall(SYS_signal, SIGUSR1, SIG_IGN);
  //staptest// signal (SIGUSR1, SIG_IGN)

  syscall (SYS_signal, SIGUSR1, SIG_DFL);
  //staptest// signal (SIGUSR1, SIG_DFL) = 1

  syscall (SYS_signal, SIGUSR1, sig_act_handler);
  //staptest// signal (SIGUSR1, XXXX) = 0
#endif

  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR2);

#ifdef SYS_sigprocmask
  syscall (SYS_sigprocmask, SIG_BLOCK, &mask, NULL);
  //staptest// sigprocmask (SIG_BLOCK, XXXX, 0x0+) = 0

  syscall (SYS_sigprocmask, SIG_UNBLOCK, &mask, NULL);
  //staptest// sigprocmask (SIG_UNBLOCK, XXXX, 0x0+) = 0
#endif

  memset(&sa, 0, sizeof(sa));
  sigfillset(&sa.sa_mask);
  sa.sa_handler = SIG_IGN;

#ifdef SYS_sigaction
  syscall (SYS_sigaction, SIGUSR1, &sa, NULL);
  //staptest// sigaction (SIGUSR1, {SIG_IGN}, 0x0+) = 0
 #endif

#ifdef SYS_tgkill
  syscall(SYS_tgkill, 1234, 5678, 0);
  //staptest// tgkill (1234, 5678, SIG_0)
#endif

  return 0;
}
