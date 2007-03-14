/* -*- linux-c -*- 
 * transport.c - stp transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Red Hat Inc, 2005-2007
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
#include "symbols.c"

#ifdef STP_OLD_TRANSPORT
#include "relayfs.c"
#else
#include "utt.c"
#endif

static struct utt_trace *_stp_utt = NULL;
static unsigned int utt_seq = 1;

static int _stp_start_finished = 0;
static int _stp_probes_started = 0;

/* module parameters */
static int _stp_pid, _stp_bufsize;
module_param(_stp_pid, int, 0);
MODULE_PARM_DESC(_stp_pid, "pid");
module_param(_stp_bufsize, int, 0);
MODULE_PARM_DESC(_stp_bufsize, "buffer size");

pid_t _stp_target = 0;
static int _stp_exit_called = 0;
int _stp_exit_flag = 0;

/* forward declarations */
void probe_exit(void);
int probe_start(void);
void _stp_exit(void);
void _stp_handle_start (struct _stp_msg_start *st);


/* check for new workqueue API */
#ifdef DECLARE_DELAYED_WORK 
static void _stp_work_queue (struct work_struct *data);
static DECLARE_DELAYED_WORK(_stp_work, _stp_work_queue);
#else
static void _stp_work_queue (void *data);
static DECLARE_WORK(_stp_work, _stp_work_queue, NULL);
#endif

static struct workqueue_struct *_stp_wq;

#ifdef STP_OLD_TRANSPORT
#include "procfs.c"
#else
#include "control.c"
#endif

void _stp_ask_for_symbols(void)
{
	struct _stp_msg_symbol req;
	struct _stp_module mod;
	struct _stp_msg_trans tran;

	/* ask for symbols, modules, and send transport info */
	kbug("AFS\n");

	req.endian = 0x1234;
	req.ptr_size = sizeof(char *);
	_stp_ctl_send(STP_SYMBOLS, &req, sizeof(req));
	
	strcpy(mod.name, "");
	_stp_ctl_send(STP_MODULE, &mod, sizeof(mod));

#ifdef STP_BULKMODE
	tran.bulk_mode = 1;
#else
	tran.bulk_mode = 0;
#endif
	tran.subbuf_size =_stp_subbuf_size;
	tran.n_subbufs = _stp_nsubbufs;
	_stp_ctl_send(STP_TRANSPORT, &tran, sizeof(tran));
}

/*
 *	_stp_handle_start - handle STP_START
 */

void _stp_handle_start (struct _stp_msg_start *st)
{
	kbug ("stp_handle_start\n");

	if (register_module_notifier(&_stp_module_load_nb))
		errk("failed to load module notifier\n");

	_stp_target = st->target;
	st->res = probe_start();
	_stp_start_finished = 1;
	if (st->res >= 0)
		_stp_probes_started = 1;

	_stp_ctl_send(STP_START, st, sizeof(*st));
}


/* common cleanup code. */
/* This is called from the kernel thread when an exit was requested */
/* by staprun or the exit() function. It is also called by transport_close() */
/* when the module  is removed. In that case "dont_rmmod" is set to 1. */
/* We need to call it both times because we want to clean up properly */
/* when someone does /sbin/rmmod on a loaded systemtap module. */
static void _stp_cleanup_and_exit (int dont_rmmod)
{
	kbug("cleanup_and_exit (%d)\n", dont_rmmod);
	if (!_stp_exit_called) {
		int failures;

		unregister_module_notifier(&_stp_module_load_nb);

		/* we only want to do this stuff once */
		_stp_exit_called = 1;

		if (_stp_probes_started) {
			kbug("calling probe_exit\n");
			/* tell the stap-generated code to unload its probes, etc */
			probe_exit();
			kbug("done with probe_exit\n");
		}

		failures = atomic_read(&_stp_transport_failures);
		if (failures)
			_stp_warn ("There were %d transport failures.\n", failures);

		kbug("************** calling startstop 0 *************\n");
		if (_stp_utt) utt_trace_startstop(_stp_utt, 0, &utt_seq);

		kbug("ctl_send STP_EXIT\n");
		/* tell staprun to exit (if it is still there) */
		_stp_ctl_send(STP_EXIT, &dont_rmmod, sizeof(int));
		kbug("done with ctl_send STP_EXIT\n");
	}
}

