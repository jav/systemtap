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

int init_ctl_channel(const char *name, int verb)
{
	char buf[PATH_MAX];
	struct statfs st;
	int old_transport = 0;

	if (statfs("/sys/kernel/debug", &st) == 0 && (int)st.f_type == (int)DEBUGFS_MAGIC) {
		if (sprintf_chk(buf, "/sys/kernel/debug/systemtap/%s/.cmd", name))
			return -1;
	} else {
		old_transport = 1;
		if (sprintf_chk(buf, "/proc/systemtap/%s/.cmd", name))
			return -2;
	}

	control_channel = open(buf, O_RDWR);
	dbug(2, "Opened %s (%d)\n", buf, control_channel);

	/* NB: Even if open() succeeded with effective-UID permissions, we
	 * need the access() check to make sure real-UID permissions are also
	 * sufficient.  When we run under the setuid staprun, effective and
	 * real UID may not be the same.
	 *
	 * The access() is done *after* open() to avoid any TOCTOU-style race
	 * condition.  We believe it's probably safe either way, as the file
	 * we're trying to access connot be modified by a typical user, but
	 * better safe than sorry.
	 */
	if (access(buf, R_OK|W_OK) != 0){
		close(control_channel);
		return -5;
	}

	if (control_channel < 0) {
		if (verb) {
			if (attach_mod && errno == ENOENT)
				err(_("ERROR: Can not attach. Module %s not running.\n"), name);
			else
				perr(_("Couldn't open control channel '%s'"), buf);
		}
		return -3;
	}
	if (set_clexec(control_channel) < 0)
		return -4;

	return old_transport;
}

void close_ctl_channel(void)
{
  if (control_channel >= 0) {
          	dbug(2, "Closed ctl fd %d\n", control_channel);
		close(control_channel);
		control_channel = -1;
	}
}
