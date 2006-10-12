/* -*- linux-c -*-
 * i386 stack tracing functions
 * Copyright (C) 2005, 2006 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */
#ifdef CONFIG_STACK_UNWIND
#include <linux/unwind.h>

static inline int _stp_valid_stack_ptr_info(struct unwind_frame_info *info)
{
	unsigned long context = (unsigned long)UNW_SP(info) & ~(THREAD_SIZE - 1);
	unsigned long p = UNW_PC(info);
	return	p > context && p < context + THREAD_SIZE - 3;
}

static int
_stp_show_trace_unwind(String str, struct unwind_frame_info *info, int verbose)
{
	int n = 0;

	while (unwind(info) == 0 && UNW_PC(info)) {
		if (_stp_valid_stack_ptr_info(info))
			break;
		n++;
		if (verbose) {
			_stp_string_cat(str, " ");
			_stp_symbol_sprint (str, UNW_PC(info));
			_stp_string_cat(str, "\n");
		} else
			_stp_sprintf (str, "%p ", UNW_PC(info));
		if (arch_unw_user_mode(info))
			break;
	}
	return n;
}
#endif /* CONFIG_STACK_UNWIND */

static inline int _stp_valid_stack_ptr(unsigned long context, unsigned long p)
{
	return	p > context && p < context + THREAD_SIZE - 3;
}

static void __stp_stack_sprint (String str, struct pt_regs *regs, int verbose, int levels)
{
	unsigned long *stack = (unsigned long *)&REG_SP(regs);
	unsigned long context = (unsigned long)stack & ~(THREAD_SIZE - 1);
	unsigned long addr;
	int uw_ret = 0;

#ifdef CONFIG_STACK_UNWIND
	struct unwind_frame_info info;
	if (unwind_init_frame_info(&info, current, regs) == 0)
		uw_ret = _stp_show_trace_unwind(str, &info, verbose);
	stack = (void *)UNW_SP(&info);
#endif

	if (uw_ret == 0)
		_stp_string_cat(str, "Inexact backtrace:\n");

#ifdef	CONFIG_FRAME_POINTER
	{
		unsigned long ebp;
		/* Grab ebp right from our regs.*/
		asm ("movl %%ebp, %0" : "=r" (ebp) : );
		
		while (_stp_valid_stack_ptr(context, (unsigned long)ebp)) {
			addr = *(unsigned long *)(ebp + 4);
			if (verbose) {
				if (uw_ret) {
					uw_ret = 0;
					_stp_string_cat(str, "Leftover inexact backtrace:\n");
				}
				_stp_string_cat(str, " ");
				_stp_symbol_sprint (str, addr);
				_stp_string_cat(str, "\n");
			} else
				_stp_sprintf (str, "%p ", (void *)addr);
			ebp = *(unsigned long *)ebp;
		}
	}
#else
	while (_stp_valid_stack_ptr(context, (unsigned long)stack)) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			if (verbose) {
				if (uw_ret) {
					uw_ret = 0;
					_stp_string_cat(str, "Leftover inexact backtrace:\n");
				}
				_stp_string_cat(str, " ");
				_stp_symbol_sprint(str, addr);
				_stp_string_cat(str, "\n");
			} else
				_stp_sprintf (str, "%p ", (void *)addr);
		}
	}
#endif
}
