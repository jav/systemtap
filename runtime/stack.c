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

#if defined(STAPCONF_KERNEL_STACKTRACE) || defined(STAPCONF_KERNEL_STACKTRACE_NO_BP)
#include <linux/stacktrace.h>
#include <asm/stacktrace.h>
#endif

static void _stp_stack_print_fallback(unsigned long, int, int, int);

#ifdef STP_USE_DWARF_UNWINDER
#include "stack-dwarf.c"
#endif

#if defined (__ia64__)
#include "stack-ia64.c"
#elif defined (__arm__)
#include "stack-arm.c"
#elif defined (__s390__)
#include "stack-s390.c"
#else
#ifndef STP_USE_DWARF_UNWINDER
#error "Unsupported architecture"
#endif
#endif

#if defined(STAPCONF_KERNEL_STACKTRACE) || defined(STAPCONF_KERNEL_STACKTRACE_NO_BP)

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
#if defined(STAPCONF_KERNEL_STACKTRACE)
        dump_trace(current, NULL, (long *)stack, 0, &print_stack_ops,
                   &print_data);
#else
	/* STAPCONF_KERNEL_STACKTRACE_NO_BP */
        dump_trace(current, NULL, (long *)stack, &print_stack_ops,
                   &print_data);
#endif
}
#else
static void _stp_stack_print_fallback(unsigned long s, int v, int l, int k) {
	/* Don't guess, just give up. */
	_stp_print_addr(0, v | _STP_SYM_INEXACT, NULL);
}

#endif /* defined(STAPCONF_KERNEL_STACKTRACE) || defined(STAPCONF_KERNEL_STACKTRACE_NO_BP) */

// Without KPROBES very little works atm.
// But this file is unconditionally imported, while these two functions are only
// used through context-unwind.stp.
#if defined (CONFIG_KPROBES)

/** Gets user space registers when available, also sets context probe_flags
 * _STP_PROBE_STATE_FULL_UREGS if appropriate.  Should be used instead of
 * accessing context uregs field directly when (full) uregs are needed
 * from kernel context.
 */
static struct pt_regs *_stp_get_uregs(struct context *c)
{
  /* When the probe occurred in user context uregs are always complete. */
  if (c->uregs && c->probe_flags & _STP_PROBE_STATE_USER_MODE)
    c->probe_flags |= _STP_PROBE_STATE_FULL_UREGS;
  else if (c->uregs == NULL)
    {
      /* First try simple recovery through task_pt_regs,
	 on some platforms that already provides complete uregs. */
      c->uregs = _stp_current_pt_regs();
      if (c->uregs && _stp_task_pt_regs_valid(current, c->uregs))
	c->probe_flags |= _STP_PROBE_STATE_FULL_UREGS;

/* Sadly powerpc does support the dwarf unwinder, but doesn't have enough
   CFI in the kernel to recover fully to user space. */
#if defined(STP_USE_DWARF_UNWINDER) && !defined (__powerpc__)
      else if (c->uregs != NULL && c->kregs != NULL
	       && ! (c->probe_flags & _STP_PROBE_STATE_USER_MODE))
	{
	  struct unwind_frame_info *info = &c->uwcontext.info;
	  int ret = 0;
	  int levels;

	  /* We might be lucky and this probe already ran the kernel
	     unwind to end up in the user regs. */
	  if (UNW_PC(info) == REG_IP(c->uregs))
	    {
	      levels = 0;
	      dbug_unwind(1, "feeling lucky, info pc == uregs pc\n");
	    }
	  else
	    {
	      /* Try to recover the uregs by unwinding from the the kernel
		 probe location. */
	      levels = MAXBACKTRACE;
	      arch_unw_init_frame_info(info, c->kregs, 0);
	      dbug_unwind(1, "Trying to recover... searching for 0x%lx\n",
			  REG_IP(c->uregs));
	    }

	  while (levels > 0 && ret == 0 && UNW_PC(info) != REG_IP(c->uregs))
	    {
	      levels--;
	      ret = unwind(&c->uwcontext, 0);
	      dbug_unwind(1, "unwind levels: %d, ret: %d, pc=0x%lx\n",
			  levels, ret, UNW_PC(info));
	    }

	  /* Have we arrived where we think user space currently is? */
	  if (ret == 0 && UNW_PC(info) == REG_IP(c->uregs))
	    {
	      /* Note we need to clear this state again when the unwinder
		 has been rerun. See __stp_stack_print invocation below. */
	      UNW_SP(info) = REG_SP(c->uregs); /* Fix up user stack */
	      c->uregs = &info->regs;
	      c->probe_flags |= _STP_PROBE_STATE_FULL_UREGS;
	      dbug_unwind(1, "recovered with pc=0x%lx sp=0x%lx\n",
			  UNW_PC(info), UNW_SP(info));
	    }
	  else
	    dbug_unwind(1, "failed to recover user reg state\n");
	}
#endif
    }
  return c->uregs;
}

/** Prints the stack backtrace
 * @param regs A pointer to the struct pt_regs.
 * @param verbose _STP_SYM_FULL or _STP_SYM_BRIEF
 */

