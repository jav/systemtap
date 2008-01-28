/* -*- linux-c -*-
 *
 * debugfs control channel
 * Copyright (C) 2007-2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#define STP_DEFAULT_BUFFERS 50
static int _stp_current_buffers = STP_DEFAULT_BUFFERS;

static struct list_head _stp_ctl_ready_q;
static struct list_head _stp_sym_ready_q;
static struct list_head _stp_pool_q;
DEFINE_SPINLOCK(_stp_pool_lock);
DEFINE_SPINLOCK(_stp_ctl_ready_lock);
DEFINE_SPINLOCK(_stp_sym_ready_lock);

static ssize_t _stp_sym_write_cmd (struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	static int saved_type = 0;
	int type;

	if (count < sizeof(int32_t))
		return 0;

	/* Allow sending of packet type followed by data in the next packet.*/
	if (count == sizeof(int32_t)) {
		if (get_user(saved_type, (int __user *)buf))
			return -EFAULT;
		return count;
	} else if (saved_type) {
		type = saved_type;
		saved_type = 0;
	} else {
		if (get_user(type, (int __user *)buf))
			return -EFAULT;
		count -= sizeof(int);
		buf += sizeof(int);
	}
	
	kbug ("count:%d type:%d\n", (int)count, type);

	switch (type) {
	case STP_SYMBOLS:		
		count = _stp_do_symbols(buf, count);
		break;
	case STP_MODULE:
		if (count > 1)
			count = _stp_do_module(buf, count);
		else {
			/* count == 1 indicates end of initial modules list */
			_stp_ctl_send(STP_TRANSPORT, NULL, 0);			
		}
		break;
	case STP_EXIT:
		_stp_exit_flag = 1;
		break;
	default:
		errk ("invalid symbol command type %d\n", type);
		return -EINVAL;
	}

	return count;
}
static ssize_t _stp_ctl_write_cmd (struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	int type;
	static int started = 0;

	if (count < sizeof(int))
		return 0;

	if (get_user(type, (int __user *)buf))
		return -EFAULT;

	kbug ("count:%d type:%d\n", (int)count, type);

	count -= sizeof(int);
	buf += sizeof(int);

	switch (type) {
	case STP_START:
		if (started == 0) {
			struct _stp_msg_start st;
			if (count < sizeof(st))
				return 0;
			if (copy_from_user (&st, buf, sizeof(st)))
				return -EFAULT;
			_stp_handle_start (&st);
			started = 1;
		}
		break;
	case STP_EXIT:
		_stp_exit_flag = 1;
		break;
	case STP_BULK:
#ifdef STP_BULKMODE
		return count;
#else
		return -1;
#endif
	case STP_READY:
		/* request symbolic information */
		_stp_ask_for_symbols();		
		break;
		
	default:
		errk ("invalid command type %d\n", type);
		return -EINVAL;
	}

	return count;
}

#define STP_CTL_BUFFER_SIZE 256

struct _stp_buffer {
	struct list_head list;
	int len;
	int type;
	char buf[STP_CTL_BUFFER_SIZE];
};

static DECLARE_WAIT_QUEUE_HEAD(_stp_ctl_wq);
static DECLARE_WAIT_QUEUE_HEAD(_stp_sym_wq);

#ifdef DEBUG
static void _stp_ctl_write_dbug (int type, void *data, int len)
{
	char buf[64];
	switch (type) {
	case STP_START:
		printk("_stp_ctl_write: sending STP_START\n");
		break;
	case STP_EXIT:
		printk("_stp_ctl_write: sending STP_EXIT\n");
		break;
	case STP_OOB_DATA:
		snprintf(buf, sizeof(buf), "%s", (char *)data); 
		printk("_stp_ctl_write: sending %d bytes of STP_OOB_DATA: %s\n", len, buf);
		break;
	case STP_SYSTEM:
		snprintf(buf, sizeof(buf), "%s", (char *)data); 
		printk("_stp_ctl_write: sending STP_SYSTEM: %s\n", buf);
		break;
	case STP_TRANSPORT:
		printk("_stp_ctl_write: sending STP_TRANSPORT\n");
		break;
	default:
		printk("_stp_ctl_write: ERROR: unknown message type: %d\n", type);
		break;
	}
}
static void _stp_sym_write_dbug (int type, void *data, int len)
{
	switch (type) {
	case STP_SYMBOLS:
		printk("_stp_sym_write: sending STP_SYMBOLS\n");
		break;
	case STP_MODULE:
		printk("_stp_sym_write: sending STP_MODULE\n");
		break;
	default:
		printk("_stp_sym_write: ERROR: unknown message type: %d\n", type);
		break;
	}
}
#endif