/*
 *	_stp_work_queue - periodically check for IO or exit
 *	This is run by a kernel thread and may sleep.
 */
#ifdef DECLARE_DELAYED_WORK 
static void _stp_work_queue (struct work_struct *data)
#else
static void _stp_work_queue (void *data)
#endif
{
	int do_io = 0;
	unsigned long flags;

	spin_lock_irqsave(&_stp_ready_lock, flags);
	if (!list_empty(&_stp_ready_q))
		do_io = 1;
	spin_unlock_irqrestore(&_stp_ready_lock, flags);

	if (do_io)
		wake_up_interruptible(&_stp_ctl_wq);

	/* if exit flag is set AND we have finished with probe_start() */
	if (unlikely(_stp_exit_flag && _stp_start_finished)) {
		_stp_cleanup_and_exit(0);
		cancel_delayed_work(&_stp_work);
		flush_workqueue(_stp_wq);
		wake_up_interruptible(&_stp_ctl_wq);
	} else
		queue_delayed_work(_stp_wq, &_stp_work, STP_WORK_TIMER);
}

/**
 *	_stp_transport_close - close ctl and relayfs channels
 *
 *	This is called automatically when the module is unloaded.
 *     
 */
void _stp_transport_close()
{
	kbug("************** transport_close *************\n");
	_stp_cleanup_and_exit(1);
	cancel_delayed_work(&_stp_work);
	destroy_workqueue(_stp_wq);
	wake_up_interruptible(&_stp_ctl_wq);
	unregister_module_notifier(&_stp_module_load_nb);
	_stp_unregister_ctl_channel();
	if (_stp_utt) utt_trace_remove(_stp_utt);
	_stp_free_modules();
	_stp_kill_time();
	_stp_print_cleanup(); 	/* free print buffers */

	kbug("---- CLOSED ----\n");
}


static struct utt_trace *_stp_utt_open(void)
{
	struct utt_trace_setup utts;	
	sprintf(utts.root, "systemtap_%d", _stp_pid);
	utts.buf_size = _stp_subbuf_size;
	utts.buf_nr = _stp_nsubbufs;

#ifdef STP_BULKMODE
	utts.is_global = 0;
#else
	utts.is_global = 1;
#endif

	return utt_trace_setup(&utts);
}

/**
 * _stp_transport_init() is called from the module initialization.
 *   It does the bare minimum to exchange commands with staprun 
 */
int _stp_transport_init(void)
{
	int ret;

	kbug("transport_init from %ld %ld\n", (long)_stp_pid, (long)current->pid);

	if (_stp_bufsize) {
		unsigned size = _stp_bufsize * 1024 * 1024;
		_stp_subbuf_size = ((size >> 2) + 1) * 65536;
		_stp_nsubbufs = size / _stp_subbuf_size;
		kbug("Using %d subbufs of size %d\n", _stp_nsubbufs, _stp_subbuf_size);
	}

	/* initialize timer code */
	if (_stp_init_time())
		return -1;

#if !defined (STP_OLD_TRANSPORT) || defined (STP_BULKMODE)
	/* open utt (relayfs) channel to send data to userspace */
	_stp_utt = _stp_utt_open();
	if (!_stp_utt)
		goto err0;
#endif
	/* create debugfs/procfs control channel */
	if (_stp_register_ctl_channel() < 0)
		goto err1;

	/* create print buffers */
	if (_stp_print_init() < 0)
		goto err2;

	utt_trace_startstop(_stp_utt, 1, &utt_seq);

	/* create workqueue of kernel threads */
	_stp_wq = create_workqueue("systemtap");
	if (!_stp_wq)
		goto err3;

	queue_delayed_work(_stp_wq, &_stp_work, STP_WORK_TIMER);

	/* request symbolic information */
	_stp_ask_for_symbols();
	return 0;

err3:
	_stp_print_cleanup();
err2:
	_stp_unregister_ctl_channel();
err1:
	if (_stp_utt) utt_trace_remove(_stp_utt);	
err0:
	_stp_kill_time();
	return -1;
}

#endif /* _TRANSPORT_C_ */
