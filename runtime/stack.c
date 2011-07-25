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

/*
  The translator will only include this file if the session needs any
  of the backtrace functions.  Currently indicated by having the session
  need_unwind flag, which is set by tapset functions marked with
  pragme:unwind.
*/

#ifndef _STACK_C_
#define _STACK_C_

/* Maximum number of backtrace levels. */
#ifndef MAXBACKTRACE
#define MAXBACKTRACE 20
#endif

/** @file stack.c
 * @brief Stack Tracing Functions
 */

/** @addtogroup stack Stack Tracing Functions
 *
 * @{
 */

#include "sym.c"
#include "regs.h"

/* DWARF unwinder only tested so far on i386 and x86_64.
   We only need to compile in the unwinder when both STP_NEED_UNWIND_DATA
   (set when a stap script defines pragma:unwind, as done in
   [u]context-unwind.stp) is defined and the architecture actually supports
   dwarf unwinding (as defined by STP_USE_DWARF_UNWINDER in runtime.h).  */
#ifdef STP_USE_DWARF_UNWINDER
#include "unwind.c"
#else
struct unwind_context { };
#endif

#define MAXBACKTRACE 20

/* If uprobes isn't in the kernel, pull it in from the runtime. */
#if defined(CONFIG_UTRACE)      /* uprobes doesn't work without utrace */
#if defined(CONFIG_UPROBES) || defined(CONFIG_UPROBES_MODULE)
#include <linux/uprobes.h>
#else
#include "uprobes/uprobes.h"
#endif
#ifndef UPROBES_API_VERSION
#define UPROBES_API_VERSION 1
#endif
#else
struct uretprobe_instance;
#endif

#if defined(STAPCONF_KERNEL_STACKTRACE)
#include <linux/stacktrace.h>
#include <asm/stacktrace.h>
#endif

static void _stp_stack_print_fallback(unsigned long, int, int);

#if (defined(__i386__) || defined(__x86_64__))
#include "stack-x86.c"
#elif defined (__ia64__)
#include "stack-ia64.c"
#elif defined (__powerpc__)
#include "stack-ppc.c"
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

#if defined(STAPCONF_STACKTRACE_OPS_WARNING)
static void print_stack_warning(void *data, char *msg)
{
}

static void
print_stack_warning_symbol(void *data, char *msg, unsigned long symbol)
{
}
#endif

static int print_stack_stack(void *data, char *name)
{
	return -1;
}

static void print_stack_address(void *data, unsigned long addr, int reliable)
{
	struct print_stack_data *sdata = data;
        if (sdata->level++ < sdata->max_level)
                _stp_print_addr(addr, sdata->verbose | _STP_SYM_INEXACT, NULL);
}

static const struct stacktrace_ops print_stack_ops = {
#if defined(STAPCONF_STACKTRACE_OPS_WARNING)
	.warning = print_stack_warning,
	.warning_symbol = print_stack_warning_symbol,
#endif
	.stack = print_stack_stack,
	.address = print_stack_address,
#if defined(STAPCONF_WALK_STACK)
	.walk_stack = print_context_stack,
#endif
};

/* Currently only used by the stack-x64.c code when dwarf unwinding fails. */
static void _stp_stack_print_fallback(unsigned long stack, int verbose, int levels)
{
        struct print_stack_data print_data;
        print_data.verbose = verbose;
        print_data.max_level = levels;
        print_data.level = 0;
        dump_trace(current, NULL, (long *)stack, 0, &print_stack_ops,
                   &print_data);
}
#else
static void _stp_stack_print_fallback(unsigned long s, int v, int l) {
	/* Don't guess, just give up. */
	_stp_print_addr(0, v | _STP_SYM_INEXACT, NULL);
}
#endif /* defined(STAPCONF_KERNEL_STACKTRACE) */

// Without KPROBES very little works atm.
// But this file is unconditionally imported, while these two functions are only
// used through context-unwind.stp.
#if defined (CONFIG_KPROBES)

/** Prints the stack backtrace
 * @param regs A pointer to the struct pt_regs.
 * @param verbose _STP_SYM_FULL or _STP_SYM_BRIEF
 */

static void _stp_stack_print(struct pt_regs *regs, int verbose,
			     struct kretprobe_instance *pi,
			     struct task_struct *tsk,
			     struct unwind_context *context,
			     struct uretprobe_instance *ri, int uregs_valid)
{
	/* print the current address */
	if (pi) {
		if ((verbose & _STP_SYM_FULL) == _STP_SYM_FULL) {
			_stp_print("Returning from: ");
			_stp_print_addr((unsigned long)_stp_probe_addr_r(pi),
					verbose, tsk);
			_stp_print("Returning to  : ");
		}
		_stp_print_addr((unsigned long)_stp_ret_addr_r(pi), verbose, tsk);
#ifdef STAPCONF_UPROBE_GET_PC
	} else if (ri && ri != GET_PC_URETPROBE_NONE) {
		if ((verbose & _STP_SYM_FULL) == _STP_SYM_FULL) {
			_stp_print("Returning from: ");
			/* ... otherwise this dereference fails */
			_stp_print_addr(ri->rp->u.vaddr, verbose, tsk);
			_stp_print("Returning to  : ");
			_stp_print_addr(ri->ret_addr, verbose, tsk);
		} else
			_stp_print_addr(ri->ret_addr, verbose, tsk);
#endif
	} else {
		_stp_print_addr(REG_IP(regs), verbose, tsk);
	}

	/* print rest of stack... */
	__stp_stack_print(regs, verbose, MAXBACKTRACE, tsk,
			  context, ri, uregs_valid);
}

/** Writes stack backtrace to a string
 *
 * @param str string
 * @param regs A pointer to the struct pt_regs.
 * @returns void
 */
static void _stp_stack_sprint(char *str, int size, int flags,
			      struct pt_regs *regs,
			      struct kretprobe_instance *pi,
			      struct task_struct *tsk,
			      struct unwind_context *context,
			      struct uretprobe_instance *ri, int uregs_valid)
{
	/* To get an hex string, we use a simple trick.
	 * First flush the print buffer,
	 * then call _stp_stack_print,
	 * then copy the result into the output string
	 * and clear the print buffer. */
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	_stp_print_flush();

	if (pi)
		_stp_print_addr((int64_t) (long) _stp_ret_addr_r(pi),
				flags, tsk);

	_stp_print_addr((int64_t) REG_IP(regs), flags, tsk);
	__stp_stack_print(regs, flags, MAXBACKTRACE, tsk,
			  context, ri, uregs_valid);

	strlcpy(str, pb->buf, size < (int)pb->len ? size : (int)pb->len);
	pb->len = 0;
}

#endif /* CONFIG_KPROBES */

#endif /* _STACK_C_ */
