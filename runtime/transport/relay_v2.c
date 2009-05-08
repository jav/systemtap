/* -*- linux-c -*- 
 *
 * This transport version uses relayfs on top of a debugfs file.  This
 * code started as a proposed relayfs interface called 'utt'.  It has
 * been modified and simplified for systemtap.
 *
 * Changes Copyright (C) 2009 Red Hat Inc.
 *
 * Original utt code by:
 *   Copyright (C) 2006 Jens Axboe <axboe@suse.de>
 *   Moved to utt.c by Tom Zanussi, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/relay.h>
#include <linux/timer.h>

#ifndef STP_RELAY_TIMER_INTERVAL
/* Wakeup timer interval in jiffies (default 10 ms) */
#define STP_RELAY_TIMER_INTERVAL		((HZ + 99) / 100)
#endif

enum _stp_transport_state {
	STP_TRANSPORT_STOPPED,
	STP_TRANSPORT_INITIALIZED,
	STP_TRANSPORT_RUNNING,
};

struct _stp_relay_data_type {
	enum _stp_transport_state transport_state;
	struct rchan *rchan;
	struct dentry *dropped_file;
	atomic_t dropped;
	atomic_t wakeup;
	struct timer_list timer;
	int overwrite_flag;
};
struct _stp_relay_data_type _stp_relay_data;

/*
 *	__stp_relay_switch_subbuf - switch to a new sub-buffer
 *
 *	Most of this function is deadcopy of relay_switch_subbuf.
 */
static size_t __stp_relay_switch_subbuf(struct rchan_buf *buf, size_t length)
{
	char *old, *new;
	size_t old_subbuf, new_subbuf;

	if (unlikely(buf == NULL))
		return 0;

	if (unlikely(length > buf->chan->subbuf_size))
		goto toobig;

	if (buf->offset != buf->chan->subbuf_size + 1) {
		buf->prev_padding = buf->chan->subbuf_size - buf->offset;
		old_subbuf = buf->subbufs_produced % buf->chan->n_subbufs;
		buf->padding[old_subbuf] = buf->prev_padding;
		buf->subbufs_produced++;
		buf->dentry->d_inode->i_size += buf->chan->subbuf_size -
			buf->padding[old_subbuf];
		smp_mb();
		if (waitqueue_active(&buf->read_wait))
			/*
			 * Calling wake_up_interruptible() and __mod_timer()
			 * from here will deadlock if we happen to be logging
			 * from the scheduler and timer (trying to re-grab
			 * rq->lock/timer->base->lock), so just set a flag.
			 */
			atomic_set(&_stp_relay_data.wakeup, 1);
	}

	old = buf->data;
	new_subbuf = buf->subbufs_produced % buf->chan->n_subbufs;
	new = (char*)buf->start + new_subbuf * buf->chan->subbuf_size;
	buf->offset = 0;
	if (!buf->chan->cb->subbuf_start(buf, new, old, buf->prev_padding)) {
		buf->offset = buf->chan->subbuf_size + 1;
		return 0;
	}
	buf->data = new;
	buf->padding[new_subbuf] = 0;

	if (unlikely(length + buf->offset > buf->chan->subbuf_size))
		goto toobig;

	return length;

toobig:
	buf->chan->last_toobig = length;
	return 0;
}

static void __stp_relay_wakeup_readers(struct rchan_buf *buf)
{
	if (buf && waitqueue_active(&buf->read_wait) &&
	    buf->subbufs_produced != buf->subbufs_consumed)
		wake_up_interruptible(&buf->read_wait);
}

static void __stp_relay_wakeup_timer(unsigned long val)
{
#ifdef STP_BULKMODE
	int i;
#endif

	if (atomic_read(&_stp_relay_data.wakeup)) {
		atomic_set(&_stp_relay_data.wakeup, 0);
#ifdef STP_BULKMODE
		for_each_possible_cpu(i)
			__stp_relay_wakeup_readers(_stp_relay_data.rchan->buf[i]);
#else
		__stp_relay_wakeup_readers(_stp_relay_data.rchan->buf[0]);
#endif
	}

 	mod_timer(&_stp_relay_data.timer, jiffies + STP_RELAY_TIMER_INTERVAL);
}

static void __stp_relay_timer_init(void)
{
	atomic_set(&_stp_relay_data.wakeup, 0);
	init_timer(&_stp_relay_data.timer);
	_stp_relay_data.timer.expires = jiffies + STP_RELAY_TIMER_INTERVAL;
	_stp_relay_data.timer.function = __stp_relay_wakeup_timer;
	_stp_relay_data.timer.data = 0;
	add_timer(&_stp_relay_data.timer);
	smp_mb();
}

static void stp_relay_set_overwrite(int overwrite)
{
	_stp_relay_data.overwrite_flag = overwrite;
}

static int __stp_relay_dropped_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t __stp_relay_dropped_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%u\n",
		 atomic_read(&_stp_relay_data.dropped));

	return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}

static struct file_operations __stp_relay_dropped_fops = {
	.owner =	THIS_MODULE,
	.open =		__stp_relay_dropped_open,
	.read =		__stp_relay_dropped_read,
};

/*
 * Keep track of how many times we encountered a full subbuffer, to aid
 * the user space app in telling how many lost events there were.
 */
static int __stp_relay_subbuf_start_callback(struct rchan_buf *buf,
					     void *subbuf, void *prev_subbuf,
					     size_t prev_padding)
{
	if (_stp_relay_data.overwrite_flag || !relay_buf_full(buf))
		return 1;

	atomic_inc(&_stp_relay_data.dropped);
	return 0;
}

