/*
 * linux/include/linux/relayfs_fs.h
 *
 * Copyright (C) 2002, 2003 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999, 2000, 2001, 2002 - Karim Yaghmour (karim@opersys.com)
 *
 * RelayFS definitions and declarations
 */

#ifndef _LINUX_RELAYFS_FS_H
#define _LINUX_RELAYFS_FS_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kref.h>

/*
 * Tracks changes to rchan_buf struct
 */
#define RELAYFS_CHANNEL_VERSION		3

/*
 * Per-cpu relay channel buffer
 */
struct rchan_buf
{
	void *start;			/* start of channel buffer */
	void *data;			/* start of current sub-buffer */
	unsigned offset;		/* current offset into sub-buffer */
	atomic_t subbufs_produced;	/* count of sub-buffers produced */
	atomic_t subbufs_consumed;	/* count of sub-buffers consumed */
	atomic_t unfull;		/* state has gone from full to not */
	struct rchan *chan;		/* associated channel */
	wait_queue_head_t read_wait;	/* reader wait queue */
	struct work_struct wake_readers; /* reader wake-up work struct */
	struct dentry *dentry;		/* channel file dentry */
	struct kref kref;		/* channel buffer refcount */
	struct page **page_array;	/* array of current buffer pages */
	int page_count;			/* number of current buffer pages */
	unsigned *padding;		/* padding counts per sub-buffer */
	unsigned *commit;		/* commit counts per sub-buffer */
	int finalized;			/* buffer has been finalized */
} ____cacheline_aligned;

/*
 * Relay channel data structure
 */
struct rchan
{
	u32 version;			/* the version of this struct */
	unsigned subbuf_size;		/* sub-buffer size */
	unsigned n_subbufs;		/* number of sub-buffers per buffer */
	unsigned alloc_size;		/* total buffer size allocated */
	int overwrite;			/* overwrite buffer when full? */
	struct rchan_callbacks *cb;	/* client callbacks */
	struct kref kref;		/* channel refcount */
	struct rchan_buf *buf[NR_CPUS]; /* per-cpu channel buffers */
};

/*
 * Relayfs inode
 */
struct relayfs_inode_info
{
	struct inode vfs_inode;
	struct rchan_buf *buf;
};

static inline struct relayfs_inode_info *RELAYFS_I(struct inode *inode)
{
	return container_of(inode, struct relayfs_inode_info, vfs_inode);
}

/*
 * Relay channel client callbacks
 */
struct rchan_callbacks
{
	/*
	 * subbuf_start - called on buffer-switch to a new sub-buffer
	 * @buf: the channel buffer containing the new sub-buffer
	 * @subbuf: the start of the new sub-buffer
	 * @prev_subbuf_idx: the previous sub-buffer's index
	 * @prev_subbuf: the start of the previous sub-buffer
	 *
	 * NOTE: subbuf_start will also be invoked when the buffer is
	 *       created, so that the first sub-buffer can be initialized
	 *       if necessary.  In this case, prev_subbuf will be NULL.
	 */
	int (*subbuf_start) (struct rchan_buf *buf,
			     void *subbuf,
			     unsigned prev_subbuf_idx,
			     void *prev_subbuf);

	/*
	 * deliver - deliver a guaranteed full sub-buffer to client
	 * @buf: the channel buffer containing the sub-buffer
	 * @subbuf_idx: the sub-buffer's index
	 * @subbuf: the start of the new sub-buffer
	 *
	 * Only works if relay_commit is also used
	 */
	void (*deliver) (struct rchan_buf *buf,
			 unsigned subbuf_idx,
			 void *subbuf);

	/*
	 * buf_mapped - relayfs buffer mmap notification
	 * @buf: the channel buffer
	 * @filp: relayfs file pointer
	 *
	 * Called when a relayfs file is successfully mmapped
	 */
        void (*buf_mapped)(struct rchan_buf *buf,
			   struct file *filp);

	/*
	 * buf_unmapped - relayfs buffer unmap notification
	 * @buf: the channel buffer
	 * @filp: relayfs file pointer
	 *
	 * Called when a relayfs file is successfully unmapped
	 */
        void (*buf_unmapped)(struct rchan_buf *buf,
			     struct file *filp);

