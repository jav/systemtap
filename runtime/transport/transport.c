/* -*- linux-c -*- 
 * transport.c - stp transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Red Hat Inc, 2005-2009
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
#include <linux/namei.h>
#include "transport.h"
#include "time.c"
#include "../mempool.c"
#include "symbols.c"

static struct utt_trace *_stp_utt = NULL;
static unsigned int utt_seq = 1;
static int _stp_probes_started = 0;
static pid_t _stp_target = 0;
static int _stp_exit_called = 0;
static int _stp_exit_flag = 0;
#include "control.h"
#ifdef STP_OLD_TRANSPORT
#include "relayfs.c"
#include "procfs.c"
#else
#include "utt.c"
#include "debugfs.c"
#endif
#include "control.c"

/* module parameters */
static int _stp_bufsize;
module_param(_stp_bufsize, int, 0);
MODULE_PARM_DESC(_stp_bufsize, "buffer size");

/* forward declarations */
static void probe_exit(void);
static int probe_start(void);
static void _stp_exit(void);

/* check for new workqueue API */
#ifdef DECLARE_DELAYED_WORK
static void _stp_work_queue(struct work_struct *data);
static DECLARE_DELAYED_WORK(_stp_work, _stp_work_queue);
#else
static void _stp_work_queue(void *data);
static DECLARE_WORK(_stp_work, _stp_work_queue, NULL);
#endif

static struct workqueue_struct *_stp_wq;

/*
 *	_stp_handle_start - handle STP_START
 */

static void _stp_handle_start(struct _stp_msg_start *st)
{
	dbug_trans(1, "stp_handle_start\n");

#ifdef STAPCONF_VM_AREA
        { /* PR9740: workaround for kernel valloc bug. */
                void *dummy;
                dummy = alloc_vm_area (PAGE_SIZE);
                free_vm_area (dummy);
        }
#endif

	_stp_target = st->target;
	st->res = probe_start();
	if (st->res >= 0)
		_stp_probes_started = 1;

	_stp_ctl_send(STP_START, st, sizeof(*st));
}

/* common cleanup code. */
/* This is called from the kernel thread when an exit was requested */
/* by staprun or the exit() function. */
/* We need to call it both times because we want to clean up properly */
/* when someone does /sbin/rmmod on a loaded systemtap module. */
static void _stp_cleanup_and_exit(int send_exit)
{
	if (!_stp_exit_called) {
		int failures;

                dbug_trans(1, "cleanup_and_exit (%d)\n", send_exit);
		_stp_exit_flag = 1;
		/* we only want to do this stuff once */
		_stp_exit_called = 1;

		if (_stp_probes_started) {
			dbug_trans(1, "calling probe_exit\n");
			/* tell the stap-generated code to unload its probes, etc */
			probe_exit();
			dbug_trans(1, "done with probe_exit\n");
		}

		failures = atomic_read(&_stp_transport_failures);
		if (failures)
			_stp_warn("There were %d transport failures.\n", failures);

		dbug_trans(1, "************** calling startstop 0 *************\n");
		if (_stp_utt)
			utt_trace_startstop(_stp_utt, 0, &utt_seq);

		dbug_trans(1, "ctl_send STP_EXIT\n");
		if (send_exit)
			_stp_ctl_send(STP_EXIT, NULL, 0);
		dbug_trans(1, "done with ctl_send STP_EXIT\n");
	}
}

/*
 * Called when stapio closes the control channel.
 */
static void _stp_detach(void)
{
	dbug_trans(1, "detach\n");
	_stp_attached = 0;
	_stp_pid = 0;

	if (!_stp_exit_flag)
		utt_set_overwrite(1);

	cancel_delayed_work(&_stp_work);
	wake_up_interruptible(&_stp_ctl_wq);
}

/*
 * Called when stapio opens the control channel.
 */
static void _stp_attach(void)
{
	dbug_trans(1, "attach\n");
	_stp_attached = 1;
	_stp_pid = current->pid;
	utt_set_overwrite(0);
	queue_delayed_work(_stp_wq, &_stp_work, STP_WORK_TIMER);
}

/*
 *	_stp_work_queue - periodically check for IO or exit
 *	This is run by a kernel thread and may sleep.
 */
#ifdef DECLARE_DELAYED_WORK
static void _stp_work_queue(struct work_struct *data)
#else
static void _stp_work_queue(void *data)
#endif
{
	int do_io = 0;
	unsigned long flags;

	spin_lock_irqsave(&_stp_ctl_ready_lock, flags);
	if (!list_empty(&_stp_ctl_ready_q))
		do_io = 1;
	spin_unlock_irqrestore(&_stp_ctl_ready_lock, flags);
	if (do_io)
		wake_up_interruptible(&_stp_ctl_wq);

	/* if exit flag is set AND we have finished with probe_start() */
	if (unlikely(_stp_exit_flag && _stp_probes_started))
		_stp_cleanup_and_exit(1);
	if (likely(_stp_attached))
		queue_delayed_work(_stp_wq, &_stp_work, STP_WORK_TIMER);
}

/**
 *	_stp_transport_close - close ctl and relayfs channels
 *
 *	This is called automatically when the module is unloaded.
 *     
 */