static int _stp_ctl_write (int type, void *data, unsigned len)
{
	struct _stp_buffer *bptr;
	unsigned long flags;
	unsigned numtrylock;
#ifdef DEBUG
	_stp_ctl_write_dbug(type, data, len);
#endif

	/* make sure we won't overflow the buffer */
	if (unlikely(len > STP_CTL_BUFFER_SIZE))
		return 0;

	numtrylock = 0;
	while (!spin_trylock_irqsave (&_stp_pool_lock, flags) && (++numtrylock < MAXTRYLOCK)) 
		ndelay (TRYLOCKDELAY);
	if (unlikely (numtrylock >= MAXTRYLOCK))
		return 0;

	if (unlikely(list_empty(&_stp_pool_q))) {
		spin_unlock_irqrestore(&_stp_pool_lock, flags); 
		dbug("_stp_pool_q empty\n");
		return -1;
	}

	/* get the next buffer from the pool */
	bptr = (struct _stp_buffer *)_stp_pool_q.next;
	list_del_init(&bptr->list);
	spin_unlock_irqrestore(&_stp_pool_lock, flags);

	bptr->type = type;
	memcpy(bptr->buf, data, len);
	bptr->len = len;
	
	/* put it on the pool of ready buffers */
	numtrylock = 0;
	while (!spin_trylock_irqsave (&_stp_ctl_ready_lock, flags) && (++numtrylock < MAXTRYLOCK)) 
		ndelay (TRYLOCKDELAY);

	if (unlikely (numtrylock >= MAXTRYLOCK)) 
		return 0;

	list_add_tail(&bptr->list, &_stp_ctl_ready_q);
	spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);

	return len;
}

static int _stp_sym_write (int type, void *data, unsigned len)
{
	struct _stp_buffer *bptr;
	unsigned long flags;

#ifdef DEBUG
	_stp_sym_write_dbug(type, data, len);
#endif

	/* make sure we won't overflow the buffer */
	if (unlikely(len > STP_CTL_BUFFER_SIZE))
		return 0;

	spin_lock_irqsave (&_stp_pool_lock, flags);
	if (unlikely(list_empty(&_stp_pool_q))) {
		spin_unlock_irqrestore(&_stp_pool_lock, flags); 
		dbug("_stp_pool_q empty\n");
		return -1;
	}

	/* get the next buffer from the pool */
	bptr = (struct _stp_buffer *)_stp_pool_q.next;
	list_del_init(&bptr->list);
	spin_unlock_irqrestore(&_stp_pool_lock, flags);

	bptr->type = type;
	memcpy(bptr->buf, data, len);
	bptr->len = len;
	
	/* put it on the pool of ready buffers */
	spin_lock_irqsave (&_stp_sym_ready_lock, flags);
	list_add_tail(&bptr->list, &_stp_sym_ready_q);
	spin_unlock_irqrestore(&_stp_sym_ready_lock, flags);

	/* OK, it's queued. Now signal any waiters. */
	wake_up_interruptible(&_stp_sym_wq);

	return len;
}

/* send commands with timeout and retry */
static int _stp_ctl_send (int type, void *data, int len)
{
	int err, trylimit = 50;
	kbug("ctl_send: type=%d len=%d\n", type, len);
	if (unlikely(type == STP_SYMBOLS || type == STP_MODULE)) {
		while ((err = _stp_sym_write(type, data, len)) < 0 && trylimit--)
			msleep (5);
	} else {
		while ((err = _stp_ctl_write(type, data, len)) < 0 && trylimit--)
			msleep (5);
		if (err > 0)
			wake_up_interruptible(&_stp_ctl_wq);
	}
	kbug("returning %d\n", err);
	return err;
}

static ssize_t
_stp_sym_read_cmd (struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct _stp_buffer *bptr;
	int len;
	unsigned long flags;

	/* wait for nonempty ready queue */
	spin_lock_irqsave(&_stp_sym_ready_lock, flags);
	while (list_empty(&_stp_sym_ready_q)) {
		spin_unlock_irqrestore(&_stp_sym_ready_lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(_stp_sym_wq, !list_empty(&_stp_sym_ready_q)))
			return -ERESTARTSYS;
		spin_lock_irqsave(&_stp_sym_ready_lock, flags);
	}
  
	/* get the next buffer off the ready list */
	bptr = (struct _stp_buffer *)_stp_sym_ready_q.next;
	list_del_init(&bptr->list);
	spin_unlock_irqrestore(&_stp_sym_ready_lock, flags);

	/* write it out */
	len = bptr->len + 4;
	if (len > count || copy_to_user(buf, &bptr->type, len)) {
		/* now what?  We took it off the queue then failed to send it */
		/* we can't put it back on the queue because it will likely be out-of-order */
		/* fortunately this should never happen */
		/* FIXME need to mark this as a transport failure */
		errk("Supplied buffer too small. count:%d len:%d\n", (int)count, len);
		return -EFAULT;
	}

	/* put it on the pool of free buffers */
	spin_lock_irqsave(&_stp_pool_lock, flags);
	list_add_tail(&bptr->list, &_stp_pool_q);
	spin_unlock_irqrestore(&_stp_pool_lock, flags);

	return len;
}

