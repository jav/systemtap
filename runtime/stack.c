/* Stack tracing functions
 * Copyright (C) 2005 Red Hat Inc.
 * Copyright (C) 2005 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STACK_C_ /* -*- linux-c -*- */
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

static int _stp_kta(unsigned long addr)
{
	if (addr >= stap_symbols[0].addr && 
	    addr <= stap_symbols[stap_num_symbols-1].addr)
		return 1;
	return 0;
}

#if defined (__x86_64__)

static void __stp_stack_sprint (String str, unsigned long *stack, int verbose, int levels)
{
	unsigned long addr;
	while (((long) stack & (THREAD_SIZE-1)) != 0) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			if (verbose) {
				_stp_string_cat(str, " ");
				_stp_symbol_sprint (str, addr);
				_stp_string_cat (str, "\n");
			} else 
				_stp_sprintf (str, "%lx ", addr);
		}
	}
}

#elif defined (__ia64__)
struct dump_para{
  unsigned long *sp;
  String str;
};

static void __stp_show_stack_sym(struct unw_frame_info *info, void *arg)
{
   unsigned long ip, skip=1;
   String str = ((struct dump_para*)arg)->str;
   struct pt_regs *regs = container_of(((struct dump_para*)arg)->sp, struct pt_regs, r12);

	do {
		unw_get_ip(info, &ip);
		if (ip == 0) break;
                if (skip){
			if (ip == REG_IP(regs))
				skip = 0;
                        else continue;
                }
		_stp_string_cat(str, " ");
		_stp_symbol_sprint(str, ip);
		_stp_string_cat (str, "\n");
        } while (unw_unwind(info) >= 0);
}

static void __stp_show_stack_addr(struct unw_frame_info *info, void *arg)
{
   unsigned long ip, skip=1;
   String str = ((struct dump_para*)arg)->str;
   struct pt_regs *regs = container_of(((struct dump_para*)arg)->sp, struct pt_regs, r12);	

	do {
		unw_get_ip(info, &ip);
		if (ip == 0) break;
		if (skip){
			if (ip == REG_IP(regs))
				skip = 0;
			continue;
		}
		_stp_sprintf (str, "%lx ", ip);
	} while (unw_unwind(info) >= 0);
}

static void __stp_stack_sprint (String str, unsigned long *stack, int verbose, int levels)
{
  struct dump_para para;

	para.str = str;
	para.sp  = stack; 
	if (verbose)
	    unw_init_running(__stp_show_stack_sym, &para);
        else
	    unw_init_running(__stp_show_stack_addr, &para);
}

#elif  defined (__i386__)

static inline int valid_stack_ptr(struct thread_info *tinfo, void *p)
{
	return	p > (void *)tinfo &&
		p < (void *)tinfo + THREAD_SIZE - 3;
}

static inline unsigned long print_context_stack(String str, struct thread_info *tinfo,
						unsigned long *stack, unsigned long ebp, int verbose)
{
	unsigned long addr;

#ifdef	CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		if (verbose) {
			_stp_string_cat(str, " ");
			_stp_symbol_sprint (str, addr);
			_stp_string_cat(str, "\n");
		} else
			_stp_sprintf (str, "%lx ", addr);
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (_stp_kta(addr)) {
			if (verbose) {
				_stp_string_cat(str, " ");
				_stp_symbol_sprint(str, addr);
				_stp_string_cat(str, "\n");
			} else
				_stp_sprintf (str, "%lx ", addr);
		}
	}
#endif
	return ebp;
}

static void __stp_stack_sprint (String str, unsigned long *stack, int verbose, int levels)
{
	unsigned long ebp;

	/* Grab ebp right from our regs */
	asm ("movl %%ebp, %0" : "=r" (ebp) : );

	while (1) {
		struct thread_info *context;
		context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		ebp = print_context_stack(str, context, stack, ebp, verbose);
		stack = (unsigned long*)context->previous_esp;
		if (!stack)
			break;
	}
}
#elif defined (__powerpc64__)

static void __stp_stack_sprint (String str, unsigned long *_sp,
				int verbose, int levels)
{
	unsigned long ip, newsp, lr = 0;
	int count = 0;
	unsigned long sp = (unsigned long)_sp;
	int firstframe = 1;

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

#else
#error "Unsupported architecture"
#endif


/** Writes stack backtrace to a String
 *
 * @param str String
 * @param regs A pointer to the struct pt_regs.
 * @returns Same String as was input with trace info appended,
 */
String _stp_stack_sprint (String str, struct pt_regs *regs, int verbose)
{
	if (verbose) {
		_stp_sprintf (str, "trace for %d (%s)\n ", current->pid, current->comm);
		_stp_symbol_sprint (str, REG_IP(regs));
		_stp_string_cat(str, "\n");
	} else
		_stp_sprintf (str, "%lx ", REG_IP(regs));
	__stp_stack_sprint (str, (unsigned long *)&REG_SP(regs), verbose, 0);
	return str;
}

/** Prints the stack backtrace
 * @param regs A pointer to the struct pt_regs.
 */

#define _stp_stack_print(regs)	(void)_stp_stack_sprint(_stp_stdout,regs,1)

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
	__stp_stack_sprint (str, &stack, 1, 0);
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