static void _stp_transport_close()
{
	dbug_trans(1, "%d: ************** transport_close *************\n", current->pid);
	_stp_cleanup_and_exit(0);
	destroy_workqueue(_stp_wq);
	_stp_unregister_ctl_channel();
	if (_stp_utt)
		utt_trace_remove(_stp_utt);
	_stp_print_cleanup();	/* free print buffers */
	_stp_mem_debug_done();
	dbug_trans(1, "---- CLOSED ----\n");
}

static struct utt_trace *_stp_utt_open(void)
{
	struct utt_trace_setup utts;
	strlcpy(utts.root, "systemtap", sizeof(utts.root));
	strlcpy(utts.name, THIS_MODULE->name, sizeof(utts.name));
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
static int _stp_transport_init(void)
{
	dbug_trans(1, "transport_init\n");
	_stp_init_pid = current->pid;
#ifdef STAPCONF_TASK_UID
	_stp_uid = current->uid;
	_stp_gid = current->gid;
#else
	_stp_uid = current_uid();
	_stp_gid = current_gid();
#endif

#ifdef RELAY_GUEST
	/* Guest scripts use relay only for reporting warnings and errors */
	_stp_subbuf_size = 65536;
	_stp_nsubbufs = 2;
#endif

	if (_stp_bufsize) {
		unsigned size = _stp_bufsize * 1024 * 1024;
		_stp_subbuf_size = 65536;
		while (size / _stp_subbuf_size > 64 &&
		       _stp_subbuf_size < 1024 * 1024) {
			_stp_subbuf_size <<= 1;
		}
		_stp_nsubbufs = size / _stp_subbuf_size;
		dbug_trans(1, "Using %d subbufs of size %d\n", _stp_nsubbufs, _stp_subbuf_size);
	}

#if !defined (STP_OLD_TRANSPORT) || defined (STP_BULKMODE)
	/* open utt (relayfs) channel to send data to userspace */
	_stp_utt = _stp_utt_open();
	if (!_stp_utt)
		goto err0;
#endif

	/* create control channel */
	if (_stp_register_ctl_channel() < 0)
		goto err1;

	/* create print buffers */
	if (_stp_print_init() < 0)
		goto err2;

	/* start transport */
	utt_trace_startstop(_stp_utt, 1, &utt_seq);

	/* create workqueue of kernel threads */
	_stp_wq = create_workqueue("systemtap");
	if (!_stp_wq)
		goto err3;
	
	_stp_transport_state = 1;

        /* Signal stapio to send us STP_START back (XXX: ?!?!?!).  */
	_stp_ctl_send(STP_TRANSPORT, NULL, 0);

	return 0;

err3:
	_stp_print_cleanup();
err2:
	_stp_unregister_ctl_channel();
err1:
	if (_stp_utt)
		utt_trace_remove(_stp_utt);
err0:
	return -1;
}

static inline void _stp_lock_inode(struct inode *inode)
{
#ifdef DEFINE_MUTEX
	mutex_lock(&inode->i_mutex);
#else
	down(&inode->i_sem);
#endif
}

static inline void _stp_unlock_inode(struct inode *inode)
{
#ifdef DEFINE_MUTEX
	mutex_unlock(&inode->i_mutex);
#else
	up(&inode->i_sem);
#endif
}

static struct dentry *_stp_lockfile = NULL;

static int _stp_lock_transport_dir(void)
{
	int numtries = 0;
#ifdef STP_OLD_TRANSPORT
	while ((_stp_lockfile = relayfs_create_dir("systemtap_lock", NULL)) == NULL) {
#else
	while ((_stp_lockfile = debugfs_create_dir("systemtap_lock", NULL)) == NULL) {
#endif
		if (numtries++ >= 50)
			return 0;
		msleep(50);
	}
	return 1;
}

static void _stp_unlock_transport_dir(void)
{
	if (_stp_lockfile) {
#ifdef STP_OLD_TRANSPORT
		relayfs_remove_dir(_stp_lockfile);
#else
		debugfs_remove(_stp_lockfile);
#endif
		_stp_lockfile = NULL;
	}
}

/* _stp_get_root_dir(name) - creates root directory 'name' or */
/* returns a pointer to it if it already exists. Used in */
/* utt.c and relayfs.c. Will not be necessary if utt is included */
/* in the kernel. */

static struct dentry *_stp_get_root_dir(const char *name)
{
	struct file_system_type *fs;
	struct dentry *root;
	struct super_block *sb;

#ifdef STP_OLD_TRANSPORT
	fs = get_fs_type("relayfs");
#else
	fs = get_fs_type("debugfs");
#endif
	if (!fs) {
		errk("Couldn't find debugfs or relayfs filesystem.\n");
		return NULL;
	}

	if (!_stp_lock_transport_dir()) {
		errk("Couldn't lock transport directory.\n");
		return NULL;
	}
#ifdef STP_OLD_TRANSPORT
	root = relayfs_create_dir(name, NULL);
#else
	root = debugfs_create_dir(name, NULL);
#endif
	if (!root) {
		/* couldn't create it because it is already there, so find it. */
		sb = list_entry(fs->fs_supers.next, struct super_block, s_instances);
		_stp_lock_inode(sb->s_root->d_inode);
		root = lookup_one_len(name, sb->s_root, strlen(name));
		_stp_unlock_inode(sb->s_root->d_inode);
		if (!IS_ERR(root))
			dput(root);
		else {
			root = NULL;
			errk("Could not create or find transport directory.\n");
		}
	}
	_stp_unlock_transport_dir();
	return root;
}

#endif /* _TRANSPORT_C_ */
