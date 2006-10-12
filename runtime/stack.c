/*  -*- linux-c -*-
 * Stack tracing functions
 * Copyright (C) 2005, 2006 Red Hat Inc.
 * Copyright (C) 2005 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STACK_C_
#define _STACK_C_


/** @file stack.c
 * @brief Stack Tracing Functions
 */

/** @addtogroup stack Stack Tracing Functions
 * Without frames the best that can be done here is to scan the stack and
 * display everything that fits in the range of a valid IP. Things like function pointers
 * on the stack will certainly result in bogus addresses in the backtrace.
 *
 * With debug info, we could get a proper backtrace, but it would be too slow to do
 * during a probe.  We can eventually make this a postprocessing feature.
 *
 * @{
 */

#include "sym.c"
#include "regs.h"
static int _stp_kta(unsigned long addr);

#if defined (__x86_64__)
#include "stack-x86_64.c"
#elif defined (__ia64__)
#include "stack-ia64.c"
#elif  defined (__i386__)
#include "stack-i386.c"
#elif defined (__powerpc64__)
#include "stack-ppc64.c"
#else
#error "Unsupported architecture"
#endif


/* our copy of kernel_text_address() */
static int _stp_kta(unsigned long addr)
{
	static unsigned long stext, etext, sinittext, einittext;
	static int init = 0;
	
	if (init == 0) {
		init = 1;
		etext = _stp_kallsyms_lookup_name("_etext");
		stext = _stp_kallsyms_lookup_name("_stext");
		sinittext = _stp_kallsyms_lookup_name("_sinittext");
		einittext = _stp_kallsyms_lookup_name("_einittext");
	}

	if (addr >= stext && addr <= etext)
		return 1;

	if (addr >= sinittext && addr <= einittext)
		return 1;

	return 0;
}

/** Writes stack backtrace to a String
 *
 * @param str String
 * @param regs A pointer to the struct pt_regs.
 * @returns Same String as was input with trace info appended,
 */
String _stp_stack_sprint (String str, struct pt_regs *regs, int verbose, struct kretprobe_instance *pi)
{
	if (verbose) {
		/* print the current address */
		if (pi) {
			_stp_string_cat(str, "Returning from: ");
			_stp_symbol_sprint(str, (unsigned long)_stp_probe_addr_r(pi));
			_stp_string_cat(str, "\nReturning to: ");
			_stp_symbol_sprint(str, (unsigned long)_stp_ret_addr_r(pi));
		} else
			_stp_symbol_sprint (str, REG_IP(regs));
		_stp_string_cat(str, "\n");
	} else
		_stp_sprintf (str, "%lx ", REG_IP(regs));
	__stp_stack_sprint (str, regs, verbose, 0);
	return str;
}

/** Prints the stack backtrace
 * @param regs A pointer to the struct pt_regs.
 */

#define _stp_stack_print(regs,pi)	(void)_stp_stack_sprint(_stp_stdout,regs,1,pi)

/** Writes stack backtrace to a String.
 * Use this when calling from a jprobe.
 * @param str String
 * @returns Same String as was input with trace info appended,
 * @sa _stp_stack_sprint()
 */
String _stp_stack_sprintj(String str)
{
	unsigned long stack;
	_stp_sprintf (str, "trace for %d (%s)\n", current->pid, current->comm);
/*	__stp_stack_sprint (str, &stack, 1, 0); */
	return str;
}

/** Prints the stack backtrace.
 * Use this when calling from a jprobe.
 * @sa _stp_stack_print()
 */
#define _stp_stack_printj() (void)_stp_stack_sprintj(_stp_stdout)

/** Writes the user stack backtrace to a String
 * @param str String
 * @returns Same String as was input with trace info appended,
 * @note Currently limited to a depth of two. Works from jprobes and kprobes.
 */
String _stp_ustack_sprint (String str)
{
	struct pt_regs *nregs = ((struct pt_regs *) (THREAD_SIZE + (unsigned long) current->thread_info)) - 1;
#if BITS_PER_LONG == 64
	_stp_sprintf (str, " 0x%016lx : [user]\n", REG_IP(nregs));
	if (REG_SP(nregs))
		_stp_sprintf (str, " 0x%016lx : [user]\n", *(unsigned long *)REG_SP(nregs));
#else
	_stp_sprintf (str, " 0x%08lx : [user]\n", REG_IP(nregs));
	if (REG_SP(nregs))
		_stp_sprintf (str, " 0x%08lx : [user]\n", *(unsigned long *)REG_SP(nregs));
#endif
	return str;
}

/** Prints the user stack backtrace
 * @note Currently limited to a depth of two. Works from jprobes and kprobes.
 */
#define _stp_ustack_print() (void)_stp_ustack_sprint(_stp_stdout)

/** @} */
#endif /* _STACK_C_ */
