/* -*- linux-c -*-
 *
 * control channel
 * Copyright (C) 2007-2011 Red Hat Inc.
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
#ifdef CONFIG_PREEMPT_RT
static DEFINE_RAW_SPINLOCK(_stp_ctl_ready_lock);
static DEFINE_RAW_SPINLOCK(_stp_ctl_special_msg_lock);
#else
static DEFINE_SPINLOCK(_stp_ctl_ready_lock);
static DEFINE_SPINLOCK(_stp_ctl_special_msg_lock);
#endif

static void _stp_cleanup_and_exit(int send_exit);
static void _stp_handle_tzinfo (struct _stp_msg_tzinfo* tzi);

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

#if defined(DEBUG_TRANS) && (DEBUG_TRANS >= 2)
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
		return -EINVAL;
#endif
	case STP_RELOCATION:
          	_stp_do_relocation (buf, count);
          	break;

        case STP_TZINFO:
        {
                struct _stp_msg_tzinfo tzi;
                if (count < sizeof(tzi))
                        return 0;
                if (copy_from_user(&tzi, buf, sizeof(tzi)))
                        return -EFAULT;
                _stp_handle_tzinfo(&tzi);
        }
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

/* Marker to show a "special" message buffer isn't being used.
   Will be put in the _stp_buffer type field.  The type field Should
   only be manipulated while holding the _stp_ctl_special_msg_lock.  */
#define _STP_CTL_MSG_UNUSED STP_MAX_CMD

/* cmd messages allocated ahead of time.  There can be only one.  */
static struct _stp_buffer *_stp_ctl_start_msg;
static struct _stp_buffer *_stp_ctl_exit_msg;
static struct _stp_buffer *_stp_ctl_transport_msg;
static struct _stp_buffer *_stp_ctl_request_exit_msg;

/* generic overflow messages allocated ahread of time.  */
static struct _stp_buffer *_stp_ctl_oob_warn;
static struct _stp_buffer *_stp_ctl_oob_err;
static struct _stp_buffer *_stp_ctl_system_warn;
static struct _stp_buffer *_stp_ctl_realtime_err;

/* Set aside buffers for all "special" message types, plus generic
   warning and error messages.  */
static int _stp_ctl_alloc_special_buffers(void)
{
	size_t len;
	const char *msg;

	/* There can be only one of start, exit, transport and request.  */
	_stp_ctl_start_msg = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_start_msg == NULL)
		return -1;
	_stp_ctl_start_msg->type = _STP_CTL_MSG_UNUSED;

	_stp_ctl_exit_msg = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_exit_msg == NULL)
		return -1;
	_stp_ctl_exit_msg->type = _STP_CTL_MSG_UNUSED;

	_stp_ctl_transport_msg = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_transport_msg == NULL)
		return -1;
	_stp_ctl_transport_msg->type = _STP_CTL_MSG_UNUSED;

	_stp_ctl_request_exit_msg = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_request_exit_msg == NULL)
		return -1;
	_stp_ctl_request_exit_msg->type = _STP_CTL_MSG_UNUSED;

	/* oob_warn, oob_err, system and realtime are dynamically
	   allocated and a special static warn/err message take their
	   place if we run out of memory before delivery.  */
	_stp_ctl_oob_warn = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_oob_warn == NULL)
		return -1;
	_stp_ctl_oob_warn->type = _STP_CTL_MSG_UNUSED;
	/* Note that the following message shouldn't be translated,
	 * since "WARNING:" is part of the module cmd protocol. */
	msg = "WARNING: too many pending (warning) messages\n";
	len = strlen(msg) + 1;
	_stp_ctl_oob_warn->len = len;
	memcpy(&_stp_ctl_oob_warn->buf, msg, len);

	_stp_ctl_oob_err = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_oob_err == NULL)
		return -1;
	_stp_ctl_oob_err->type = _STP_CTL_MSG_UNUSED;
	/* Note that the following message shouldn't be translated,
	 * since "ERROR:" is part of the module cmd protocol. */
	msg = "ERROR: too many pending (error) messages\n";
	len = strlen(msg) + 1;
	_stp_ctl_oob_err->len = len;
	memcpy(&_stp_ctl_oob_err->buf, msg, len);

	_stp_ctl_system_warn = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_system_warn == NULL)
		return -1;
	_stp_ctl_system_warn->type = _STP_CTL_MSG_UNUSED;
	/* Note that the following message shouldn't be translated,
	 * since "WARNING:" is part of the module cmd protocol. */
	msg = "WARNING: too many pending (system) messages\n";
	len = strlen(msg) + 1;
	_stp_ctl_system_warn->len = len;
	memcpy(&_stp_ctl_system_warn->buf, msg, len);

	_stp_ctl_realtime_err = _stp_mempool_alloc(_stp_pool_q);
	if (_stp_ctl_realtime_err == NULL)
		return -1;
	_stp_ctl_realtime_err->type = _STP_CTL_MSG_UNUSED;
	/* Note that the following message shouldn't be translated,
	 * since "ERROR:" is part of the module cmd protocol. */
	msg = "ERROR: too many pending (realtime) messages\n";
	len = strlen(msg) + 1;
	_stp_ctl_realtime_err->len = len;
	memcpy(&_stp_ctl_realtime_err->buf, msg, len);

	return 0;
}


