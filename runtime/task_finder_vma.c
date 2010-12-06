#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/spinlock.h>

#include <linux/fs.h>
#include <linux/dcache.h>

// __stp_tf_vma_lock protects the hash table.
// Documentation/spinlocks.txt suggest we can be a bit more clever
// if we guarantee that in interrupt context we only read, not write
// the datastructures. We should never change the hash table or the
// contents in interrupt context (which should only ever call 
// stap_find_vma_map_info for getting stored vma info). So we might
// want to look into that if this seems a bottleneck.
#ifdef CONFIG_PREEMPT_RT
static DEFINE_RAW_RWLOCK(__stp_tf_vma_lock);
#else
static DEFINE_RWLOCK(__stp_tf_vma_lock);
#endif

#define __STP_TF_HASH_BITS 4
#define __STP_TF_TABLE_SIZE (1 << __STP_TF_HASH_BITS)

// Somewhat arbitrary default, this is often way too much for tracking
// single process, but often too little when tracking whole system.
// FIXME Would be nice to make this dynamic. PR11671
#ifndef TASK_FINDER_VMA_ENTRY_ITEMS
#define TASK_FINDER_VMA_ENTRY_ITEMS 1536
#endif

#ifndef TASK_FINDER_VMA_ENTRY_PATHLEN
#define TASK_FINDER_VMA_ENTRY_PATHLEN 64
#elif TASK_FINDER_VMA_ENTRY_PATHLEN < 8
#error "gimme a little more TASK_FINDER_VMA_ENTRY_PATHLEN"
#endif


struct __stp_tf_vma_entry {
	struct hlist_node hlist;

	pid_t pid;
	unsigned long vm_start;
	unsigned long vm_end;
        char path[TASK_FINDER_VMA_ENTRY_PATHLEN]; /* mmpath name, if known */

	// User data (possibly stp_module)
	void *user;
};

static struct __stp_tf_vma_entry
__stp_tf_vma_free_list_items[TASK_FINDER_VMA_ENTRY_ITEMS];

static struct hlist_head __stp_tf_vma_free_list[1];

static struct hlist_head __stp_tf_vma_map[__STP_TF_TABLE_SIZE];

// stap_initialize_vma_map():  Initialize the free list.  Grabs the
// spinlock.  Should be called before any of the other stap_*_vma_map
// functions.
static void
stap_initialize_vma_map(void)
{
	int i;
	struct hlist_head *head = &__stp_tf_vma_free_list[0];

	unsigned long flags;
	write_lock_irqsave(&__stp_tf_vma_lock, flags);
	for (i = 0; i < TASK_FINDER_VMA_ENTRY_ITEMS; i++) {
		hlist_add_head(&__stp_tf_vma_free_list_items[i].hlist, head);
	}
	write_unlock_irqrestore(&__stp_tf_vma_lock, flags);
}


// __stp_tf_vma_get_free_entry(): Returns an entry from the free list
// or NULL.  The __stp_tf_vma_lock must be write locked before calling this
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
// list.  The __stp_tf_vma_lock must be write locked before calling this
// function.
static void
__stp_tf_vma_put_free_entry(struct __stp_tf_vma_entry *entry)
{
	struct hlist_head *head = &__stp_tf_vma_free_list[0];
	hlist_add_head(&entry->hlist, head);
}

// __stp_tf_vma_map_hash(): Compute the vma map hash.
static inline u32
__stp_tf_vma_map_hash(struct task_struct *tsk)
{
    return (jhash_1word(tsk->pid, 0) & (__STP_TF_TABLE_SIZE - 1));
}

// Get vma_entry if the vma is present in the vma map hash table.
// Returns NULL if not present.  The __stp_tf_vma_lock must be read locked
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
		    && vm_start == entry->vm_start) {
			return entry;
		}
	}
	return NULL;
}


// Add the vma info to the vma map hash table.
// Caller is responsible for name lifetime.
static int
stap_add_vma_map_info(struct task_struct *tsk,
		      unsigned long vm_start, unsigned long vm_end,
		      const char *path, void *user)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;

	unsigned long flags;
	// Take a write lock, since we are most likely going to write
	// after reading.
	write_lock_irqsave(&__stp_tf_vma_lock, flags);
	entry = __stp_tf_get_vma_map_entry_internal(tsk, vm_start);
	if (entry != NULL) {
#if 0
		printk(KERN_NOTICE
		       "vma (pid: %d, vm_start: 0x%lx) present?\n",
		       tsk->pid, entry->vm_start);
#endif
		write_unlock_irqrestore(&__stp_tf_vma_lock, flags);
		return -EBUSY;	/* Already there */
	}

	// Get an element from the free list.
	entry = __stp_tf_vma_get_free_entry();
	if (!entry) {
		write_unlock_irqrestore(&__stp_tf_vma_lock, flags);
		return -ENOMEM;
	}

	// Fill in the info
	entry->pid = tsk->pid;
	entry->vm_start = vm_start;
	entry->vm_end = vm_end;
        if (strlen(path) >= TASK_FINDER_VMA_ENTRY_PATHLEN-3)
          {
            strncpy (entry->path, "...", TASK_FINDER_VMA_ENTRY_PATHLEN);
            strlcpy (entry->path+3, &path[strlen(path)-TASK_FINDER_VMA_ENTRY_PATHLEN+4],
                     TASK_FINDER_VMA_ENTRY_PATHLEN-3);
          }
        else
          {
            strlcpy (entry->path, path, TASK_FINDER_VMA_ENTRY_PATHLEN);
          }
	entry->user = user;

	head = &__stp_tf_vma_map[__stp_tf_vma_map_hash(tsk)];
	hlist_add_head(&entry->hlist, head);
	write_unlock_irqrestore(&__stp_tf_vma_lock, flags);
	return 0;
}


