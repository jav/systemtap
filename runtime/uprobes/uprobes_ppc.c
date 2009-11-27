/*
 * Userspace Probes (UProbes) for PowerPC
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
 * Copyright IBM Corporation, 2007
 */
/*
 * In versions of uprobes built in the SystemTap runtime, this file
 * is #included at the end of uprobes.c.
 */

/*
 * Replace the return address with the trampoline address.  Returns
 * the original return address.
 */
static
unsigned long arch_hijack_uret_addr(unsigned long trampoline_address,
		struct pt_regs *regs, struct uprobe_task *utask)
{
	unsigned long orig_ret_addr = regs->link;

	regs->link = trampoline_address;
	return orig_ret_addr;
}

/*
 * Get an instruction slot from the process's SSOL area, containing the
 * instruction at ppt's probepoint.  Point the eip at that slot, in preparation
 * for single-stepping out of line.
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
	regs->nip = (long)slot->insn;
}


static inline void calc_offset(struct uprobe_probept *ppt,
	       struct pt_regs *regs)
{
	int offset = 0;
	unsigned int opcode = 0;
	unsigned int insn = *ppt->insn;

	opcode = insn >> 26;
	switch (opcode) {
	case 16:	/* bc */
		if ((insn & 2) == 0) {
			offset = (signed short)(insn & 0xfffc);
			regs->nip = ppt->vaddr + offset;
		}
		if (insn & 1)
			regs->link = ppt->vaddr + MAX_UINSN_BYTES;
		break;
	case 17:	/* sc */
		/* Do we need to do anything */
		break;
	case 18:	/* b */
		if ((insn & 2) == 0) {
			offset = insn & 0x03fffffc;
			if (offset & 0x02000000)
				offset -= 0x04000000;
			regs->nip = ppt->vaddr + offset;
		}
		if (insn & 1)
			regs->link = ppt->vaddr + MAX_UINSN_BYTES;
		break;
	}
#ifdef UPROBES_DEBUG
	printk (KERN_ERR "ppt->vaddr=%p, regs->nip=%p, offset=%ld\n",
			ppt->vaddr, regs->nip, offset);
	if (insn & 1)
		printk (KERN_ERR "regs->link=%p \n", regs->link);
#endif
	return;
}

/*
 * Called after single-stepping.  ppt->vaddr is the address of the
 * instruction which was replaced by a breakpoint instruction.  To avoid
 * the SMP problems that can occur when we temporarily put back the
 * original opcode to single-step, we single-stepped a copy of the
 * instruction.
 *
 * This function prepares to return from the post-single-step
 * interrupt.
 *
 * 1) Typically, the new nip is relative to the copied instruction.  We
 * need to make it relative to the original instruction.  Exceptions are
 * branch instructions.
 *
 * 2) For branch instructions, update the nip if the branch uses
 * relative addressing.  Update the link instruction to the instruction
 * following the original instruction address.
 */

static
void uprobe_post_ssout(struct uprobe_task *utask, struct uprobe_probept *ppt,
		struct pt_regs *regs)
{
	unsigned long copy_nip;

	copy_nip = (unsigned long) ppt->slot->insn;
	up_read(&ppt->slot->rwsem);

	/*
	 * If the single stepped instruction is non-branch instruction
	 * then update the IP to be relative to probepoint.
	 */
	if (regs->nip == copy_nip + MAX_UINSN_BYTES)
		regs->nip = ppt->vaddr + MAX_UINSN_BYTES;
	else
		calc_offset(ppt,regs);
}

static
int arch_validate_probed_insn(struct uprobe_probept *ppt,
		 struct task_struct *tsk)
{
	if ((unsigned long)ppt->vaddr & 0x03) {
		printk(KERN_WARNING
			"Attempt to register uprobe at an unaligned addr\n");
		return -EINVAL;
	}
	return 0;
}