	/*
	 * buf_full - relayfs buffer full notification
	 * @buf: the channel channel buffer
	 * @subbuf_idx: the current sub-buffer's index
	 * @subbuf: the start of the current sub-buffer
	 *
	 * Called when a relayfs buffer becomes full
	 */
        void (*buf_full)(struct rchan_buf *buf,
			 unsigned subbuf_idx,
			 void *subbuf);
};

/*
 * relayfs kernel API, fs/relayfs/relay.c
 */

struct rchan *relay_open(const char *base_filename,
			 struct dentry *parent,
			 unsigned subbuf_size,
			 unsigned n_subbufs,
			 int overwrite,
			 struct rchan_callbacks *cb);
extern void relay_close(struct rchan *chan);
extern void relay_flush(struct rchan *chan);
extern void relay_subbufs_consumed(struct rchan *chan,
				   int cpu,
				   int subbufs_consumed);
extern void relay_reset(struct rchan *chan);
extern unsigned relay_switch_subbuf(struct rchan_buf *buf,
				    unsigned length);
extern void relay_commit(struct rchan_buf *buf,
			 void *reserved,
			 unsigned count);
extern struct dentry *relayfs_create_dir(const char *name,
					 struct dentry *parent);
extern int relayfs_remove_dir(struct dentry *dentry);

/**
 *	relay_write - write data into the channel
 *	@chan: relay channel
 *	@data: data to be written
 *	@length: number of bytes to write
 *
 *	Writes data into the current cpu's channel buffer.
 *
 *	Protects the buffer by disabling interrupts.  Use this
 *	if you might be logging from interrupt context.  Try
 *	__relay_write() if you know you	won't be logging from
 *	interrupt context.
 */
static inline void relay_write(struct rchan *chan,
			       const void *data,
			       unsigned length)
{
	unsigned long flags;
	struct rchan_buf *buf;

	local_irq_save(flags);
	buf = chan->buf[smp_processor_id()];
	if (unlikely(buf->offset + length > chan->subbuf_size))
		length = relay_switch_subbuf(buf, length);
	memcpy(buf->data + buf->offset, data, length);
	buf->offset += length;
	local_irq_restore(flags);
}

/**
 *	__relay_write - write data into the channel
 *	@chan: relay channel
 *	@data: data to be written
 *	@length: number of bytes to write
 *
 *	Writes data into the current cpu's channel buffer.
 *
 *	Protects the buffer by disabling preemption.  Use
 *	relay_write() if you might be logging from interrupt
 *	context.
 */
static inline void __relay_write(struct rchan *chan,
				 const void *data,
				 unsigned length)
{
	struct rchan_buf *buf;

	buf = chan->buf[get_cpu()];
	if (unlikely(buf->offset + length > buf->chan->subbuf_size))
		length = relay_switch_subbuf(buf, length);
	memcpy(buf->data + buf->offset, data, length);
	buf->offset += length;
	put_cpu();
}

/**
 *	relay_reserve - reserve slot in channel buffer
 *	@chan: relay channel
 *	@length: number of bytes to reserve
 *
 *	Returns pointer to reserved slot, NULL if full.
 *
 *	Reserves a slot in the current cpu's channel buffer.
 *	Does not protect the buffer at all - caller must provide
 *	appropriate synchronization.
 */
static inline void *relay_reserve(struct rchan *chan, unsigned length)
{
	void *reserved;
	struct rchan_buf *buf = chan->buf[smp_processor_id()];

	if (unlikely(buf->offset + length > buf->chan->subbuf_size)) {
		length = relay_switch_subbuf(buf, length);
		if (!length)
			return NULL;
	}
	reserved = buf->data + buf->offset;
	buf->offset += length;

	return reserved;
}

/*
 * exported relayfs file operations, fs/relayfs/inode.c
 */

extern struct file_operations relayfs_file_operations;
extern int relayfs_open(struct inode *inode, struct file *filp);
extern unsigned int relayfs_poll(struct file *filp, poll_table *wait);
extern int relayfs_mmap(struct file *filp, struct vm_area_struct *vma);
extern int relayfs_release(struct inode *inode, struct file *filp);

#endif /* _LINUX_RELAYFS_FS_H */

