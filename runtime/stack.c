/*  -*- linux-c -*-
 * Stack tracing functions
 * Copyright (C) 2005-2009, 2011 Red Hat Inc.
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

static void _stp_stack_print_fallback(unsigned long, int, int, int);

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
        int flags;
        int levels;
        int skip;
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
	if (sdata->skip > 0)
		sdata->skip--;
	else if (sdata->levels > 0) {
		_stp_print_addr(addr,
				sdata->flags | (reliable ? 0 :_STP_SYM_INEXACT),
				NULL);
		sdata->levels--;
	}
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

/* Used for kernel backtrace printing when other mechanisms fail. */
static void _stp_stack_print_fallback(unsigned long stack,
				      int sym_flags, int levels, int skip)
{
        struct print_stack_data print_data;
        print_data.flags = sym_flags;
        print_data.levels = levels;
        print_data.skip = skip;
        dump_trace(current, NULL, (long *)stack, 0, &print_stack_ops,
                   &print_data);
}
#else
static void _stp_stack_print_fallback(unsigned long s, int v, int l, int k) {
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

static void _stp_stack_print(struct context *c, int sym_flags, int stack_flags)
{
	struct pt_regs *regs = NULL;
	struct task_struct *tsk = NULL;
	int uregs_valid = 0;
	struct uretprobe_instance *ri;

	if (c->probe_type == _STP_PROBE_HANDLER_URETPROBE)
		ri = c->ips.ri;
#ifdef STAPCONF_UPROBE_GET_PC
	else if (c->probe_type == _STP_PROBE_HANDLER_UPROBE)
		ri = GET_PC_URETPROBE_NONE;
#endif
	else
		ri = NULL;

	if (stack_flags == _STP_STACK_KERNEL) {
		if (! c->regs
		    || (c->probe_flags & _STP_PROBE_STATE_USER_MODE)) {
			/* For the kernel we can use an inexact fallback.
			   When compiled with frame pointers we can do
			   a pretty good guess at the stack value,
			   otherwise let dump_stack guess it
			   (and skip some framework frames). */
#if defined(STAPCONF_KERNEL_STACKTRACE)
			unsigned long sp;
			int skip;
#ifdef CONFIG_FRAME_POINTER
			sp  = *(unsigned long *) __builtin_frame_address (0);
			skip = 1; /* Skip just this frame. */
#else
			sp = 0;
			skip = 5; /* yes, that many framework frames. */
#endif
			_stp_stack_print_fallback(sp, sym_flags,
						  MAXBACKTRACE, skip);
#else
			if (sym_flags & _STP_SYM_SYMBOL)
				_stp_printf("<no kernel backtrace at %s>\n",
					    c->probe_point);
			else
				_stp_print("\n");
#endif
			return;
		} else {
			regs = c->regs;
			ri = NULL; /* This is a hint for GCC so that it can
				      eliminate the call to uprobe_get_pc()
				      in __stp_stack_print() below. */
		}
	} else if (stack_flags == _STP_STACK_USER) {
		/* use task_pt_regs, regs might be kernel regs, or not set. */
		if (c->regs && (c->probe_flags & _STP_PROBE_STATE_USER_MODE)) {
			regs = c->regs;
			uregs_valid = 1;
		} else {
			regs = task_pt_regs(current);
			uregs_valid = _stp_task_pt_regs_valid(current, regs);
		}

		if (! current->mm || ! regs) {
			if (sym_flags & _STP_SYM_SYMBOL)
				_stp_printf("<no user backtrace at %s>\n",
					    c->probe_point);
			else
				_stp_print("\n");
			return;
		} else {
			tsk = current;
		}
	}

	/* print the current address */
	if (stack_flags == _STP_STACK_KERNEL
	    && c->probe_type == _STP_PROBE_HANDLER_KRETPROBE
	    && c->ips.krp.pi) {
		if ((sym_flags & _STP_SYM_FULL) == _STP_SYM_FULL) {
			_stp_print("Returning from: ");
			_stp_print_addr((unsigned long)_stp_probe_addr_r(c->ips.krp.pi),
					sym_flags, tsk);
			_stp_print("Returning to  : ");
		}
		_stp_print_addr((unsigned long)_stp_ret_addr_r(c->ips.krp.pi),
				sym_flags, tsk);
#ifdef STAPCONF_UPROBE_GET_PC
	} else if (stack_flags == _STP_STACK_USER
		   && c->probe_type == _STP_PROBE_HANDLER_URETPROBE
		   && ri) {
		if ((sym_flags & _STP_SYM_FULL) == _STP_SYM_FULL) {
			_stp_print("Returning from: ");
			/* ... otherwise this dereference fails */
			_stp_print_addr(ri->rp->u.vaddr, sym_flags, tsk);
			_stp_print("Returning to  : ");
			_stp_print_addr(ri->ret_addr, sym_flags, tsk);
		} else
			_stp_print_addr(ri->ret_addr, sym_flags, tsk);
#endif
	} else {
		_stp_print_addr(REG_IP(regs), sym_flags, tsk);
	}

	/* print rest of stack... */
	__stp_stack_print(regs, sym_flags, MAXBACKTRACE, tsk,
			  &c->uwcontext, ri, uregs_valid);
}

/** Writes stack backtrace to a string
 *
 * @param str string
 * @param regs A pointer to the struct pt_regs.
 * @returns void
 */
static void _stp_stack_sprint(char *str, int size, struct context* c,
			      int sym_flags, int stack_flags)
{
	/* To get an hex string, we use a simple trick.
	 * First flush the print buffer,
	 * then call _stp_stack_print,
	 * then copy the result into the output string
	 * and clear the print buffer. */
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	_stp_print_flush();

	_stp_stack_print(c, sym_flags, stack_flags);

	strlcpy(str, pb->buf, size < (int)pb->len ? size : (int)pb->len);
	pb->len = 0;
}

#endif /* CONFIG_KPROBES */

#endif /* _STACK_C_ */
