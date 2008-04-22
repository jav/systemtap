/* -*- linux-c -*-
 *
 * cap.c - staprun capabilities functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2007 Red Hat, Inc.
 *
 */

#include "staprun.h"
#include <sys/prctl.h>

static int _stp_no_caps = 0;

/* like perror, but exits */
#define ferror(msg) {						  \
		_perr(msg);					  \
		exit(1);					  \
	}							  \

/*
 * init_cap() sets up the initial capabilities for staprun. Then
 * it calls prctl( PR_SET_KEEPCAPS) to arrrange to keep these capabilities
 * even when not running as root. Next it resets the real, effective, and 
 * saved uid and gid back to the normal user.
 *
 * There are two sets of capabilities we are concerned with; permitted
 * and effective. The permitted capabilities are all the capabilities
 * that this process is ever permitted to have. They are defined in init_cap()
 * and may be permanently removed with drop_cap().
 *
 * Effective capabilities are the capabilities from the permitted set
 * that are currently enabled. A good practice would be to only enable 
 * capabilities when necessary and to delete or drop them as soon as possible.
 *
 * Capabilities we might use include:
 *
 * CAP_SYS_MODULE - insert and remove kernel modules
 * CAP_SYS_ADMIN - misc, including mounting and unmounting
 * CAP_SYS_NICE - setpriority()
 * CAP_SETUID - allows setuid
 * CAP_SETGID - allows setgid
 * CAP_CHOWN - allows chown
 */

void init_cap(void)
{
	cap_t caps = cap_init();
	cap_value_t capv[] = { CAP_SYS_MODULE, CAP_SYS_ADMIN, CAP_SYS_NICE, CAP_SETUID, CAP_SETGID, CAP_DAC_OVERRIDE };
	const int numcaps = sizeof(capv) / sizeof(capv[0]);
	uid_t uid = getuid();
	gid_t gid = getgid();

	cap_clear(caps);
	if (caps == NULL)
		ferror("cap_init");

	if (cap_set_flag(caps, CAP_PERMITTED, numcaps, capv, CAP_SET) < 0)
		ferror("cap_set_flag");

	if (cap_set_proc(caps) < 0) {
		dbug(1, "Setting capabilities failed. Capabilities disabled.\n");
		_stp_no_caps = 1;
		return;
	}

	cap_free(caps);

	if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) < 0)
		ferror("prctl");

	if (setresuid(uid, uid, uid) < 0)
		ferror("setresuid");

	if (setresgid(gid, gid, gid) < 0)
		ferror("setresgid");
}

void print_cap(char *text)
{
	int p;
	cap_t caps = cap_get_proc();
	uid_t uid, euid, suid;
	gid_t gid, egid, sgid;

	if (caps == NULL) {
		perr("cap_get_proc");
		return;
	}

	getresuid(&uid, &euid, &suid);
	getresgid(&gid, &egid, &sgid);

	printf("***** %s\n", text);

	if ((p = prctl(PR_GET_KEEPCAPS, 0, 0, 0, 0)) < 0)
		perr("Couldn't get PR_SET_KEEPCAPS flag value");
	else
		printf("KEEPCAPS: %d\n", p);

	printf("uid: %d, euid: %d, suid: %d\ngid: %d. egid: %d, sgid: %d\n", uid, euid, suid, gid, egid, sgid);
	printf("Caps: %s\n", cap_to_text(caps, NULL));
	cap_free(caps);
	printf("*****\n\n");
}

/* drop_cap() permanently removes a capability from the permitted set. There is
 * no way to recover the capability after this.  You do not need to remove
 * it from the effective set before calling this.
 */
void drop_cap(cap_value_t cap)
{
	if (_stp_no_caps == 0) {
		cap_t caps = cap_get_proc();
		if (caps == NULL)
			ferror("cap_get_proc failed");
		if (cap_set_flag(caps, CAP_PERMITTED, 1, &cap, CAP_CLEAR) < 0)
			ferror("Could not clear effective capabilities");
		if (cap_set_proc(caps) < 0)
			ferror("Could not apply capability set");
		cap_free(caps);
	}
}

/* add_cap() adds a permitted capability to the effective set. */
void add_cap(cap_value_t cap)
{
	if (_stp_no_caps == 0) {
		cap_t caps = cap_get_proc();
		if (caps == NULL)
			ferror("cap_get_proc failed");
		if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, CAP_SET) < 0)
			ferror("Could not set effective capabilities");
		if (cap_set_proc(caps) < 0)
			ferror("Could not apply capability set");
		cap_free(caps);
	}
}

/* del_cap() deletes a permitted capability from the effective set. */
void del_cap(cap_value_t cap)
{
	if (_stp_no_caps == 0) {
		cap_t caps = cap_get_proc();
		if (caps == NULL)
			ferror("cap_get_proc failed");
		if (cap_set_flag(caps, CAP_EFFECTIVE, 1, &cap, CAP_CLEAR) < 0)
			ferror("Could not clear effective capabilities");
		if (cap_set_proc(caps) < 0)
			ferror("Could not apply capability set");
		cap_free(caps);
	}
}
