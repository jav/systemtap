#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/mutex.h>

// When handling memcpy() syscall tracing to notice memory map
// changes, we need to cache memcpy() entry parameter values for
// processing at memcpy() exit.

// __stp_tf_vma_mutex protects the hash table.
static DEFINE_MUTEX(__stp_tf_vma_mutex);

#define __STP_TF_HASH_BITS 4
#define __STP_TF_TABLE_SIZE (1 << __STP_TF_HASH_BITS)

#ifndef TASK_FINDER_VMA_ENTRY_ITEMS
#define TASK_FINDER_VMA_ENTRY_ITEMS 100
#endif

struct __stp_tf_vma_entry {
	struct hlist_node hlist;

	pid_t pid;
	unsigned long addr;
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long vm_pgoff;
	// Is that enough?  Should we store a dcookie for vm_file?
};

static struct __stp_tf_vma_entry
__stp_tf_vma_free_list_items[TASK_FINDER_VMA_ENTRY_ITEMS];

static struct hlist_head __stp_tf_vma_free_list[1];

static struct hlist_head __stp_tf_vma_table[__STP_TF_TABLE_SIZE];

static struct hlist_head __stp_tf_vma_map[__STP_TF_TABLE_SIZE];

// __stp_tf_vma_initialize():  Initialize the free list.  Grabs the
// mutex.
static void
__stp_tf_vma_initialize(void)
{
	int i;
	struct hlist_head *head = &__stp_tf_vma_free_list[0];

	mutex_lock(&__stp_tf_vma_mutex);
	for (i = 0; i < TASK_FINDER_VMA_ENTRY_ITEMS; i++) {
		hlist_add_head(&__stp_tf_vma_free_list_items[i].hlist, head);
	}
	mutex_unlock(&__stp_tf_vma_mutex);
}


// __stp_tf_vma_get_free_entry(): Returns an entry from the free list
// or NULL.  The __stp_tf_vma_mutex must be locked before calling this
// function.
static struct __stp_tf_vma_entry *
__stp_tf_vma_get_free_entry(void)
{
	struct hlist_head *head = &__stp_tf_vma_free_list[0];
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry = NULL;

	if (hlist_empty(head))
		return NULL;
	hlist_for_each_entry(entry, node, head, hlist) {
		break;
	}
	if (entry != NULL)
		hlist_del(&entry->hlist);
	return entry;
}


// __stp_tf_vma_put_free_entry(): Puts an entry back on the free
// list.  The __stp_tf_vma_mutex must be locked before calling this
// function.
static void
__stp_tf_vma_put_free_entry(struct __stp_tf_vma_entry *entry)
{
	struct hlist_head *head = &__stp_tf_vma_free_list[0];
	hlist_add_head(&entry->hlist, head);
}


// __stp_tf_vma_hash(): Compute the vma hash.
static inline u32
__stp_tf_vma_hash(struct task_struct *tsk, unsigned long addr)
{
#ifdef CONFIG_64BIT
    return (jhash_3words(tsk->pid, (u32)addr, (u32)(addr >> 32), 0)
	    & (__STP_TF_TABLE_SIZE - 1));
#else
    return (jhash_2words(tsk->pid, addr, 0) & (__STP_TF_TABLE_SIZE - 1));
#endif
}


// Get vma_entry if the vma is present in the vma hash table.
// Returns NULL if not present.
static struct __stp_tf_vma_entry *
__stp_tf_get_vma_entry(struct task_struct *tsk, unsigned long addr)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;

	mutex_lock(&__stp_tf_vma_mutex);
	head = &__stp_tf_vma_table[__stp_tf_vma_hash(tsk, addr)];
	hlist_for_each_entry(entry, node, head, hlist) {
		if (tsk->pid == entry->pid
		    && addr == entry->addr) {
			mutex_unlock(&__stp_tf_vma_mutex);
			return entry;
		}
	}
	mutex_unlock(&__stp_tf_vma_mutex);
	return NULL;
}

// Add the vma info to the vma hash table.
static int
__stp_tf_add_vma(struct task_struct *tsk, unsigned long addr,
		 struct vm_area_struct *vma)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;

	mutex_lock(&__stp_tf_vma_mutex);
	head = &__stp_tf_vma_table[__stp_tf_vma_hash(tsk, addr)];
	hlist_for_each_entry(entry, node, head, hlist) {
		if (tsk->pid == entry->pid
		    && addr == entry->addr) {
#ifdef DEBUG_TASK_FINDER_VMA
                  printk(KERN_NOTICE
                         "vma (pid: %d, vm_start: 0x%lx) present?\n",
                         tsk->pid, vma->vm_start);
#endif
                  mutex_unlock(&__stp_tf_vma_mutex);
                  return -EBUSY;	/* Already there */
		}
	}

	// Get an element from the free list.
	entry = __stp_tf_vma_get_free_entry();
	if (!entry) {
		mutex_unlock(&__stp_tf_vma_mutex);
		return -ENOMEM;
	}
	entry->pid = tsk->pid;
	entry->addr = addr;
	entry->vm_start = vma->vm_start;
	entry->vm_end = vma->vm_end;
	entry->vm_pgoff = vma->vm_pgoff;
	hlist_add_head(&entry->hlist, head);
	mutex_unlock(&__stp_tf_vma_mutex);
	return 0;
}

