/* COVERAGE:  rt_sigprocmask rt_sigaction */
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>


static void 
sig_act_handler(int signo)
{
}


int main()
{
  sigset_t mask;
  struct sigaction sa;

  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  sigprocmask(SIG_BLOCK, &mask, NULL);
  // rt_sigprocmask (SIG_BLOCK, XXXX, 0x[0]+, 8) = 0

  sigprocmask(SIG_UNBLOCK, &mask, NULL);
  // rt_sigprocmask (SIG_UNBLOCK, XXXX, 0x[0]+, 8) = 0

  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGALRM);
  sa.sa_flags = 0;
  sigaction(SIGUSR1, &sa, NULL);
  // rt_sigaction (SIGUSR1, XXXX, 0x[0]+, 8) = 0

  sa.sa_handler = SIG_DFL;
  sigaction(SIGUSR1, &sa, NULL);
  // rt_sigaction (SIGUSR1, XXXX, 0x[0]+, 8) = 0
  
  sa.sa_handler = sig_act_handler;
  sigaction(SIGUSR1, &sa, NULL);
  // rt_sigaction (SIGUSR1, XXXX, 0x[0]+, 8) = 0

  return 0;
}

