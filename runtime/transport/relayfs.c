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

#if RELAYFS_VERSION_GE_4 || defined (CONFIG_RELAY)

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

#endif /* RELAYFS_VERSION_GE_4 || CONFIG_RELAY */

#if defined (CONFIG_RELAY)
static struct dentry *_stp_create_buf_file(const char *filename,
					   struct dentry *parent,
					   int mode,
					   struct rchan_buf *buf,
					   int *is_global)
{
	struct proc_dir_entry *pde;
	struct dentry *dentry;
	struct proc_dir_entry *parent_pde = NULL;

	if (parent)
		parent_pde = PDE(parent->d_inode);
	pde = create_proc_entry(filename, S_IFREG|S_IRUSR, parent_pde);
	if (unlikely(!pde))
		return NULL;    
	pde->proc_fops = &relay_file_operations;

	mutex_lock(&parent->d_inode->i_mutex);
	dentry = lookup_one_len(filename, parent, strlen(filename));
	mutex_unlock(&parent->d_inode->i_mutex);
	if (IS_ERR(dentry))
		remove_proc_entry(filename, parent_pde);

	dentry->d_inode->u.generic_ip = buf;

	return dentry;
}

static int _stp_remove_buf_file(struct dentry *dentry)
{
	struct proc_dir_entry *pde = PDE(dentry->d_inode);
	
	remove_proc_entry(pde->name, pde->parent);

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
#if !RELAYFS_VERSION_GE_4
	.buf_full = _stp_buf_full,
#endif  /* !RELAYFS_VERSION_GE_4 */
};
#endif  /* CONFIG_RELAY */

/**
 *	_stp_relayfs_close - destroys relayfs channel
 *	@chan: the relayfs channel
 *	@dir: the directory containing the relayfs files
 */
#if defined (CONFIG_RELAY)
void _stp_relayfs_close(struct rchan *chan, struct dentry *dir)
{
	if (!chan)
		return;

	relay_close(chan);
	if (dir) {
		struct proc_dir_entry *pde = PDE(dir->d_inode);
		remove_proc_entry(pde->name, pde->parent);
	}
}
#else
void _stp_relayfs_close(struct rchan *chan, struct dentry *dir)
{
	if (!chan)
		return;

	relay_close(chan);
	if (dir)
		relayfs_remove_dir(dir);
}
#endif /* CONFIG_RELAY */

/**
 *	_stp_relayfs_open - create relayfs channel
 *	@n_subbufs: number of relayfs sub-buffers
 *	@subbuf_size: size of relayfs sub-buffers
 *	@pid: daemon pid
 *	@outdir: receives directory dentry
 *	@parentdir: parent directory dentry
 *
 *	Returns relay channel, NULL on failure
 *
 *	Creates relayfs files as /systemtap/pid/cpuX in relayfs root
 */
#if defined (CONFIG_RELAY)
extern struct dentry *module_dentry;
struct rchan *_stp_relayfs_open(unsigned n_subbufs,
				unsigned subbuf_size,
				int pid,
				struct dentry **outdir,
				struct dentry *parent_dir)
{
	char dirname[16];
	struct rchan *chan;
	struct dentry* dir = NULL;

	sprintf(dirname, "%d", pid);
	
	/* TODO: need to create systemtap dir */
	chan = relay_open("cpu", parent_dir, subbuf_size,
			  n_subbufs, &stp_rchan_callbacks);
	if (!chan) {
		printk("STP: couldn't create relayfs channel.\n");
		if (dir)
			remove_proc_entry(dirname, NULL);
	}
	*outdir = dir;
	return chan;
}
#else
struct rchan *_stp_relayfs_open(unsigned n_subbufs,
				unsigned subbuf_size,
				int pid,
				struct dentry **outdir)
{
	char dirname[16];
	struct rchan *chan;
	struct dentry* dir = NULL;

	sprintf(dirname, "%d", pid);
	
	/* TODO: need to create systemtap dir */
	dir = relayfs_create_dir(dirname, NULL);
	if (!dir) {
		printk("STP: couldn't create relayfs dir %s.\n", dirname);
		return NULL;
	}

#if RELAYFS_VERSION_GE_4
	chan = relay_open("cpu", dir, subbuf_size,
			  n_subbufs, &stp_rchan_callbacks);
#else
	chan = relay_open("cpu", dir, subbuf_size,
			  n_subbufs, 0, &stp_rchan_callbacks);
#endif /* RELAYFS_VERSION_GE_4 */

	if (!chan) {
		printk("STP: couldn't create relayfs channel.\n");
		if (dir)
			relayfs_remove_dir(dir);
	}

	*outdir = dir;
	return chan;
}
#endif /* CONFIG_RELAY */

#endif /* _TRANSPORT_RELAYFS_C_ */

