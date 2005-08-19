/* main header file
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

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

#ifdef DEBUG
/** Prints debug line.
 * This function prints a debug message immediately to stpd. 
 * If the last character is not a newline, then one is added. 
 * @param args A variable number of args in a format like printf.
 * @ingroup io
 */
#define dbug(args...) _stp_dbug(__FUNCTION__, __LINE__, args)
#else
#define dbug(args...) ;
#endif /* DEBUG */

/* atomic globals */
static atomic_t _stp_transport_failures = ATOMIC_INIT (0);

#ifdef STP_RELAYFS
static struct
{
	atomic_t ____cacheline_aligned_in_smp seq;
} _stp_seq = { ATOMIC_INIT (0) };

#define _stp_seq_inc() (atomic_inc_return(&_stp_seq.seq))
#endif /* RELAYFS */

#include "print.c"
#include "string.c"
#include "arith.c"

/************* Module Stuff ********************/
int probe_start(void);

int init_module (void)
{
  return _stp_transport_init();
}

void cleanup_module(void)
{
  _stp_transport_close();
}

MODULE_LICENSE("GPL");

#endif /* _RUNTIME_H_ */