static ssize_t
_stp_ctl_read_cmd (struct file *file, char __user *buf, size_t count, loff_t *ppos)
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
		/* now what?  We took it off the queue then failed to send it */
		/* we can't put it back on the queue because it will likely be out-of-order */
		/* fortunately this should never happen */
		/* FIXME need to mark this as a transport failure */
		errk("Supplied buffer too small. count:%d len:%d\n", (int)count, len);
		return -EFAULT;
	}

	/* put it on the pool of free buffers */
	spin_lock_irqsave(&_stp_pool_lock, flags);
	list_add_tail(&bptr->list, &_stp_pool_q);
	spin_unlock_irqrestore(&_stp_pool_lock, flags);

	return len;
}

static int _stp_sym_opens = 0;
static int _stp_sym_open_cmd (struct inode *inode, struct file *file)
{
	/* only allow one reader */
	if (_stp_sym_opens)
		return -1;

	_stp_sym_opens++;
	return 0;
}

static int _stp_sym_close_cmd (struct inode *inode, struct file *file)
{
	if (_stp_sym_opens)
		_stp_sym_opens--;
	return 0;
}

static int _stp_ctl_open_cmd (struct inode *inode, struct file *file)
{
	if (_stp_attached)
		return -1;

	_stp_attach();
	return 0;
}

static int _stp_ctl_close_cmd (struct inode *inode, struct file *file)
{
	if (_stp_attached)
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

static struct file_operations _stp_sym_fops_cmd = {
	.owner = THIS_MODULE,
	.read = _stp_sym_read_cmd,
	.write = _stp_sym_write_cmd,
	.open = _stp_sym_open_cmd,
	.release = _stp_sym_close_cmd,
};

static struct dentry *_stp_cmd_file = NULL;
static struct dentry *_stp_sym_file = NULL;

static int _stp_register_ctl_channel (void)
{
	int i;
	struct list_head *p, *tmp;
	char buf[32];
	
	if (_stp_utt == NULL) {
		errk("_expected _stp_utt to be set.\n");
		return -1;
	}

	INIT_LIST_HEAD(&_stp_ctl_ready_q);
	INIT_LIST_HEAD(&_stp_sym_ready_q);
	INIT_LIST_HEAD(&_stp_pool_q);

	/* allocate buffers */
	for (i = 0; i < STP_DEFAULT_BUFFERS; i++) {
		p = (struct list_head *)_stp_kmalloc(sizeof(struct _stp_buffer));
		// printk("allocated buffer at %lx\n", (long)p);
		if (!p)
			goto err0;
		_stp_allocated_net_memory += sizeof(struct _stp_buffer);
		list_add (p, &_stp_pool_q);
	}

	/* create [debugfs]/systemtap/module_name/.cmd  */
	_stp_cmd_file = debugfs_create_file(".cmd", 0600, _stp_utt->dir, NULL, &_stp_ctl_fops_cmd);
	if (_stp_cmd_file == NULL) 
		goto err0;
	_stp_cmd_file->d_inode->i_uid = _stp_uid;
	_stp_cmd_file->d_inode->i_gid = _stp_gid;

	/* create [debugfs]/systemtap/module_name/.symbols  */
	_stp_sym_file = debugfs_create_file(".symbols", 0600, _stp_utt->dir, NULL, &_stp_sym_fops_cmd);
	if (_stp_sym_file == NULL)
		goto err0;
	return 0;

err0:
	if (_stp_cmd_file) debugfs_remove(_stp_cmd_file);

	list_for_each_safe(p, tmp, &_stp_pool_q) {
		list_del(p);
		_stp_kfree(p);
	}
	errk ("Error creating systemtap debugfs entries.\n");
	return -1;
}


static void _stp_unregister_ctl_channel (void)
{
	struct list_head *p, *tmp;
	if (_stp_sym_file) debugfs_remove(_stp_sym_file);
	if (_stp_cmd_file) debugfs_remove(_stp_cmd_file);

	/* free memory pools */
	list_for_each_safe(p, tmp, &_stp_pool_q) {
		list_del(p);
		_stp_kfree(p);
	}
	list_for_each_safe(p, tmp, &_stp_sym_ready_q) {
		list_del(p);
		_stp_kfree(p);
	}
	list_for_each_safe(p, tmp, &_stp_ctl_ready_q) {
		list_del(p);
		_stp_kfree(p);
	}
}

