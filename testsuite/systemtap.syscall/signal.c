/* COVERAGE: signal kill tgkill sigprocmask sigaction getpid */
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

#ifdef SYS_signal

static void 
sig_act_handler(int signo)
{
}

int main()
{
  sigset_t mask;
  struct sigaction sa;
  pid_t pid;

  syscall(SYS_signal, SIGUSR1, SIG_IGN);
  // signal (SIGUSR1, 0x00000001) = 0

  syscall(SYS_signal, SIGUSR1, SIG_DFL);
  // signal (SIGUSR1, 0x00000000) = 1

  syscall(SYS_signal, SIGUSR1, sig_act_handler);
  // signal (SIGUSR1, XXXX) = 0

  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  syscall(SYS_sigprocmask,SIG_BLOCK, &mask, NULL);
  // sigprocmask (SIG_BLOCK, XXXX, 0x[0]+) = 0

  syscall(SYS_sigprocmask,SIG_UNBLOCK, &mask, NULL);
  // sigprocmask (SIG_UNBLOCK, XXXX, 0x[0]+) = 0

  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGALRM);
  sa.sa_flags = 0;
  syscall(SYS_sigaction,SIGUSR1, &sa, NULL);
  // sigaction (SIGUSR1, XXXX, 0x[0]+) = 0

  /* syscall(SYS_kill,0,SIGUSR1);
   kill (0, SIGUSR1) = 0

   pid = getpid();
   getpid () = NNNN


  syscall(SYS_tgkill,pid,pid,SIGUSR1);
   tgkill (NNNN, NNNN, SIGUSR1) = 0
  */

  return 0;
}

#endif
