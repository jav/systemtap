/* -*- linux-c -*-
 * ppc64 stack tracing functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

static void __stp_stack_sprint (String str, struct pt_regs *regs, int verbose, int levels)
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
				_stp_sprintf(str, "[%016lx] [%016lx] ", sp, ip);
				_stp_symbol_sprint(str, ip);
				if (firstframe)
					_stp_string_cat(str, " (unreliable)");
				_stp_string_cat(str, "\n");
			}
			else
				_stp_sprintf(str,"%lx ", ip);
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
				_stp_sprintf(str, "--- Exception: %lx at ",
						regs->trap);
				_stp_symbol_sprint(str, regs->nip);
				_stp_string_cat(str, "\n");
				lr = regs->link;
				_stp_string_cat(str, "    LR =");
				_stp_symbol_sprint(str, lr);
				_stp_string_cat(str, "\n");
				firstframe = 1;
			}
			else {
				_stp_sprintf(str, "%lx ",regs->nip);
				_stp_sprintf(str, "%lx ",regs->link);
			}
		}

		sp = newsp;
	} while (str->len < STP_STRING_SIZE);
}
