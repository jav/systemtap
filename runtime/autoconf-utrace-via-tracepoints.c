#include <trace/events/sched.h>
#include <trace/events/syscalls.h>
#include <linux/task_work.h>

// The utrace-less task_finder needs either:
// - 5 specific tracepoints
// - 4 specific tracepoints and ftrace_set_filter()
// Both scenarios need <linux/task_work.h>.
// 
// Check scenario #1.

void __sched_process_fork(void *cb_data __attribute__((unused)),
			  struct task_struct *parent __attribute__((unused)),
			  struct task_struct *child __attribute__((unused)))
{
	return;
}

void __sched_process_exit(void *cb_data __attribute__((unused)),
			  struct task_struct *task __attribute__((unused)))
{
	return;
}

void __sched_process_exec(void *cb_data __attribute__ ((unused)),
			  struct task_struct *task __attribute__((unused)),
			  pid_t old_pid __attribute__((unused)),
			  struct linux_binprm *bprm __attribute__((unused)))
{
	return;
}

void __sys_enter(void *cb_data __attribute__ ((unused)),
		 struct pt_regs *regs __attribute__((unused)),
		 long id __attribute__((unused)))
{
	return;
}

void __sys_exit(void *cb_data __attribute__ ((unused)),
		struct pt_regs *regs __attribute__((unused)),
		long ret __attribute__((unused)))
{
	return;
}

void __autoconf_func(void)
{
	struct task_work work;

	(void) register_trace_sched_process_fork(__sched_process_fork, NULL);
	(void) register_trace_sched_process_exit(__sched_process_exit, NULL);
	(void) register_trace_sched_process_exec(__sched_process_exec, NULL);
	(void) register_trace_sys_enter(__sys_enter, NULL);
	(void) register_trace_sys_exit(__sys_exit, NULL);
	init_task_work(&work, NULL, NULL);
}
