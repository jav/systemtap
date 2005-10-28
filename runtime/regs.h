/* common register includes used in multiple modules
 * Copyright (C) 2005 Red Hat Inc.
 * Copyright (C) 2005 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _REGS_H_ /* -*- linux-c -*- */
#define _REGS_H_


#ifdef __x86_64__

#define REG_IP(regs) regs->rip
#define REG_SP(regs) regs->rsp

#elif defined (__i386__)

#define REG_IP(regs) regs->eip
#define REG_SP(regs) regs->esp

#elif defined (__ia64__)
#define REG_IP(regs)    ((regs)->cr_iip +ia64_psr(regs)->ri)
#define REG_SP(regs)    ((regs)->r12)

#elif defined (__powerpc64__)

#define REG_IP(regs) regs->nip
#define REG_SP(regs) regs->gpr[1]
#define REG_LINK(regs) regs->link

#else
#error "Unimplemented architecture"
#endif

#endif /* _REGS_H_ */