// Remove the vma entry from the vma hash table.
// Returns -ESRCH if the entry isn't present.
static int
stap_remove_vma_map_info(struct task_struct *tsk, unsigned long vm_start)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;
	int rc = -ESRCH;

	// Take a write lock since we are most likely going to delete
	// after reading.
	unsigned long flags;
	write_lock_irqsave(&__stp_tf_vma_lock, flags);
	entry = __stp_tf_get_vma_map_entry_internal(tsk, vm_start);
	if (entry != NULL) {
		hlist_del(&entry->hlist);
		__stp_tf_vma_put_free_entry(entry);
                rc = 0;
	}
	write_unlock_irqrestore(&__stp_tf_vma_lock, flags);
	return rc;
}

// Finds vma info if the vma is present in the vma map hash table for
// a given task and address (between vm_start and vm_end).
// Returns -ESRCH if not present.  The __stp_tf_vma_lock must *not* be
// locked before calling this function.
static int
stap_find_vma_map_info(struct task_struct *tsk, unsigned long addr,
		       unsigned long *vm_start, unsigned long *vm_end,
		       const char **path, void **user)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;
	struct __stp_tf_vma_entry *found_entry = NULL;
	int rc = -ESRCH;

	unsigned long flags;
	read_lock_irqsave(&__stp_tf_vma_lock, flags);
	head = &__stp_tf_vma_map[__stp_tf_vma_map_hash(tsk)];
	hlist_for_each_entry(entry, node, head, hlist) {
		if (tsk->pid == entry->pid
		    && addr >= entry->vm_start
		    && addr < entry->vm_end) {
			found_entry = entry;
			break;
		}
	}
	if (found_entry != NULL) {
		if (vm_start != NULL)
			*vm_start = found_entry->vm_start;
		if (vm_end != NULL)
			*vm_end = found_entry->vm_end;
		if (path != NULL)
			*path = found_entry->path;
		if (user != NULL)
			*user = found_entry->user;
		rc = 0;
	}
	read_unlock_irqrestore(&__stp_tf_vma_lock, flags);
	return rc;
}

// Finds vma info if the vma is present in the vma map hash table for
// a given task with the given user handle.
// Returns -ESRCH if not present.  The __stp_tf_vma_lock must *not* be
// locked before calling this function.
static int
stap_find_vma_map_info_user(struct task_struct *tsk, void *user,
			    unsigned long *vm_start, unsigned long *vm_end,
			    const char **path)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct __stp_tf_vma_entry *entry;
	struct __stp_tf_vma_entry *found_entry = NULL;
	int rc = -ESRCH;

	unsigned long flags;
	read_lock_irqsave(&__stp_tf_vma_lock, flags);
	head = &__stp_tf_vma_map[__stp_tf_vma_map_hash(tsk)];
	hlist_for_each_entry(entry, node, head, hlist) {
		if (tsk->pid == entry->pid
		    && user == entry->user) {
			found_entry = entry;
			break;
		}
	}
	if (found_entry != NULL) {
		if (vm_start != NULL)
			*vm_start = found_entry->vm_start;
		if (vm_end != NULL)
			*vm_end = found_entry->vm_end;
		if (path != NULL)
			*path = found_entry->path;
		rc = 0;
	}
	read_unlock_irqrestore(&__stp_tf_vma_lock, flags);
	return rc;
}

static int
stap_drop_vma_maps(struct task_struct *tsk)
{
	struct hlist_head *head;
	struct hlist_node *node;
	struct hlist_node *n;
	struct __stp_tf_vma_entry *entry;

	unsigned long flags;
	write_lock_irqsave(&__stp_tf_vma_lock, flags);
	head = &__stp_tf_vma_map[__stp_tf_vma_map_hash(tsk)];
        hlist_for_each_entry_safe(entry, node, n, head, hlist) {
            if (tsk->pid == entry->pid) {
		    hlist_del(&entry->hlist);
		    __stp_tf_vma_put_free_entry(entry);
            }
        }
	write_unlock_irqrestore(&__stp_tf_vma_lock, flags);
	return 0;
}
