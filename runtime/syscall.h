/*
 * syscall defines and inlines
 * Copyright (C) 2008-2009 Red Hat Inc.
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

#ifdef STAPCONF_ASM_SYSCALL_H

/* If the system has asm/syscall.h, use defines from it. */
#include <asm/syscall.h>

#else  /* !STAPCONF_ASM_SYSCALL_H */

/* If the system doesn't have asm/syscall.h, use our defines. */
#if defined(__i386__) || defined(__x86_64__)
static inline long
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
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
static inline long
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->gpr[0];
}
#endif

#if defined(__ia64__)
static inline long
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
        return regs->r15;
}
#endif

#if defined(__s390__) || defined(__s390x__)
static inline long
syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	// might need to be 'orig_gpr2'
	return regs->gprs[2];
}
#endif

#if defined(__i386__) || defined(__x86_64__)
static inline long
syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
#ifdef CONFIG_IA32_EMULATION
// This code works, but isn't what we need.  Since
// syscall_get_syscall_arg() doesn't sign-extend, a value passed in as
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
	return regs->ax;
#elif defined(__x86_64__)
	return regs->rax;
#elif defined (__i386__)
	return regs->eax;
#endif
}
#endif

#if defined(__powerpc__)
static inline long
syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->gpr[3];
} 
#endif

#if defined(__ia64__)
static inline long
syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->r8;
}
#endif

#if defined(__s390__) || defined(__s390x__)
static inline long
syscall_get_return_value(struct task_struct *task, struct pt_regs *regs)
{
	return regs->gprs[2];
}
#endif

#if defined(__i386__) || defined(__x86_64__)
static inline void
syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, unsigned long *args)
{
	if (i + n > 6) {
		_stp_error("invalid syscall arg request");
		return;
	}
#if defined(__i386__)
#if defined(STAPCONF_X86_UNIREGS)
	memcpy(args, &regs->bx + i, n * sizeof(args[0]));
#else
	memcpy(args, &regs->ebx + i, n * sizeof(args[0]));
#endif
#elif defined(__x86_64__)
#ifdef CONFIG_IA32_EMULATION
	if (test_tsk_thread_flag(task, TIF_IA32)) {
		switch (i) {
#if defined(STAPCONF_X86_UNIREGS)
		case 0:
			if (!n--) break;
			*args++ = regs->bx;
		case 1:
			if (!n--) break;
			*args++ = regs->cx;
		case 2:
			if (!n--) break;
			*args++ = regs->dx;
		case 3:
			if (!n--) break;
			*args++ = regs->si;
		case 4:
			if (!n--) break;
			*args++ = regs->di;
		case 5:
			if (!n--) break;
			*args++ = regs->bp;
#else
		case 0:
			if (!n--) break;
			*args++ = regs->rbx;
		case 1:
			if (!n--) break;
			*args++ = regs->rcx;
		case 2:
			if (!n--) break;
			*args++ = regs->rdx;
		case 3:
			if (!n--) break;
			*args++ = regs->rsi;
		case 4:
			if (!n--) break;
			*args++ = regs->rdi;
		case 5:
			if (!n--) break;
			*args++ = regs->rbp;
#endif
		}
		return;
	}
#endif /* CONFIG_IA32_EMULATION */
	switch (i) {
#if defined(STAPCONF_X86_UNIREGS)
	case 0:
		if (!n--) break;
		*args++ = regs->di;
	case 1:
		if (!n--) break;
		*args++ = regs->si;
	case 2:
		if (!n--) break;
		*args++ = regs->dx;
	case 3:
		if (!n--) break;
		*args++ = regs->r10;
	case 4:
		if (!n--) break;
		*args++ = regs->r8;
	case 5:
		if (!n--) break;
		*args++ = regs->r9;
#else
	case 0:
		if (!n--) break;
		*args++ = regs->rdi;
	case 1:
		if (!n--) break;
		*args++ = regs->rsi;
	case 2:
		if (!n--) break;
		*args++ = regs->rdx;
	case 3:
		if (!n--) break;
		*args++ = regs->r10;
	case 4:
		if (!n--) break;
		*args++ = regs->r8;
	case 5:
		if (!n--) break;
		*args++ = regs->r9;
#endif
	}
#endif /* CONFIG_X86_32 */
	return;
}
#endif

#if defined(__powerpc__)
static inline void
syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, unsigned long *args)
{
	if (i + n > 6) {
		_stp_error("invalid syscall arg request");
		return;
	}
	memcpy(args, &regs->gpr[3 + i], n * sizeof(args[0]));
}
#endif

#if defined(__ia64__)
#define syscall_get_arguments(task, regs, i, n, args)		\
	__ia64_syscall_get_arguments(task, regs, i, n, args, &c->unwaddr)

static inline void
__ia64_syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
			     unsigned int i, unsigned int n,
			     unsigned long *args, unsigned long **cache)
{
	if (i + n > 6) {
		_stp_error("invalid syscall arg request");
		return;
	}
	switch (i) {
	case 0:
		if (!n--) break;
		*args++ = *__ia64_fetch_register(i + 32, regs, cache);
	case 1:
		if (!n--) break;
		*args++ = *__ia64_fetch_register(i + 33, regs, cache);
	case 2:
		if (!n--) break;
		*args++ = *__ia64_fetch_register(i + 34, regs, cache);
	case 3:
		if (!n--) break;
		*args++ = *__ia64_fetch_register(i + 35, regs, cache);
	case 4:
		if (!n--) break;
		*args++ = *__ia64_fetch_register(i + 36, regs, cache);
	case 5:
		if (!n--) break;
		*args++ = *__ia64_fetch_register(i + 37, regs, cache);
	}
}
#endif

#if defined(__s390__) || defined(__s390x__)
static inline void
syscall_get_arguments(struct task_struct *task, struct pt_regs *regs,
		      unsigned int i, unsigned int n, unsigned long *args)
{
	unsigned long mask = -1UL;

	if (i + n > 6) {
		_stp_error("invalid syscall arg request");
		return;
	}
#ifdef CONFIG_COMPAT
	if (test_tsk_thread_flag(task, TIF_31BIT))
		mask = 0xffffffff;
#endif
	switch (i) {
	case 0:
		if (!n--) break;
		*args++ = regs->orig_gpr2 & mask;
	case 1:
		if (!n--) break;
		*args++ = regs->gprs[3] & mask;
	case 2:
		if (!n--) break;
		*args++ = regs->gprs[4] & mask;
	case 3:
		if (!n--) break;
		*args++ = regs->gprs[5] & mask;
	case 4:
		if (!n--) break;
		*args++ = regs->gprs[6] & mask;
	case 5:
		if (!n--) break;
		*args++ = regs->args[0] & mask;
	}
}
#endif

#endif /* !STAPCONF_ASM_SYSCALL_H */
#endif /* _SYSCALL_H_ */
