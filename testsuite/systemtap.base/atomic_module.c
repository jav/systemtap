/* -*- linux-c -*- 
 * Systemtap Atomic Test Module
 * Copyright (C) 2010 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/atomic.h>

/* The purpose of this module is to provide a bunch of functions that */
/* do nothing important, and then call them in different contexts. */
/* We use a /proc file to trigger function calls from user context. */
/* Then systemtap scripts set probes on the functions and run tests */
/* to see if the expected output is received. This is better than using */
/* the kernel because kernel internals frequently change. */


/************ Below are the functions to create this module ************/

struct {
	ulong barrier1;
	atomic_long_t a;
	ulong barrier2;
} stp_atomic_struct;

atomic_long_t *stp_get_atomic_long_addr(void)
{
	return(&stp_atomic_struct.a);
}
EXPORT_SYMBOL(stp_get_atomic_long_addr);

int init_module(void)
{
	stp_atomic_struct.barrier1 = ULONG_MAX;
	atomic_long_set(&stp_atomic_struct.a, 5);
        stp_atomic_struct.barrier2 = ULONG_MAX;
	return 0;
}

void cleanup_module(void)
{
}

MODULE_DESCRIPTION("systemtap atomic test module");
MODULE_LICENSE("GPL");
