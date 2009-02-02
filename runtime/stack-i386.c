/* -*- linux-c -*-
 * i386 stack tracing functions
 * Copyright (C) 2005-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

static int _stp_valid_stack_ptr(unsigned long context, unsigned long p)
{
	return	p > context && p < context + THREAD_SIZE - 3;
}

/* DWARF unwinder failed.  Just dump intereting addresses on kernel stack. */
#ifndef CONFIG_STACKTRACE
static void _stp_stack_print_fallback(unsigned long context, unsigned long stack, int verbose, int levels)
{
	unsigned long addr;
	while (levels && _stp_valid_stack_ptr(context, stack)) {
		if (unlikely(_stp_read_address(addr, (unsigned long *)stack, KERNEL_DS))) {
			/* cannot access stack.  give up. */
			return;
		}
		if (_stp_func_print(addr, verbose, 0))
			levels--;
		stack++;
	}
}
#endif

static void __stp_stack_print (struct pt_regs *regs, int verbose, int levels)
{
	unsigned long context = (unsigned long)&REG_SP(regs) & ~(THREAD_SIZE - 1);

#ifdef	STP_USE_FRAME_POINTER
	unsigned long addr;
	unsigned long next_fp, fp = REG_FP(regs);

	while (levels && _stp_valid_stack_ptr(context, (unsigned long)fp)) {
		if (unlikely(_stp_read_address(addr, (unsigned long *)(fp + 4), KERNEL_DS))) {
			/* cannot access stack.  give up. */
			return;
		}
		_stp_func_print(addr, verbose, 1);
		if (unlikely(_stp_read_address(next_fp, (unsigned long *)fp, KERNEL_DS))) {
			/* cannot access stack.  give up. */
			return;
		}
		levels--;

		/* frame pointers move upwards */
		if (next_fp <= fp)
			break;
		fp = next_fp;
	}
#else
#ifdef STP_USE_DWARF_UNWINDER
	struct unwind_frame_info info;
	arch_unw_init_frame_info(&info, regs);

	while (levels && !arch_unw_user_mode(&info)) {
		int ret = unwind(&info);
		dbug_unwind(1, "ret=%d PC=%lx SP=%lx\n", ret, UNW_PC(&info), UNW_SP(&info));
		if (ret == 0) {
			_stp_func_print(UNW_PC(&info), verbose, 1);
			levels--;
			continue;
		}
		/* If an error happened or we hit a kretprobe trampoline, use fallback backtrace */
		/* FIXME: is there a way to unwind across kretprobe trampolines? */
		if (ret < 0 || (ret > 0 && UNW_PC(&info) == _stp_kretprobe_trampoline))
			_stp_stack_print_fallback(context, UNW_SP(&info), verbose, levels);
		break;
	}
#else /* ! STP_USE_DWARF_UNWINDER */
	_stp_stack_print_fallback(context, (unsigned long)&REG_SP(regs), verbose, levels);
#endif /* STP_USE_FRAME_POINTER */
#endif
}
