/* -*- linux-c -*- 
 * transport.c - stp transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Red Hat Inc, 2005, 2006
 * Copyright (C) Intel Corporation, 2006
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _TRANSPORT_TRANSPORT_C_
#define _TRANSPORT_TRANSPORT_C_

#include <linux/delay.h>
#include "transport.h"
#include "time.c"

#ifdef STP_RELAYFS
#include "relayfs.c"
static struct rchan *_stp_chan;
static struct dentry *_stp_dir;
int _stp_relay_flushing = 0;
#endif

static atomic_t _stp_start_finished = ATOMIC_INIT (0);
static int _stp_dpid;
static int _stp_pid;

module_param(_stp_pid, int, 0);
MODULE_PARM_DESC(_stp_pid, "daemon pid");

int _stp_target = 0;
int _stp_exit_called = 0;
int _stp_exit_flag = 0;

/* forward declarations */
void probe_exit(void);
int probe_start(void);
void _stp_exit(void);
void _stp_handle_start (struct transport_start *st);
static void _stp_work_queue (void *data);
static DECLARE_WORK(stp_exit, _stp_work_queue, NULL);
static struct workqueue_struct *_stp_wq;
int _stp_transport_open(struct transport_info *info);

#include "procfs.c"

/* send commands with timeout and retry */
int _stp_transport_send (int type, void *data, int len)
{
	int err, trylimit = 50;
	while ((err = _stp_write(type, data, len)) < 0 && trylimit--)
		msleep (5);
	return err;
}

#ifndef STP_RELAYFS
int _stp_transport_write (void *data, int len)  
{
	/* when _stp_exit_called is set, we are in probe_exit() and we can sleep */
	if (_stp_exit_called)
		return _stp_transport_send (STP_REALTIME_DATA, data, len);
	return _stp_write(STP_REALTIME_DATA, data, len);
}
#endif /*STP_RELAYFS */

/*
 *	_stp_handle_buf_info - handle STP_BUF_INFO
 */
#ifdef STP_RELAYFS
static void _stp_handle_buf_info(int *cpuptr)
{
	struct buf_info out;

	out.cpu = *cpuptr;
#if (RELAYFS_CHANNEL_VERSION >= 4) || defined (CONFIG_RELAY)
	out.produced = _stp_chan->buf[*cpuptr]->subbufs_produced;
	out.consumed = _stp_chan->buf[*cpuptr]->subbufs_consumed;
#else
	out.produced = atomic_read(&_stp_chan->buf[*cpuptr]->subbufs_produced);
	out.consumed = atomic_read(&_stp_chan->buf[*cpuptr]->subbufs_consumed);
#endif /* RELAYFS_CHANNEL_VERSION >=_4 || CONFIG_RELAY */

	_stp_transport_send(STP_BUF_INFO, &out, sizeof(out));
}
#endif

/*
 *	_stp_handle_start - handle STP_START
 */
void _stp_handle_start (struct transport_start *st)
{
	int ret;
	kbug ("stp_handle_start pid=%d\n", st->pid);

	ret = _stp_init_time();

	/* note: st->pid is actually the return code for the reply packet */
	st->pid = unlikely(ret) ? ret : probe_start();
	atomic_set(&_stp_start_finished,1);

	/* if probe_start() failed, suppress calling probe_exit() */
	if (st->pid < 0)
		_stp_exit_called = 1;

	_stp_transport_send(STP_START, st, sizeof(*st));
}

#ifdef STP_RELAYFS
/**
 *	_stp_handle_subbufs_consumed - handle STP_SUBBUFS_CONSUMED
 */
static void _stp_handle_subbufs_consumed(int pid, struct consumed_info *info)
{
	relay_subbufs_consumed(_stp_chan, info->cpu, info->consumed);
}
#endif

/* common cleanup code. */
/* This is called from the kernel thread when an exit was requested */
/* by stpd or the exit() function. It is also called by transport_close() */
/* when the module  is removed. In that case "dont_rmmod" is set to 1. */
/* We need to call it both times because we want to clean up properly */
/* when someone does /sbin/rmmod on a loaded systemtap module. */
static void _stp_cleanup_and_exit (int dont_rmmod)
{
	kbug("cleanup_and_exit (%d)\n", dont_rmmod);
	if (!_stp_exit_called) {
		int failures;

		/* we only want to do this stuff once */
		_stp_exit_called = 1;

		kbug("calling probe_exit\n");
		/* tell the stap-generated code to unload its probes, etc */
		probe_exit();
		kbug("done with probe_exit\n");

		failures = atomic_read(&_stp_transport_failures);
		if (failures)
			_stp_warn ("There were %d transport failures.\n", failures);

#ifdef STP_RELAYFS
		if (_stp_transport_mode == STP_TRANSPORT_RELAYFS) {
			_stp_relay_flushing = 1;
			relay_flush(_stp_chan);
		}
#endif
		kbug("transport_send STP_EXIT\n");
		/* tell stpd to exit (if it is still there) */
		_stp_transport_send(STP_EXIT, &dont_rmmod, sizeof(int));
		kbug("done with transport_send STP_EXIT\n");

		_stp_kill_time();

		/* free print buffers */
		_stp_print_cleanup();
	}
}

