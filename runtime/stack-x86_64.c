/* -*- linux-c -*-
 * x86_64 stack tracing functions
 * Copyright (C) 2005-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

// todo: don't use unwinder  for kernel if CONFIG_FRAME

/* DWARF unwinder failed.  Just dump intereting addresses on kernel stack. */
static void _stp_stack_print_fallback(unsigned long stack, int verbose)
{
	unsigned long addr;

	while (stack & (THREAD_SIZE - 1)) {
		if (unlikely(__stp_get_user(addr, (unsigned long *)stack))) {
			/* cannot access stack.  give up. */
			return;
		}
		_stp_func_print(addr, verbose, 0);
		stack++;
	}
}

static void __stp_stack_print(struct pt_regs *regs, int verbose, int levels)
{
	struct unwind_frame_info info;
	arch_unw_init_frame_info(&info, regs);

	while (!arch_unw_user_mode(&info)) {
		int ret = unwind(&info);
		dbug_unwind(1, "ret=%d PC=%lx SP=%lx\n", ret, UNW_PC(&info), UNW_SP(&info));
		if (ret < 0) {
			_stp_stack_print_fallback(UNW_SP(&info), verbose);
			break;
		}
		if (ret)
			break;
		_stp_func_print(UNW_PC(&info), verbose, 1);
	}
}
