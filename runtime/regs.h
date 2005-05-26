#ifndef _REGS_H_ /* -*- linux-c -*- */
#define _REGS_H_

/* common register includes used in multiple modules */

#ifdef __x86_64__

#define REG_IP(regs) regs->rip
#define REG_SP(regs) regs->rsp

#elif defined (__i386__)

#define REG_IP(regs) regs->eip
#define REG_SP(regs) regs->esp

#else
#error "Unimplemented architecture"
#endif

#endif /* _REGS_H_ */
