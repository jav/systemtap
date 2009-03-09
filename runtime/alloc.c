/* -*- linux-c -*- 
 * Memory allocation functions
 * Copyright (C) 2005-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _ALLOC_C_
#define _ALLOC_C_

static int _stp_allocated_net_memory = 0;
#define STP_ALLOC_FLAGS (GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN)

//#define DEBUG_MEM
/*
 * If DEBUG_MEM is defined (stap -DDEBUG_MEM ...) then full memory
 * tracking is used. Each allocation is recorded and matched with 
 * a free. Also a fence is set around the allocated memory so overflows
 * and underflows can be detected. Errors are written to the system log
 * with printk.
 *
 * NOTE: if youy system is slow or your script makes a very large number
 * of allocations, you may get a warning in the system log:
 * BUG: soft lockup - CPU#1 stuck for 11s! [staprun:28269]
 * This is an expected side-effect of the overhead of tracking, especially
 * with a simple linked list of allocations. Optimization
 * would be nice, but DEBUG_MEM is only for testing.
 */

#ifdef DEBUG_MEM

static DEFINE_SPINLOCK(_stp_mem_lock);
static int _stp_allocated_memory = 0;

#define MEM_MAGIC 0xc11cf77f
#define MEM_FENCE_SIZE 32

enum _stp_memtype { MEM_KMALLOC, MEM_VMALLOC, MEM_PERCPU };

typedef struct {
	char *alloc;
	char *free;
} _stp_malloc_type;

static const _stp_malloc_type const _stp_malloc_types[] = {
	{"kmalloc", "kfree"},
	{"vmalloc", "vfree"},
	{"alloc_percpu", "free_percpu"}
};

struct _stp_mem_entry {
	struct list_head list;
	int32_t magic;
	enum _stp_memtype type;
	size_t len;
	void *addr;
};

#define MEM_DEBUG_SIZE (2*MEM_FENCE_SIZE+sizeof(struct _stp_mem_entry))

static LIST_HEAD(_stp_mem_list);

static void _stp_check_mem_fence (char *addr, int size)
{
	char *ptr;
	int i;

	ptr = addr - MEM_FENCE_SIZE;
	while (ptr < addr) {
		if (*ptr != 0x55) {
			printk("SYSTEMTAP ERROR: Memory fence corrupted before allocated memory\n");
			printk("at addr %p. (Allocation starts at %p)", ptr, addr);
			return;
		}
		ptr++;
	}
	ptr = addr + size;
	while (ptr < addr + size + MEM_FENCE_SIZE) {
		if (*ptr != 0x55) {
			printk("SYSTEMTAP ERROR: Memory fence corrupted after allocated memory\n");
			printk("at addr %p. (Allocation ends at %p)", ptr, addr + size - 1);
			return;
		}
		ptr++;
	}
}

static void *_stp_mem_debug_setup(void *addr, size_t size, enum _stp_memtype type)
{
	struct list_head *p;
	struct _stp_mem_entry *m;
	memset(addr, 0x55, MEM_FENCE_SIZE);
	addr += MEM_FENCE_SIZE;
	memset(addr + size, 0x55, MEM_FENCE_SIZE);
	p = (struct list_head *)(addr + size + MEM_FENCE_SIZE);
	m = (struct _stp_mem_entry *)p;
	m->magic = MEM_MAGIC;
	m->type = type;
	m->len = size;
	m->addr = addr;
	spin_lock(&_stp_mem_lock);
	list_add(p, &_stp_mem_list); 
	spin_unlock(&_stp_mem_lock);
	return addr;
}

/* Percpu allocations don't have the fence. Implementing it is problematic. */
static void _stp_mem_debug_percpu(struct _stp_mem_entry *m, void *addr, size_t size)
{
	struct list_head *p = (struct list_head *)m;
	m->magic = MEM_MAGIC;
	m->type = MEM_PERCPU;
	m->len = size;
	m->addr = addr;
	spin_lock(&_stp_mem_lock);
	list_add(p, &_stp_mem_list);
	spin_unlock(&_stp_mem_lock);	
}

static void _stp_mem_debug_free(void *addr, enum _stp_memtype type)
{
	int found = 0;
	struct list_head *p, *tmp;
	struct _stp_mem_entry *m = NULL;

	spin_lock(&_stp_mem_lock);
	list_for_each_safe(p, tmp, &_stp_mem_list) {
		m = list_entry(p, struct _stp_mem_entry, list);
		if (m->addr == addr) {
			list_del(p);
			found = 1;
			break;
		}
	}
	spin_unlock(&_stp_mem_lock);
	if (!found) {
		printk("SYSTEMTAP ERROR: Free of unallocated memory %p type=%s\n", 
		       addr, _stp_malloc_types[type].free);
		return;
	}
	if (m->magic != MEM_MAGIC) {
		printk("SYSTEMTAP ERROR: Memory at %p corrupted!!\n", addr);
		return;
	}
	if (m->type != type) {
		printk("SYSTEMTAP ERROR: Memory allocated with %s and freed with %s\n",
		       _stp_malloc_types[m->type].alloc, 		       
		       _stp_malloc_types[type].free);
	}
	
	switch (m->type) {
	case MEM_KMALLOC:
		_stp_check_mem_fence(addr, m->len);
		kfree(addr - MEM_FENCE_SIZE);
		break;
	case MEM_PERCPU:
		free_percpu(addr);
		kfree(p);
		break;
	case MEM_VMALLOC:
		_stp_check_mem_fence(addr, m->len);
		vfree(addr - MEM_FENCE_SIZE);		
		break;
	default:
		printk("SYSTEMTAP ERROR: Attempted to free memory at addr %p len=%d with unknown allocation type.\n", addr, (int)m->len);
	}

	return;
}
#endif

