/* -*- linux-c -*-
 * ppc64 stack tracing functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

static void __stp_stack_print (struct pt_regs *regs, int verbose, int levels)
{
	unsigned long ip, newsp, lr = 0;
	int count = 0;
	int firstframe = 1;
	unsigned long *_sp = (unsigned long *)&REG_SP(regs);	
	unsigned long sp = (unsigned long)_sp;
	lr = 0;
	do {
		if (sp < KERNELBASE)
			return;
		_sp = (unsigned long *) sp;
		newsp = _sp[0];
		ip = _sp[2];
		if (!firstframe || ip != lr) {
			if (verbose) {
				_stp_printf("[%p] [%p] ", sp, ip);
				_stp_symbol_print(ip);
				if (firstframe)
					_stp_print(" (unreliable)");
				_stp_print_char('\n');
			}
			else
				_stp_printf("%p ", ip);
		}
		firstframe = 0;
		/*
		 * See if this is an exception frame.
		 * We look for the "regshere" marker in the current frame.
		 */
		if ( _sp[12] == 0x7265677368657265ul) {
			struct pt_regs *regs = (struct pt_regs *)
				(sp + STACK_FRAME_OVERHEAD);
			if (verbose) {
				_stp_printf("--- Exception: %lx at ",regs->trap);
				_stp_symbol_print(regs->nip);
				_stp_print_char('\n');
				lr = regs->link;
				_stp_print("    LR =");
				_stp_symbol_print(lr);
				_stp_print_char('\n');
				firstframe = 1;
			}
			else {
				_stp_printf("%p ",regs->nip);
				_stp_printf("%p ",regs->link);
			}
		}

		sp = newsp;
	} while (str->len < STP_STRING_SIZE);
}
