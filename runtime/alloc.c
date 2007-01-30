/* -*- linux-c -*- 
 * Memory allocation functions
 * Copyright (C) 2005, 2006, 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _ALLOC_C_
#define _ALLOC_C_

/* counters of how much memory has been allocated */
static int _stp_allocated_memory = 0;
static int _stp_allocated_net_memory = 0;

#define STP_ALLOC_FLAGS (GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN)

static void *_stp_kmalloc(size_t size)
{
	void *ret = kmalloc(size, STP_ALLOC_FLAGS);
	if (ret)
		_stp_allocated_memory += size;
	return ret;
}

static void *_stp_kzalloc(size_t size)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
{
	void *ret = kmalloc(size, STP_ALLOC_FLAGS);
	if (ret) {
		memset (ret, 0, size);
		_stp_allocated_memory += size;
	}
	return ret;
}
#else
{
	void *ret = kzalloc(size, STP_ALLOC_FLAGS);
	if (ret)
		_stp_allocated_memory += size;
	return ret;
}
#endif

static void *_stp_vmalloc(unsigned long size)
{
	void *ret = __vmalloc(size, STP_ALLOC_FLAGS, PAGE_KERNEL);
	if (ret)
		_stp_allocated_memory += size;
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
#define _stp_alloc_percpu(size) __alloc_percpu(size, 8)
#else
#define _stp_alloc_percpu(size) __alloc_percpu(size)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
#define kmalloc_node(size,flags,node) kmalloc(size,flags)
#endif /* LINUX_VERSION_CODE */

#endif /* _ALLOC_C_ */
