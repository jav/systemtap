/* -*- linux-c -*-
 *
 * /proc transport and control
 * Copyright (C) 2005-2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#define STP_DEFAULT_BUFFERS 256
static int _stp_current_buffers = STP_DEFAULT_BUFFERS;

static struct list_head _stp_ready_q;
static struct list_head _stp_pool_q;
spinlock_t _stp_pool_lock = SPIN_LOCK_UNLOCKED;
spinlock_t _stp_ready_lock = SPIN_LOCK_UNLOCKED;

#ifdef STP_BULKMODE
extern int _stp_relay_flushing;
/* handle the per-cpu subbuf info read for relayfs */
static ssize_t
_stp_proc_read (struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int num;
	struct _stp_buf_info out;

	int cpu = *(int *)(PDE(file->f_dentry->d_inode)->data);

	if (!_stp_utt->rchan)
		return -EINVAL;

	out.cpu = cpu;
	out.produced = atomic_read(&_stp_utt->rchan->buf[cpu]->subbufs_produced);
	out.consumed = atomic_read(&_stp_utt->rchan->buf[cpu]->subbufs_consumed);
	out.flushing = _stp_relay_flushing;

	num = sizeof(out);
	if (copy_to_user(buf, &out, num))
		return -EFAULT;

	return num;
}

/* handle the per-cpu subbuf info write for relayfs */
static ssize_t _stp_proc_write (struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct _stp_consumed_info info;
	int cpu = *(int *)(PDE(file->f_dentry->d_inode)->data);
	if (copy_from_user(&info, buf, count))
		return -EFAULT;

	relay_subbufs_consumed(_stp_utt->rchan, cpu, info.consumed);
	return count;
}

static struct file_operations _stp_proc_fops = {
	.owner = THIS_MODULE,
	.read = _stp_proc_read,
	.write = _stp_proc_write,
};
#endif /* STP_BULKMODE */

static ssize_t _stp_ctl_write_cmd (struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	int type;

	if (count < sizeof(int))
		return 0;

	if (get_user(type, (int __user *)buf))
		return -EFAULT;

	//printk ("_stp_proc_write_cmd. count:%d type:%d\n", count, type);

	if (type == STP_SYMBOLS) {
		count -= sizeof(long);
		buf += sizeof(long);
	} else {
		count -= sizeof(int);
		buf += sizeof(int);
	}

	switch (type) {
	case STP_START:
	{
		struct _stp_msg_start st;
		if (count < sizeof(st))
			return 0;
		if (copy_from_user (&st, buf, sizeof(st)))
			return -EFAULT;
		_stp_handle_start (&st);
		break;
	}

	case STP_SYMBOLS:
		count = _stp_do_symbols(buf, count);
		break;
	case STP_MODULE:
		count = _stp_do_module(buf, count);
		break;
	case STP_EXIT:
		_stp_exit_flag = 1;
		break;
	default:
		errk ("invalid command type %d\n", type);
		return -EINVAL;
	}

	return count;
}

struct _stp_buffer {
	struct list_head list;
	int len;
	int type;
	char buf[STP_BUFFER_SIZE];
};

static DECLARE_WAIT_QUEUE_HEAD(_stp_ctl_wq);

static int _stp_ctl_write (int type, void *data, int len)
{
	struct _stp_buffer *bptr;
	unsigned long flags;
	unsigned numtrylock;

#define WRITE_AGG
#ifdef WRITE_AGG

	numtrylock = 0;
	while (!spin_trylock_irqsave (&_stp_ready_lock, flags) && (++numtrylock < MAXTRYLOCK)) 
		ndelay (TRYLOCKDELAY);
	if (unlikely (numtrylock >= MAXTRYLOCK))
		return 0;

	if (!list_empty(&_stp_ready_q)) {
		bptr = (struct _stp_buffer *)_stp_ready_q.prev;
		if (bptr->len + len <= STP_BUFFER_SIZE 
		    && type == STP_REALTIME_DATA 
		    && bptr->type == STP_REALTIME_DATA) {
			memcpy (bptr->buf + bptr->len, data, len);
			bptr->len += len;
			spin_unlock_irqrestore(&_stp_ready_lock, flags);
			return len;
		}
	}
	spin_unlock_irqrestore(&_stp_ready_lock, flags);
#endif

	numtrylock = 0;
	while (!spin_trylock_irqsave (&_stp_pool_lock, flags) && (++numtrylock < MAXTRYLOCK)) 
		ndelay (TRYLOCKDELAY);
	if (unlikely (numtrylock >= MAXTRYLOCK))
		return 0;

	if (list_empty(&_stp_pool_q)) {
		spin_unlock_irqrestore(&_stp_pool_lock, flags);
		return -1;
	}

	/* get the next buffer from the pool */
	bptr = (struct _stp_buffer *)_stp_pool_q.next;
	list_del_init(&bptr->list);
	spin_unlock_irqrestore(&_stp_pool_lock, flags);

	bptr->type = type;
	memcpy (bptr->buf, data, len);
	bptr->len = len;
	
	/* put it on the pool of ready buffers */
	numtrylock = 0;
	while (!spin_trylock_irqsave (&_stp_ready_lock, flags) && (++numtrylock < MAXTRYLOCK)) 
		ndelay (TRYLOCKDELAY);
	if (unlikely (numtrylock >= MAXTRYLOCK))
		return 0;
	list_add_tail(&bptr->list, &_stp_ready_q);
	spin_unlock_irqrestore(&_stp_ready_lock, flags);

	return len;
}

