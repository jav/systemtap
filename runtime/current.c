#ifndef _CURRENT_C_
#define _CURRENT_C_

/* -*- linux-c -*- */
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
 * @return The return address saved in esp or rsp.
 * @note i386 and x86_64 only so far.
 */
 
unsigned long _stp_ret_addr (struct pt_regs *regs)
{
#ifdef __x86_64__
  unsigned long *ra = (unsigned long *)regs->rsp;
#else
  unsigned long *ra = (unsigned long *)regs->esp;
#endif
  if (ra)
    return *ra;
  else
    return 0;
}
/** @} */
#endif /* _CURRENT_C_ */
