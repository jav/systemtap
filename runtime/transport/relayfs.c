#ifndef _TRANSPORT_RELAYFS_C_ /* -*- linux-c -*- */
#define _TRANSPORT_RELAYFS_C_

/*
 * relayfs.c - stp relayfs-related transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Redhat Inc, 2005
 *
 * This file is released under the GPL.
 */

/** @file relayfs.c
 * @brief Systemtap relayfs-related transport functions
 */

/** @addtogroup transport Transport Functions
 * @{
 */

#include "relayfs.h"

#if (RELAYFS_CHANNEL_VERSION >= 4) || defined (CONFIG_RELAY)

/**
 *	_stp_subbuf_start - subbuf_start() relayfs callback implementation
 */
static int _stp_subbuf_start(struct rchan_buf *buf,
			     void *subbuf,
			     void *prev_subbuf,
			     size_t prev_padding)
{
	if (relay_buf_full(buf))
		return 0;

	if (prev_subbuf)
		*((unsigned *)prev_subbuf) = prev_padding;

	subbuf_start_reserve(buf, sizeof(unsigned int));

	return 1;
}

#else

/**
 *	_stp_subbuf_start - subbuf_start() relayfs callback implementation
 */
static int _stp_subbuf_start(struct rchan_buf *buf,
			     void *subbuf,
			     unsigned prev_subbuf_idx,
			     void *prev_subbuf)
{
	unsigned padding = buf->padding[prev_subbuf_idx];
	if (prev_subbuf)
		*((unsigned *)prev_subbuf) = padding;

	return sizeof(padding); /* reserve space for padding */
}

/**
 *	_stp_buf_full - buf_full() relayfs callback implementation
 */
static void _stp_buf_full(struct rchan_buf *buf,
			  unsigned subbuf_idx,
			  void *subbuf)
{
	unsigned padding = buf->padding[subbuf_idx];
	*((unsigned *)subbuf) = padding;
}

#endif /* RELAYFS_CHANNEL_VERSION >= 4 || CONFIG_RELAY */

#if defined (CONFIG_RELAY)
static struct dentry *_stp_create_buf_file(const char *filename,
					   struct dentry *parent,
					   int mode,
					   struct rchan_buf *buf,
					   int *is_global)
{
	return debugfs_create_file(filename, mode, parent, buf,
                                   &relay_file_operations);
}

static int _stp_remove_buf_file(struct dentry *dentry)
{
        debugfs_remove(dentry);

        return 0;
}
#endif /* CONFIG_RELAY */

/* relayfs callback functions */
#if defined (CONFIG_RELAY)
static struct rchan_callbacks stp_rchan_callbacks =
{
	.subbuf_start = _stp_subbuf_start,
	.create_buf_file = _stp_create_buf_file,
	.remove_buf_file = _stp_remove_buf_file,
};
#else
static struct rchan_callbacks stp_rchan_callbacks =
{
	.subbuf_start = _stp_subbuf_start,
#if (RELAYFS_CHANNEL_VERSION < 4)
	.buf_full = _stp_buf_full,
#endif  /* RELAYFS_CHANNEL_VERSION < 4 */
};
#endif  /* CONFIG_RELAY */

static struct dentry *_stp_create_relay_dir(const char *dirname, struct dentry *parent)
{
	struct dentry *dir;
	
#if defined (CONFIG_RELAY)
	dir = debugfs_create_dir(dirname, parent);
	if (IS_ERR(dir)) {
		printk("STP: Couldn't create directory %s - debugfs not configured in.\n", dirname);
		dir = NULL;
	}
#else
	dir = relayfs_create_dir(dirname, parent);
#endif

	return dir;
}

static void _stp_remove_relay_dir(struct dentry *dir)
{
	if (dir == NULL)
		return;
	
#if defined (CONFIG_RELAY)
		debugfs_remove(dir);
#else
		relayfs_remove_dir(dir);
#endif
}

static struct dentry *_stp_get_relay_root(void)
{
	struct file_system_type *fs;
	struct super_block *sb;
	struct dentry *root;
	char *dirname = "systemtap";

	root = _stp_create_relay_dir(dirname, NULL);
	if (root)
		return root;
	
#if defined (CONFIG_RELAY)
	fs = get_fs_type("debugfs");
#else
	fs = get_fs_type("relayfs");
#endif
	if (!fs)
		return NULL;

	sb = list_entry(fs->fs_supers.next, struct super_block, s_instances);
	mutex_lock(&sb->s_root->d_inode->i_mutex);
	root = lookup_one_len(dirname, sb->s_root, strlen(dirname));
	mutex_unlock(&sb->s_root->d_inode->i_mutex);
	if (!IS_ERR(root))
		dput(root);
	
	return root;
}

static void _stp_put_relay_root(struct dentry *root)
{
	if (root)
		_stp_remove_relay_dir(root);
}

static struct dentry *_relay_root;

/**
 *	_stp_relayfs_close - destroys relayfs channel
 *	@chan: the relayfs channel
 *	@dir: the directory containing the relayfs files
 */
void _stp_relayfs_close(struct rchan *chan, struct dentry *dir)
{
	if (!chan)
		return;
	relay_close(chan);
	_stp_remove_relay_dir(dir);
	_stp_put_relay_root(_relay_root);
}

/**
 *	_stp_relayfs_open - create relayfs channel
 *	@n_subbufs: number of relayfs sub-buffers
 *	@subbuf_size: size of relayfs sub-buffers
 *	@pid: daemon pid
 *	@outdir: receives directory dentry
 *
 *	Returns relay channel, NULL on failure
 *
 *	Creates relayfs files as /systemtap/pid/cpuX in relayfs root
 */
struct rchan *_stp_relayfs_open(unsigned n_subbufs,
				unsigned subbuf_size,
				int pid,
				struct dentry **outdir)
{
	char dirname[16];
	struct rchan *chan;
	struct dentry* root, *dir;

	sprintf(dirname, "%d", pid);
	
	root = _stp_get_relay_root();
	if (!root) {
		printk("STP: couldn't get relay root dir.\n");
		return NULL;
	}

	dir = _stp_create_relay_dir(dirname, root);
	if (!dir) {
		printk("STP: couldn't create relay dir %s.\n", dirname);
		_stp_put_relay_root(root);
		return NULL;
	}
	
#if (RELAYFS_CHANNEL_VERSION >= 4)
	chan = relay_open("cpu", dir, subbuf_size,
			  n_subbufs, &stp_rchan_callbacks);
#else
	chan = relay_open("cpu", dir, subbuf_size,
			  n_subbufs, 0, &stp_rchan_callbacks);
#endif /* RELAYFS_CHANNEL_VERSION >= 4 */

	if (!chan) {
		printk("STP: couldn't create relay channel.\n");
		_stp_remove_relay_dir(dir);
		_stp_put_relay_root(root);
	}

	_relay_root = root;
	*outdir = dir;
	return chan;
}

#endif /* _TRANSPORT_RELAYFS_C_ */

