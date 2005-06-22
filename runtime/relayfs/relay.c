/*
 * Public API and common code for RelayFS.
 *
 * See Documentation/filesystems/relayfs.txt for an overview of relayfs.
 *
 * Copyright (C) 2002-2005 - Tom Zanussi (zanussi@us.ibm.com), IBM Corp
 * Copyright (C) 1999-2005 - Karim Yaghmour (karim@opersys.com)
 *
 * This file is released under the GPL.
 */

#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/string.h>
#include "linux/relayfs_fs.h"
#include "relay.h"
#include "buffers.h"

/**
 *	relay_buf_empty - boolean, is the channel buffer empty?
 *	@buf: channel buffer
 *
 *	Returns 1 if the buffer is empty, 0 otherwise.
 */
int relay_buf_empty(struct rchan_buf *buf)
{
	int produced = atomic_read(&buf->subbufs_produced);
	int consumed = atomic_read(&buf->subbufs_consumed);

	return (produced - consumed) ? 0 : 1;
}

/**
 *	relay_buf_full - boolean, is the channel buffer full?
 *	@buf: channel buffer
 *
 *	Returns 1 if the buffer is full, 0 otherwise.
 */
static inline int relay_buf_full(struct rchan_buf *buf)
{
	int produced, consumed;

	if (buf->chan->overwrite)
		return 0;

	produced = atomic_read(&buf->subbufs_produced);
	consumed = atomic_read(&buf->subbufs_consumed);

	return (produced - consumed > buf->chan->n_subbufs - 1) ? 1 : 0;
}

/*
 * High-level relayfs kernel API and associated functions.
 */

/*
 * rchan_callback implementations defining default channel behavior.  Used
 * in place of corresponding NULL values in client callback struct.
 */

/*
 * subbuf_start() default callback.  Does nothing.
 */
static int subbuf_start_default_callback (struct rchan_buf *buf,
					  void *subbuf,
					  unsigned prev_subbuf_idx,
					  void *prev_subbuf)
{
	return 0;
}

/*
 * deliver() default callback.  Does nothing.
 */
static void deliver_default_callback (struct rchan_buf *buf,
				      unsigned subbuf_idx,
				      void *subbuf)
{
}

/*
 * buf_mapped() default callback.  Does nothing.
 */
static void buf_mapped_default_callback(struct rchan_buf *buf,
					struct file *filp)
{
}

/*
 * buf_unmapped() default callback.  Does nothing.
 */
static void buf_unmapped_default_callback(struct rchan_buf *buf,
					  struct file *filp)
{
}

/*
 * buf_full() default callback.  Does nothing.
 */
static void buf_full_default_callback(struct rchan_buf *buf,
				      unsigned subbuf_idx,
				      void *subbuf)
{
}

/* relay channel default callbacks */
static struct rchan_callbacks default_channel_callbacks = {
	.subbuf_start = subbuf_start_default_callback,
	.deliver = deliver_default_callback,
	.buf_mapped = buf_mapped_default_callback,
	.buf_unmapped = buf_unmapped_default_callback,
	.buf_full = buf_full_default_callback,
};

/**
 *	wakeup_readers - wake up readers waiting on a channel
 *	@private: the channel buffer
 *
 *	This is the work function used to defer reader waking.  The
 *	reason waking is deferred is that calling directly from write
 *	causes problems if you're writing from say the scheduler.
 */
static void wakeup_readers(void *private)
{
	struct rchan_buf *buf = private;
	wake_up_interruptible(&buf->read_wait);
}

/**
 *	get_next_subbuf - return next sub-buffer within channel buffer
 *	@buf: channel buffer
 */
static inline void *get_next_subbuf(struct rchan_buf *buf)
{
	void *next = buf->data + buf->chan->subbuf_size;
	if (next >= buf->start + buf->chan->subbuf_size * buf->chan->n_subbufs)
		next = buf->start;

	return next;
}

