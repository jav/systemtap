/* -*- linux-c -*-
 * ppc64 stack tracing functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

static unsigned long
__stp_show_stack (String str, unsigned long sp, unsigned long low,
 		  unsigned long high, int verbose)
{
 
	struct stack_frame *sf;
	struct pt_regs *regs;
	unsigned long ip;
 
	while (1) {
		sp = sp & PSW_ADDR_INSN;
		/* fixme: verify  this is a kernel stack */
		if (sp < low || sp > high - sizeof(*sf))
			return sp;
		sf = (struct stack_frame *) sp;
		ip = sf->gprs[8] & PSW_ADDR_INSN;
		if (verbose) {
			_stp_sprintf(str, "[%016lx] [%016lx] ", sp, ip);
			_stp_symbol_sprint(str, ip);
			_stp_string_cat(str, "\n");
		}else{
			_stp_sprintf(str,"%lx ", ip);
		}
		/* Follow the back_chain */
		while (1) {
			low = sp;
			sp = sf->back_chain & PSW_ADDR_INSN;
			if (!sp)
				break;
			if (sp <= low || sp > high - sizeof(*sf))
				return sp;
			sf = (struct stack_frame *) sp;
			ip = sf->gprs[8] & PSW_ADDR_INSN;
			if (verbose) {
				_stp_sprintf(str, "[%016lx] [%016lx] ", sp, ip);
				_stp_symbol_sprint(str, ip);
				_stp_string_cat(str, "\n");
			}else{
				_stp_sprintf(str,"%lx ", ip);
			}
		}
		/* Zero backchain detected, check for interrupt frame. */
		sp = (unsigned long) (sf + 1);
		if (sp <= low || sp > high - sizeof(*regs))
			return sp;
		regs = (struct pt_regs *) sp;
		if (verbose) {
			_stp_sprintf(str, "[%016lx] [%016lx] ", sp, ip);
			_stp_symbol_sprint(str, ip);
			_stp_string_cat(str, "\n");
		}else{
			_stp_sprintf(str,"%lx ", ip);
		}
		low = sp;
		sp = regs->gprs[15];
	}
}
 
static void __stp_stack_sprint (String str, struct pt_regs *regs,
				int verbose, int levels)
{
		unsigned long *_sp = (unsigned long *)&REG_SP(regs);
		unsigned long sp = (unsigned long)_sp;
		// unsigned long sp = (unsigned long)*_sp;
 
		sp = __stp_show_stack(str, sp,
			S390_lowcore.async_stack - ASYNC_SIZE,
			S390_lowcore.async_stack,verbose);
 
		__stp_show_stack(str, sp,
			S390_lowcore.thread_info,
			S390_lowcore.thread_info + THREAD_SIZE,verbose);
}
