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

#include <linux/delay.h>
#include "transport.h"
#include "control.h"
#include "relayfs.c"

enum _stp_tstate { STP_TRANS_NONE,
		   STP_TRANS_LOADED, /* module loaded */
		   STP_TRANS_PARAM,  /* parameters exchanged */
		   STP_TRANS_START   /* started */
};

enum _stp_tstate _stp_transport_state = STP_TRANS_NONE;

static struct rchan *_stp_chan;
static struct dentry *_stp_dir;
static int _stp_dpid;
static int _stp_pid;

module_param(_stp_pid, int, 0);
MODULE_PARM_DESC(_stp_pid, "daemon pid");

int _stp_target = 0;

/* forward declarations */
void probe_exit(void);
int probe_start(void);
void _stp_exit(void);

/*
 *	_stp_streaming - boolean, are we using 'streaming' output?
 */
static inline int _stp_streaming(void)
{
	if (_stp_transport_mode == STP_TRANSPORT_NETLINK)
		return 1;
	return 0;
}

/* send commands with timeout and retry */
int _stp_transport_send (int type, void *data, int len)
{
	int err, trylimit = 50;
	while ((err = _stp_ctrl_send(type, data, len, _stp_pid)) < 0 && trylimit--)
		msleep (5);
	return err;
}


/*
 *	_stp_handle_buf_info - handle STP_BUF_INFO
 */
static void _stp_handle_buf_info(int *cpuptr)
{
	struct buf_info out;
	out.cpu = *cpuptr;
	out.produced = atomic_read(&_stp_chan->buf[*cpuptr]->subbufs_produced);
	out.consumed = atomic_read(&_stp_chan->buf[*cpuptr]->subbufs_consumed);

	_stp_transport_send(STP_BUF_INFO, &out, sizeof(out));
}

/*
 *	_stp_handle_start - handle STP_START
 */
void _stp_handle_start (struct transport_start *st)
{
	int err;
	//printk ("stp_handle_start pid=%d\n", st->pid);
	err = probe_start();
	if (err >= 0)
		_stp_transport_state = STP_TRANS_START;
	else {
		st->pid = err;
		_stp_transport_send(STP_START, st, sizeof(*st));
	}
}

/**
 *	_stp_handle_subbufs_consumed - handle STP_SUBBUFS_CONSUMED
 */
static void _stp_handle_subbufs_consumed(int pid, struct consumed_info *info)
{
	relay_subbufs_consumed(_stp_chan, info->cpu, info->consumed);
}


int _stp_exit_called = 0;

static void _stp_cleanup_and_exit (int closing)
{
	int failures;

	if (!_stp_exit_called) {
		_stp_exit_called = 1;

		probe_exit();

		failures = atomic_read(&_stp_transport_failures);
		if (failures)
			_stp_warn ("There were %d transport failures.\n", failures);

		if (_stp_transport_mode == STP_TRANSPORT_RELAYFS)
			relay_flush(_stp_chan);

		 _stp_transport_send(STP_EXIT, &closing, sizeof(int));
	}
}

static void _stp_handle_exit (void *data);
static DECLARE_WORK(stp_exit, _stp_handle_exit, NULL);

/*
 *	_stp_handle_exit - handle STP_EXIT
 */
static void _stp_handle_exit (void *data)
{
	_stp_cleanup_and_exit(0);
}


/**
 *	_stp_transport_close - close netlink and relayfs channels
 *
 *	This must be called after all I/O is done, probably at the end
 *	of module cleanup.
 */
void _stp_transport_close()
{
	//printk("************** transport_close *************\n");
	_stp_cleanup_and_exit(1);

	if (_stp_transport_mode == STP_TRANSPORT_RELAYFS) 
		_stp_relayfs_close(_stp_chan, _stp_dir);

	_stp_ctrl_unregister(_stp_pid);
	//printk("---- CLOSED ----\n");
}



/**
 *	_stp_transport_open - open netlink and relayfs channels
 *      with proper parameters
 *	Returns negative on failure, 0 otherwise.
 *
 *	This function registers the probe with the control channel,
 *	and if the probe output will not be 'streaming', creates a
 *	relayfs channel for it.  This must be called before any I/O is
 *	done. 
 * 
 *      This function is called in response to an STP_TRANSPORT
 *      message from stpd cmd.  It replies with a similar message
 *      containing the final parameters used.
 */

int _stp_transport_open(struct transport_info *info)
{
	//printk ("stp_transport_open: %d bufs of %d bytes. target=%d\n", info->n_subbufs, info->subbuf_size, info->target);

	info->transport_mode = _stp_transport_mode;
	_stp_target = info->target;

	if (!_stp_streaming()) {

		/* if stpd specified subbufs, use those, otherwise use defaults */
		if (info->n_subbufs) {
			n_subbufs = info->n_subbufs;
			subbuf_size = info->subbuf_size;
		} else {
			info->n_subbufs = n_subbufs;
			info->subbuf_size = subbuf_size;
		}

		_stp_chan = _stp_relayfs_open(n_subbufs, subbuf_size, _stp_pid, &_stp_dir);
		if (!_stp_chan) {
			_stp_ctrl_unregister(_stp_pid);
			return -ENOMEM;
		}
	}

	/* send reply */
	return _stp_transport_send (STP_TRANSPORT_INFO, info, sizeof(*info));
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
		_stp_handle_buf_info(data);
		break;
	case STP_SUBBUFS_CONSUMED:
		_stp_handle_subbufs_consumed(pid, data);
		break;
	case STP_EXIT:
		_stp_handle_exit (data);
		break;
	case STP_TRANSPORT_INFO:
		_stp_transport_open (data);
		break;
	case STP_START:
		_stp_handle_start (data);
		break;
	default:
		err = -1;
		break;
	}
	
	return err;
}

/**
 * _stp_transport_init() is called from the module initialization.
 *   It does the bare minimum to exchange commands with stpd 
 */
int _stp_transport_init(void)
{
	//printk("transport_init from %ld %ld\n", (long)_stp_pid, (long)current->pid);

	_stp_ctrl_register(_stp_pid, _stp_cmd_handler);

	/* register procfs  here */

	return 0;
}

/* write DATA via netlink.  used for streaming mode only */
int _stp_netlink_write (void *data, int len)
{
	int err, trylimit;
	if (_stp_exit_called)
		trylimit = 50;
	else
		trylimit = 0;

	while ((err = _stp_ctrl_send(STP_REALTIME_DATA, data, len, _stp_pid)) < 0 && trylimit--)
		msleep (5);
	return err;
}

/* like relay_write except returns an error code */

static int _stp_relay_write (const void *data, unsigned length)
{
	unsigned long flags;
	struct rchan_buf *buf;

	if (unlikely(length == 0))
		return 0;

	local_irq_save(flags);
	buf = _stp_chan->buf[smp_processor_id()];
	if (unlikely(buf->offset + length > _stp_chan->subbuf_size))
		length = relay_switch_subbuf(buf, length);
	memcpy(buf->data + buf->offset, data, length);
	buf->offset += length;
	local_irq_restore(flags);
	
	if (unlikely(length == 0))
		return -1;
	
	return length;
}

#endif /* _TRANSPORT_C_ */