static void _stp_stack_kernel_print(struct context *c, int sym_flags)
{
	struct pt_regs *regs = NULL;

	if (! c->kregs) {
		/* For the kernel we can use an inexact fallback.
		   When compiled with frame pointers we can do
		   a pretty good guess at the stack value,
		   otherwise let dump_stack guess it
		   (and skip some framework frames). */
#if defined(STAPCONF_KERNEL_STACKTRACE) || defined(STAPCONF_KERNEL_STACKTRACE_NO_BP)
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
		regs = c->kregs;
	}

	/* print the current address */
	if (c->probe_type == _STP_PROBE_HANDLER_KRETPROBE && c->ips.krp.pi) {
		if ((sym_flags & _STP_SYM_FULL) == _STP_SYM_FULL) {
			_stp_print("Returning from: ");
			_stp_print_addr((unsigned long)_stp_probe_addr_r(c->ips.krp.pi),
					sym_flags, NULL);
			_stp_print("Returning to  : ");
		}
		_stp_print_addr((unsigned long)_stp_ret_addr_r(c->ips.krp.pi),
				sym_flags, NULL);
	} else {
		_stp_print_addr(REG_IP(regs), sym_flags, NULL);
	}

	/* print rest of stack... */
#ifdef STP_USE_DWARF_UNWINDER
	if (c->uregs == &c->uwcontext.info.regs) {
		/* Unwinder needs the reg state, clear uregs ref. */
		c->uregs = NULL;
		c->probe_flags &= ~_STP_PROBE_STATE_FULL_UREGS;
	}
	__stp_dwarf_stack_kernel_print(regs, sym_flags, MAXBACKTRACE,
				       &c->uwcontext);
#else
	/* Arch specific fallback for kernel backtraces. */
	__stp_stack_print(regs, sym_flags, MAXBACKTRACE);
#endif
}

static void _stp_stack_user_print(struct context *c, int sym_flags)
{
	struct pt_regs *regs = NULL;
	int uregs_valid = 0;
	struct uretprobe_instance *ri = NULL;

	if (c->probe_type == _STP_PROBE_HANDLER_URETPROBE)
		ri = c->ips.ri;
#ifdef STAPCONF_UPROBE_GET_PC
	else if (c->probe_type == _STP_PROBE_HANDLER_UPROBE)
		ri = GET_PC_URETPROBE_NONE;
#endif

	regs = _stp_get_uregs(c);
	uregs_valid = c->probe_flags & _STP_PROBE_STATE_FULL_UREGS;

	if (! current->mm || ! regs) {
		if (sym_flags & _STP_SYM_SYMBOL)
			_stp_printf("<no user backtrace at %s>\n",
				    c->probe_point);
		else
			_stp_print("\n");
		return;
	}

	/* print the current address */
#ifdef STAPCONF_UPROBE_GET_PC
	if (c->probe_type == _STP_PROBE_HANDLER_URETPROBE && ri) {
		if ((sym_flags & _STP_SYM_FULL) == _STP_SYM_FULL) {
			_stp_print("Returning from: ");
			/* ... otherwise this dereference fails */
			_stp_print_addr(ri->rp->u.vaddr, sym_flags, current);
			_stp_print("Returning to  : ");
			_stp_print_addr(ri->ret_addr, sym_flags, current);
		} else
			_stp_print_addr(ri->ret_addr, sym_flags, current);
	} else {
		_stp_print_addr(REG_IP(regs), sym_flags, current);
	}
#else
	_stp_print_addr(REG_IP(regs), sym_flags, current);
#endif

	/* print rest of stack... */
#ifdef STP_USE_DWARF_UNWINDER
	if (c->uregs == &c->uwcontext.info.regs) {
		/* Unwinder needs the reg state, clear uregs ref. */
		c->uregs = NULL;
		c->probe_flags &= ~_STP_PROBE_STATE_FULL_UREGS;
	}
	__stp_dwarf_stack_user_print(regs, sym_flags, MAXBACKTRACE,
				     &c->uwcontext, ri, uregs_valid);
#else
	/* User stack traces only supported for arches with dwarf unwinder. */
	if (sym_flags & _STP_SYM_SYMBOL)
		_stp_printf("<no user backtrace support on arch>\n");
	else
		_stp_print("\n");
#endif
}

/** Writes stack backtrace to a string
 *
 * @param str string
 * @param regs A pointer to the struct pt_regs.
 * @returns void
 */
static void _stp_stack_kernel_sprint(char *str, int size, struct context* c,
				     int sym_flags)
{
	/* To get an hex string, we use a simple trick.
	 * First flush the print buffer,
	 * then call _stp_stack_print,
	 * then copy the result into the output string
	 * and clear the print buffer. */
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	_stp_print_flush();

	_stp_stack_kernel_print(c, sym_flags);

	strlcpy(str, pb->buf, size < (int)pb->len ? size : (int)pb->len);
	pb->len = 0;
}

static void _stp_stack_user_sprint(char *str, int size, struct context* c,
				   int sym_flags)
{
	/* To get an hex string, we use a simple trick.
	 * First flush the print buffer,
	 * then call _stp_stack_print,
	 * then copy the result into the output string
	 * and clear the print buffer. */
	_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
	_stp_print_flush();

	_stp_stack_user_print(c, sym_flags);

	strlcpy(str, pb->buf, size < (int)pb->len ? size : (int)pb->len);
	pb->len = 0;
}

#endif /* CONFIG_KPROBES */

#endif /* _STACK_C_ */
