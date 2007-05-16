/* -*- linux-c -*- 
 * Functions to access the members of pt_regs struct
 * Copyright (C) 2005 Red Hat Inc.
 * Copyright (C) 2005 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _REGS_C_
#define _REGS_C_

#include "regs.h"

/** @file current.c
 * @brief Functions to get the current state.
 */
/** @addtogroup current Current State
 * Functions to get the current state.
 * @{
 */


/** Get the current return address.
 * Call from kprobes (not jprobes).
 * @param regs The pt_regs saved by the kprobe.
 * @return The return address saved in the stack pointer.
 * @note i386 and x86_64 only so far.
 */
 
unsigned long _stp_ret_addr (struct pt_regs *regs)
{
#ifdef __x86_64__
	unsigned long *ra = (unsigned long *)regs->rsp;
	if (ra)
		return *ra;
	else
		return 0;
#elif defined (__i386__)
	return regs->esp;
#elif defined (__powerpc64__)
	return REG_LINK(regs);
#elif defined (__ia64__)
	return regs->b0;
#elif defined (__s390__) || defined (__s390x__)
	return regs->gprs[14];
#else
	#error Unimplemented architecture
#endif
}

/** Get the current return address for a return probe.
 * Call from kprobe return probe.
 * @param ri Pointer to the struct kretprobe_instance.
 * @return The return address
 */
#define _stp_ret_addr_r(ri) (ri->ret_addr)

/** Get the probe address for a kprobe.
 * Call from a kprobe. This will return the
 * address of the function that is being probed.
 * @param kp Pointer to the struct kprobe.
 * @return The function's address
 */
#define _stp_probe_addr(kp) (kp->addr)

/** Get the probe address for a return probe.
 * Call from kprobe return probe. This will return the
 * address of the function that is being probed.
 * @param ri Pointer to the struct kretprobe_instance.
 * @return The function's address
 */
#define _stp_probe_addr_r(ri) (ri->rp->kp.addr)

#ifdef  __x86_64__
void _stp_print_regs(struct pt_regs * regs)
{
        unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
        unsigned int fsindex,gsindex;
        unsigned int ds,cs,es;

        _stp_printf("RIP: %016lx\nRSP: %016lx  EFLAGS: %08lx\n", regs->rip, regs->rsp, regs->eflags);
        _stp_printf("RAX: %016lx RBX: %016lx RCX: %016lx\n",
               regs->rax, regs->rbx, regs->rcx);
        _stp_printf("RDX: %016lx RSI: %016lx RDI: %016lx\n",
               regs->rdx, regs->rsi, regs->rdi);
        _stp_printf("RBP: %016lx R08: %016lx R09: %016lx\n",
               regs->rbp, regs->r8, regs->r9);
        _stp_printf("R10: %016lx R11: %016lx R12: %016lx\n",
               regs->r10, regs->r11, regs->r12);
        _stp_printf("R13: %016lx R14: %016lx R15: %016lx\n",
               regs->r13, regs->r14, regs->r15);

        asm("movl %%ds,%0" : "=r" (ds));
        asm("movl %%cs,%0" : "=r" (cs));
        asm("movl %%es,%0" : "=r" (es));
        asm("movl %%fs,%0" : "=r" (fsindex));
        asm("movl %%gs,%0" : "=r" (gsindex));

        rdmsrl(MSR_FS_BASE, fs);
        rdmsrl(MSR_GS_BASE, gs);
        rdmsrl(MSR_KERNEL_GS_BASE, shadowgs);

        asm("movq %%cr0, %0": "=r" (cr0));
        asm("movq %%cr2, %0": "=r" (cr2));
        asm("movq %%cr3, %0": "=r" (cr3));
        asm("movq %%cr4, %0": "=r" (cr4));

        _stp_printf("FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n",
               fs,fsindex,gs,gsindex,shadowgs);
        _stp_printf("CS:  %04x DS: %04x ES: %04x CR0: %016lx\n", cs, ds, es, cr0);
        _stp_printf("CR2: %016lx CR3: %016lx CR4: %016lx\n", cr2, cr3, cr4);
}

