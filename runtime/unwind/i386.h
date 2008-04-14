/* -*- linux-c -*-
 *
 * 32-bit x86 dwarf unwinder header file
 * Copyright (C) 2008 Red Hat Inc.
 * Copyright (C) 2002-2006 Novell, Inc.
 * 
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */
#ifndef _STP_I386_UNWIND_H
#define _STP_I386_UNWIND_H

#include <linux/sched.h>
#include <asm/fixmap.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

/* these are simple for i386 */
#define _stp_get_unaligned(ptr) (*(ptr))
#define _stp_put_unaligned(val, ptr) ((void)( *(ptr) = (val) ))

struct unwind_frame_info
{
	struct pt_regs regs;
	struct task_struct *task;
	unsigned call_frame:1;
};

#define STACK_LIMIT(ptr)     (((ptr) - 1) & ~(THREAD_SIZE - 1))

#ifdef STAPCONF_X86_UNIREGS

#define UNW_PC(frame)        (frame)->regs.ip
#define UNW_SP(frame)        (frame)->regs.sp
#ifdef STP_USE_FRAME_POINTER
#define UNW_FP(frame)        (frame)->regs.bp
#define FRAME_RETADDR_OFFSET 4
#define FRAME_LINK_OFFSET    0
#define STACK_BOTTOM(tsk)    STACK_LIMIT((tsk)->thread.sp0)
#define STACK_TOP(tsk)       ((tsk)->thread.sp0)
#else
#define UNW_FP(frame) ((void)(frame), 0)
#endif

#define UNW_REGISTER_INFO \
	PTREGS_INFO(ax), \
	PTREGS_INFO(cx), \
	PTREGS_INFO(dx), \
	PTREGS_INFO(bx), \
	PTREGS_INFO(sp), \
	PTREGS_INFO(bp), \
	PTREGS_INFO(si), \
	PTREGS_INFO(di), \
	PTREGS_INFO(ip)

#else /* !STAPCONF_X86_UNIREGS */

#define UNW_PC(frame)        (frame)->regs.eip
#define UNW_SP(frame)        (frame)->regs.esp
#ifdef STP_USE_FRAME_POINTER
#define UNW_FP(frame)        (frame)->regs.ebp
#define FRAME_RETADDR_OFFSET 4
#define FRAME_LINK_OFFSET    0
#define STACK_BOTTOM(tsk)    STACK_LIMIT((tsk)->thread.esp0)
#define STACK_TOP(tsk)       ((tsk)->thread.esp0)
#else
#define UNW_FP(frame) ((void)(frame), 0)
#endif

#define UNW_REGISTER_INFO \
	PTREGS_INFO(eax), \
	PTREGS_INFO(ecx), \
	PTREGS_INFO(edx), \
	PTREGS_INFO(ebx), \
	PTREGS_INFO(esp), \
	PTREGS_INFO(ebp), \
	PTREGS_INFO(esi), \
	PTREGS_INFO(edi), \
	PTREGS_INFO(eip)

#endif /* STAPCONF_X86_UNIREGS */

#define UNW_DEFAULT_RA(raItem, dataAlign) \
	((raItem).where == Memory && \
	 !((raItem).value * (dataAlign) + 4))

static inline void arch_unw_init_frame_info(struct unwind_frame_info *info,
                                            /*const*/ struct pt_regs *regs)
{
	if (user_mode_vm(regs))
		info->regs = *regs;
	else {
#ifdef STAPCONF_X86_UNIREGS
		memcpy(&info->regs, regs, offsetof(struct pt_regs, sp));
		info->regs.sp = (unsigned long)&regs->sp;
		info->regs.ss = __KERNEL_DS;
#else
		memcpy(&info->regs, regs, offsetof(struct pt_regs, esp));
		info->regs.esp = (unsigned long)&regs->esp;
		info->regs.xss = __KERNEL_DS;		
#endif
		
	}
	info->call_frame = 1;
}

static inline void arch_unw_init_blocked(struct unwind_frame_info *info)
{
	memset(&info->regs, 0, sizeof(info->regs));
#ifdef STAPCONF_X86_UNIREGS	
	info->regs.ip = info->task->thread.ip;
	info->regs.cs = __KERNEL_CS;
	__get_user(info->regs.bp, (long *)info->task->thread.sp);
	info->regs.sp = info->task->thread.sp;
	info->regs.ss = __KERNEL_DS;
	info->regs.ds = __USER_DS;
	info->regs.es = __USER_DS;
#else
	info->regs.eip = info->task->thread.eip;
	info->regs.xcs = __KERNEL_CS;
	__get_user(info->regs.ebp, (long *)info->task->thread.esp);
	info->regs.esp = info->task->thread.esp;
	info->regs.xss = __KERNEL_DS;
	info->regs.xds = __USER_DS;
	info->regs.xes = __USER_DS;
#endif
	
}


static inline int arch_unw_user_mode(const struct unwind_frame_info *info)
{
#if 0 /* This can only work when selector register and EFLAGS saves/restores
         are properly annotated (and tracked in UNW_REGISTER_INFO). */
	return user_mode_vm(&info->regs);
#else
#ifdef STAPCONF_X86_UNIREGS		
	return info->regs.ip < PAGE_OFFSET
	       || (info->regs.ip >= __fix_to_virt(FIX_VDSO)
	            && info->regs.ip < __fix_to_virt(FIX_VDSO) + PAGE_SIZE)
	       || info->regs.sp < PAGE_OFFSET;
#else
	return info->regs.eip < PAGE_OFFSET
	       || (info->regs.eip >= __fix_to_virt(FIX_VDSO)
	            && info->regs.eip < __fix_to_virt(FIX_VDSO) + PAGE_SIZE)
	       || info->regs.esp < PAGE_OFFSET;
#endif	
#endif
}

#endif /* _STP_I386_UNWIND_H */