/**
 *	__relay_reset - reset a channel buffer
 *	@buf: the channel buffer
 *	@init: 1 if this is a first-time initialization
 *
 *	See relay_reset for description of effect.
 */
static inline void __relay_reset(struct rchan_buf *buf, int init)
{
	int i;

	if (init) {
		init_waitqueue_head(&buf->read_wait);
		kref_init(&buf->kref);
		INIT_WORK(&buf->wake_readers, NULL, NULL);
	} else {
		cancel_delayed_work(&buf->wake_readers);
		flush_scheduled_work();
	}

	atomic_set(&buf->subbufs_produced, 0);
	atomic_set(&buf->subbufs_consumed, 0);
	atomic_set(&buf->unfull, 0);
	buf->finalized = 0;
	buf->data = buf->start;
	buf->offset = 0;

	for (i = 0; i < buf->chan->n_subbufs; i++) {
		buf->padding[i] = 0;
		buf->commit[i] = 0;
	}

	buf->offset = buf->chan->cb->subbuf_start(buf, buf->data, 0, NULL);
	buf->commit[0] = buf->offset;
}

/**
 *	relay_reset - reset the channel
 *	@chan: the channel
 *
 *	Returns 0 if successful, negative if not.
 *
 *	This has the effect of erasing all data from all channel buffers
 *	and restarting the channel in its initial state.  The buffers
 *	are not freed, so any mappings are still in effect.
 *
 *	NOTE: Care should be taken that the channel isn't actually
 *	being used by anything when this call is made.
 */
void relay_reset(struct rchan *chan)
{
	int i;

	if (!chan)
		return;

	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i])
			continue;
		__relay_reset(chan->buf[i], 0);
	}
}

/**
 *	relay_open_buf - create a new channel buffer in relayfs
 *
 *	Internal - used by relay_open().
 */
static struct rchan_buf *relay_open_buf(struct rchan *chan,
					const char *filename,
					struct dentry *parent)
{
	struct rchan_buf *buf;
	struct dentry *dentry;

	/* Create file in fs */
	dentry = relayfs_create_file(filename, parent, S_IRUSR, chan);
	if (!dentry)
		return NULL;

	buf = RELAYFS_I(dentry->d_inode)->buf;
	buf->dentry = dentry;
	__relay_reset(buf, 1);

	return buf;
}

/**
 *	relay_close_buf - close a channel buffer
 *	@buf: channel buffer
 *
 *	Marks the buffer finalized and restores the default callbacks.
 *	The channel buffer and channel buffer data structure are then freed
 *	automatically when the last reference is given up.
 */
static inline void relay_close_buf(struct rchan_buf *buf)
{
	buf->finalized = 1;
	buf->chan->cb = &default_channel_callbacks;
	cancel_delayed_work(&buf->wake_readers);
	flush_scheduled_work();
	kref_put(&buf->kref, relay_remove_buf);
}

static inline void setup_callbacks(struct rchan *chan,
				   struct rchan_callbacks *cb)
{
	if (!cb) {
		chan->cb = &default_channel_callbacks;
		return;
	}

	if (!cb->subbuf_start)
		cb->subbuf_start = subbuf_start_default_callback;
	if (!cb->deliver)
		cb->deliver = deliver_default_callback;
	if (!cb->buf_mapped)
		cb->buf_mapped = buf_mapped_default_callback;
	if (!cb->buf_unmapped)
		cb->buf_unmapped = buf_unmapped_default_callback;
	if (!cb->buf_full)
		cb->buf_full = buf_full_default_callback;
	chan->cb = cb;
}

/**
 *	relay_open - create a new relayfs channel
 *	@base_filename: base name of files to create
 *	@parent: dentry of parent directory, NULL for root directory
 *	@subbuf_size: size of sub-buffers
 *	@n_subbufs: number of sub-buffers
 *	@overwrite: overwrite buffer when full?
 *	@cb: client callback functions
 *
 *	Returns channel pointer if successful, NULL otherwise.
 *
 *	Creates a channel buffer for each cpu using the sizes and
 *	attributes specified.  The created channel buffer files
 *	will be named base_filename0...base_filenameN-1.  File
 *	permissions will be S_IRUSR.
 */
