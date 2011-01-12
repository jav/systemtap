/* -*- linux-c -*- 
 * transport.c - stp transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Red Hat Inc, 2005-2010
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
#include <linux/delay.h>
#include <linux/mutex.h>

static int _stp_exit_flag = 0;

static uid_t _stp_uid = 0;
static gid_t _stp_gid = 0;
static int _stp_pid = 0;

static atomic_t _stp_ctl_attached = ATOMIC_INIT(0);

static pid_t _stp_target = 0;
static int _stp_probes_started = 0;
static int _stp_exit_called = 0;
static DEFINE_MUTEX(_stp_transport_mutex);

#ifndef STP_CTL_TIMER_INTERVAL
/* ctl timer interval in jiffies (default 20 ms) */
#define STP_CTL_TIMER_INTERVAL		((HZ+49)/50)
#endif


// For now, disable transport version 3 (unless STP_USE_RING_BUFFER is
// defined).
#if STP_TRANSPORT_VERSION == 3 && !defined(STP_USE_RING_BUFFER)
#undef STP_TRANSPORT_VERSION
#define STP_TRANSPORT_VERSION 2
#endif

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

struct timer_list _stp_ctl_work_timer;

/*
 *	_stp_handle_start - handle STP_START
 */

static void _stp_handle_start(struct _stp_msg_start *st)
{
	mutex_lock(&_stp_transport_mutex);
	if (!_stp_exit_called) {
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
	mutex_unlock(&_stp_transport_mutex);
}

/* common cleanup code. */
/* This is called from the kernel thread when an exit was requested */
/* by staprun or the exit() function. */
/* We need to call it both times because we want to clean up properly */
/* when someone does /sbin/rmmod on a loaded systemtap module. */
static void _stp_cleanup_and_exit(int send_exit)
{
	mutex_lock(&_stp_transport_mutex);
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
	mutex_unlock(&_stp_transport_mutex);
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
	_stp_pid = 0;

	if (!_stp_exit_flag)
		_stp_transport_data_fs_overwrite(1);

        del_timer_sync(&_stp_ctl_work_timer);
	wake_up_interruptible(&_stp_ctl_wq);
}


static void _stp_ctl_work_callback(unsigned long val);

/*
 * Called when stapio opens the control channel.
 */
static void _stp_attach(void)
{
	dbug_trans(1, "attach\n");
	_stp_pid = current->pid;
	_stp_transport_data_fs_overwrite(0);
	init_timer(&_stp_ctl_work_timer);
	_stp_ctl_work_timer.expires = jiffies + STP_CTL_TIMER_INTERVAL;
	_stp_ctl_work_timer.function = _stp_ctl_work_callback;
	_stp_ctl_work_timer.data= 0;
	add_timer(&_stp_ctl_work_timer);
}

/*
 *	_stp_ctl_work_callback - periodically check for IO or exit
 *	This is run by a kernel thread and may sleep.
 */
static void _stp_ctl_work_callback(unsigned long val)
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
	if (atomic_read(& _stp_ctl_attached))
                mod_timer (&_stp_ctl_work_timer, jiffies + STP_CTL_TIMER_INTERVAL);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
	mutex_lock(&inode->i_mutex);
#else
	down(&inode->i_sem);
#endif
}

static inline void _stp_unlock_inode(struct inode *inode)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
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
 * a pointer to it if it already exists.
 *
 * The caller *must* lock the transport directory.
 */

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

	return __stp_root_dir;
}

/* _stp_remove_root_dir() - removes root directory (if empty)
 *
 * The caller *must* lock the transport directory.
 */

static void _stp_remove_root_dir(void)
{
	if (__stp_root_dir) {
		if (simple_empty(__stp_root_dir)) {
#if STP_TRANSPORT_VERSION == 1
			relayfs_remove_dir(__stp_root_dir);
#else
			debugfs_remove(__stp_root_dir);
#endif
		}
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

	if (!_stp_lock_transport_dir()) {
		errk("Couldn't lock transport directory.\n");
		return -1;
	}

	root_dir = _stp_get_root_dir();
	if (root_dir == NULL) {
		_stp_unlock_transport_dir();
		return -1;
	}

#if STP_TRANSPORT_VERSION == 1
        __stp_module_dir = relayfs_create_dir(module_name, root_dir);
#else
        __stp_module_dir = debugfs_create_dir(module_name, root_dir);
#endif
        if (!__stp_module_dir) {
		errk("Could not create module directory \"%s\"\n",
		     module_name);
		_stp_remove_root_dir();
		_stp_unlock_transport_dir();
		return -1;
	}
	else if (IS_ERR(__stp_module_dir)) {
		errk("Could not create module directory \"%s\", error %ld\n",
		     module_name, -PTR_ERR(__stp_module_dir));
		_stp_remove_root_dir();
		_stp_unlock_transport_dir();
		return -1;
	}

	if (_stp_transport_data_fs_init() != 0) {
		_stp_remove_root_dir();
		_stp_unlock_transport_dir();
		return -1;
	}
	_stp_unlock_transport_dir();
	dbug_trans(1, "returning 0\n");
	return 0;
}

static void _stp_transport_fs_close(void)
{
	dbug_trans(1, "stp_transport_fs_close\n");

	_stp_transport_data_fs_close();

	if (__stp_module_dir) {
		if (!_stp_lock_transport_dir()) {
			errk("Couldn't lock transport directory.\n");
			return;
		}

#if STP_TRANSPORT_VERSION == 1
		relayfs_remove_dir(__stp_module_dir);
#else
		debugfs_remove(__stp_module_dir);
#endif
		__stp_module_dir = NULL;

		_stp_remove_root_dir();
		_stp_unlock_transport_dir();
	}
}


/* NB: Accessed from tzinfo.stp tapset */
static uint64_t tz_gmtoff;
static char tz_name[MAXSTRINGLEN];

static void _stp_handle_tzinfo (struct _stp_msg_tzinfo* tzi)
{
        tz_gmtoff = tzi->tz_gmtoff;
        strlcpy (tz_name, tzi->tz_name, MAXSTRINGLEN);
        /* We may silently truncate the incoming string,
         * for example if MAXSTRINGLEN < STP_TZ_NAME_LEN-1 */
}



#endif /* _TRANSPORT_C_ */
