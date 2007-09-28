/*
 *  Userspace Probes (UProbes)
 *  arch/i386/kernel/uprobes_i386.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2006
 */
#define UPROBES_IMPLEMENTATION 1
#include "uprobes.h"
#include <linux/uaccess.h>

/* Adapted from arch/x86_64/kprobes.c */
#undef W
#define W(row,b0,b1,b2,b3,b4,b5,b6,b7,b8,b9,ba,bb,bc,bd,be,bf)		      \
	(((b0##UL << 0x0)|(b1##UL << 0x1)|(b2##UL << 0x2)|(b3##UL << 0x3) |   \
	  (b4##UL << 0x4)|(b5##UL << 0x5)|(b6##UL << 0x6)|(b7##UL << 0x7) |   \
	  (b8##UL << 0x8)|(b9##UL << 0x9)|(ba##UL << 0xa)|(bb##UL << 0xb) |   \
	  (bc##UL << 0xc)|(bd##UL << 0xd)|(be##UL << 0xe)|(bf##UL << 0xf))    \
	 << (row % 32))
	static const unsigned long good_insns[256 / 32] = {
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
		/*      -------------------------------         */
		W(0x00, 1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0)| /* 00 */
		W(0x10, 1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0), /* 10 */
		W(0x20, 1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1)| /* 20 */
		W(0x30, 1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1), /* 30 */
		W(0x40, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 40 */
		W(0x50, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1), /* 50 */
		W(0x60, 1,1,1,0,0,0,0,0,1,1,1,1,0,0,0,0)| /* 60 */
		W(0x70, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1), /* 70 */
		W(0x80, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 80 */
		W(0x90, 1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1), /* 90 */
		W(0xa0, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* a0 */
		W(0xb0, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1), /* b0 */
		W(0xc0, 1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0)| /* c0 */
		W(0xd0, 1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1), /* d0 */
		W(0xe0, 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0)| /* e0 */
		W(0xf0, 0,0,0,0,0,1,1,1,1,1,0,0,1,1,1,1)  /* f0 */
		/*      -------------------------------         */
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
	};

	static const unsigned long good_2byte_insns[256 / 32] = {
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
		/*      -------------------------------         */
		W(0x00, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 00 */
		W(0x10, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 10 */
		W(0x20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 20 */
		W(0x30, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 30 */
		W(0x40, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 40 */
		W(0x50, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 50 */
		W(0x60, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 60 */
		W(0x70, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 70 */
		W(0x80, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 80 */
		W(0x90, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1), /* 90 */
		W(0xa0, 1,1,1,1,1,1,0,0,1,1,1,1,1,1,0,1)| /* a0 */
		W(0xb0, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1), /* b0 */
		W(0xc0, 1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1)| /* c0 */
		W(0xd0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* d0 */
		W(0xe0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* e0 */
		W(0xf0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)  /* f0 */
		/*      -------------------------------         */
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
	};

/*
 * TODO:
 * - Allow valid 2-byte opcodes (first byte = 0x0f).
 * - Where necessary, examine the modrm byte and allow valid instructions
 * in the different Groups and fpu instructions.
 * - Allow at least some instruction prefixes.
 * - Note: If we go past the first byte, do we need to verify that
 * subsequent bytes were actually there, rather than off the last page?
 * Probably overkill.  We don't verify that they specified the first byte
 * of the instruction, either.
 * - Be clearer about which instructions we'll never probe.
 */

/*
 * opcodes we'll probably never support:
 * 63 - arpl
 * 6c-6d, e4-e5, ec-ed - in
 * 6e-6f, e6-e7, ee-ef - out
 * cc, cd - int3, int
 * cf - iret
 * d6 - illegal instruction
 * f1 - int1/icebp
 * f4 - hlt
 * fa, fb - cli, sti
 *
 * opcodes we may need to refine support for:
 * 0f - valid 2-byte opcodes
 * 66 - data16 prefix
 * 8f - Group 1 - only reg = 0 is OK
 * c6-c7 - Group 11 - only reg = 0 is OK
 * d9-df - fpu insns with some illegal encodings
 * fe - Group 4 - only reg = 1 or 2 is OK
 * ff - Group 5 - only reg = 0-6 is OK
 *
 * others -- Do we need to support these?
 * 07, 17, 1f - pop es, pop ss, pop ds
 * 26, 2e, 36, 3e, 64, 65 - es:, cs:, ss:, ds:, fs:, gs: segment prefixes
 * 67 - addr16 prefix
 * 9b - wait/fwait
 * ce - into
 * f0 - lock prefix
 * f2, f3 - repnz, repz prefixes
 */

static
int arch_validate_probed_insn(struct uprobe_probept *ppt)
{
	uprobe_opcode_t *insn = ppt->insn;

	if (insn[0] == 0x66)
		/* Skip operand-size prefix */
		insn++;
	if (test_bit(insn[0], good_insns))
		return 0;
	if (insn[0] == 0x0f) {
		if (test_bit(insn[1], good_2byte_insns))
			return 0;
		printk(KERN_ERR "uprobes does not currently support probing "
			"instructions with the 2-byte opcode 0x0f 0x%2.2x\n",
			insn[1]);
	}
	printk(KERN_ERR "uprobes does not currently support probing "
		"instructions whose first byte is 0x%2.2x\n", insn[0]);
	return -EPERM;
}

/*
 * Get an instruction slot from the process's SSOL area, containing the
 * instruction at ppt's probepoint.  Point the eip at that slot, in
 * preparation for single-stepping out of line.
 */
static
void uprobe_pre_ssout(struct uprobe_task *utask, struct uprobe_probept *ppt,
		struct pt_regs *regs)
{
	struct uprobe_ssol_slot *slot;

	slot = uprobe_get_insn_slot(ppt);
	if (!slot) {
		utask->doomed = 1;
		return;
	}
	regs->eip = (long)slot->insn;
	utask->singlestep_addr = regs->eip;
}

/*
 * Called by uprobe_post_ssout() to adjust the return address
 * pushed by a call instruction executed out-of-line.
 */
static void adjust_ret_addr(long esp, long correction,
		struct uprobe_task *utask)
{
	int nleft;
	long ra;

	nleft = copy_from_user(&ra, (const void __user *) esp, 4);
	if (unlikely(nleft != 0))
		goto fail;
	ra +=  correction;
	nleft = copy_to_user((void __user *) esp, &ra, 4);
	if (unlikely(nleft != 0))
		goto fail;
	return;

fail:
	printk(KERN_ERR
		"uprobes: Failed to adjust return address after"
		" single-stepping call instruction;"
		" pid=%d, esp=%#lx\n", current->pid, esp);
	utask->doomed = 1;
}

/*
 * Called after single-stepping.  ppt->vaddr is the address of the
 * instruction whose first byte has been replaced by the "int3"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is utask->singlestep_addr.
 *
 * This function prepares to return from the post-single-step
 * interrupt.  We have to fix up the stack as follows:
 *
 * 0) Typically, the new eip is relative to the copied instruction.  We
 * need to make it relative to the original instruction.  Exceptions are
 * return instructions and absolute or indirect jump or call instructions.
 *
 * 1) If the single-stepped instruction was a call, the return address
 * that is atop the stack is the address following the copied instruction.
 * We need to make it the address following the original instruction.
 */
static
void uprobe_post_ssout(struct uprobe_task *utask, struct uprobe_probept *ppt,
		struct pt_regs *regs)
{
	long next_eip = 0;
	long copy_eip = utask->singlestep_addr;
	long orig_eip = ppt->vaddr;
	uprobe_opcode_t *insn = ppt->insn;

	up_read(&ppt->slot->rwsem);

	if (insn[0] == 0x66)
		/* Skip operand-size prefix */
		insn++;

	switch (insn[0]) {
	case 0xc3:		/* ret/lret */
	case 0xcb:
	case 0xc2:
	case 0xca:
		next_eip = regs->eip;
		/* eip is already adjusted, no more changes required*/
		break;
	case 0xe8:		/* call relative - Fix return addr */
		adjust_ret_addr(regs->esp, (orig_eip - copy_eip), utask);
		break;
	case 0xff:
		if ((insn[1] & 0x30) == 0x10) {
			/* call absolute, indirect */
			/* Fix return addr; eip is correct. */
			next_eip = regs->eip;
			adjust_ret_addr(regs->esp, (orig_eip - copy_eip),
				utask);
		} else if ((insn[1] & 0x31) == 0x20 ||
			   (insn[1] & 0x31) == 0x21) {
			/* jmp near or jmp far  absolute indirect */
			/* eip is correct. */
			next_eip = regs->eip;
		}
		break;
	case 0xea:		/* jmp absolute -- eip is correct */
		next_eip = regs->eip;
		break;
	default:
		break;
	}

	if (next_eip)
		regs->eip = next_eip;
	else
		regs->eip = orig_eip + (regs->eip - copy_eip);
}

/*
 * Replace the return address with the trampoline address.  Returns
 * the original return address.
 */
static
unsigned long arch_hijack_uret_addr(unsigned long trampoline_address,
		struct pt_regs *regs, struct uprobe_task *utask)
{
	int nleft;
	unsigned long orig_ret_addr;
#define RASIZE (sizeof(unsigned long))

	nleft = copy_from_user(&orig_ret_addr,
		       (const void __user *)regs->esp, RASIZE);
	if (unlikely(nleft != 0))
		return 0;

	if (orig_ret_addr == trampoline_address)
		/*
		 * There's another uretprobe on this function, and it was
		 * processed first, so the return address has already
		 * been hijacked.
		 */
		return orig_ret_addr;

	nleft = copy_to_user((void __user *)regs->esp,
		       &trampoline_address, RASIZE);
	if (unlikely(nleft != 0)) {
		if (nleft != RASIZE) {
			printk(KERN_ERR "uretprobe_entry_handler: "
					"return address partially clobbered -- "
					"pid=%d, %%esp=%#lx, %%eip=%#lx\n",
					current->pid, regs->esp, regs->eip);
			utask->doomed = 1;
		} /* else nothing written, so no harm */
		return 0;
	}
	return orig_ret_addr;
}
