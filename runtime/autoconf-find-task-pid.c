#include <linux/sched.h>

void foo (pid_t k) {
  struct task_struct *tsk = find_task_by_pid (k);
  (void) tsk;
}
