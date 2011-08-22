/* -*- linux-c -*-
 *
 * /proc command channels
 * Copyright (C) 2007-2009 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_PROCFS_C_
#define _STP_PROCFS_C_

#if (!defined(STAPCONF_PATH_LOOKUP) && !defined(STAPCONF_KERN_PATH_PARENT) \
     && !defined(STAPCONF_VFS_PATH_LOOKUP))
#error "Either path_lookup(), kern_path_parent(), or vfs_path_lookup() must be exported by the kernel."
#endif

#ifdef STAPCONF_VFS_PATH_LOOKUP
#include <linux/pid_namespace.h>
#endif

/* The maximum number of files AND directories that can be opened.
 * It would be great if the translator would emit this based on the actual
 * number of needed files.
 */
#ifndef STP_MAX_PROCFS_FILES
#define STP_MAX_PROCFS_FILES 16
#endif

#if defined(STAPCONF_PATH_LOOKUP) && !defined(STAPCONF_KERN_PATH_PARENT)
#define kern_path_parent(name, nameidata) \
	path_lookup(name, LOOKUP_PARENT, nameidata)
#endif

static int _stp_num_pde = 0;
static struct proc_dir_entry *_stp_pde[STP_MAX_PROCFS_FILES];
static struct proc_dir_entry *_stp_procfs_files[STP_MAX_PROCFS_FILES];
static struct proc_dir_entry *_stp_proc_stap = NULL;
static struct proc_dir_entry *_stp_proc_root = NULL;

static void _stp_close_procfs(void);

// 2.6.24 fixed proc_dir_entry refcounting.
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
#define LAST_ENTRY_COUNT 0
#else
#define LAST_ENTRY_COUNT 1
#endif

/*
 * Removes /proc/systemtap/{module_name} and /proc/systemtap (if empty)
 */
static void _stp_rmdir_proc_module(void)
{
	if (!_stp_lock_transport_dir()) {
		errk("Unable to lock transport directory.\n");
		return;
	}

        if (_stp_proc_root && _stp_proc_root->subdir == NULL) {
		if (atomic_read(&_stp_proc_root->count) != LAST_ENTRY_COUNT)
			_stp_warn("Removal of /proc/systemtap/%s\nis deferred until it is no longer in use.\n"
				  "Systemtap module removal will block.\n", THIS_MODULE->name);	
		remove_proc_entry(THIS_MODULE->name, _stp_proc_stap);
		_stp_proc_root = NULL;
	}

	if (_stp_proc_stap && _stp_proc_stap->subdir == NULL) {
		/* Important! Do not attempt removal of
		 * /proc/systemtap if in use.  This will put the PDE
		 * in deleted state pending usage count dropping to
		 * 0. During this time, kern_path_parent() will still
		 * find it and allow new modules to use it, even
		 * though it will not show up in directory
		 * listings. */

 		if (atomic_read(&_stp_proc_stap->count) == LAST_ENTRY_COUNT) {
			remove_proc_entry("systemtap", NULL);
			_stp_proc_stap = NULL;
		}
	}
	_stp_unlock_transport_dir();
}


/*
 * Safely creates /proc/systemtap (if necessary) and
 * /proc/systemtap/{module_name}.
 */
