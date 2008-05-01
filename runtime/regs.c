/* -*- linux-c -*- 
 * Functions to access the members of pt_regs struct
 * Copyright (C) 2005, 2007 Red Hat Inc.
 * Copyright (C) 2005 Intel Corporation.
 * Copyright (C) 2007 Quentin Barnes.
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
#if defined  (STAPCONF_X86_UNIREGS)  && (defined (__x86_64__) || defined (__i386__))
         unsigned long *ra = (unsigned long *)regs->sp;
          if (ra)
                  return *ra;
          else
                  return 0;
#elif defined  (__x86_64__)
	unsigned long *ra = (unsigned long *)regs->rsp;
	if (ra)
		return *ra;
	else
		return 0;
#elif defined (__i386__)
	return regs->esp;
#elif defined (__powerpc64__) || defined (__arm__)
	return REG_LINK(regs);
#elif defined (__ia64__)
	return regs->b0;
#elif defined (__s390__) || defined (__s390x__)
	return regs->gprs[14];
#elif defined (__arm__)
	return regs->ARM_r0;
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

#if defined  (STAPCONF_X86_UNIREGS)  && defined (__x86_64__)

void _stp_print_regs(struct pt_regs * regs)
{
        unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L, fs, gs, shadowgs;
        unsigned int fsindex,gsindex;
        unsigned int ds,cs,es;

        _stp_printf("RIP: %016lx\nRSP: %016lx  EFLAGS: %08lx\n", regs->ip, regs->sp, regs->flags);
        _stp_printf("RAX: %016lx RBX: %016lx RCX: %016lx\n",
               regs->ax, regs->bx, regs->cx);
        _stp_printf("RDX: %016lx RSI: %016lx RDI: %016lx\n",
               regs->dx, regs->si, regs->di);
        _stp_printf("RBP: %016lx R08: %016lx R09: %016lx\n",
               regs->bp, regs->r8, regs->r9);
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

 #elif defined (STAPCONF_X86_UNIREGS) && defined (__i386__)

void _stp_print_regs(struct pt_regs * regs)
{
       unsigned long cr0 = 0L, cr2 = 0L, cr3 = 0L, cr4 = 0L;

       _stp_printf ("EIP: %08lx\n",regs->ip);
       _stp_printf ("ESP: %08lx\n",regs->sp);
       _stp_printf ("EAX: %08lx EBX: %08lx ECX: %08lx EDX: %08lx\n",
                     regs->ax,regs->bx,regs->cx,regs->dx);
       _stp_printf ("ESI: %08lx EDI: %08lx EBP: %08lx",
                     regs->si, regs->di, regs->bp);
       _stp_printf (" DS: %04x ES: %04x\n",
                     0xffff & regs->ds,0xffff & regs->es);

       __asm__("movl %%cr0, %0": "=r" (cr0));
       __asm__("movl %%cr2, %0": "=r" (cr2));
       __asm__("movl %%cr3, %0": "=r" (cr3));
       /* This could fault if %cr4 does not exist */
       __asm__("1: movl %%cr4, %0              \n"
               "2:                             \n"
               ".section __ex_table,\"a\"      \n"
               ".long 1b,2b                    \n"
               ".previous                      \n"
               : "=r" (cr4): "0" (0));
       _stp_printf ("CR0: %08lx CR2: %08lx CR3: %08lx CR4: %08lx\n", cr0, cr2, cr3, cr4);
}

#elif defined  (__x86_64__)
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

#elif defined (__arm__)

static const char *processor_modes[]=
{ "USER_26", "FIQ_26" , "IRQ_26" , "SVC_26" , "UK4_26" , "UK5_26" , "UK6_26" , "UK7_26" ,
  "UK8_26" , "UK9_26" , "UK10_26", "UK11_26", "UK12_26", "UK13_26", "UK14_26", "UK15_26",
  "USER_32", "FIQ_32" , "IRQ_32" , "SVC_32" , "UK4_32" , "UK5_32" , "UK6_32" , "ABT_32" ,
  "UK8_32" , "UK9_32" , "UK10_32", "UND_32" , "UK12_32", "UK13_32", "UK14_32", "SYS_32"
};