#elif defined (__ia64__)
void _stp_print_regs(struct pt_regs * regs)
{
     unsigned long ip = regs->cr_iip + ia64_psr(regs)->ri;

	_stp_printf("\nPid: %d, CPU %d, comm: %20s\n", current->pid,
		smp_processor_id(), current->comm);
	_stp_printf("psr : %016lx ifs : %016lx ip  : [<%016lx>]  \n",
		regs->cr_ipsr, regs->cr_ifs, ip);
	_stp_printf("unat: %016lx pfs : %016lx rsc : %016lx\n",
		regs->ar_unat, regs->ar_pfs, regs->ar_rsc);
	_stp_printf("rnat: %016lx bsps: %016lx pr  : %016lx\n",
		regs->ar_rnat, regs->ar_bspstore, regs->pr);
	_stp_printf("ldrs: %016lx ccv : %016lx fpsr: %016lx\n",
		regs->loadrs, regs->ar_ccv, regs->ar_fpsr);
	_stp_printf("csd : %016lx ssd : %016lx\n",
		regs->ar_csd, regs->ar_ssd);
	_stp_printf("b0  : %016lx b6  : %016lx b7  : %016lx\n",
		regs->b0, regs->b6, regs->b7);
	_stp_printf("f6  : %05lx%016lx f7  : %05lx%016lx\n",
		regs->f6.u.bits[1], regs->f6.u.bits[0],
		regs->f7.u.bits[1], regs->f7.u.bits[0]);
	_stp_printf("f8  : %05lx%016lx f9  : %05lx%016lx\n",
		regs->f8.u.bits[1], regs->f8.u.bits[0],
		regs->f9.u.bits[1], regs->f9.u.bits[0]);
	_stp_printf("f10 : %05lx%016lx f11 : %05lx%016lx\n",
		regs->f10.u.bits[1], regs->f10.u.bits[0],
		regs->f11.u.bits[1], regs->f11.u.bits[0]);
}

#elif defined (__i386__)

/** Write the registers to a string.
 * @param regs The pt_regs saved by the kprobe.
 * @note i386 and x86_64 only so far. 
 */
void _stp_print_regs(struct pt_regs * regs)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L;
	
	_stp_printf ("EIP: %08lx\n",regs->eip);
	_stp_printf ("ESP: %08lx\n",regs->esp);
	_stp_printf ("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		      regs->eax,regs->ebx,regs->ecx,regs->edx);
	_stp_printf ("ESI: %08lx EDI: %08lx EBP: %08lx",
		      regs->esi, regs->edi, regs->ebp);
	_stp_printf (" DS: %04x ES: %04x\n",
		      0xffff & regs->xds,0xffff & regs->xes);
	
	__asm__("movl %%cr0, %0": "=r" (cr0));
	__asm__("movl %%cr2, %0": "=r" (cr2));
	__asm__("movl %%cr3, %0": "=r" (cr3));
	/* This could fault if %cr4 does not exist */
	__asm__("1: movl %%cr4, %0		\n"
		"2:				\n"
		".section __ex_table,\"a\"	\n"
		".long 1b,2b			\n"
		".previous			\n"
		: "=r" (cr4): "0" (0));
	_stp_printf ("CR0: %08lx CR2: %08lx CR3: %08lx CR4: %08lx\n", cr0, cr2, cr3, cr4);
}

#elif defined (__powerpc64__)

void _stp_print_regs(struct pt_regs * regs)
{
	int i;

	_stp_printf("NIP: %016lX XER: %08X LR: %016lX CTR: %016lX\n",
	       regs->nip, (unsigned int)regs->xer, regs->link, regs->ctr);
	_stp_printf("REGS: %016lx TRAP: %04lx\n", (long)regs, regs->trap);
	_stp_printf("MSR: %016lx CR: %08X\n",
			regs->msr, (unsigned int)regs->ccr);
	_stp_printf("DAR: %016lx DSISR: %016lx\n",
		       	regs->dar, regs->dsisr);

#ifdef CONFIG_SMP
	_stp_printf(" CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	for (i = 0; i < 32; i++) {
		if ((i % 4) == 0) {
			_stp_printf("\n GPR%02d: ", i);
		}

		_stp_printf("%016lX ", regs->gpr[i]);
		if (i == 13 && !FULL_REGS(regs))
			break;
	}
	_stp_printf("\nNIP [%016lx] ", regs->nip);
	_stp_printf("LR [%016lx]\n", regs->link);
}

#elif defined (__s390x__) || defined (__s390__)

#ifdef __s390x__
#define GPRSIZE "%016lX "
#else	/* s390 */
#define GPRSIZE "%08lX "
#endif

void _stp_print_regs(struct pt_regs * regs)
{
	char *mode;
	int i;

	mode = (regs->psw.mask & PSW_MASK_PSTATE) ? "User" : "Krnl";
	_stp_printf("%s PSW : ["GPRSIZE"] ["GPRSIZE"]",
		mode, (void *) regs->psw.mask,
		(void *) regs->psw.addr);

#ifdef CONFIG_SMP
	_stp_printf(" CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	for (i = 0; i < 16; i++) {
		if ((i % 4) == 0) {
			_stp_printf("\n GPRS%02d: ", i);
		}
		_stp_printf(GPRSIZE, regs->gprs[i]);
	}
	_stp_printf("\n");
}

#endif

/** @} */
#endif /* _REGS_C_ */
