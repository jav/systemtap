/* syscall defines and inlines
 * Copyright (C) 2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _SYSCALL_H_ /* -*- linux-c -*- */
#define _SYSCALL_H_

#if defined(__i386__) || defined(CONFIG_IA32_EMULATION)
#define __MMAP_SYSCALL_NO_IA32		90
#define __MMAP2_SYSCALL_NO_IA32		192
#define __MPROTECT_SYSCALL_NO_IA32	125
#define __MUNMAP_SYSCALL_NO_IA32	91
#define __MREMAP_SYSCALL_NO_IA32	163
# if !defined(CONFIG_IA32_EMULATION)
#define MMAP_SYSCALL_NO(tsk) __MMAP_SYSCALL_NO_IA32
#define MMAP2_SYSCALL_NO(tsk) __MMAP2_SYSCALL_NO_IA32
#define MPROTECT_SYSCALL_NO(tsk) __MPROTECT_SYSCALL_NO_IA32
#define MUNMAP_SYSCALL_NO(tsk) __MUNMAP_SYSCALL_NO_IA32
#define MREMAP_SYSCALL_NO(tsk) __MREMAP_SYSCALL_NO_IA32
# endif
#endif

#if defined(__x86_64__)
#define __MMAP_SYSCALL_NO_X86_64	9
/* x86_64 doesn't have a mmap2 system call.  So, we'll use a number
 * that doesn't map to a real system call. */
#define __MMAP2_SYSCALL_NO_X86_64	((unsigned long)-1)
#define __MPROTECT_SYSCALL_NO_X86_64	10
#define __MUNMAP_SYSCALL_NO_X86_64	11
#define __MREMAP_SYSCALL_NO_X86_64	25
# if defined(CONFIG_IA32_EMULATION)
#define MMAP_SYSCALL_NO(tsk) ((test_tsk_thread_flag((tsk), TIF_IA32))	\
			      ? __MMAP_SYSCALL_NO_IA32			\
			      : __MMAP_SYSCALL_NO_X86_64)
#define MMAP2_SYSCALL_NO(tsk) ((test_tsk_thread_flag((tsk), TIF_IA32))	\
			       ? __MMAP2_SYSCALL_NO_IA32		\
			       : __MMAP2_SYSCALL_NO_X86_64)
#define MPROTECT_SYSCALL_NO(tsk) ((test_tsk_thread_flag((tsk), TIF_IA32)) \
				  ? __MPROTECT_SYSCALL_NO_IA32		\
				  : __MPROTECT_SYSCALL_NO_X86_64)
#define MUNMAP_SYSCALL_NO(tsk) ((test_tsk_thread_flag((tsk), TIF_IA32)) \
				  ? __MUNMAP_SYSCALL_NO_IA32		\
				  : __MUNMAP_SYSCALL_NO_X86_64)
#define MREMAP_SYSCALL_NO(tsk) ((test_tsk_thread_flag((tsk), TIF_IA32)) \
				  ? __MREMAP_SYSCALL_NO_IA32		\
				  : __MREMAP_SYSCALL_NO_X86_64)
# else
#define MMAP_SYSCALL_NO(tsk) __MMAP_SYSCALL_NO_X86_64
#define MPROTECT_SYSCALL_NO(tsk) __MPROTECT_SYSCALL_NO_X86_64
#define MUNMAP_SYSCALL_NO(tsk) __MUNMAP_SYSCALL_NO_X86_64
#define MREMAP_SYSCALL_NO(tsk) __MREMAP_SYSCALL_NO_X86_64
# endif
#endif

#if defined(__powerpc__)
#define MMAP_SYSCALL_NO(tsk)		90
/* MMAP2 only exists on a 32-bit kernel.  On a 64-bit kernel, we'll
 * never see mmap2 (but that's OK). */
#define MMAP2_SYSCALL_NO(tsk)		192
#define MPROTECT_SYSCALL_NO(tsk)	125
#define MUNMAP_SYSCALL_NO(tsk)		91
#define MREMAP_SYSCALL_NO(tsk)		163
#endif

#if defined(__ia64__)
#define MMAP_SYSCALL_NO(tsk)		1151
#define MMAP2_SYSCALL_NO(tsk)		1172
#define MPROTECT_SYSCALL_NO(tsk)	1155
#define MUNMAP_SYSCALL_NO(tsk)		1152
#define MREMAP_SYSCALL_NO(tsk)		1156
#endif

#if defined(__s390__) || defined(__s390x__)
#define MMAP_SYSCALL_NO(tsk)		90
#define MMAP2_SYSCALL_NO(tsk)		192
#define MPROTECT_SYSCALL_NO(tsk)	125
#define MUNMAP_SYSCALL_NO(tsk)		91
#define MREMAP_SYSCALL_NO(tsk)		163
#endif

#if !defined(MMAP_SYSCALL_NO) || !defined(MMAP2_SYSCALL_NO)		\
	|| !defined(MPROTECT_SYSCALL_NO) || !defined(MUNMAP_SYSCALL_NO)	\
	|| !defined(MREMAP_SYSCALL_NO)
#error "Unimplemented architecture"
#endif

#if defined(__i386__) || defined(__x86_64__)
static inline unsigned long
__stp_user_syscall_nr(struct pt_regs *regs)
{
#if defined(STAPCONF_X86_UNIREGS)
	return regs->orig_ax;
#elif defined(__x86_64__)
	return regs->orig_rax;
#elif defined (__i386__)
	return regs->orig_eax;
#endif
}
#endif

#if defined(__powerpc__)
static inline unsigned long
__stp_user_syscall_nr(struct pt_regs *regs)
{
	return regs->gpr[0];
}
#endif