static int __stp_relay_remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

static struct dentry *
__stp_relay_create_buf_file_callback(const char *filename,
				     struct dentry *parent,
				     int mode,
				     struct rchan_buf *buf,
				     int *is_global)
{
	struct dentry *file = debugfs_create_file(filename, mode, parent, buf,
						  &relay_file_operations);
	if (file) {
		file->d_inode->i_uid = _stp_uid;
		file->d_inode->i_gid = _stp_gid;
	}
	return file;
}

static struct rchan_callbacks __stp_relay_callbacks = {
	.subbuf_start		= __stp_relay_subbuf_start_callback,
	.create_buf_file	= __stp_relay_create_buf_file_callback,
	.remove_buf_file	= __stp_relay_remove_buf_file_callback,
};

static void _stp_transport_data_fs_close(void)
{
	if (_stp_relay_data.transport_state == STP_TRANSPORT_RUNNING)
		del_timer_sync(&_stp_relay_data.timer);

	if (_stp_relay_data.dropped_file)
		debugfs_remove(_stp_relay_data.dropped_file);
	if (_stp_relay_data.rchan) {
		relay_flush(_stp_relay_data.rchan);
		relay_close(_stp_relay_data.rchan);
	}
	_stp_relay_data.transport_state = STP_TRANSPORT_STOPPED;
}

static int _stp_transport_data_fs_init(void)
{
	int rc;
	u64 npages;
	struct sysinfo si;

	_stp_relay_data.transport_state = STP_TRANSPORT_STOPPED;
	_stp_relay_data.overwrite_flag = 0;
	atomic_set(&_stp_relay_data.dropped, 0);
	_stp_relay_data.dropped_file = NULL;
	_stp_relay_data.rchan = NULL;

	/* Create "dropped" file. */
	_stp_relay_data.dropped_file
		= debugfs_create_file("dropped", 0444, _stp_get_module_dir(),
				      NULL, &__stp_relay_dropped_fops);
	if (!_stp_relay_data.dropped_file) {
		rc = -EIO;
		goto err;
	}
	_stp_relay_data.dropped_file->d_inode->i_uid = _stp_uid;
	_stp_relay_data.dropped_file->d_inode->i_gid = _stp_gid;

	/* Create "trace" file. */
	npages = _stp_subbuf_size * _stp_nsubbufs;
#ifdef STP_BULKMODE
	npages *= num_possible_cpus();
#endif
	npages >>= PAGE_SHIFT;
	si_meminfo(&si);
#define MB(i) (unsigned long)((i) >> (20 - PAGE_SHIFT))
	if (npages > (si.freeram + si.bufferram)) {
		errk("Not enough free+buffered memory(%luMB) for log buffer(%luMB)\n",
		     MB(si.freeram + si.bufferram),
		     MB(npages));
		rc = -ENOMEM;
		goto err;
	}
	else if (npages > si.freeram) {
		/* exceeds freeram, but below freeram+bufferram */
		printk(KERN_WARNING
		       "log buffer size exceeds free memory(%luMB)\n",
		       MB(si.freeram));
	}
#if (RELAYFS_CHANNEL_VERSION >= 7)
	_stp_relay_data.rchan = relay_open("trace", _stp_get_module_dir(),
					   _stp_subbuf_size, _stp_nsubbufs,
					   &__stp_relay_callbacks, NULL);
#else  /* (RELAYFS_CHANNEL_VERSION < 7) */
	_stp_relay_data.rchan = relay_open("trace", _stp_get_module_dir(),
					   _stp_subbuf_size, _stp_nsubbufs,
					   &__stp_relay_callbacks);
#endif  /* (RELAYFS_CHANNEL_VERSION < 7) */
	if (!_stp_relay_data.rchan) {
		rc = -ENOENT;
		goto err;
	}
	dbug_trans(1, "returning 0...\n");
	_stp_relay_data.transport_state = STP_TRANSPORT_INITIALIZED;

	/* We're initialized.  Now start the timer. */
	__stp_relay_timer_init();
	_stp_relay_data.transport_state = STP_TRANSPORT_RUNNING;

	return 0;

err:
	_stp_transport_data_fs_close();
	return rc;
}


/**
 *      _stp_data_write_reserve - try to reserve size_request bytes
 *      @size_request: number of bytes to attempt to reserve
 *      @entry: entry is returned here
 *
 *      Returns number of bytes reserved, 0 if full.  On return, entry
 *      will point to allocated opaque pointer.  Use
 *      _stp_data_entry_data() to get pointer to copy data into.
 *
 *	(For this code's purposes, entry is filled in with the actual
 *	data pointer, but the caller doesn't know that.)
 */
static size_t
_stp_data_write_reserve(size_t size_request, void **entry)
{
	struct rchan_buf *buf;

	if (entry == NULL)
		return -EINVAL;

	buf = _stp_relay_data.rchan->buf[smp_processor_id()];
	if (unlikely(buf->offset + size_request > buf->chan->subbuf_size)) {
		size_request = __stp_relay_switch_subbuf(buf, size_request);
		if (!size_request)
			return 0;
	}
	*entry = (char*)buf->data + buf->offset;
	buf->offset += size_request;

	return size_request;
}

static unsigned char *_stp_data_entry_data(void *entry)
{
	/* Nothing to do here. */
	return entry;
}

static int _stp_data_write_commit(void *entry)
{
	/* Nothing to do here. */
	return 0;
}