struct rchan *relay_open(const char *base_filename,
			 struct dentry *parent,
			 unsigned subbuf_size,
			 unsigned n_subbufs,
			 int overwrite,
			 struct rchan_callbacks *cb)
{
	int i;
	struct rchan *chan;
	char *tmpname;

	if (!base_filename)
		return NULL;

	if (!(subbuf_size && n_subbufs))
		return NULL;

	chan = kcalloc(1, sizeof(struct rchan), GFP_KERNEL);
	if (!chan)
		return NULL;

	chan->version = RELAYFS_CHANNEL_VERSION;
	chan->overwrite = overwrite;
	chan->n_subbufs = n_subbufs;
	chan->subbuf_size = subbuf_size;
	chan->alloc_size = FIX_SIZE(subbuf_size * n_subbufs);
	setup_callbacks(chan, cb);
	kref_init(&chan->kref);

	tmpname = kmalloc(NAME_MAX + 1, GFP_KERNEL);
	if (!tmpname)
		goto free_chan;

	for_each_online_cpu(i) {
		sprintf(tmpname, "%s%d", base_filename, i);
		chan->buf[i] = relay_open_buf(chan, tmpname, parent);
		if (!chan->buf[i])
			goto free_bufs;
	}

	kfree(tmpname);
	return chan;

free_bufs:
	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i])
			break;
		relay_close_buf(chan->buf[i]);
	}
	kfree(tmpname);

free_chan:
	kref_put(&chan->kref, relay_destroy_channel);
	return NULL;
}

/**
 *	deliver_check - deliver a guaranteed full sub-buffer if applicable
 */
static inline void deliver_check(struct rchan_buf *buf,
				 unsigned subbuf_idx)
{
	void *subbuf;
	unsigned full = buf->chan->subbuf_size - buf->padding[subbuf_idx];

	if (buf->commit[subbuf_idx] == full) {
		subbuf = buf->start + subbuf_idx * buf->chan->subbuf_size;
		buf->chan->cb->deliver(buf, subbuf_idx, subbuf);
	}
}

/**
 *	do_switch - change subbuf pointer and do related bookkeeping
 */
static inline void do_switch(struct rchan_buf *buf, unsigned new, unsigned old)
{
	unsigned start = 0;
	void *old_data = buf->start + old * buf->chan->subbuf_size;

	buf->data = get_next_subbuf(buf);
	buf->padding[new] = 0;
	start = buf->chan->cb->subbuf_start(buf, buf->data, old, old_data);
	buf->offset = buf->commit[new] = start;
}

/**
 *	relay_switch_subbuf - switch to a new sub-buffer
 *	@buf: channel buffer
 *	@length: size of current event
 *
 *	Returns either the length passed in or 0 if full.

 *	Performs sub-buffer-switch tasks such as invoking callbacks,
 *	updating padding counts, waking up readers, etc.
 */
unsigned relay_switch_subbuf(struct rchan_buf *buf, unsigned length)
{
	int new, old, produced = atomic_read(&buf->subbufs_produced);
	unsigned padding;

	if (unlikely(length > buf->chan->subbuf_size))
	  goto toobig;
	
	if (unlikely(atomic_read(&buf->unfull))) {
		atomic_set(&buf->unfull, 0);
		new = produced % buf->chan->n_subbufs;
		old = (produced - 1) % buf->chan->n_subbufs;
		do_switch(buf, new, old);
		return 0;
	}

	if (unlikely(relay_buf_full(buf)))
		return 0;

	old = produced % buf->chan->n_subbufs;
	padding = buf->chan->subbuf_size - buf->offset;
	buf->padding[old] = padding;
	deliver_check(buf, old);
	buf->offset = buf->chan->subbuf_size;
	atomic_inc(&buf->subbufs_produced);

	if (waitqueue_active(&buf->read_wait)) {
		PREPARE_WORK(&buf->wake_readers, wakeup_readers, buf);
		schedule_delayed_work(&buf->wake_readers, 1);
	}

	if (unlikely(relay_buf_full(buf))) {
		void *old_data = buf->start + old * buf->chan->subbuf_size;
		buf->chan->cb->buf_full(buf, old, old_data);
		return 0;
	}

	new = (produced + 1) % buf->chan->n_subbufs;
	do_switch(buf, new, old);

	if (unlikely(length + buf->offset > buf->chan->subbuf_size))
	  goto toobig;

	return length;
	
 toobig:
	printk(KERN_WARNING "relayfs: event too large (%u)\n", length);
	WARN_ON(1);
	return 0;
}

