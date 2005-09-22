#ifndef _CURRENT_C_ /* -*- linux-c -*- */
#define _CURRENT_C_

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
void _stp_sprint_regs(String str, struct pt_regs * regs)
{
        unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
        unsigned int fsindex,gsindex;
        unsigned int ds,cs,es;

        _stp_sprintf(str,"RIP: %016lx\nRSP: %016lx  EFLAGS: %08lx\n", regs->rip, regs->rsp, regs->eflags);
        _stp_sprintf(str,"RAX: %016lx RBX: %016lx RCX: %016lx\n",
               regs->rax, regs->rbx, regs->rcx);
        _stp_sprintf(str,"RDX: %016lx RSI: %016lx RDI: %016lx\n",
               regs->rdx, regs->rsi, regs->rdi);
        _stp_sprintf(str,"RBP: %016lx R08: %016lx R09: %016lx\n",
               regs->rbp, regs->r8, regs->r9);
        _stp_sprintf(str,"R10: %016lx R11: %016lx R12: %016lx\n",
               regs->r10, regs->r11, regs->r12);
        _stp_sprintf(str,"R13: %016lx R14: %016lx R15: %016lx\n",
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

        _stp_sprintf(str,"FS:  %016lx(%04x) GS:%016lx(%04x) knlGS:%016lx\n",
               fs,fsindex,gs,gsindex,shadowgs);
        _stp_sprintf(str,"CS:  %04x DS: %04x ES: %04x CR0: %016lx\n", cs, ds, es, cr0);
        _stp_sprintf(str,"CR2: %016lx CR3: %016lx CR4: %016lx\n", cr2, cr3, cr4);
}

#elif defined (__i386__)

/** Write the registers to a string.
 * @param regs The pt_regs saved by the kprobe.
 * @note i386 and x86_64 only so far. 
 */
void _stp_sprint_regs(String str, struct pt_regs * regs)
{
	unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L;
	
	_stp_sprintf (str, "EIP: %08lx\n",regs->eip);
	_stp_sprintf (str, "ESP: %08lx\n",regs->esp);
	_stp_sprintf (str, "EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
		      regs->eax,regs->ebx,regs->ecx,regs->edx);
	_stp_sprintf (str, "ESI: %08lx EDI: %08lx EBP: %08lx",
		      regs->esi, regs->edi, regs->ebp);
	_stp_sprintf (str, " DS: %04x ES: %04x\n",
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
	_stp_sprintf (str, "CR0: %08lx CR2: %08lx CR3: %08lx CR4: %08lx\n", cr0, cr2, cr3, cr4);
}

#elif defined (__powerpc64__)

void _stp_sprint_regs(String str, struct pt_regs * regs)
{
	int i;

	_stp_sprintf(str, "NIP: %016lX XER: %08X LR: %016lX CTR: %016lX\n",
	       regs->nip, (unsigned int)regs->xer, regs->link, regs->ctr);
	_stp_sprintf(str, "REGS: %p TRAP: %04lx\n", regs, regs->trap);
	_stp_sprintf(str, "MSR: %016lx CR: %08X\n",
			regs->msr, (unsigned int)regs->ccr);
	_stp_sprintf(str, "DAR: %016lx DSISR: %016lx\n",
		       	regs->dar, regs->dsisr);

#ifdef CONFIG_SMP
	_stp_sprintf(str, " CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	for (i = 0; i < 32; i++) {
		if ((i % 4) == 0) {
			_stp_sprintf(str, "\n GPR%02d: ", i);
		}

		_stp_sprintf(str, "%016lX ", regs->gpr[i]);
		if (i == 13 && !FULL_REGS(regs))
			break;
	}
	_stp_string_cat(str, "\n");
	_stp_sprintf(str, "NIP [%016lx] ", regs->nip);
	_stp_sprintf(str, "LR [%016lx] ", regs->link);
	_stp_string_cat(str, regs->link);
	_stp_string_cat(str, "\n");
}

#endif

/** Print the registers.
 * @param regs The pt_regs saved by the kprobe.
 * @note i386 and x86_64 only so far.
 */
#define _stp_print_regs(regs)				\
	{						\
		_stp_sprint_regs(_stp_stdout,regs);	\
		_stp_print_flush();			\
	}

/** @} */
#endif /* _CURRENT_C_ */
