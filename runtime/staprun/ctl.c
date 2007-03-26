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
	char buf[128];
	struct statfs st;

 	if (statfs("/sys/kernel/debug", &st) == 0 && (int) st.f_type == (int) DEBUGFS_MAGIC)
 		sprintf (buf, "/sys/kernel/debug/systemtap/%s/cmd", modname);
	else
		sprintf (buf, "/proc/systemtap/%s/cmd", modname);

	dbug("Opening %s\n", buf); 
	control_channel = open(buf, O_RDWR);
	if (control_channel < 0) {
		if (attach_mod) 
			fprintf (stderr, "ERROR: Cannot connect to module \"%s\".\n", modname);
		else
			fprintf (stderr, "ERROR: couldn't open control channel %s\n", buf);
		fprintf (stderr, "errcode = %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

void close_ctl_channel(void)
{
	if (control_channel > 0) {
		close(control_channel);
		control_channel = 0;
	}
}
