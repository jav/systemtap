#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/mutex.h>

// __stp_tf_vma_mutex protects the hash table.
static DEFINE_MUTEX(__stp_tf_vma_mutex);

#define __STP_TF_HASH_BITS 4
#define __STP_TF_TABLE_SIZE (1 << __STP_TF_HASH_BITS)

struct __stp_tf_vma_entry {
	struct hlist_node hlist;

	pid_t pid;
	unsigned long addr;
	unsigned long vm_start;
	unsigned long vm_end;
	unsigned long vm_pgoff;
	// Is that enough?  Should we store a dcookie for vm_file?
};

static struct hlist_head __stp_tf_vma_table[__STP_TF_TABLE_SIZE];

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
			printk(KERN_NOTICE
			       "vma (pid: %d, vm_start: 0x%lx) present?\n",
			       tsk->pid, vma->vm_start);
			mutex_unlock(&__stp_tf_vma_mutex);
			return -EBUSY;	/* Already there */
		}
	}

	// Using kmalloc here to allocate an element. Could cause some
	// memory fragmentation if overused.
	entry = kmalloc(sizeof(struct __stp_tf_vma_entry), GFP_KERNEL);
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
		kfree(entry);
		mutex_unlock(&__stp_tf_vma_mutex);
	}
	return 0;
}
