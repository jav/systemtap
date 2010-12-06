/* -*- linux-c -*-
 * x86_64 stack tracing functions
 * Copyright (C) 2005-2010 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifdef STAPCONF_LINUX_UACCESS_H
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif
#include <linux/types.h>
#define intptr_t long
#define uintptr_t unsigned long

static int _stp_valid_pc_addr(unsigned long addr, struct task_struct *tsk)
{
	/* Just a simple check of whether the the address can be accessed
	   as a user space address. Zero is always bad. */
	int ok;
	mm_segment_t oldfs = get_fs();
	set_fs(USER_DS);
	ok = access_ok(VERIFY_READ, (long *) (intptr_t) addr, sizeof(long));
	set_fs(oldfs);
	return addr != 0L && tsk != NULL ? ok : ! ok;
}


/* DWARF unwinder failed.  Just dump intereting addresses on kernel stack. */
#if !defined(STAPCONF_KERNEL_STACKTRACE)
static void _stp_stack_print_fallback(unsigned long stack, int verbose, int levels)
{
	unsigned long addr;
	while (levels && stack & (THREAD_SIZE - 1)) {
		if (unlikely(_stp_read_address(addr, (unsigned long *)stack,
					       KERNEL_DS))
		    || ! _stp_valid_pc_addr(addr, NULL)) {
			/* cannot access stack.  give up. */
			return;
		}
		_stp_print_addr(addr, verbose | _STP_SYM_INEXACT, NULL);
		levels--;
		stack++;
	}
}
#endif

static void __stp_stack_print(struct pt_regs *regs, int verbose, int levels,
			      struct task_struct *tsk,
			      struct unwind_context *uwcontext,
			      struct uretprobe_instance *ri, int uregs_valid)
{
#ifdef STP_USE_DWARF_UNWINDER
        int start_levels = levels;
	struct unwind_frame_info *info = &uwcontext->info;
	int sanitize = tsk && ! uregs_valid;
	arch_unw_init_frame_info(info, regs, sanitize);

	while (levels) {
		int ret = unwind(uwcontext, tsk);
#ifdef STAPCONF_UPROBE_GET_PC
                unsigned long maybe_pc = 0;
                if (ri) {
                        maybe_pc = uprobe_get_pc(ri, UNW_PC(info),
                                                 UNW_SP(info));
                        if (!maybe_pc)
                                printk("SYSTEMTAP ERROR: uprobe_get_return returned 0\n");
                        else
                                UNW_PC(info) = maybe_pc;
                }
#endif
		dbug_unwind(1, "ret=%d PC=%lx SP=%lx\n", ret, UNW_PC(info), UNW_SP(info));
		if (ret == 0 && _stp_valid_pc_addr(UNW_PC(info), tsk)) {
			_stp_print_addr(UNW_PC(info), verbose, tsk);
			levels--;
			if (UNW_PC(info) != _stp_kretprobe_trampoline)
			  continue;
		}
		/* If an error happened or we hit a kretprobe trampoline,
		 * and the current pc frame address is still valid kernel
		 * address use fallback backtrace, unless user task backtrace.
		 * FIXME: is there a way to unwind across kretprobe
		 * trampolines? PR9999. */
		if ((ret < 0 || UNW_PC(info) == _stp_kretprobe_trampoline)
		    && ! tsk)
			_stp_stack_print_fallback(UNW_SP(info), verbose, levels);
		return;
	}
#else /* ! STP_USE_DWARF_UNWINDER */
	_stp_stack_print_fallback(REG_SP(regs), verbose, levels);
#endif
}


