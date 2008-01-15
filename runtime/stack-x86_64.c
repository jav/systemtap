/* -*- linux-c -*-
 * x86_64 stack tracing functions
 * Copyright (C) 2005, 2006, 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

static void __stp_stack_print (struct pt_regs *regs, int verbose, int levels)
{
	unsigned long *stack = (unsigned long *)REG_SP(regs);
	unsigned long addr;

	while ((long)stack & (THREAD_SIZE-1)) {
		addr = *stack++;
		_stp_func_print(addr, verbose, 1);
	}
}
