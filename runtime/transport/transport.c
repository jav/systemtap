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

#include "transport.h"
#include <linux/debugfs.h>
#include <linux/namei.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

static int _stp_exit_flag = 0;

static uid_t _stp_uid = 0;
static gid_t _stp_gid = 0;
static int _stp_pid = 0;

static int _stp_ctl_attached = 0;

static pid_t _stp_target = 0;
static int _stp_probes_started = 0;

#include "control.h"
#if STP_TRANSPORT_VERSION == 1
#include "relayfs.c"
#elif STP_TRANSPORT_VERSION == 2
#include "relay_v2.c"
#include "debugfs.c"
#elif STP_TRANSPORT_VERSION == 3
#include "ring_buffer.c"
#include "debugfs.c"
#else
#error "Unknown STP_TRANSPORT_VERSION"
#endif
#include "control.c"

static unsigned _stp_nsubbufs = 8;
static unsigned _stp_subbuf_size = 65536*4;

/* module parameters */
static int _stp_bufsize;
module_param(_stp_bufsize, int, 0);
MODULE_PARM_DESC(_stp_bufsize, "buffer size");

/* forward declarations */
static void probe_exit(void);
static int probe_start(void);

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
	static int _stp_exit_called = 0;

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

		dbug_trans(1, "*** calling _stp_transport_data_fs_stop ***\n");
		_stp_transport_data_fs_stop();

		dbug_trans(1, "ctl_send STP_EXIT\n");
		if (send_exit)
			_stp_ctl_send(STP_EXIT, NULL, 0);
		dbug_trans(1, "done with ctl_send STP_EXIT\n");
	}
}

static void _stp_request_exit(void)
{
	static int called = 0;
	if (!called) {
		/* we only want to do this once */
		called = 1;
		dbug_trans(1, "ctl_send STP_REQUEST_EXIT\n");
		_stp_ctl_send(STP_REQUEST_EXIT, NULL, 0);
		dbug_trans(1, "done with ctl_send STP_REQUEST_EXIT\n");
	}
}

/*
 * Called when stapio closes the control channel.
 */
static void _stp_detach(void)
{
	dbug_trans(1, "detach\n");
	_stp_ctl_attached = 0;
	_stp_pid = 0;

	if (!_stp_exit_flag)
		_stp_transport_data_fs_overwrite(1);

	cancel_delayed_work(&_stp_work);
	wake_up_interruptible(&_stp_ctl_wq);
}

/*
 * Called when stapio opens the control channel.
 */
static void _stp_attach(void)
{
	dbug_trans(1, "attach\n");
	_stp_ctl_attached = 1;
	_stp_pid = current->pid;
	_stp_transport_data_fs_overwrite(0);
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
		_stp_request_exit();
	if (likely(_stp_ctl_attached))
		queue_delayed_work(_stp_wq, &_stp_work, STP_WORK_TIMER);
}

/**
 *	_stp_transport_close - close ctl and relayfs channels
 *
 *	This is called automatically when the module is unloaded.
 *     
 */
static void _stp_transport_close(void)
{
	dbug_trans(1, "%d: ************** transport_close *************\n",
		   current->pid);
	_stp_cleanup_and_exit(0);
	destroy_workqueue(_stp_wq);
	_stp_unregister_ctl_channel();
	_stp_transport_fs_close();
	_stp_print_cleanup();	/* free print buffers */
	_stp_mem_debug_done();

	dbug_trans(1, "---- CLOSED ----\n");
}

/**
 * _stp_transport_init() is called from the module initialization.
 *   It does the bare minimum to exchange commands with staprun 
 */
static int _stp_transport_init(void)
{
	dbug_trans(1, "transport_init\n");
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

	if (_stp_transport_fs_init(THIS_MODULE->name) != 0)
		goto err0;

	/* create control channel */
	if (_stp_register_ctl_channel() < 0)
		goto err1;

	/* create print buffers */
	if (_stp_print_init() < 0)
		goto err2;

	/* start transport */
	_stp_transport_data_fs_start();

	/* create workqueue of kernel threads */
	_stp_wq = create_workqueue("systemtap");
	if (!_stp_wq)
		goto err3;
	
        /* Signal stapio to send us STP_START back (XXX: ?!?!?!).  */
	_stp_ctl_send(STP_TRANSPORT, NULL, 0);

	dbug_trans(1, "returning 0...\n");
	return 0;

err3:
	_stp_print_cleanup();
err2:
	_stp_unregister_ctl_channel();
err1:
	_stp_transport_fs_close();
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
#if STP_TRANSPORT_VERSION == 1
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
#if STP_TRANSPORT_VERSION == 1
		relayfs_remove_dir(_stp_lockfile);
#else
		debugfs_remove(_stp_lockfile);
#endif
		_stp_lockfile = NULL;
	}
}