/* send commands with timeout and retry */
static int _stp_ctl_send (int type, void *data, int len)
{
	int err, trylimit = 50;
	kbug("ctl_send: type=%d len=%d\n", type, len);
	while ((err = _stp_ctl_write(type, data, len)) < 0 && trylimit--)
		msleep (5);
	kbug("returning %d\n", err);
	return err;
}

static ssize_t
_stp_ctl_read_cmd (struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct _stp_buffer *bptr;
	int len;
	unsigned long flags;

	/* FIXME FIXME FIXME. assuming count is large enough to hold buffer!! */

	/* wait for nonempty ready queue */
	spin_lock_irqsave(&_stp_ready_lock, flags);
	while (list_empty(&_stp_ready_q)) {
		spin_unlock_irqrestore(&_stp_ready_lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(_stp_ctl_wq, !list_empty(&_stp_ready_q)))
			return -ERESTARTSYS;
		spin_lock_irqsave(&_stp_ready_lock, flags);
	}
  
	/* get the next buffer off the ready list */
	bptr = (struct _stp_buffer *)_stp_ready_q.next;
	list_del_init(&bptr->list);
	spin_unlock_irqrestore(&_stp_ready_lock, flags);

	/* write it out */
	len = bptr->len + 4;
	if (copy_to_user(buf, &bptr->type, len)) {
		/* now what?  We took it off the queue then failed to send it */
		/* we can't put it back on the queue because it will likely be out-of-order */
		/* fortunately this should never happen */
		/* FIXME need to mark this as a transport failure */
		return -EFAULT;
	}

	/* put it on the pool of free buffers */
	spin_lock_irqsave(&_stp_pool_lock, flags);
	list_add_tail(&bptr->list, &_stp_pool_q);
	spin_unlock_irqrestore(&_stp_pool_lock, flags);

	return len;
}

static int _stp_ctl_open_cmd (struct inode *inode, struct file *file)
{
	_stp_pid = current->pid;
	return 0;
}

static int _stp_ctl_close_cmd (struct inode *inode, struct file *file)
{
	_stp_pid = 0;
	return 0;

}

static struct file_operations _stp_proc_fops_cmd = {
	.owner = THIS_MODULE,
	.read = _stp_ctl_read_cmd,
	.write = _stp_ctl_write_cmd,
	.open = _stp_ctl_open_cmd,
	.release = _stp_ctl_close_cmd,
};

static struct proc_dir_entry *_stp_proc_root, *_stp_proc_mod;

/* copy since proc_match is not MODULE_EXPORT'd */
static int my_proc_match(int len, const char *name, struct proc_dir_entry *de)
{
	if (de->namelen != len)
		return 0;
	return !memcmp(name, de->name, len);
}

/* set the number of buffers to use to 'num' */
static int _stp_set_buffers(int num)
{
	int i;
	struct list_head *p;
	unsigned long flags;

	//printk("stp_set_buffers %d\n", num);

	if (num == 0 || num == _stp_current_buffers)
		return _stp_current_buffers;
	
	if (num > _stp_current_buffers) {
		for (i = 0; i < num - _stp_current_buffers; i++) {
			p = (struct list_head *)kmalloc(sizeof(struct _stp_buffer),STP_ALLOC_FLAGS);
			if (!p)	{
				_stp_current_buffers += i;
				goto err;
			}
			_stp_allocated_net_memory += sizeof(struct _stp_buffer);
			spin_lock_irqsave(&_stp_pool_lock, flags);
			list_add (p, &_stp_pool_q);
			spin_unlock_irqrestore(&_stp_pool_lock, flags);
		}
	} else {
		for (i = 0; i < _stp_current_buffers - num; i++) {
			spin_lock_irqsave(&_stp_pool_lock, flags);
			p = _stp_pool_q.next;
			list_del(p);
			spin_unlock_irqrestore(&_stp_pool_lock, flags);
			kfree(p);
		}
	}
	_stp_current_buffers = num;
err:
	return _stp_current_buffers;
}