static int _stp_mkdir_proc_module(void)
{	
        if (_stp_proc_root == NULL) {
#if defined(STAPCONF_PATH_LOOKUP) || defined(STAPCONF_KERN_PATH_PARENT)
		struct nameidata nd;

		if (!_stp_lock_transport_dir()) {
			errk("Unable to lock transport directory.\n");
			goto done;
		}
		
		/* We use kern_path_parent() because there is no
		 * lookup function for procfs we can call directly.
		 * And proc_mkdir() will always succeed, creating
		 * multiple directory entries, all with the same
		 * name.
		 *
		 * Why "/proc/systemtap/foo"?  kern_path_parent() is
		 * basically the same thing as calling the old
		 * path_lookup() with flags set to LOOKUP_PARENT,
		 * which means to look up the parent of the path,
		 * which in this case is "/proc/systemtap". */

		if (kern_path_parent("/proc/systemtap/foo", &nd)) {
			/* doesn't exist, so create it */
			_stp_proc_stap = proc_mkdir ("systemtap", NULL);
			if (_stp_proc_stap == NULL) {
				_stp_unlock_transport_dir();
				goto done;
			}
		} else {
                        #ifdef STAPCONF_NAMEIDATA_CLEANUP
                        _stp_proc_stap = PDE(nd.path.dentry->d_inode);
                        path_put (&nd.path);

                        #else
			_stp_proc_stap = PDE(nd.dentry->d_inode);
			path_release (&nd);
			#endif
		}
#else  /* STAPCONF_VFS_PATH_LOOKUP */
		struct path path;
		struct vfsmount *mnt;
		int rc;

		if (!_stp_lock_transport_dir()) {
			errk("Unable to lock transport directory.\n");
			goto done;
		}

		/* See if '/proc/systemtap' exists. */
		if (! init_pid_ns.proc_mnt) {
			_stp_unlock_transport_dir();
			goto done;
		}
		mnt = init_pid_ns.proc_mnt;
		rc = vfs_path_lookup(mnt->mnt_root, mnt, "systemtap", 0,
				     &path);

		/* If '/proc/systemtap' exists, update
		 * _stp_proc_stap.  Otherwise create the directory. */
		if (rc == 0) {
                        _stp_proc_stap = PDE(path.dentry->d_inode);
                        path_put (&path);
		}
		else if (rc == -ENOENT) {
			_stp_proc_stap = proc_mkdir ("systemtap", NULL);
			if (_stp_proc_stap == NULL) {
				_stp_unlock_transport_dir();
				goto done;
			}
		}
#endif	/* STAPCONF_VFS_PATH_LOOKUP */

		/* Now that we've found '/proc/systemtap', create the
		 * module specific directory
		 * '/proc/systemtap/MODULE_NAME'. */
		_stp_proc_root = proc_mkdir(THIS_MODULE->name, _stp_proc_stap);
#ifdef STAPCONF_PROCFS_OWNER
		if (_stp_proc_root != NULL)
			_stp_proc_root->owner = THIS_MODULE;
#endif

		_stp_unlock_transport_dir();
	}
done:
	return (_stp_proc_root) ? 1 : 0;
}

/*
 * This checks our local cache to see if we already made the dir.
 */
static struct proc_dir_entry *_stp_procfs_lookup(const char *dir, struct proc_dir_entry *parent)
{
	int i;
	for (i = 0; i <_stp_num_pde; i++) {
		struct proc_dir_entry *pde = _stp_pde[i];
		if (pde->parent == parent && !strcmp(dir, pde->name))
			return pde;
	}
	return NULL;
}

static int _stp_create_procfs(const char *path, int num,
			      const struct file_operations *fops, int perm) 
{  
	const char *p; char *next;
	struct proc_dir_entry *last_dir, *de;

	if (num >= STP_MAX_PROCFS_FILES) {
		_stp_error("Requested file number %d is larger than max (%d)\n", 
			   num, STP_MAX_PROCFS_FILES);
		return -1;
	}

	_stp_mkdir_proc_module();
	last_dir = _stp_proc_root;

	/* if no path, use default one */
	if (strlen(path) == 0)
		p = "command";
	else
		p = path;
	
	while ((next = strchr(p, '/'))) {
		if (_stp_num_pde == STP_MAX_PROCFS_FILES)
			goto too_many;
		*next = 0;
		de = _stp_procfs_lookup(p, last_dir);
		if (de == NULL) {
			    last_dir = proc_mkdir(p, last_dir);
			    if (!last_dir) {
				    _stp_error("Could not create directory \"%s\"\n", p);
				    goto err;
			    }
			    _stp_pde[_stp_num_pde++] = last_dir;
#ifdef STAPCONF_PROCFS_OWNER
			    last_dir->owner = THIS_MODULE;
#endif
			    last_dir->uid = _stp_uid;
			    last_dir->gid = _stp_gid;
		} else {
			if (!S_ISDIR(de->mode)) {
				_stp_error("Could not create directory \"%s\"\n", p);
				goto err;
			}
			last_dir = de;
		}
		p = next + 1;
	}
	
	if (_stp_num_pde == STP_MAX_PROCFS_FILES)
		goto too_many;
	
	de = proc_create(p, perm, last_dir, fops);

	if (de == NULL) {
		_stp_error("Could not create file \"%s\" in path \"%s\"\n", p, path);
		goto err;
	}
#ifdef STAPCONF_PROCFS_OWNER
	de->owner = THIS_MODULE;
#endif
	de->uid = _stp_uid;
	de->gid = _stp_gid;
	_stp_pde[_stp_num_pde++] = de;
	_stp_procfs_files[num] = de;
	return 0;
	
too_many:
	_stp_error("Attempted to open too many procfs files. Maximum is %d\n", STP_MAX_PROCFS_FILES);
err:
	_stp_close_procfs();
	return -1;
}

static void _stp_close_procfs(void)
{
	int i;
	for (i = _stp_num_pde-1; i >= 0; i--) {
		struct proc_dir_entry *pde = _stp_pde[i];
		remove_proc_entry(pde->name, pde->parent);
	}
	_stp_num_pde = 0;
	_stp_rmdir_proc_module();
}

#endif	/* _STP_PROCFS_C_ */