#if defined(__ia64__)
static inline unsigned long
__stp_user_syscall_nr(struct pt_regs *regs)
{
        return regs->r15;
}
#endif

#if defined(__s390__) || defined(__s390x__)
static inline unsigned long
__stp_user_syscall_nr(struct pt_regs *regs)
{
	    // might need to be 'orig_gpr2'
	return regs->gprs[2];
}
#endif

#if defined(__i386__) || defined(__x86_64__)
static inline long *
__stp_user_syscall_return_value(struct task_struct *task, struct pt_regs *regs)
{
#ifdef CONFIG_IA32_EMULATION
// This code works, but isn't what we need.  Since
// __stp_user_syscall_arg() doesn't sign-extend, a value passed in as
// an argument and then returned won't compare correctly anymore.  So,
// for now, disable this code.
# if 0
	if (test_tsk_thread_flag(task, TIF_IA32))
		// Sign-extend the value so (int)-EFOO becomes (long)-EFOO
		// and will match correctly in comparisons.
		regs->ax = (long) (int) regs->ax;
# endif
#endif
#if defined(STAPCONF_X86_UNIREGS)
	return &regs->ax;
#elif defined(__x86_64__)
	return &regs->rax;
#elif defined (__i386__)
	return &regs->eax;
#endif
}
#endif

#if defined(__powerpc__)
static inline long *
__stp_user_syscall_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return &regs->gpr[3];
} 
#endif

#if defined(__ia64__)
static inline long *
__stp_user_syscall_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return &regs->r8;
}
#endif

#if defined(__s390__) || defined(__s390x__)
static inline long *
__stp_user_syscall_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return &regs->gprs[2];
}
#endif

#if defined(__i386__) || defined(__x86_64__)
static inline long *
__stp_user_syscall_arg(struct task_struct *task, struct pt_regs *regs,
		       unsigned int n)
{
#if defined(__i386__)
	if (n > 5) {
		_stp_error("syscall arg > 5");
		return NULL;
	}
#if defined(STAPCONF_X86_UNIREGS)
	return &regs->bx + n;
#else
	return &regs->ebx + n;
#endif
#elif defined(__x86_64__)
#ifdef CONFIG_IA32_EMULATION
	if (test_tsk_thread_flag(task, TIF_IA32))
		switch (n) {
#if defined(STAPCONF_X86_UNIREGS)
		case 0: return &regs->bx;
		case 1: return &regs->cx;
		case 2: return &regs->dx;
		case 3: return &regs->si;
		case 4: return &regs->di;
		case 5: return &regs->bp;
#else
		case 0: return &regs->rbx;
		case 1: return &regs->rcx;
		case 2: return &regs->rdx;
		case 3: return &regs->rsi;
		case 4: return &regs->rdi;
		case 5: return &regs->rbp;
#endif
		default: 
			_stp_error("syscall arg > 5");
			return NULL;
		}
#endif /* CONFIG_IA32_EMULATION */
	switch (n) {
#if defined(STAPCONF_X86_UNIREGS)
	case 0: return &regs->di;
	case 1: return &regs->si;
	case 2: return &regs->dx;
	case 3: return &regs->r10;
	case 4: return &regs->r8;
	case 5: return &regs->r9;
#else
	case 0: return &regs->rdi;
	case 1: return &regs->rsi;
	case 2: return &regs->rdx;
	case 3: return &regs->r10;
	case 4: return &regs->r8;
	case 5: return &regs->r9;
#endif
	default: 
		_stp_error("syscall arg > 5");
		return NULL;
	}
#endif /* CONFIG_X86_32 */
}
#endif

#if defined(__powerpc__)
static inline long *
__stp_user_syscall_arg(struct task_struct *task, struct pt_regs *regs,
		       unsigned int n)
{
	switch (n) {
	case 0: return &regs->gpr[3];
	case 1: return &regs->gpr[4];
	case 2: return &regs->gpr[5];
	case 3: return &regs->gpr[6];
	case 4: return &regs->gpr[7];
	case 5: return &regs->gpr[8];
	default:
		_stp_error("syscall arg > 5");
		return NULL;
	}
}
#endif

#if defined(__ia64__)
/* Defined in regs-ia64.c. Forward declaration for ____stp_user_syscall_arg. */
static long * __ia64_fetch_register(int, struct pt_regs *, unsigned long **);

#define __stp_user_syscall_arg(task, regs, n) \
	____stp_user_syscall_arg(task, regs, n, &c->unwaddr)

static inline long *
____stp_user_syscall_arg(struct task_struct *task, struct pt_regs *regs,
			 unsigned int n, unsigned long **cache)
{
	if (n > 5) {
		_stp_error("syscall arg > 5");
		return NULL;
	}
	return __ia64_fetch_register(n + 32, regs, cache);
}
#endif

#if defined(__s390__) || defined(__s390x__)
static inline long *
__stp_user_syscall_arg(struct task_struct *task, struct pt_regs *regs,
		       unsigned int n)
{
	/* If we were returning a value, we could check for TIF_31BIT
	 * here and cast the value with '(u32)' to make sure it got
	 * down to 32bits.  But, since we're returning an address,
	 * there isn't much we can do. */
	switch (n) {
	case 0: return &regs->orig_gpr2;
	case 1: return &regs->gprs[3];
	case 2: return &regs->gprs[4];
	case 3: return &regs->gprs[5];
	case 4: return &regs->gprs[6];
	case 5: return &regs->args[0];
	default:
		_stp_error("syscall arg > 5");
		return NULL;
	}
}
#endif

#endif /* _SYSCALL_H_ */