/*
 *	_stp_work_queue - periodically check for IO or exit
 *	This is run by a kernel thread and may sleep.
 */
static void _stp_work_queue (void *data)
{
	int do_io = 0;
	unsigned long flags;

	spin_lock_irqsave(&_stp_ready_lock, flags);
	if (!list_empty(&_stp_ready_q))
		do_io = 1;
	spin_unlock_irqrestore(&_stp_ready_lock, flags);

	if (do_io)
		wake_up_interruptible(&_stp_proc_wq);

	/* if exit flag is set AND we have finished with probe_start() */
	if (unlikely(_stp_exit_flag && atomic_read(&_stp_start_finished))) {
		_stp_cleanup_and_exit(0);
		cancel_delayed_work(&stp_exit);
		flush_workqueue(_stp_wq);
		wake_up_interruptible(&_stp_proc_wq);
	} else
		queue_delayed_work(_stp_wq, &stp_exit, STP_WORK_TIMER);
}

/**
 *	_stp_transport_close - close proc and relayfs channels
 *
 *	This is called automatically when the module is unloaded.
 *     
 */
void _stp_transport_close()
{
	kbug("************** transport_close *************\n");
	_stp_cleanup_and_exit(1);
	cancel_delayed_work(&stp_exit);
	destroy_workqueue(_stp_wq);
	wake_up_interruptible(&_stp_proc_wq);
#ifdef STP_RELAYFS
	if (_stp_transport_mode == STP_TRANSPORT_RELAYFS) 
		_stp_relayfs_close(_stp_chan, _stp_dir);
#endif
	_stp_unregister_procfs();
	kbug("---- CLOSED ----\n");
}

/**
 *	_stp_transport_open - open proc and relayfs channels
 *      with proper parameters
 *	Returns negative on failure, >0 otherwise.
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
	kbug ("stp_transport_open: %d Mb buffer. target=%d\n", info->buf_size, info->target);

	info->transport_mode = _stp_transport_mode;
	info->merge = 0;

	kbug("transport_mode=%d\n", info->transport_mode);
	_stp_target = info->target;

#ifdef STP_RELAYFS
	if (_stp_transport_mode == STP_TRANSPORT_RELAYFS) {
		if (info->buf_size) {
			unsigned size = info->buf_size * 1024 * 1024;
			subbuf_size = ((size >> 2) + 1) * 65536;
			n_subbufs = size / subbuf_size;
		}
		info->n_subbufs = n_subbufs;
		info->subbuf_size = subbuf_size;

#ifdef STP_RELAYFS_MERGE
		info->merge = 1;
#endif

		_stp_chan = _stp_relayfs_open(n_subbufs, subbuf_size, _stp_pid, &_stp_dir);

		if (!_stp_chan)
			return -ENOMEM;
		kbug ("stp_transport_open: %u Mb buffers, subbuf_size=%u, n_subbufs=%u\n",
		      info->buf_size, subbuf_size, n_subbufs);
	} else 
#endif
	{
		if (info->buf_size) 
			_stp_set_buffers(info->buf_size * 1024 * 1024 / STP_BUFFER_SIZE);
	}

	/* send reply */
	return _stp_transport_send (STP_TRANSPORT_INFO, info, sizeof(*info));
}

/**
 * _stp_transport_init() is called from the module initialization.
 *   It does the bare minimum to exchange commands with stpd 
 */
int _stp_transport_init(void)
{
	kbug("transport_init from %ld %ld\n", (long)_stp_pid, (long)current->pid);

	/* create print buffers */
	if (_stp_print_init() < 0)
		return -1;

	/* set up procfs communications */
	if (_stp_register_procfs() < 0)
		return -1;

	/* create workqueue of kernel threads */
	_stp_wq = create_workqueue("systemtap");
	queue_delayed_work(_stp_wq, &stp_exit, STP_WORK_TIMER);

	return 0;
}


/* like relay_write except returns an error code */

#ifdef STP_RELAYFS
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
#endif

#endif /* _TRANSPORT_C_ */
