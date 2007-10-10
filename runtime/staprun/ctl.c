/* -*- linux-c -*-
 *
 * ctl.c - staprun control channel
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2007 Red Hat Inc.
 */

#include "staprun.h"

int init_ctl_channel(void)
{
	char buf[PATH_MAX];
	struct statfs st;
	int old_transport = 0;

 	if (statfs("/sys/kernel/debug", &st) == 0 && (int) st.f_type == (int) DEBUGFS_MAGIC) {
		if (sprintf_chk(buf, "/sys/kernel/debug/systemtap/%s/cmd", modname))
			return -1;
	} else {
		old_transport = 1;
		if (sprintf_chk(buf, "/proc/systemtap/%s/cmd", modname))
			return -1;
	}
	
	dbug(2, "Opening %s\n", buf); 
	control_channel = open(buf, O_RDWR);
	if (control_channel < 0) {
		if (attach_mod && errno == ENOENT)
			err("ERROR: Can not attach. Module %s not running.\n", modname);
		else
			perr("Couldn't open control channel '%s'", buf);
		return -1;
	}
	if (set_clexec(control_channel) < 0)
		return -1;
	
	return old_transport;
}

void close_ctl_channel(void)
{
	if (control_channel > 0) {
		close(control_channel);
		control_channel = 0;
	}
}
