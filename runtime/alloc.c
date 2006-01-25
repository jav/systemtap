/* -*- linux-c -*- 
 * Memory allocation functions
 * Copyright (C) 2005, 2006 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _ALLOC_C_
#define _ALLOC_C_

/* This file exists because all the NUMA-compatible allocators keep
   changing in 2.6 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
#define kmalloc_node(size,flags,node) kmalloc(size,flags)
#endif /* LINUX_VERSION_CODE */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
#define _stp_alloc_percpu(size) __alloc_percpu(size, 8)
#else
#ifdef CONFIG_SMP
/* This is like alloc_percpu() except it simply takes a size, instead of a type. */
void *_stp_alloc_percpu(size_t size)
{
	int i;
	struct percpu_data *pdata = kmalloc(sizeof (*pdata), GFP_KERNEL);

	if (!pdata)
		return NULL;

	for_each_cpu(i) {
		int node = cpu_to_node(i);

		if (node_online(node))
			pdata->ptrs[i] = kmalloc_node(size, GFP_KERNEL, node);
		else
			pdata->ptrs[i] = kmalloc(size, GFP_KERNEL);

		if (!pdata->ptrs[i])
			goto unwind_oom;
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
#else
void *_stp_alloc_percpu(size_t size)
{
        void *ret = kmalloc(size, GFP_KERNEL);
        if (ret)
                memset(ret, 0, size);
        return ret;
}
#define _stp_free_percpu(ptr) kfree(ptr)

#endif /* CONFIG_SMP */
#endif /* LINUX_VERSION_CODE */
#endif /* _ALLOC_C_ */
