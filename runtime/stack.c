/*  -*- linux-c -*-
 * Stack tracing functions
 * Copyright (C) 2005-2008 Red Hat Inc.
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

/** Prints the stack backtrace
 * @param regs A pointer to the struct pt_regs.
 */

void _stp_stack_print(struct pt_regs *regs, int verbose, struct kretprobe_instance *pi, int levels)
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

	__stp_stack_print(regs, verbose, levels);
}

/** Writes stack backtrace to a string
 *
 * @param str string
 * @param regs A pointer to the struct pt_regs.
 * @returns void
 */
void _stp_stack_snprint(char *str, int size, struct pt_regs *regs, int verbose, struct kretprobe_instance *pi, int levels)
{
	/* To get a string, we use a simple trick. First flush the print buffer, */
	/* then call _stp_stack_print, then copy the result into the output string  */
	/* and clear the print buffer. */
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	_stp_print_flush();
	_stp_stack_print(regs, verbose, pi, levels);
	strlcpy(str, pb->buf, size < (int)pb->len ? size : (int)pb->len);
	pb->len = 0;
}

/** Prints the user stack backtrace
 * @param str string
 * @returns Same string as was input with trace info appended,
 * @note Currently limited to a depth of two. Works from jprobes and kprobes.
 */
#if 0
void _stp_ustack_print(char *str)
{
	struct pt_regs *nregs = ((struct pt_regs *)(THREAD_SIZE + (unsigned long)current->thread_info)) - 1;
	_stp_printf("%p : [user]\n", (int64_t) REG_IP(nregs));
	if (REG_SP(nregs))
		_stp_printf("%p : [user]\n", (int64_t) (*(unsigned long *)REG_SP(nregs)));
}
#endif /* 0 */

/** @} */
#endif /* _STACK_C_ */