// Remove the vma entry from the vma hash table.
static int
__stp_tf_remove_vma_entry(struct __stp_tf_vma_entry *entry)
{
	struct hlist_head *head;
	struct hlist_node *node;
	int found = 0;

	if (entry != NULL) {
		mutex_lock(&__stp_tf_vma_mutex);
		hlist_del(&entry->hlist);
		__stp_tf_vma_put_free_entry(entry);
		mutex_unlock(&__stp_tf_vma_mutex);
	}
	return 0;
}



// __stp_tf_vma_map_hash(): Compute the vma map hash.
static inline u32
__stp_tf_vma_map_hash(struct task_struct *tsk)
{
    return (jhash_1word(tsk->pid, 0) & (__STP_TF_TABLE_SIZE - 1));
}

// Get vma_entry if the vma is present in the vma map hash table.
// Returns NULL if not present.  The __stp_tf_vma_mutex must be locked
// before calling this function.
static struct __stp_tf_vma_entry *
__stp_tf_get_vma_map_entry_internal(struct task_struct *tsk,
				    unsigned long vm_start)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;

	head = &__stp_tf_vma_map[__stp_tf_vma_map_hash(tsk)];
	hlist_for_each_entry(entry, node, head, hlist) {
		if (tsk->pid == entry->pid
		    && vm_start == entry->addr) {
			mutex_unlock(&__stp_tf_vma_mutex);
			return entry;
		}
	}
	return NULL;
}


// Add the vma info to the vma map hash table.
static int
stap_add_vma_map_info(struct task_struct *tsk, unsigned long vm_start,
			  unsigned long vm_end, unsigned long vm_pgoff)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;

	mutex_lock(&__stp_tf_vma_mutex);
	entry = __stp_tf_get_vma_map_entry_internal(tsk, vm_start);
	if (entry != NULL) {
#if 0
		printk(KERN_NOTICE
		       "vma (pid: %d, vm_start: 0x%lx) present?\n",
		       tsk->pid, entry->vm_start);
#endif
		mutex_unlock(&__stp_tf_vma_mutex);
		return -EBUSY;	/* Already there */
	}

	// Get an element from the free list.
	entry = __stp_tf_vma_get_free_entry();
	if (!entry) {
		mutex_unlock(&__stp_tf_vma_mutex);
		return -ENOMEM;
	}

	// Fill in the info
	entry->pid = tsk->pid;
	//entry->addr = addr; ???
	entry->vm_start = vm_start;
	entry->vm_end = vm_end;
	entry->vm_pgoff = vm_pgoff;

	head = &__stp_tf_vma_map[__stp_tf_vma_map_hash(tsk)];
	hlist_add_head(&entry->hlist, head);
	mutex_unlock(&__stp_tf_vma_mutex);
	return 0;
}


// Remove the vma entry from the vma hash table.
static int
stap_remove_vma_map_info(struct task_struct *tsk, unsigned long vm_start,
			     unsigned long vm_end, unsigned long vm_pgoff)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;

	mutex_lock(&__stp_tf_vma_mutex);
	entry = __stp_tf_get_vma_map_entry_internal(tsk, vm_start);
	if (entry != NULL) {
		hlist_del(&entry->hlist);
		__stp_tf_vma_put_free_entry(entry);
	}
	mutex_unlock(&__stp_tf_vma_mutex);
	return 0;
}

// Finds vma info if the vma is present in the vma map hash table.
// Returns ESRCH if not present.  The __stp_tf_vma_mutex must *not* be
// locked before calling this function.
static int
stap_find_vma_map_info(struct task_struct *tsk, unsigned long vm_addr,
		       unsigned long *vm_start, unsigned long *vm_end,
		       unsigned long *vm_pgoff)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;
	struct __stp_tf_vma_entry *found_entry = NULL;
	int rc = ESRCH;

	mutex_lock(&__stp_tf_vma_mutex);
	head = &__stp_tf_vma_map[__stp_tf_vma_map_hash(tsk)];
	hlist_for_each_entry(entry, node, head, hlist) {
		if (tsk->pid == entry->pid
		    && vm_addr >= entry->vm_start
		    && vm_addr < entry->vm_end) {
			found_entry = entry;
			break;
		}
	}
	if (found_entry != NULL) {
		if (vm_start != NULL)
			*vm_start = found_entry->vm_start;
		if (vm_end != NULL)
			*vm_end = found_entry->vm_end;
		if (vm_pgoff != NULL)
			*vm_pgoff = found_entry->vm_pgoff;
		rc = 0;
	}
	mutex_unlock(&__stp_tf_vma_mutex);
	return rc;
}