/* Get a buffer based on type, possibly a generic buffer, when all else
   fails returns NULL and there is nothing we can do.  */
static struct _stp_buffer *_stp_ctl_get_buffer(int type, void *data,
					       unsigned len)
{
	unsigned long flags;
	struct _stp_buffer *bptr = NULL;

	/* Is it a dynamically allocated message type? */
	if (type == STP_OOB_DATA
	    || type == STP_SYSTEM
	    || type == STP_REALTIME_DATA)
		bptr = _stp_mempool_alloc(_stp_pool_q);

	if (bptr != NULL) {
		bptr->type = type;
		memcpy(bptr->buf, data, len);
		bptr->len = len;
	} else {
		/* "special" type, or no more dynamic buffers.
		   We must be careful to lock to avoid races between
		   marking as used/free.  There can be only one.  */
		switch (type) {
		case STP_START:
			bptr = _stp_ctl_start_msg;
			break;
		case STP_EXIT:
			bptr = _stp_ctl_exit_msg;
			break;
		case STP_TRANSPORT:
			bptr = _stp_ctl_transport_msg;
			break;
		case STP_REQUEST_EXIT:
			bptr = _stp_ctl_request_exit_msg;
			break;
		case STP_OOB_DATA:
			/* Note that "WARNING:" should not be
			 * translated, since it is part of the module
			 * cmd protocol. */
			if (data && len >= 7
			    && strncmp(data, "WARNING:", 7) == 0)
				bptr = _stp_ctl_oob_warn;
			/* Note that "ERROR:" should not be
			 * translated, since it is part of the module
			 * cmd protocol. */
			else if (data && len >= 5
				 && strncmp(data, "ERROR:", 5) == 0)
				bptr = _stp_ctl_oob_err;
			else
				printk(KERN_WARNING "_stp_ctl_get_buffer unexpected STP_OOB_DATA\n");
			break;
		case STP_SYSTEM:
			bptr = _stp_ctl_system_warn;
			type = STP_OOB_DATA; /* overflow message */
			break;
		case STP_REALTIME_DATA:
			bptr = _stp_ctl_realtime_err;
			type = STP_OOB_DATA; /* overflow message */
			break;
		default:
			printk(KERN_WARNING "_stp_ctl_get_buffer unknown type: %d\n", type);
			bptr = NULL;
			break;
		}
		if (bptr != NULL) {
			/* OK, it is a special one, but is it free?  */
			spin_lock_irqsave(&_stp_ctl_special_msg_lock, flags);
			if (bptr->type == _STP_CTL_MSG_UNUSED)
				bptr->type = type;
			else
				bptr = NULL;
			spin_unlock_irqrestore(&_stp_ctl_special_msg_lock, flags);
		}

		/* Got a special message buffer, with type set, fill it in,
		   unless it is an "overflow" message.  */
		if (bptr != NULL
		    && bptr != _stp_ctl_oob_warn
		    && bptr != _stp_ctl_oob_err
		    && bptr != _stp_ctl_system_warn
		    && bptr != _stp_ctl_realtime_err) {
			memcpy(bptr->buf, data, len);
			bptr->len = len;
		}
	}
	return bptr;
}

