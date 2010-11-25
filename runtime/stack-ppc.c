/* -*- linux-c -*-
 * ppc64 stack tracing functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

static void __stp_stack_print (struct pt_regs *regs, int verbose, int levels,
			       struct task_struct *tsk,
			       struct unwind_context *uwcontext,
			       struct uretprobe_instance *ri, int uregs_valid)
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
#ifndef STACK_FRAME_LR_SAVE /* from arch/powerpc/include/asm/ptrace.h */
#ifdef __powerpc64__
#define STACK_FRAME_OVERHEAD    112     /* size of minimum stack frame */
#define STACK_FRAME_LR_SAVE     2       /* Location of LR in stack frame */
#define STACK_FRAME_REGS_MARKER ASM_CONST(0x7265677368657265)
#define STACK_INT_FRAME_SIZE    (sizeof(struct pt_regs) + STACK_FRAME_OVERHEAD + 288)
#define STACK_FRAME_MARKER      12
#define __SIGNAL_FRAMESIZE      128
#define __SIGNAL_FRAMESIZE32    64
#else /* __powerpc64__ */
#define STACK_FRAME_OVERHEAD    16      /* size of minimum stack frame */
#define STACK_FRAME_LR_SAVE     1       /* Location of LR in stack frame */
#define STACK_FRAME_REGS_MARKER ASM_CONST(0x72656773)
#define STACK_INT_FRAME_SIZE    (sizeof(struct pt_regs) + STACK_FRAME_OVERHEAD)
#define STACK_FRAME_MARKER      2
#define __SIGNAL_FRAMESIZE      64
#endif
#endif
		ip = _sp[STACK_FRAME_LR_SAVE];
		if (!firstframe || ip != lr) {
			if (verbose)
				_stp_printf("[0x%016lx] [0x%016lx]", sp, ip);
			_stp_print_addr(ip,
					(firstframe
					 ? (verbose | _STP_SYM_INEXACT)
					 : verbose), tsk);
		}
		firstframe = 0;
		/*
		 * See if this is an exception frame.
		 * We look for the "regshere" marker in the current frame.
		 */
		if (_sp[STACK_FRAME_MARKER] == STACK_FRAME_REGS_MARKER) {
			struct pt_regs *regs = (struct pt_regs *)
				(sp + STACK_FRAME_OVERHEAD);
			if (verbose) {
				_stp_printf("--- Exception: %lx at ",regs->trap);
				_stp_print_addr(regs->nip, verbose, tsk);
				lr = regs->link;
				_stp_print("    LR =");
				_stp_print_addr(lr, verbose, tsk);
				firstframe = 1;
			}
			else {
				_stp_printf("0x%016lx ",regs->nip);
				_stp_printf("0x%016lx ",regs->link);
			}
		}

		sp = newsp;
	} while (count++ < MAXBACKTRACE);
}
