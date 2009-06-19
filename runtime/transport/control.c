/* -*- linux-c -*-
 *
 * control channel
 * Copyright (C) 2007-2009 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include "control.h"
#include "../mempool.c"
#include "symbols.c"
#include <linux/delay.h>

static _stp_mempool_t *_stp_pool_q;
static struct list_head _stp_ctl_ready_q;
static DEFINE_SPINLOCK(_stp_ctl_ready_lock);

static void _stp_cleanup_and_exit(int send_exit);

static ssize_t _stp_ctl_write_cmd(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	u32 type;
	static int started = 0;

	if (count < sizeof(u32))
		return 0;

	if (get_user(type, (u32 __user *)buf))
		return -EFAULT;

	count -= sizeof(u32);
	buf += sizeof(u32);


#ifdef DEBUG_TRANS
	if (type < STP_MAX_CMD)
		dbug_trans2("Got %s. len=%d\n", _stp_command_name[type],
			    (int)count);
#endif

	switch (type) {
	case STP_START:
		if (started == 0) {
			struct _stp_msg_start st;
			if (count < sizeof(st))
				return 0;
			if (copy_from_user(&st, buf, sizeof(st)))
				return -EFAULT;
			_stp_handle_start(&st);
			started = 1;
		}
		break;
	case STP_EXIT:
		_stp_cleanup_and_exit(1);
		break;
	case STP_BULK:
#ifdef STP_BULKMODE
		return count;
#else
		return -1;
#endif
	case STP_RELOCATION:
          	_stp_do_relocation (buf, count);
          	break;

	case STP_READY:
		break;

	default:
		errk("invalid command type %d\n", type);
		return -EINVAL;
	}

	return count; /* Pretend that we absorbed the entire message. */
}

static DECLARE_WAIT_QUEUE_HEAD(_stp_ctl_wq);

#ifdef DEBUG_TRANS
static void _stp_ctl_write_dbug(int type, void *data, int len)
{
	char buf[64];
	switch (type) {
	case STP_START:
		dbug_trans2("sending STP_START\n");
		break;
	case STP_EXIT:
		dbug_trans2("sending STP_EXIT\n");
		break;
	case STP_OOB_DATA:
		snprintf(buf, sizeof(buf), "%s", (char *)data);
		dbug_trans2("sending %d bytes of STP_OOB_DATA: %s\n", len,
			    buf);
		break;
	case STP_SYSTEM:
		snprintf(buf, sizeof(buf), "%s", (char *)data);
		dbug_trans2("sending STP_SYSTEM: %s\n", buf);
		break;
	case STP_TRANSPORT:
		dbug_trans2("sending STP_TRANSPORT\n");
		break;
	case STP_CONNECT:
		dbug_trans2("sending STP_CONNECT\n");
		break;
	case STP_DISCONNECT:
		dbug_trans2("sending STP_DISCONNECT\n");
		break;
	case STP_BULK:
		dbug_trans2("sending STP_BULK\n");
		break;
	case STP_READY:
	case STP_RELOCATION:
	case STP_BUF_INFO:
	case STP_SUBBUFS_CONSUMED:
		dbug_trans2("sending old message\n");
		break;
	case STP_REALTIME_DATA:
		dbug_trans2("sending %d bytes of STP_REALTIME_DATA\n", len);
		break;
	case STP_REQUEST_EXIT:
		dbug_trans2("sending STP_REQUEST_EXIT\n");
		break;
	default:
		dbug_trans2("ERROR: unknown message type: %d\n", type);
		break;
	}
}
#endif

