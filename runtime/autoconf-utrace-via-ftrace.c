#include <trace/events/sched.h>
#include <trace/events/syscalls.h>
#include <linux/ftrace.h>

// The utrace-less task_finder needs either:
// - 5 specific tracepoints
// - 4 specific tracepoints and ftrace_set_filter()
// Check scenario #2.

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

struct ftrace_ops __ftrace_ops;

void __autoconf_func(void)
{
	char *report_exec_name;

	(void) register_trace_sched_process_fork(__sched_process_fork, NULL);
	(void) register_trace_sched_process_exit(__sched_process_exit, NULL);
	(void) register_trace_sys_enter(__sys_enter, NULL);
	(void) register_trace_sys_exit(__sys_exit, NULL);

	report_exec_name = "*" __stringify(proc_exec_connector);
	ftrace_set_filter(&__ftrace_ops, report_exec_name,
			  strlen(report_exec_name), 1);
	(void) register_ftrace_function(&__ftrace_ops);
}