/* Returns the given buffer to the pool when dynamically allocated.
   Marks special buffers as being unused.  */
static void _stp_ctl_free_buffer(struct _stp_buffer *bptr)
{
	unsigned long flags;

	/* Special buffers need special care and locking.  */
	if (bptr == _stp_ctl_start_msg
	    || bptr == _stp_ctl_exit_msg
	    || bptr == _stp_ctl_transport_msg
	    || bptr == _stp_ctl_request_exit_msg
	    || bptr == _stp_ctl_oob_warn
	    || bptr == _stp_ctl_oob_err
	    || bptr == _stp_ctl_system_warn
	    || bptr == _stp_ctl_realtime_err) {
		spin_lock_irqsave(&_stp_ctl_special_msg_lock, flags);
		bptr->type = _STP_CTL_MSG_UNUSED;
		spin_unlock_irqrestore(&_stp_ctl_special_msg_lock, flags);
	} else {
		_stp_mempool_free(bptr);
	}
}

/* Send a message directly (only old_relay) or puts it on the
   _stp_ctl_ready_q.  Doesn't call wake_up on _stp_ctl_wq (use
   _stp_ctl_send if you need that behavior).  Returns zero if len
   was zero, or len > STP_CTL_BUFFER_SIZE.  Returns the the length
   if the send or stored message on success. Returns a negative
   error code on failure.  */
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
	bptr = _stp_ctl_get_buffer(type, data, len);
	if (unlikely(bptr == NULL))
		return -ENOMEM;

	/* put it on the pool of ready buffers */
	spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
	list_add_tail(&bptr->list, &_stp_ctl_ready_q);
	spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);

	return len + sizeof(bptr->type);
}

/* send commands with timeout and retry (can still fail though).
   Will call wake_up on _stp_ctl_wq if new data is available for
   _stp_ctl_read_cmd, so stapio can immediately read it if wanted.
   Returns zero if the message had zero length or was too big.
   Returns the length of the final message if sucessfully send or queued.
   Returns a negative error number on failure.  */
static int _stp_ctl_send(int type, void *data, int len)
{
	int err, mesg_on_queue = 0;
	unsigned long flags;
	dbug_trans(1, "ctl_send: type=%d len=%d\n", type, len);
	err = _stp_ctl_write(type, data, len);
	if (err > 0) {
		/* A message was queued (or directly written), so wake up
		   _stp_ctl_read_cmd so stapio can pick it up asap.  */
		wake_up_interruptible(&_stp_ctl_wq);
        } else {
                /* printk instead of _stp_error since an error here means
		   our message or transport is suspect.  */
                printk(KERN_ERR "ctl_send (type=%d len=%d) failed: %d\n", type, len, err);

		/* If there are pending messages on the queue, then yell
		   and scream for someone to pick them off quickly.  */
		spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
		if (!list_empty(&_stp_ctl_ready_q))
			mesg_on_queue = 1;
		spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);
		if (!mesg_on_queue)
			printk(KERN_ERR "_stp_ctl_write failed, but no messages on queue\n");
		else
			wake_up_interruptible(&_stp_ctl_wq);
	}
	dbug_trans(1, "returning %d\n", err);
	return err;
}

/** Called when someone tries to read from our .cmd file.
    Will take _stp_ctl_ready_lock and pick off the next _stp_buffer
    from the _stp_ctl_ready_q, will wait_event on _stp_ctl_wq.  */
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
	_stp_ctl_free_buffer(bptr);

	return len;
}

static int _stp_ctl_open_cmd(struct inode *inode, struct file *file)
{
	if (atomic_inc_return (&_stp_ctl_attached) > 1) {
                atomic_dec (&_stp_ctl_attached);
		return -EBUSY;
        }
	_stp_attach();
	return 0;
}

static int _stp_ctl_close_cmd(struct inode *inode, struct file *file)
{
        if (atomic_dec_return (&_stp_ctl_attached) > 0) {
                BUG();
                return -EINVAL;
        }
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

	if (unlikely(_stp_ctl_alloc_special_buffers() != 0))
		goto err0;

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
