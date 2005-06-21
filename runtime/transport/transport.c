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
 *	_stp_streaming - boolean, are we using 'streaming' output?
 */
static inline int _stp_streaming(void)
{
	if (t->transport_mode == STP_TRANSPORT_NETLINK)
		return 1;
	
	return 0;
}

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
 *	_stp_handle_subbufs_consumed - handle relayfs subbufs consumed command
 */
static void _stp_handle_transport(int pid)
{
	struct transport_info out;
	BUG_ON(!(t));

	out.transport_mode = t->transport_mode;
	if (t->transport_mode == STP_TRANSPORT_RELAYFS) {
		out.subbuf_size = subbuf_size;
		out.n_subbufs = n_subbufs;
	}

	_stp_ctrl_send(STP_TRANSPORT_MODE, &out, sizeof(out), pid);
}

int _stp_exit_called = 0;

static int global_pid;
static void stp_exit_helper (void *data);
static DECLARE_WORK(stp_exit, stp_exit_helper, &global_pid);

/**
 *	_stp_transport_flush - flush the transport, if applicable
 */
static inline void _stp_transport_flush(void)
{
	extern struct stp_transport *t;

	if (t->transport_mode == STP_TRANSPORT_RELAYFS) {
		BUG_ON(!t->chan);
		relay_flush(t->chan);
		ssleep(1); /* FIXME: time for data to be flushed */
	}
}

extern atomic_t _stp_transport_failures;
static void stp_exit_helper (void *data)
{
	int err, trylimit = 50, pid = *(int *)data;

	if (_stp_exit_called == 0) {
		_stp_exit_called = 1;
		probe_exit();
		_stp_transport_flush();
	}

	while ((err =_stp_ctrl_send(STP_EXIT, __this_module.name,
				    strlen(__this_module.name) + 1, pid)) < 0) {
		//printk("stp_handle_exit: sent STP_EXIT.  err=%d\n", err);
		msleep (5);
		if (!trylimit--) /* limit e.g. if user died */
			break;
	}
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
	case STP_TRANSPORT_MODE:
		_stp_handle_transport(pid);
		break;
	case STP_EXIT:
		schedule_work (&stp_exit);
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

	stp_exit_helper (&t->pid);

	_stp_ctrl_unregister(t->pid);
	if (!_stp_streaming())
		_stp_relayfs_close(t->chan, t->dir);

//	stp_exit_helper (&t->pid);
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
int _stp_transport_open(int transport_mode,
			unsigned n_subbufs,
			unsigned subbuf_size,
			int pid)
{
	BUG_ON(!(n_subbufs && subbuf_size));
	
	t = kcalloc(1, sizeof(struct stp_transport), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->pid = pid;
	global_pid = pid;
	_stp_ctrl_register(t->pid, _stp_cmd_handler);

	t->transport_mode = transport_mode;

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

int _stp_transport_send (int pid, void *data, int len)
{
	int err = _stp_ctrl_send(STP_REALTIME_DATA, data, len, pid);
	if (err < 0 && _stp_exit_called) {
		do {
			msleep (5);
			err = _stp_ctrl_send(STP_REALTIME_DATA, data, len, pid);
		} while (err < 0);
	}
	return err;
}

/** @} */
#endif /* _TRANSPORT_C_ */
