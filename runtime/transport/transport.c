#ifndef _TRANSPORT_TRANSPORT_C_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_C_

/*
 * transport.c - stp transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Redhat Inc, 2005
 *
 * This file is released under the GPL.
 */

/** @file transport.c
 * @brief Systemtap transport functions
 */

/** @addtogroup transport Transport Functions
 * @{
 */

#include <linux/delay.h>
#include "transport.h"
#include "control.h"
#include "relayfs.c"

/** @file transport.c
 * @brief transport functions
 */
/** @addtogroup io transport
 * transport functions
 * @{
 */

/* transport-related data for this probe */
struct stp_transport *t;

/* forward declaration of probe-defined exit function */
static void probe_exit(void);

/**
 *	_stp_handle_buf_info - handle relayfs buffer info command
 */
static void _stp_handle_buf_info(int pid, struct buf_info *in)
{
	struct buf_info out;
	BUG_ON(!(t && t->chan));

	out.cpu = in->cpu;
	out.produced = atomic_read(&t->chan->buf[in->cpu]->subbufs_produced);
	out.consumed = atomic_read(&t->chan->buf[in->cpu]->subbufs_consumed);

	_stp_ctrl_send(STP_BUF_INFO, &out, sizeof(out), pid);
}

/**
 *	_stp_handle_subbufs_consumed - handle relayfs subbufs consumed command
 */
static void _stp_handle_subbufs_consumed(int pid, struct consumed_info *info)
{
	BUG_ON(!(t && t->chan));
	relay_subbufs_consumed(t->chan, info->cpu, info->consumed);
}

/**
 *	_stp_handle_exit - handle exit command
 */
static void _stp_handle_exit(int pid)
{
	BUG_ON(!t);
	probe_exit();
	_stp_transport_flush();
	_stp_ctrl_send(STP_EXIT, __this_module.name,
		       strlen(__this_module.name) + 1, pid);
}

/**
 *	_stp_cmd_handler - control channel command handler callback
 *	@pid: the pid of the daemon the command was sent from
 *	@cmd: the command id
 *	@data: command-specific data
 *
 *	This function must return 0 if the command was handled, nonzero
 *	otherwise.
 */
static int _stp_cmd_handler(int pid, int cmd, void *data)
{
	int err = 0;

	switch (cmd) {
	case STP_BUF_INFO:
		_stp_handle_buf_info(pid, data);
		break;
	case STP_SUBBUFS_CONSUMED:
		_stp_handle_subbufs_consumed(pid, data);
		break;
	case STP_EXIT:
		_stp_handle_exit(pid);
		break;
	default:
		err = -1;
		break;
	}
	
	return err;
}

/**
 *	_stp_transport_close - close netlink and relayfs channels
 *
 *	This must be called after all I/O is done, probably at the end
 *	of module cleanup.
 */
void _stp_transport_close()
{
	if (!t)
		return;

	_stp_ctrl_unregister(t->pid);
	if (!_stp_streaming())
		_stp_relayfs_close(t->chan, t->dir);

	/* in case the module has been manually removed */
	_stp_ctrl_send(STP_EXIT, __this_module.name,
		       strlen(__this_module.name) + 1, t->pid);
	kfree(t);
}

/**
 *	_stp_transport_open - open netlink and relayfs channels
 *	@n_subbufs: number of relayfs sub-buffers
 *	@subbuf_size: size of relayfs sub-buffers
 *	@pid: daemon pid
 *
 *	Returns negative on failure, 0 otherwise.
 *
 *	This function registers the probe with the control channel,
 *	and if the probe output will not be 'streaming', creates a
 *	relayfs channel for it.  This must be called before any I/O is
 *	done, probably at the start of module initialization.
 */
int _stp_transport_open(unsigned n_subbufs, unsigned subbuf_size, int pid)
{
	BUG_ON(!(n_subbufs && subbuf_size));
	
	t = kcalloc(1, sizeof(struct stp_transport), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->pid = pid;
	_stp_ctrl_register(t->pid, _stp_cmd_handler);

	if (_stp_streaming())
		return 0;

	t->chan = _stp_relayfs_open(n_subbufs, subbuf_size, t->pid, &t->dir);
	if (!t->chan) {
		_stp_ctrl_unregister(t->pid);
		kfree(t);
		return -ENOMEM;
	}

	return 0;
}

/** @} */
#endif /* _TRANSPORT_C_ */