void _stp_print_regs(struct pt_regs * regs)
{
	unsigned long flags = condition_codes(regs);

#ifdef CONFIG_SMP
	_stp_printf(" CPU: %d", smp_processor_id());
#endif /* CONFIG_SMP */

	_stp_printf("pc : [<%08lx>]    lr : [<%08lx>]\n"
	       "sp : %08lx  ip : %08lx  fp : %08lx\n",
		instruction_pointer(regs),
		regs->ARM_lr, regs->ARM_sp,
		regs->ARM_ip, regs->ARM_fp);
	_stp_printf("r10: %08lx  r9 : %08lx  r8 : %08lx\n",
		regs->ARM_r10, regs->ARM_r9,
		regs->ARM_r8);
	_stp_printf("r7 : %08lx  r6 : %08lx  r5 : %08lx  r4 : %08lx\n",
		regs->ARM_r7, regs->ARM_r6,
		regs->ARM_r5, regs->ARM_r4);
	_stp_printf("r3 : %08lx  r2 : %08lx  r1 : %08lx  r0 : %08lx\n",
		regs->ARM_r3, regs->ARM_r2,
		regs->ARM_r1, regs->ARM_r0);
	_stp_printf("Flags: %c%c%c%c",
		flags & PSR_N_BIT ? 'N' : 'n',
		flags & PSR_Z_BIT ? 'Z' : 'z',
		flags & PSR_C_BIT ? 'C' : 'c',
		flags & PSR_V_BIT ? 'V' : 'v');
	_stp_printf("  IRQs o%s  FIQs o%s  Mode %s%s  Segment %s\n",
		interrupts_enabled(regs) ? "n" : "ff",
		fast_interrupts_enabled(regs) ? "n" : "ff",
		processor_modes[processor_mode(regs)],
		thumb_mode(regs) ? " (T)" : "",
		get_fs() == get_ds() ? "kernel" : "user");
#ifdef CONFIG_CPU_CP15
	{
		unsigned int ctrl;
		  __asm__ (
		"	mrc p15, 0, %0, c1, c0\n"
		: "=r" (ctrl));
		_stp_printf("Control: %04X\n", ctrl);
	}
#ifdef CONFIG_CPU_CP15_MMU
	{
		unsigned int transbase, dac;
		  __asm__ (
		"	mrc p15, 0, %0, c2, c0\n"
		"	mrc p15, 0, %1, c3, c0\n"
		: "=r" (transbase), "=r" (dac));
		_stp_printf("Table: %08X  DAC: %08X\n",
		  	transbase, dac);
	}
#endif
#endif
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

/*
 * (Theoretically) arch-independent scheme for binary lookup of register
 * values (from pt_regs) by register name.  A register may be called by
 * more than one name.
 */
struct _stp_register_desc {
	const char *name;
	unsigned short size;	// in bytes
	unsigned short offset;	// in bytes, from start of pt_regs
};

struct _stp_register_table {
	struct _stp_register_desc *registers;
	unsigned nr_registers;
	unsigned nr_slots;	// capacity
};

static void _stp_populate_register_table(void);

/*
 * If the named register is in the list, return its slot number and *found=1.
 * Else *found=0 and return the slot number where the name should be inserted.
 */
static int _stp_lookup_register(const char *name,
			struct _stp_register_table *table, int *found)
{
	unsigned begin, mid, end;

	*found = 0;
	end = table->nr_registers;
	if (end == 0)
		return 0;
	begin = 0;
	mid = -1;
	for (;;) {
		int cmp;
		int prev_mid = mid;
		mid = (begin + end) / 2;
		if (mid == prev_mid)
			break;
		cmp = strcmp(name, table->registers[mid].name);
		if (cmp == 0) {
			*found = 1;
			return mid;
		} else if (cmp < 0)
			end = mid;
		else
			begin = mid;
	}
	if (begin == 0 && strcmp(name, table->registers[0].name) < 0)
		return 0;
	return begin + 1;
}

/*
 * If found, return 1 and the size and/or offset in the pt_regs array.
 * Else return 0.
 */
static int _stp_find_register(const char *name,
	struct _stp_register_table *table, size_t *size, size_t *offset)
{
	int slot, found;
	if (table->nr_registers == 0)
		/*
		 * Should we do this at the beginning of time to avoid
		 * the possibility of spending too long in a handler?
		 */
		_stp_populate_register_table();
	slot = _stp_lookup_register(name, table, &found);
	if (found) {
		if (size)
			*size = table->registers[slot].size;
		if (offset)
			*offset = table->registers[slot].offset;
		return 1;
	}
	return 0;
}

/*
 * Add name to the register-lookup table.  Note that the name pointer
 * is merely copied, not strdup-ed.
 */
void _stp_add_register(const char *name, struct _stp_register_table *table,
						size_t size, size_t offset)
{
	int idx, found;
	struct _stp_register_desc *slot;

	idx = _stp_lookup_register(name, table, &found);
	if (found)
		_stp_error("stap runtime internal error: "
				"register name %s used twice\n", name);
	if (table->nr_registers >= table->nr_slots)
		_stp_error("stap runtime internal error: "
				"register table overflow\n");
	slot = &table->registers[idx];

	// Move the slots later in the array out of the way.
	if (idx < table->nr_registers)
		memmove(slot+1, slot,
			sizeof(*slot) * (table->nr_registers - idx));
	table->nr_registers++;
	slot->name = name;
	slot->size = size;
	slot->offset = offset;
}

#if defined(__i386__) || defined(__x86_64__)
/*
 * This register set is used for i386 kernel and apps, and for 32-bit apps
 * running on x86_64.  For the latter case, this allows the user to use
 * things like reg("eax") as well as the standard x86_64 pt_regs names.
 */

/*
 * x86_64 and i386 are especially ugly because the pt_reg member names
 * changed as part of the x86 merge.  We allow (and use, as needed)
 * either the pre-merge name or the post-merge name.
 */

// I count 32 different names, but add a fudge factor.
static struct _stp_register_desc i386_registers[32+8];
static struct _stp_register_table i386_register_table = {
	.registers = i386_registers,
	.nr_slots = ARRAY_SIZE(i386_registers)
};

/*
 * sizeof(long) is indeed what we want here, for both i386 and x86_64.
 * Unlike function args, x86_64 pt_regs is the same even if the int3
 * was in an -m32 app.
 */
#define ADD_PT_REG(name, member) \
	_stp_add_register(name, &i386_register_table, \
		sizeof(long), offsetof(struct pt_regs, member))
#define ADD2NAMES(nm1, nm2, member) \
	do { \
		ADD_PT_REG(nm1, member); \
		ADD_PT_REG(nm2, member); \
	} while (0)

#ifdef STAPCONF_X86_UNIREGS

/* Map "ax" and "eax" to regs->ax, and "cs" and "xcs" to regs->cs */
#define ADD_EREG(nm) ADD2NAMES(#nm, "e" #nm, nm)
#define ADD_XREG(nm) ADD2NAMES(#nm, "x" #nm, nm)
#define ADD_FLAGS_REG() ADD_EREG(flags)
#define EREG(nm, regs) ((regs)->nm)
#define RREG(nm, regs) ((regs)->nm)

#else	/* ! STAPCONF_X86_UNIREGS */

#ifdef __i386__
#define ADD_EREG(nm) ADD2NAMES(#nm, "e" #nm, e##nm)
#define ADD_XREG(nm) ADD2NAMES(#nm, "x" #nm, x##nm)
#define ADD_FLAGS_REG() ADD_EREG(flags)
#define EREG(nm, regs) ((regs)->e##nm)
#else	/* __x86_64__ */
/*
 * Map "eax" to regs->rax and "xcs" to regs->cs.  Other mappings are
 * handled in x86_64_register_table.
 */
#define ADD_EREG(nm) ADD_PT_REG("e" #nm, r##nm)
#define ADD_XREG(nm) ADD_PT_REG("x" #nm, nm)
#define ADD_FLAGS_REG() ADD2NAMES("flags", "eflags", eflags)
#define EREG(nm, regs) ((regs)->r##nm)
#define RREG(nm, regs) ((regs)->r##nm)
#endif	/* __x86_64__ */

#endif	/* ! STAPCONF_X86_UNIREGS */

static void _stp_populate_i386_register_table(void)
{
	/*
	 * The order here is the same as in i386 struct pt_regs.
	 * It's a different order from x86_64 pt_regs; but that doesn't
	 * matter -- even when compiling for x86_64 -- because the
	 * offsets are determined by offsetof(), not the calling order.
	 */
	ADD_EREG(bx);
	ADD_EREG(cx);
	ADD_EREG(dx);
	ADD_EREG(si);
	ADD_EREG(di);
	ADD_EREG(bp);
	ADD_EREG(ax);
#ifdef __i386__
	ADD_XREG(ds);
	ADD_XREG(es);
	ADD_XREG(fs);
	/* gs not saved */
#endif
#ifdef STAPCONF_X86_UNIREGS
	ADD2NAMES("orig_ax", "orig_eax", orig_ax);
#else
#ifdef __i386__
	ADD2NAMES("orig_ax", "orig_eax", orig_eax);
#else	/* __x86_64__ */
	ADD2NAMES("orig_ax", "orig_eax", orig_rax);
#endif
#endif	/* STAPCONF_X86_UNIREGS */
	ADD_EREG(ip);
	ADD_XREG(cs);
	ADD_FLAGS_REG();
	ADD_EREG(sp);
	ADD_XREG(ss);
}

static long
_stp_get_reg32_by_name(const char *name, struct pt_regs *regs)
{
	size_t offset = 0;
	long value;	// works for i386 or x86_64
	BUG_ON(!name);
	if (!regs)
		_stp_error("Register values not available in this context.\n");
#ifdef __i386__
	if (!user_mode(regs)) {
		/* esp and ss aren't saved on trap from kernel mode. */
		if (!strcmp(name,"esp") || !strcmp(name, "sp"))
			return (long) &EREG(sp, regs);
		if (!strcmp(name,"xss") || !strcmp(name, "ss")) {
			/*
			 * Assume ss register hasn't changed since we took
			 * the trap.
			 */
			unsigned short ss;
			asm volatile("movw %%ss, %0" : : "m" (ss));
			return ss;
		}
	}
#endif
	if (!_stp_find_register(name, &i386_register_table, NULL, &offset))
		_stp_error("Unknown register name: %s\n", name);
	(void) memcpy(&value, ((char*)regs) + offset, sizeof(value));
	return value;
}

#endif	/* __i386__ || __x86_64__ */

#ifdef __i386__
static void _stp_populate_register_table(void)
{
	_stp_populate_i386_register_table();
}
#endif	/* __i386__ */

#ifdef __x86_64__
// I count 32 different names (not the same 32 as i386), but add a fudge factor.
static struct _stp_register_desc x86_64_registers[32+8];
static struct _stp_register_table x86_64_register_table = {
	.registers = x86_64_registers,
	.nr_slots = ARRAY_SIZE(x86_64_registers)
};

/* NB: Redefining ADD_PT_REG here.  ADD2NAMES and such change accordingly. */
#undef ADD_PT_REG
#define ADD_PT_REG(name, member) \
	_stp_add_register(name, &x86_64_register_table, \
		sizeof(unsigned long), offsetof(struct pt_regs, member))

#define ADD_NREG(nm) ADD_PT_REG(#nm, nm)

#ifdef STAPCONF_X86_UNIREGS
#define ADD_RREG(nm) ADD2NAMES(#nm, "r" #nm, nm)
#else
#define ADD_RREG(nm) ADD2NAMES(#nm, "r" #nm, r##nm)
#endif

static void _stp_populate_register_table(void)
{
	/* Same order as in struct pt_regs */
	ADD_NREG(r15);
	ADD_NREG(r14);
	ADD_NREG(r13);
	ADD_NREG(r12);
	ADD_RREG(bp);
	ADD_RREG(bx);
	ADD_NREG(r11);
	ADD_NREG(r10);
	ADD_NREG(r9);
	ADD_NREG(r8);
	ADD_RREG(ax);
	ADD_RREG(cx);
	ADD_RREG(dx);
	ADD_RREG(si);
	ADD_RREG(di);
#ifdef STAPCONF_X86_UNIREGS
	ADD2NAMES("orig_ax", "orig_rax", orig_ax);
#else
	ADD2NAMES("orig_ax", "orig_rax", orig_rax);
#endif
	ADD_RREG(ip);
	ADD_NREG(cs);
	ADD_FLAGS_REG();
	ADD_RREG(sp);
	ADD_NREG(ss);

	_stp_populate_i386_register_table();
}

static int _stp_probing_32bit_app(struct pt_regs *regs)
{
	return (user_mode(regs) && test_tsk_thread_flag(current, TIF_IA32));
}

static unsigned long
_stp_get_reg64_by_name(const char *name, struct pt_regs *regs)
{
	size_t offset = 0;
	unsigned long value;
	BUG_ON(!name);
	if (!regs)
		_stp_error("Register values not available in this context.\n");
	if (_stp_find_register(name, &x86_64_register_table, NULL, &offset)) {
		(void) memcpy(&value, ((char*)regs) + offset, sizeof(value));
		return value;
	}
	if (_stp_probing_32bit_app(regs))
		return _stp_get_reg32_by_name(name, regs);
	_stp_error("Unknown register name: %s\n", name);
	/* NOTREACHED */
	return 0;
}
#endif /* __x86_64__ */

/* Function arguments */

#if defined(__i386__) || defined(__x86_64__)
static long _stp_get_sp(struct pt_regs *regs)
{
#ifdef __i386__
	if (!user_mode(regs))
		return (long) &EREG(sp, regs);
#endif
	return EREG(sp, regs);
}

/*
 * Use this for i386 kernel and apps, and for 32-bit apps running on x86_64.
 * Does arch-specific work for fetching function arg #argnum (1 = first arg).
 * nr_regargs is the number of arguments that reside in registers (e.g.,
 * 3 for fastcall functions).
 * Returns:
 * 0 if the arg resides in a register.  *val contains its value.
 * 1 if the arg resides on the kernel stack.  *val contains its address.
 * 2 if the arg resides on the user stack.  *val contains its address.
 * -1 if the arg number is invalid.
 * We assume that the regs pointer is valid.
 */
static int _stp_get_arg32_by_number(int n, int nr_regargs,
					struct pt_regs *regs, long *val)
{
	if (nr_regargs < 0)
		return -1;
	if (n > nr_regargs) {
		/*
		 * The typical case: arg n is on the stack.
		 * stack[0] = return address
		 */
		int stack_index = n - nr_regargs;
		int32_t *stack = (int32_t*) _stp_get_sp(regs);
		*val = (long) &stack[stack_index];
		return (user_mode(regs) ? 2 : 1);
	} else {
		switch (n) {
		case 1: *val = EREG(ax, regs); break;
		case 2: *val = EREG(dx, regs); break;
		case 3: *val = EREG(cx, regs); break;
		default:
			/* gcc rejects regparm values > 3. */
			return -1;
		}
		return 0;
	}
}
#endif	/* __i386__ || __x86_64__ */

#ifdef __x86_64__
/* See _stp_get_arg32_by_number(). */
static int _stp_get_arg64_by_number(int n, int nr_regargs,
				struct pt_regs *regs, unsigned long *val)
{
	if (nr_regargs < 0)
		return -1;
	if (n > nr_regargs) {
		/* arg n is on the stack.  stack[0] = return address */
		int stack_index = n - nr_regargs;
		unsigned long *stack = (unsigned long*) _stp_get_sp(regs);
		*val = (unsigned long) &stack[stack_index];
		return (user_mode(regs) ? 2 : 1);
	} else {
		switch (n) {
		case 1: *val = RREG(di, regs); break;
		case 2: *val = RREG(si, regs); break;
		case 3: *val = RREG(dx, regs); break;
		case 4: *val = RREG(cx, regs); break;
		case 5: *val = regs->r8; break;
		case 6: *val = regs->r9; break;
		default:
			/* gcc rejects regparm values > 6. */
			return -1;
		}
		return 0;
	}
}
#endif	/* __x86_64__ */

/** @} */
#endif /* _REGS_C_ */