static void *_stp_kmalloc(size_t size)
{
#ifdef DEBUG_MEM
	void *ret = kmalloc(size + MEM_DEBUG_SIZE, STP_ALLOC_FLAGS);
	if (likely(ret)) {
		ret = _stp_mem_debug_setup(ret, size, MEM_KMALLOC);
		_stp_allocated_memory += size;
	}
	return ret;
#else
	return kmalloc(size, STP_ALLOC_FLAGS);
#endif
}

static void *_stp_kzalloc(size_t size)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
{
#ifdef DEBUG_MEM
	void *ret = kmalloc(size + MEM_DEBUG_SIZE, STP_ALLOC_FLAGS);
	if (likely(ret)) {
		ret = _stp_mem_debug_setup(ret, size, MEM_KMALLOC);
		memset (ret, 0, size);
		_stp_allocated_memory += size;
	}
#else
	void *ret = kmalloc(size, STP_ALLOC_FLAGS);
	if (likely(ret))
		memset (ret, 0, size);
#endif /* DEBUG_MEM */
	return ret;
}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) */
{
#ifdef DEBUG_MEM
	void *ret = kzalloc(size + MEM_DEBUG_SIZE, STP_ALLOC_FLAGS);
	if (likely(ret)) {
		ret = _stp_mem_debug_setup(ret, size, MEM_KMALLOC);
		_stp_allocated_memory += size;
	}
	return ret;
#else
	return kzalloc(size, STP_ALLOC_FLAGS);
#endif
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15) */

static void *_stp_vmalloc(unsigned long size)
{
#ifdef DEBUG_MEM
	void *ret = __vmalloc(size + MEM_DEBUG_SIZE, STP_ALLOC_FLAGS, PAGE_KERNEL);
	if (likely(ret)) {
		ret = _stp_mem_debug_setup(ret, size, MEM_VMALLOC);
		_stp_allocated_memory += size;
	}
	return ret;
#else
	return __vmalloc(size, STP_ALLOC_FLAGS, PAGE_KERNEL);
#endif

}

static void *_stp_alloc_percpu(size_t size)
{
#ifdef STAPCONF_ALLOC_PERCPU_ALIGN
	void *ret = __alloc_percpu(size, 8);
#else
	void *ret = __alloc_percpu(size);
#endif
#ifdef DEBUG_MEM
	if (likely(ret)) {
		struct _stp_mem_entry *m = kmalloc(sizeof(struct _stp_mem_entry), STP_ALLOC_FLAGS);
		if (unlikely(m == NULL)) {
			free_percpu(ret);
			return NULL;
		}
		_stp_mem_debug_percpu(m, ret, size);
		_stp_allocated_memory += size * num_online_cpus();
	}
#endif
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
#define _stp_kmalloc_node(size,node) _stp_kmalloc(size)
#else
static void *_stp_kmalloc_node(size_t size, int node)
{
#ifdef DEBUG_MEM
	void *ret = kmalloc_node(size + MEM_DEBUG_SIZE, STP_ALLOC_FLAGS, node);
	if (likely(ret)) {
		ret = _stp_mem_debug_setup(ret, size, MEM_KMALLOC);
		_stp_allocated_memory += size;
	}
	return ret;
#else
	return kmalloc_node(size, STP_ALLOC_FLAGS, node);
#endif
}
#endif /* LINUX_VERSION_CODE */

static void _stp_kfree(void *addr)
{
#ifdef DEBUG_MEM
	_stp_mem_debug_free(addr, MEM_KMALLOC);
#else
	kfree(addr);
#endif
}

static void _stp_vfree(void *addr)
{
#ifdef DEBUG_MEM
	_stp_mem_debug_free(addr, MEM_VMALLOC);
#else
	vfree(addr);
#endif
}

static void _stp_free_percpu(void *addr)
{
#ifdef DEBUG_MEM
	_stp_mem_debug_free(addr, MEM_PERCPU);
#else
	free_percpu(addr);
#endif
}

static void _stp_mem_debug_done(void)
{
#ifdef DEBUG_MEM
	struct list_head *p, *tmp;
	struct _stp_mem_entry *m;

	spin_lock(&_stp_mem_lock);
	list_for_each_safe(p, tmp, &_stp_mem_list) {
		m = list_entry(p, struct _stp_mem_entry, list);
		list_del(p);

		printk("SYSTEMTAP ERROR: Memory %p len=%d allocation type: %s. Not freed.\n", 
		       m->addr, (int)m->len, _stp_malloc_types[m->type].alloc);

		if (m->magic != MEM_MAGIC) {
			printk("SYSTEMTAP ERROR: Memory at %p len=%d corrupted!!\n", m->addr, (int)m->len);
			/* Don't free. Too dangerous */
			goto done;
		}

		switch (m->type) {
		case MEM_KMALLOC:
			_stp_check_mem_fence(m->addr, m->len);
			kfree(m->addr - MEM_FENCE_SIZE);
			break;
		case MEM_PERCPU:
			free_percpu(m->addr);
			kfree(p);
			break;
		case MEM_VMALLOC:
			_stp_check_mem_fence(m->addr, m->len);
			vfree(m->addr - MEM_FENCE_SIZE);		
			break;
		default:
			printk("SYSTEMTAP ERROR: Attempted to free memory at addr %p len=%d with unknown allocation type.\n", m->addr, (int)m->len);
		}
	}
done:
	spin_unlock(&_stp_mem_lock);

	return;

#endif
}
#endif /* _ALLOC_C_ */
