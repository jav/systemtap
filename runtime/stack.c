/*  -*- linux-c -*-
 * Stack tracing functions
 * Copyright (C) 2005-2009 Red Hat Inc.
 * Copyright (C) 2005 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STACK_C_
#define _STACK_C_

/** @file stack.c
 * @brief Stack Tracing Functions
 */

/** @addtogroup stack Stack Tracing Functions
 *
 * @{
 */

#include "sym.c"
#include "regs.h"
#include "unwind.c"

#define MAXBACKTRACE 20

#if defined(STAPCONF_KERNEL_STACKTRACE)
#include <linux/stacktrace.h>
#include <asm/stacktrace.h>
#endif

static void _stp_stack_print_fallback(unsigned long, int, int);

#if defined (__x86_64__)
#include "stack-x86_64.c"
#elif defined (__ia64__)
#include "stack-ia64.c"
#elif  defined (__i386__)
#include "stack-i386.c"
#elif defined (__powerpc64__)
#include "stack-ppc64.c"
#elif defined (__arm__)
#include "stack-arm.c"
#elif defined (__s390__) || defined (__s390x__)
#include "stack-s390.c"
#else
#error "Unsupported architecture"
#endif

#if defined(STAPCONF_KERNEL_STACKTRACE)

struct print_stack_data
{
        int verbose;
        int max_level;
        int level;
};

static void print_stack_warning(void *data, char *msg)
{
}

static void
print_stack_warning_symbol(void *data, char *msg, unsigned long symbol)
{
}

static int print_stack_stack(void *data, char *name)
{
	return -1;
}

static void print_stack_address(void *data, unsigned long addr, int reliable)
{
	struct print_stack_data *sdata = data;
        if (sdata->level++ < sdata->max_level)
                _stp_func_print(addr, sdata->verbose, 0, NULL);
}

static const struct stacktrace_ops print_stack_ops = {
	.warning = print_stack_warning,
	.warning_symbol = print_stack_warning_symbol,
	.stack = print_stack_stack,
	.address = print_stack_address,
};

static void _stp_stack_print_fallback(unsigned long stack, int verbose, int levels)
{
        struct print_stack_data print_data;
        print_data.verbose = verbose;
        print_data.max_level = levels;
        print_data.level = 0;
        dump_trace(current, NULL, (long *)stack, 0, &print_stack_ops,
                   &print_data);
}
#endif

// Without KPROBES very little works atm.
// But this file is unconditionally imported, while these two functions are only
// used through context-unwind.stp.
#if defined (CONFIG_KPROBES)

/** Prints the stack backtrace
 * @param regs A pointer to the struct pt_regs.
 */

static void _stp_stack_print(struct pt_regs *regs, int verbose, struct kretprobe_instance *pi, int levels, struct task_struct *tsk)
{
	if (verbose) {
		/* print the current address */
		if (pi) {
			_stp_print("Returning from: ");
			_stp_symbol_print((unsigned long)_stp_probe_addr_r(pi));
			_stp_print("\nReturning to  : ");
			_stp_symbol_print((unsigned long)_stp_ret_addr_r(pi));
		} else {
			_stp_print_char(' ');
			_stp_symbol_print(REG_IP(regs));
		}
		_stp_print_char('\n');
	} else if (pi)
		_stp_printf("%p %p ", (int64_t)(long)_stp_ret_addr_r(pi), (int64_t) REG_IP(regs));
	else 
		_stp_printf("%p ", (int64_t) REG_IP(regs));

	__stp_stack_print(regs, verbose, levels, tsk);
}

/** Writes stack backtrace to a string
 *
 * @param str string
 * @param regs A pointer to the struct pt_regs.
 * @returns void
 */
static void _stp_stack_snprint(char *str, int size, struct pt_regs *regs, int verbose, struct kretprobe_instance *pi, int levels, struct task_struct *tsk)
{
	/* To get a string, we use a simple trick. First flush the print buffer, */
	/* then call _stp_stack_print, then copy the result into the output string  */
	/* and clear the print buffer. */
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	_stp_print_flush();
	_stp_stack_print(regs, verbose, pi, levels, tsk);
	strlcpy(str, pb->buf, size < (int)pb->len ? size : (int)pb->len);
	pb->len = 0;
}

#endif /* CONFIG_KPROBES */

/** Prints the user stack backtrace
 * @param str string
 * @returns Same string as was input with trace info appended,
 * @note Currently limited to a depth of two. Works from jprobes and kprobes.
 */
#if 0
static void _stp_ustack_print(char *str)
{
	struct pt_regs *nregs = ((struct pt_regs *)(THREAD_SIZE + (unsigned long)current->thread_info)) - 1;
	_stp_printf("%p : [user]\n", (int64_t) REG_IP(nregs));
	if (REG_SP(nregs))
		_stp_printf("%p : [user]\n", (int64_t) (*(unsigned long *)REG_SP(nregs)));
}
#endif /* 0 */

/** @} */

void _stp_stack_print_tsk(struct task_struct *tsk, int verbose, int levels)
{
#if defined(STAPCONF_KERNEL_STACKTRACE)
        int i;
        unsigned long backtrace[MAXBACKTRACE];
        struct stack_trace trace;
        int maxLevels = min(levels, MAXBACKTRACE);
        memset(&trace, 0, sizeof(trace));
        trace.entries = &backtrace[0];
        trace.max_entries = maxLevels;
        trace.skip = 0;
        save_stack_trace_tsk(tsk, &trace);
        for (i = 0; i < maxLevels; ++i) {
                if (backtrace[i] == 0 || backtrace[i] == ULONG_MAX)
                        break;
                _stp_printf("%lx ", backtrace[i]);
        }
#endif
}

/** Writes a task stack backtrace to a string
 *
 * @param str string
 * @param tsk A pointer to the task_struct
 * @returns void
 */
void _stp_stack_snprint_tsk(char *str, int size, struct task_struct *tsk, int verbose, int levels)
{
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	_stp_print_flush();
	_stp_stack_print_tsk(tsk, verbose, levels);
	strlcpy(str, pb->buf, size < (int)pb->len ? size : (int)pb->len);
	pb->len = 0;
}
#endif /* _STACK_C_ */
