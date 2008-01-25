/* -*- linux-c -*-
 * i386 stack tracing functions
 * Copyright (C) 2005-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

static inline int _stp_valid_stack_ptr(unsigned long context, unsigned long p)
{
	return	p > context && p < context + THREAD_SIZE - 3;
}

static void __stp_stack_print (struct pt_regs *regs, int verbose, int levels)
{
	unsigned long *stack = (unsigned long *)&REG_SP(regs);
	unsigned long context = (unsigned long)stack & ~(THREAD_SIZE - 1);
	unsigned long addr;

#ifdef	CONFIG_FRAME_POINTER
	{
                #ifdef STAPCONF_X86_UNIREGS
                unsigned long ebp = regs->bp;
                #elif
		unsigned long ebp = regs->ebp;
		#endif
		
		while (_stp_valid_stack_ptr(context, (unsigned long)ebp)) {
			addr = *(unsigned long *)(ebp + 4);
			if (verbose) {
				_stp_print_char(' ');
				_stp_symbol_print (addr);
				_stp_print_char('\n');
			} else
				_stp_printf ("0x%08lx ", addr);
			ebp = *(unsigned long *)ebp;
		}
	}
#else
	while (_stp_valid_stack_ptr(context, (unsigned long)stack)) {
		addr = *stack++;
		_stp_func_print(addr, verbose, 1);
	}
#endif
}
