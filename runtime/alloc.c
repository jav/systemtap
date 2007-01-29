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

/* This file exists because all the NUMA-compatible allocators keep
   changing in 2.6 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
#define kmalloc_node(size,flags,node) kmalloc(size,flags)
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_SMP
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
#define _stp_alloc_percpu(size) __alloc_percpu(size, 8)
#define _stp_free_percpu(ptr) free_percpu(ptr)
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) */

/* This is like alloc_percpu() except it simply takes a size, instead of a type. */
void *_stp_alloc_percpu(size_t size)
{
	int i;
	struct percpu_data *pdata = kmalloc(sizeof (*pdata), STP_ALLOC_FLAGS);

	if (!pdata)
		return NULL;

	for_each_cpu(i) {
		int node = cpu_to_node(i);

		if (node_online(node))
			pdata->ptrs[i] = kmalloc_node(size, STP_ALLOC_FLAGS, node);
		else
			pdata->ptrs[i] = kmalloc(size, STP_ALLOC_FLAGS);

		if (!pdata->ptrs[i])
			goto unwind_oom;

		_stp_allocated_memory += size;
		memset(pdata->ptrs[i], 0, size);
	}

	/* Catch derefs w/o wrappers */
	return (void *) (~(unsigned long) pdata);

unwind_oom:
	while (--i >= 0) {
		if (!cpu_possible(i))
			continue;
		kfree(pdata->ptrs[i]);
	}
	kfree(pdata);
	return NULL;
}

void _stp_free_percpu(const void *objp)
{
	int i;
	struct percpu_data *p = (struct percpu_data *) (~(unsigned long) objp);

	for_each_cpu(i)
		kfree(p->ptrs[i]);
	kfree(p);
}
#endif /* LINUX_VERSION_CODE */

#else /* CONFIG_SMP */
#define _stp_free_percpu(ptr) kfree(ptr)
void *_stp_alloc_percpu(size_t size)
{
        void *ret = kmalloc(size, STP_ALLOC_FLAGS);
        if (ret)
                memset(ret, 0, size);
        return ret;
}
#endif /* CONFIG_SMP */

#endif /* _ALLOC_C_ */