static int _stp_ctl_write(int type, void *data, unsigned len)
{
	struct _stp_buffer *bptr;
	unsigned long flags;
	unsigned hlen;

#ifdef DEBUG_TRANS
	_stp_ctl_write_dbug(type, data, len);
#endif

	hlen = _stp_ctl_write_fs(type, data, len);
	if (hlen > 0)
		return hlen;

	/* make sure we won't overflow the buffer */
	if (unlikely(len > STP_CTL_BUFFER_SIZE))
		return 0;

	/* get a buffer from the free pool */
	bptr = _stp_mempool_alloc(_stp_pool_q);
	if (unlikely(bptr == NULL))
		return -1;

	bptr->type = type;
	memcpy(bptr->buf, data, len);
	bptr->len = len;

	/* put it on the pool of ready buffers */
	spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
	list_add_tail(&bptr->list, &_stp_ctl_ready_q);
	spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);

	return len + sizeof(bptr->type);
}

/* send commands with timeout and retry */
static int _stp_ctl_send(int type, void *data, int len)
{
	int err, trylimit = 50;
	dbug_trans(1, "ctl_send: type=%d len=%d\n", type, len);
	while ((err = _stp_ctl_write(type, data, len)) < 0 && trylimit--)
		msleep(5);
	if (err > 0)
		wake_up_interruptible(&_stp_ctl_wq);
	dbug_trans(1, "returning %d\n", err);
	return err;
}

static ssize_t _stp_ctl_read_cmd(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct _stp_buffer *bptr;
	int len;
	unsigned long flags;

	/* wait for nonempty ready queue */
	spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
	while (list_empty(&_stp_ctl_ready_q)) {
		spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(_stp_ctl_wq, !list_empty(&_stp_ctl_ready_q)))
			return -ERESTARTSYS;
		spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
	}

	/* get the next buffer off the ready list */
	bptr = (struct _stp_buffer *)_stp_ctl_ready_q.next;
	list_del_init(&bptr->list);
	spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);

	/* write it out */
	len = bptr->len + 4;
	if (len > count || copy_to_user(buf, &bptr->type, len)) {
		/* Now what?  We took it off the queue then failed to
		 * send it.  We can't put it back on the queue because
		 * it will likely be out-of-order.  Fortunately, this
		 * should never happen.
		 *
		 * FIXME: need to mark this as a transport failure. */
		errk("Supplied buffer too small. count:%d len:%d\n", (int)count, len);
		return -EFAULT;
	}

	/* put it on the pool of free buffers */
	_stp_mempool_free(bptr);

	return len;
}

static int _stp_ctl_open_cmd(struct inode *inode, struct file *file)
{
	if (_stp_ctl_attached)
		return -1;
	_stp_attach();
	return 0;
}

static int _stp_ctl_close_cmd(struct inode *inode, struct file *file)
{
	if (_stp_ctl_attached)
		_stp_detach();
	return 0;
}

static struct file_operations _stp_ctl_fops_cmd = {
	.owner = THIS_MODULE,
	.read = _stp_ctl_read_cmd,
	.write = _stp_ctl_write_cmd,
	.open = _stp_ctl_open_cmd,
	.release = _stp_ctl_close_cmd,
};

static int _stp_register_ctl_channel(void)
{
	INIT_LIST_HEAD(&_stp_ctl_ready_q);

	/* allocate buffers */
	_stp_pool_q = _stp_mempool_init(sizeof(struct _stp_buffer),
					STP_DEFAULT_BUFFERS);
	if (unlikely(_stp_pool_q == NULL))
		goto err0;
	_stp_allocated_net_memory += sizeof(struct _stp_buffer) * STP_DEFAULT_BUFFERS;

	if (_stp_register_ctl_channel_fs() != 0)
		goto err0;

	return 0;

err0:
	_stp_mempool_destroy(_stp_pool_q);
	errk("Error creating systemtap control channel.\n");
	return -1;
}

static void _stp_unregister_ctl_channel(void)
{
	struct list_head *p, *tmp;

	_stp_unregister_ctl_channel_fs();

	/* Return memory to pool and free it. */
	list_for_each_safe(p, tmp, &_stp_ctl_ready_q) {
		list_del(p);
		_stp_mempool_free(p);
	}
	_stp_mempool_destroy(_stp_pool_q);
}
