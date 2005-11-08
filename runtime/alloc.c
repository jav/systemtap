/* -*- linux-c -*- 
 * Memory allocation functions
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _ALLOC_C_
#define _ALLOC_C_

/** @file alloc.c
 * @brief Memory functions.
 */
/** @addtogroup alloc Memory Functions
 * Basic malloc/calloc/free functions. These will be changed so 
 * that memory allocation errors will call a handler.  The default will
 * send a signal to the user-space daemon that will trigger the module to
 * be unloaded.
 * @{
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
/**
 * vmalloc_node - allocate virtually contiguous memory
 *
 *	@size:		allocation size
 *	@node:		preferred node
 *
 * This vmalloc variant try to allocate memory from a preferred node.
 * This code is from Eric Dumazet, posted to the LKML.
 * FIXME: The version in the mm kernel is different. Should probably
 * switch if is is easily backported.
 */
#ifdef CONFIG_NUMA
void *vmalloc_node(unsigned long size, int node)
{
	void *result;
	struct mempolicy *oldpol = current->mempolicy;
	mm_segment_t oldfs = get_fs();
	DECLARE_BITMAP(prefnode, MAX_NUMNODES);

	mpol_get(oldpol);
	bitmap_zero(prefnode, MAX_NUMNODES);
	set_bit(node, prefnode);

	set_fs(KERNEL_DS);
	sys_set_mempolicy(MPOL_PREFERRED, prefnode, MAX_NUMNODES);
	set_fs(oldfs);

	result = vmalloc(size);

	mpol_free(current->mempolicy);
	current->mempolicy = oldpol;
	return result;
}
#else
#define vmalloc_node(size,node) vmalloc(size)
#endif /* CONFIG_NUMA */
#endif /* LINUX_VERSION_CODE */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
#define kmalloc_node(size,flags,node) kmalloc(size,flags)
#endif /* LINUX_VERSION_CODE */

/** Allocates memory within a probe.
 * This is used for small allocations from within a running
 * probe where the process cannot sleep. 
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 * @note Not currently used by the runtime. Deprecate?
 */

void *_stp_alloc(size_t len)
{
	void *ptr = kmalloc(len, GFP_ATOMIC);
	if (unlikely(ptr == NULL))
		_stp_error("_stp_alloc failed.\n");
	return ptr;
}

void *_stp_alloc_cpu(size_t len, int cpu)
{
	void *ptr = kmalloc_node(len, GFP_ATOMIC, cpu_to_node(cpu));
	if (unlikely(ptr == NULL))
		_stp_error("_stp_alloc failed.\n");
	return ptr;
}

/** Allocates and clears memory within a probe.
 * This is used for small allocations from within a running
 * probe where the process cannot sleep. 
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 * @note Not currently used by the runtime. Deprecate?
 */

void *_stp_calloc(size_t len)
{
	void *ptr = _stp_alloc(len);
	if (likely(ptr))
		memset(ptr, 0, len);
	return ptr;
}

/** Allocates and clears memory outside a probe.
 * This is typically used in the module initialization to
 * allocate new maps, lists, etc.
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 */

void *_stp_valloc(size_t len)
{
	void *ptr = vmalloc(len);
	if (likely(ptr))
		memset(ptr, 0, len);
	else
		_stp_error("_stp_valloc failed.\n");
	return ptr;
}

void *_stp_valloc_cpu(size_t len, int cpu)
{
	void *ptr = vmalloc_node(len, cpu_to_node(cpu));
	if (likely(ptr))
		memset(ptr, 0, len);
	else
		_stp_error("_stp_valloc failed.\n");
	return ptr;
}

struct percpu_data {
	void *ptrs[NR_CPUS];
	void *blkp;
};

#ifdef CONFIG_SMP
/**
 * __stp_valloc_percpu - allocate one copy of the object for every present
 * cpu in the system, using vmalloc and zeroing them.
 * Objects should be dereferenced using the per_cpu_ptr macro only.
 *
 * @size: how many bytes of memory are required.
 * @align: the alignment, which can't be greater than SMP_CACHE_BYTES.
 */
static void *__stp_valloc_percpu(size_t size, size_t align)
{
	int i;
	struct percpu_data *pdata = kmalloc(sizeof (*pdata), GFP_KERNEL);

	if (!pdata)
		return NULL;

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		pdata->ptrs[i] = vmalloc_node(size, cpu_to_node(i));

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
		vfree(pdata->ptrs[i]);
	}
	kfree(pdata);
	return NULL;
}

void _stp_vfree_percpu(const void *objp)
{
	int i;
	struct percpu_data *p = (struct percpu_data *) (~(unsigned long) objp);

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		vfree(p->ptrs[i]);
	}
	kfree(p);
}
#else
static inline void *__stp_valloc_percpu(size_t size, size_t align)
{
	void *ret = kmalloc(size, GFP_KERNEL);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
void _stp_vfree_percpu(const void *ptr)
{	
	kfree(ptr);
}
#endif

#define _stp_valloc_percpu(type) \
	((type *)(__stp_valloc_percpu(sizeof(type), __alignof__(type))))

#define _stp_percpu_dptr(ptr)  (((struct percpu_data *)~(unsigned long)(ptr))->blkp)

/** Frees memory allocated by _stp_alloc or _stp_calloc.
 * @param ptr pointer to memory to free
 * @note Not currently used by the runtime. Deprecate?
 */

void _stp_free(void *ptr)
{
	if (likely(ptr))
		kfree(ptr);
}

/** Frees memory allocated by _stp_valloc.
 * @param ptr pointer to memory to free
 */

void _stp_vfree(void *ptr)
{
	if (likely(ptr))
		vfree(ptr);
}

/** @} */
#endif /* _ALLOC_C_ */
