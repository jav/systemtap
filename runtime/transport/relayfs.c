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

/* relayfs callback functions */
static struct rchan_callbacks stp_rchan_callbacks =
{
	.subbuf_start = _stp_subbuf_start,
	.buf_full = _stp_buf_full,
};

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
	if (dir)
		relayfs_remove_dir(dir);
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
	struct dentry* dir = NULL;

	sprintf(dirname, "%d", pid);
	
	/* TODO: need to create systemtap dir */
	dir = relayfs_create_dir(dirname, NULL);
	if (!dir) {
		printk("STP: couldn't create relayfs dir %s.\n", dirname);
		return NULL;
	}

	chan = relay_open("cpu", dir, subbuf_size,
			  n_subbufs, 0, &stp_rchan_callbacks);
	if (!chan) {
		printk("STP: couldn't create relayfs channel.\n");
		if (dir)
			relayfs_remove_dir(dir);
	}

	*outdir = dir;
	return chan;
}

#endif /* _TRANSPORT_RELAYFS_C_ */

