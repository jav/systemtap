#ifndef _RUNTIME_H_
#define _RUNTIME_H_
/** @file runtime.h
 * @brief Main include file for runtime functions.
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/string.h>
#include <linux/kprobes.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <asm/uaccess.h>
#include <linux/kallsyms.h>

#define dbug(args...) ;

/* atomic globals */
static atomic_t _stp_transport_failures = ATOMIC_INIT (0);

/* some relayfs defaults that don't belong here */
static unsigned n_subbufs = 4;
static unsigned subbuf_size = 65536;

#include "print.c"
#include "string.c"


#endif /* _RUNTIME_H_ */