static struct dentry *__stp_root_dir = NULL;

/* _stp_get_root_dir() - creates root directory or returns
 * a pointer to it if it already exists. */

static struct dentry *_stp_get_root_dir(void)
{
	struct file_system_type *fs;
	struct super_block *sb;
	const char *name = "systemtap";

	if (__stp_root_dir != NULL) {
		return __stp_root_dir;
	}

#if STP_TRANSPORT_VERSION == 1
	fs = get_fs_type("relayfs");
	if (!fs) {
		errk("Couldn't find relayfs filesystem.\n");
		return NULL;
	}
#else
	fs = get_fs_type("debugfs");
	if (!fs) {
		errk("Couldn't find debugfs filesystem.\n");
		return NULL;
	}
#endif

	if (!_stp_lock_transport_dir()) {
		errk("Couldn't lock transport directory.\n");
		return NULL;
	}
#if STP_TRANSPORT_VERSION == 1
	__stp_root_dir = relayfs_create_dir(name, NULL);
#else
	__stp_root_dir = debugfs_create_dir(name, NULL);
#endif
	if (!__stp_root_dir) {
		/* Couldn't create it because it is already there, so
		 * find it. */
		sb = list_entry(fs->fs_supers.next, struct super_block,
				s_instances);
		_stp_lock_inode(sb->s_root->d_inode);
		__stp_root_dir = lookup_one_len(name, sb->s_root,
						strlen(name));
		_stp_unlock_inode(sb->s_root->d_inode);
		if (!IS_ERR(__stp_root_dir))
			dput(__stp_root_dir);
		else {
			__stp_root_dir = NULL;
			errk("Could not create or find transport directory.\n");
		}
	}
	else if (IS_ERR(__stp_root_dir)) {
	    __stp_root_dir = NULL;
	    errk("Could not create root directory \"%s\", error %ld\n", name,
		 -PTR_ERR(__stp_root_dir));
	}

	_stp_unlock_transport_dir();
	return __stp_root_dir;
}

static void _stp_remove_root_dir(void)
{
	if (__stp_root_dir) {
		if (!_stp_lock_transport_dir()) {
			errk("Unable to lock transport directory.\n");
			return;
		}
		if (simple_empty(__stp_root_dir)) {
#if STP_TRANSPORT_VERSION == 1
			relayfs_remove_dir(__stp_root_dir);
#else
			debugfs_remove(__stp_root_dir);
#endif
		}
		_stp_unlock_transport_dir();
		__stp_root_dir = NULL;
	}
}

static struct dentry *__stp_module_dir = NULL;

static struct dentry *_stp_get_module_dir(void)
{
	return __stp_module_dir;
}

static int _stp_transport_fs_init(const char *module_name)
{
	struct dentry *root_dir;
    
	dbug_trans(1, "entry\n");
	if (module_name == NULL)
		return -1;

	root_dir = _stp_get_root_dir();
	if (root_dir == NULL)
		return -1;

#if STP_TRANSPORT_VERSION == 1
        __stp_module_dir = relayfs_create_dir(module_name, root_dir);
#else
        __stp_module_dir = debugfs_create_dir(module_name, root_dir);
#endif
        if (!__stp_module_dir) {
		errk("Could not create module directory \"%s\"\n",
		     module_name);
		_stp_remove_root_dir();
		return -1;
	}
	else if (IS_ERR(__stp_module_dir)) {
		errk("Could not create module directory \"%s\", error %ld\n",
		     module_name, -PTR_ERR(__stp_module_dir));
		_stp_remove_root_dir();
		return -1;
	}

	if (_stp_transport_data_fs_init() != 0) {
		_stp_remove_root_dir();
		return -1;
	}
	dbug_trans(1, "returning 0\n");
	return 0;
}

static void _stp_transport_fs_close(void)
{
	dbug_trans(1, "stp_transport_fs_close\n");

	_stp_transport_data_fs_close();

	if (__stp_module_dir) {
#if STP_TRANSPORT_VERSION == 1
		relayfs_remove_dir(__stp_module_dir);
#else
		debugfs_remove(__stp_module_dir);
#endif
		__stp_module_dir = NULL;
	}

	_stp_remove_root_dir();
}


#endif /* _TRANSPORT_C_ */