/**
 *	relay_commit - add count bytes to a sub-buffer's commit count
 *	@buf: channel buffer
 *	@reserved: reserved address associated with commit
 *	@count: number of bytes committed
 *
 *	Invokes deliver() callback if sub-buffer is completely written.
 */
void relay_commit(struct rchan_buf *buf,
		  void *reserved,
		  unsigned count)
{
	unsigned offset, subbuf_idx;

	offset = reserved - buf->start;
	subbuf_idx = offset / buf->chan->subbuf_size;
	buf->commit[subbuf_idx] += count;
	deliver_check(buf, subbuf_idx);
}

/**
 *	relay_subbufs_consumed - update the buffer's sub-buffers-consumed count
 *	@chan: the channel
 *	@cpu: the cpu associated with the channel buffer to update
 *	@subbufs_consumed: number of sub-buffers to add to current buf's count
 *
 *	Adds to the channel buffer's consumed sub-buffer count.
 *	subbufs_consumed should be the number of sub-buffers newly consumed,
 *	not the total consumed.
 *
 *	NOTE: kernel clients don't need to call this function if the channel
 *	mode is 'overwrite'.
 */
void relay_subbufs_consumed(struct rchan *chan, int cpu, int subbufs_consumed)
{
	int produced, consumed;
	struct rchan_buf *buf;

	if (!chan)
		return;

	if (cpu >= NR_CPUS || !chan->buf[cpu])
		return;

	buf = chan->buf[cpu];
	if (relay_buf_full(buf))
		atomic_set(&buf->unfull, 1);

	atomic_add(subbufs_consumed, &buf->subbufs_consumed);
	produced = atomic_read(&buf->subbufs_produced);
	consumed = atomic_read(&buf->subbufs_consumed);
	if (consumed > produced)
		atomic_set(&buf->subbufs_consumed, produced);
}

/**
 *	relay_destroy_channel - free the channel struct
 *
 *	Should only be called from kref_put().
 */
void relay_destroy_channel(struct kref *kref)
{
	struct rchan *chan = container_of(kref, struct rchan, kref);
	kfree(chan);
}

/**
 *	relay_close - close the channel
 *	@chan: the channel
 *
 *	Closes all channel buffers and frees the channel.
 */
void relay_close(struct rchan *chan)
{
	int i;

	if (!chan)
		return;

	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i])
			continue;
		relay_close_buf(chan->buf[i]);
	}

	kref_put(&chan->kref, relay_destroy_channel);
}

/**
 *	relay_flush - close the channel
 *	@chan: the channel
 *
 *	Flushes all channel buffers i.e. forces buffer switch.
 */
void relay_flush(struct rchan *chan)
{
	int i;

	if (!chan)
		return;

	for (i = 0; i < NR_CPUS; i++) {
		if (!chan->buf[i])
			continue;
		relay_switch_subbuf(chan->buf[i], 0);
	}
}

EXPORT_SYMBOL_GPL(relay_open);
EXPORT_SYMBOL_GPL(relay_close);
EXPORT_SYMBOL_GPL(relay_flush);
EXPORT_SYMBOL_GPL(relay_reset);
EXPORT_SYMBOL_GPL(relay_subbufs_consumed);
EXPORT_SYMBOL_GPL(relay_commit);
EXPORT_SYMBOL_GPL(relay_switch_subbuf);
