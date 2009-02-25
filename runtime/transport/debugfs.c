/* -*- linux-c -*-
 *
 * debugfs functions
 * Copyright (C) 2009 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/debugfs.h>
#include "transport.h"

#define STP_DEFAULT_BUFFERS 50

inline static int _stp_ctl_write_fs(int type, void *data, unsigned len)
{
	return 0;
}

static struct dentry *_stp_cmd_file = NULL;

static int _stp_register_ctl_channel_fs(void)
{
	struct dentry *module_dir = _stp_get_module_dir();
	if (module_dir == NULL) {
		errk("no module directory found.\n");
		return -1;
	}

	/* create [debugfs]/systemtap/module_name/.cmd  */
	_stp_cmd_file = debugfs_create_file(".cmd", 0600, module_dir,
					    NULL, &_stp_ctl_fops_cmd);
	if (_stp_cmd_file == NULL) {
		errk("Error creating systemtap debugfs entries.\n");
		return -1;
	}
	_stp_cmd_file->d_inode->i_uid = _stp_uid;
	_stp_cmd_file->d_inode->i_gid = _stp_gid;

	return 0;
}

static void _stp_unregister_ctl_channel_fs(void)
{
	if (_stp_cmd_file)
		debugfs_remove(_stp_cmd_file);
}
