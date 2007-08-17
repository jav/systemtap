/* -*- linux-c -*- 
 *
 * This is a modified version of the proposed utt interface. If that
 * interface makes it into the kernel, this file can go away.
 *
 * Copyright (C) 2006 Jens Axboe <axboe@suse.de>
 *
 * Moved to utt.c by Tom Zanussi, 2006
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
#include <linux/relay.h>
#include "utt.h"

int utt_overwrite_flag = 0;

static void utt_remove_root(struct utt_trace *utt)
{
	if (utt->utt_tree_root) {
		if (!_stp_lock_debugfs()) {
			errk("Unable to lock transport directory.\n");
			return;
		}
		if (simple_empty(utt->utt_tree_root))
			debugfs_remove(utt->utt_tree_root);
		_stp_unlock_debugfs();
		utt->utt_tree_root = NULL;
	}
}

static void utt_remove_tree(struct utt_trace *utt)
{
	if (utt == NULL || utt->dir == NULL)
		return;
	debugfs_remove(utt->dir);
	utt_remove_root(utt);
}

static struct dentry *utt_create_tree(struct utt_trace *utt, const char *root, const char *name)
{
        struct dentry *dir = NULL;

        if (root == NULL || name == NULL)
                return NULL;

        if (!utt->utt_tree_root) {
                utt->utt_tree_root = _stp_get_root_dir(root);
                if (!utt->utt_tree_root)
                        goto err;
        }

        dir = debugfs_create_dir(name, utt->utt_tree_root);
        if (!dir)
                utt_remove_root(utt);
err:
        return dir;
}


void utt_trace_cleanup(struct utt_trace *utt)
{
	if (utt == NULL)
		return;
	if (utt->rchan)
		relay_close(utt->rchan);
	if (utt->dropped_file)
		debugfs_remove(utt->dropped_file);
	utt_remove_tree(utt);
	kfree(utt);
}

int utt_trace_remove(struct utt_trace *utt)
{
	if (utt->trace_state == Utt_trace_setup ||
	    utt->trace_state == Utt_trace_stopped)
		utt_trace_cleanup(utt);

	return 0;
}

static int utt_dropped_open(struct inode *inode, struct file *filp)
{
#ifdef STAPCONF_INODE_PRIVATE
	filp->private_data = inode->i_private;
#else
	filp->private_data = inode->u.generic_ip;
#endif
	return 0;
}

static ssize_t utt_dropped_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct utt_trace *utt = filp->private_data;
	char buf[16];

	snprintf(buf, sizeof(buf), "%u\n", atomic_read(&utt->dropped));

	return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}

static struct file_operations utt_dropped_fops = {
	.owner =	THIS_MODULE,
	.open =		utt_dropped_open,
	.read =		utt_dropped_read,
};

/*
 * Keep track of how many times we encountered a full subbuffer, to aid
 * the user space app in telling how many lost events there were.
 */
static int utt_subbuf_start_callback(struct rchan_buf *buf, void *subbuf,
				     void *prev_subbuf, size_t prev_padding)
{
	struct utt_trace *utt;

	if (utt_overwrite_flag || !relay_buf_full(buf))
		return 1;

	utt = buf->chan->private_data;
	atomic_inc(&utt->dropped);
	return 0;
}

static int utt_remove_buf_file_callback(struct dentry *dentry)
{
	debugfs_remove(dentry);
	return 0;
}

static struct dentry *utt_create_buf_file_callback(const char *filename,
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
	return  file;
}

static struct dentry *utt_create_global_buf_file_callback(const char *filename,
							  struct dentry *parent,
							  int mode,
							  struct rchan_buf *buf,
							  int *is_global)
{
	struct dentry *file; 
	*is_global = 1;
	file = debugfs_create_file(filename, mode, parent, buf,
				   &relay_file_operations);
	if (file) {
		file->d_inode->i_uid = _stp_uid;
		file->d_inode->i_gid = _stp_gid;
	}
	return  file;
}

static struct rchan_callbacks utt_relay_callbacks = {
	.subbuf_start		= utt_subbuf_start_callback,
	.create_buf_file	= utt_create_buf_file_callback,
	.remove_buf_file	= utt_remove_buf_file_callback,
};

static struct rchan_callbacks utt_relay_callbacks_global = {
	.subbuf_start		= utt_subbuf_start_callback,
	.create_buf_file	= utt_create_global_buf_file_callback,
	.remove_buf_file	= utt_remove_buf_file_callback,
};

/*
 * Setup everything required to start tracing
 */
struct utt_trace *utt_trace_setup(struct utt_trace_setup *utts)
{
	struct utt_trace *utt = NULL;
	struct dentry *dir = NULL;
	int ret = -EINVAL;

	if (!utts->buf_size || !utts->buf_nr)
		goto err;

	ret = -ENOMEM;
	utt = kzalloc(sizeof(*utt), GFP_KERNEL);
	if (!utt)
		goto err;

	ret = -ENOENT;
	dir = utt_create_tree(utt, utts->root, utts->name);
	if (!dir)
		goto err;
	utt->dir = dir;
	atomic_set(&utt->dropped, 0);

	ret = -EIO;
	utt->dropped_file = debugfs_create_file("dropped", 0444, dir, utt, &utt_dropped_fops);
	if (!utt->dropped_file)
		goto err;

#if (RELAYFS_CHANNEL_VERSION >= 7)
	if (utts->is_global)
		utt->rchan = relay_open("trace", dir, utts->buf_size, utts->buf_nr, 
					&utt_relay_callbacks_global, NULL);
	else
		utt->rchan = relay_open("trace", dir, utts->buf_size, utts->buf_nr, 
					&utt_relay_callbacks, NULL);
#else
	if (utts->is_global)
		utt->rchan = relay_open("trace", dir, utts->buf_size, utts->buf_nr, &utt_relay_callbacks_global);
	else
		utt->rchan = relay_open("trace", dir, utts->buf_size, utts->buf_nr, &utt_relay_callbacks);
#endif

	if (!utt->rchan)
		goto err;
	utt->rchan->private_data = utt;

	utt->trace_state = Utt_trace_setup;

	utts->err = 0;
	return utt;
err:
	if (utt) {
		if (utt->dropped_file)
			debugfs_remove(utt->dropped_file);
		if (utt->rchan)
			relay_close(utt->rchan);
		kfree(utt);
	}
	if (dir)
		utt_remove_tree(utt);
	utts->err = ret;
	return NULL;
}

int utt_trace_startstop(struct utt_trace *utt, int start,
			unsigned int *trace_seq)
{
	int ret;

	/*
	 * For starting a trace, we can transition from a setup or stopped
	 * trace. For stopping a trace, the state must be running
	 */
	ret = -EINVAL;
	if (start) {
		if (utt->trace_state == Utt_trace_setup ||
		    utt->trace_state == Utt_trace_stopped) {
			if (trace_seq)
				(*trace_seq)++;
			smp_mb();
			utt->trace_state = Utt_trace_running;
			ret = 0;
		}
	} else {
		if (utt->trace_state == Utt_trace_running) {
			utt->trace_state = Utt_trace_stopped;
			relay_flush(utt->rchan);
			ret = 0;
		}
	}

	return ret;
}
