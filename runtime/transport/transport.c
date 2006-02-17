#ifndef _TRANSPORT_TRANSPORT_C_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_C_

/*
 * transport.c - stp transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Red Hat Inc, 2005
 *
 * This file is released under the GPL.
 */

#include <linux/delay.h>
#include "transport.h"

#ifdef STP_RELAYFS
#include "relayfs.c"
static struct rchan *_stp_chan;
static struct dentry *_stp_dir;
#endif

static DECLARE_MUTEX(_stp_start_mutex);

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
int _stp_transport_open(struct transport_info *info);

#include "procfs.c"

/*
 *	_stp_streaming - boolean, are we using 'streaming' output?
 */
static inline int _stp_streaming(void)
{
	if (_stp_transport_mode == STP_TRANSPORT_PROC)
		return 1;
	return 0;
}

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
#ifdef RELAYFS_VERSION_GE_4
	out.produced = _stp_chan->buf[*cpuptr]->subbufs_produced;
	out.consumed = _stp_chan->buf[*cpuptr]->subbufs_consumed;
#else
	out.produced = atomic_read(&_stp_chan->buf[*cpuptr]->subbufs_produced);
	out.consumed = atomic_read(&_stp_chan->buf[*cpuptr]->subbufs_consumed);
#endif /* RELAYFS_VERSION_GE_4 */

	_stp_transport_send(STP_BUF_INFO, &out, sizeof(out));
}
#endif

/*
 *	_stp_handle_start - handle STP_START
 */
void _stp_handle_start (struct transport_start *st)
{
	kbug ("stp_handle_start pid=%d\n", st->pid);

	down (&_stp_start_mutex);
	st->pid = probe_start();
	up (&_stp_start_mutex);

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

static void _stp_cleanup_and_exit (int closing)
{
	int failures;

	kbug("cleanup_and_exit (%d)\n", closing);
	if (!_stp_exit_called) {
		_stp_exit_called = 1;

		probe_exit();

		failures = atomic_read(&_stp_transport_failures);
		if (failures)
			_stp_warn ("There were %d transport failures.\n", failures);

#ifdef STP_RELAYFS
		if (_stp_transport_mode == STP_TRANSPORT_RELAYFS) {
			relay_flush(_stp_chan);
		}
#endif
		kbug("SENDING STP_EXIT\n");
		_stp_transport_send(STP_EXIT, &closing, sizeof(int));
	}
}

/*
 *	_stp_work_queue - periodically check for IO or exit
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

	if (_stp_exit_flag) {
		cancel_delayed_work(&stp_exit);
		down (&_stp_start_mutex);
		_stp_cleanup_and_exit(0);
		up (&_stp_start_mutex);
		wake_up_interruptible(&_stp_proc_wq);
	} else
		schedule_delayed_work(&stp_exit, STP_WORK_TIMER);

}

/**
 *	_stp_transport_close - close proc and relayfs channels
 *
 *	This must be called after all I/O is done, probably at the end
 *	of module cleanup.
 */
void _stp_transport_close()
{
	kbug("************** transport_close *************\n");
	_stp_cleanup_and_exit(1);

#ifdef STP_RELAYFS
	if (_stp_transport_mode == STP_TRANSPORT_RELAYFS) 
		_stp_relayfs_close(_stp_chan, _stp_dir);
#endif

	ssleep(1);
	_stp_unregister_procfs();
	kbug("---- CLOSED ----\n");
}

/**
 *	_stp_transport_open - open proc and relayfs channels
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
	kbug ("stp_transport_open: %d Mb buffer. target=%d\n", info->buf_size, info->target);

	info->transport_mode = _stp_transport_mode;
	kbug("transport_mode=%d\n", info->transport_mode);
	_stp_target = info->target;

#ifdef STP_RELAYFS
	if (!_stp_streaming()) {
		if (info->buf_size) {
			unsigned size = info->buf_size * 1024 * 1024;
			subbuf_size = ((size >> 2) + 1) * 65536;
			n_subbufs = size / subbuf_size;
		}
		info->n_subbufs = n_subbufs;
		info->subbuf_size = subbuf_size;

		_stp_chan = _stp_relayfs_open(n_subbufs, subbuf_size, _stp_pid, &_stp_dir);
		if (!_stp_chan) {
			_stp_unregister_procfs();
			return -ENOMEM;
		}
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

	_stp_register_procfs();
	schedule_delayed_work(&stp_exit, STP_WORK_TIMER);
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