static int _stp_ctl_read_bufsize (char *page, char **start, off_t off,
				  int count, int *eof, void *data)
{
	int len = sprintf(page, "%d,%d\n", _stp_nsubbufs, _stp_subbuf_size);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int _stp_register_ctl_channel (void)
{
	int i;
	const char *dirname = "systemtap";
	char buf[32];
#ifdef STP_BULKMODE
	int j;
#endif

	struct proc_dir_entry *de, *bs = NULL;
	struct list_head *p, *tmp;

	INIT_LIST_HEAD(&_stp_ready_q);
	INIT_LIST_HEAD(&_stp_pool_q);

	/* allocate buffers */
	for (i = 0; i < STP_DEFAULT_BUFFERS; i++) {
		p = (struct list_head *)kmalloc(sizeof(struct _stp_buffer),STP_ALLOC_FLAGS);
		// printk("allocated buffer at %lx\n", (long)p);
		if (!p)
			goto err0;
		_stp_allocated_net_memory += sizeof(struct _stp_buffer);
		list_add (p, &_stp_pool_q);
	}

	if (!_stp_lock_debugfs()) {
		errk("Unable to lock transport directory.\n");
		goto err0;
	}

	  /* look for existing /proc/systemtap */
        for (de = proc_root.subdir; de; de = de->next) {
                if (my_proc_match (strlen (dirname), dirname, de)) {
                        _stp_proc_root = de;
                        break;
                }
        }

        /* create /proc/systemtap if it doesn't exist */
        if (_stp_proc_root == NULL) {
		_stp_proc_root = proc_mkdir (dirname, NULL);
                if (_stp_proc_root == NULL) {
			_stp_unlock_debugfs();
                        goto err0;
		}
        }
	_stp_unlock_debugfs();

	/* now create /proc/systemtap/module_name */
	_stp_proc_mod = proc_mkdir (THIS_MODULE->name, _stp_proc_root);
	if (_stp_proc_mod == NULL)
		goto err0;

#ifdef STP_BULKMODE
	/* now for each cpu "n", create /proc/systemtap/module_name/n  */
	for_each_cpu(i) {
		sprintf(buf, "%d", i);
		de = create_proc_entry (buf, S_IFREG|S_IRUSR, _stp_proc_mod);
		if (de == NULL) 
			goto err1;
		de->proc_fops = &_stp_proc_fops;
		de->data = _stp_kmalloc(sizeof(int));
		if (de->data == NULL) {
			remove_proc_entry (buf, _stp_proc_mod);
			goto err1;
		}
		*(int *)de->data = i;
	}
	bs = create_proc_read_entry("bufsize", 0, _stp_proc_mod, _stp_ctl_read_bufsize, NULL);
#endif /* STP_BULKMODE */

	/* finally create /proc/systemtap/module_name/cmd  */
	de = create_proc_entry ("cmd", S_IFREG|S_IRUSR|S_IWUSR, _stp_proc_mod);
	if (de == NULL) 
		goto err1;
	de->proc_fops = &_stp_proc_fops_cmd;
	return 0;

err1:
#ifdef STP_BULKMODE
	for (de = _stp_proc_mod->subdir; de; de = de->next)
		kfree (de->data);
	for_each_cpu(j) {
		if (j == i)
			break;
		sprintf(buf, "%d", i);
		remove_proc_entry (buf, _stp_proc_mod);
		
	}
	if (bs) remove_proc_entry ("bufsize", _stp_proc_mod);
#endif /* STP_BULKMODE */
err0:
	list_for_each_safe(p, tmp, &_stp_pool_q) {
		list_del(p);
		kfree(p);
	}

	errk ("Error creating systemtap /proc entries.\n");
	return -1;
}


static void _stp_unregister_ctl_channel (void)
{
	struct list_head *p, *tmp;
	char buf[32];
#ifdef STP_BULKMODE
	int i;
	struct proc_dir_entry *de;
	kbug("unregistering procfs\n");
	for (de = _stp_proc_mod->subdir; de; de = de->next)
		kfree (de->data);

	for_each_cpu(i) {
		sprintf(buf, "%d", i);
		remove_proc_entry (buf, _stp_proc_mod);
	}
	remove_proc_entry ("bufsize", _stp_proc_mod);
#endif /* STP_BULKMODE */

	remove_proc_entry ("cmd", _stp_proc_mod);
	remove_proc_entry (THIS_MODULE->name, _stp_proc_root);

	/* free memory pools */
	list_for_each_safe(p, tmp, &_stp_pool_q) {
		list_del(p);
		kfree(p);
	}
	list_for_each_safe(p, tmp, &_stp_ready_q) {
		list_del(p);
		kfree(p);
	}
}

