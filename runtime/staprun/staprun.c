/* -*- linux-c -*-
 *
 * staprun.c - SystemTap module loader 
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
 * Copyright (C) 2005-2007 Red Hat, Inc.
 *
 */

#include "staprun.h"
#include <pwd.h>

extern char *optarg;
extern int optopt;
extern int optind;

int verbose = 0;
int target_pid = 0;
int driver_pid = 0;
unsigned int buffer_size = 0;
char *modname = NULL;
char *modpath = NULL;
#define MAXMODOPTIONS 64
char *modoptions[MAXMODOPTIONS];
char *target_cmd = NULL;
char *outfile_name = NULL;
char *username = NULL;
uid_t cmd_uid;
gid_t cmd_gid;

static void usage(char *prog)
{
	fprintf(stderr, "\n%s [-v] [-c cmd ] [-x pid] [-u user]\n"
                "\t[-b bufsize] [-o FILE] kmod-name [kmod-options]\n", prog);
	fprintf(stderr, "-v  Verbose.\n");
	fprintf(stderr, "-c cmd.  Command \'cmd\' will be run and staprun will exit when it does.\n");
	fprintf(stderr, "   _stp_target will contain the pid for the command.\n");
	fprintf(stderr, "-x pid.  Sets _stp_target to pid.\n");
	fprintf(stderr, "-o FILE. Send output to FILE.\n");
	fprintf(stderr, "-u username. Run commands as username.\n");
	fprintf(stderr, "-b buffer size. The systemtap module will specify a buffer size.\n");
	fprintf(stderr, "   Setting one here will override that value. The value should be\n");
	fprintf(stderr, "   an integer between 1 and 64 which be assumed to be the\n");
	fprintf(stderr, "   buffer size in MB. That value will be per-cpu in bulk mode.\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "vb:t:d:c:o:u:x:")) != EOF) {
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 'b':
		{
			int size = (unsigned)atoi(optarg);
			if (!size)
				usage(argv[0]);
			if (size > 64) {
				fprintf(stderr, "Maximum buffer size is 64 (MB)\n");
				exit(1);
			}
			buffer_size = size;
			break;
		}
		case 't':
		case 'x':
			target_pid = atoi(optarg);
			break;
		case 'd':
			/* internal option used by stap */
			driver_pid = atoi(optarg);
			break;
		case 'c':
			target_cmd = optarg;
			break;
		case 'o':
			outfile_name = optarg;
			break;
		case 'u':
			username = optarg;
			break;
		default:
			usage(argv[0]);
		}
	}

	if (verbose) {
		if (buffer_size)
			printf ("Using a buffer of %u bytes.\n", buffer_size);
	}

	if (optind < argc) {
		/* Collect both full path and just the trailing module name.  */
		modpath = argv[optind++];
		modname = rindex (modpath, '/');
		if (modname == NULL)
			modname = modpath;
		else
			modname++; /* skip over / */
	}

        if (optind < argc) {
		unsigned start_idx = 4; /* reserve four slots in modoptions[] */
		while (optind < argc && start_idx+1 < MAXMODOPTIONS)
			modoptions[start_idx++] = argv[optind++];
		modoptions[start_idx] = NULL;
	}

	if (!modname) {
		fprintf (stderr, "Need a module to load.\n");
		usage(argv[0]);
	}

	if (username) {
		struct passwd *pw = getpwnam(username);
		if (!pw) {
			fprintf(stderr, "Cannot find user \"%s\".\n", username);
			exit(1);
		}
		cmd_uid = pw->pw_uid;
		cmd_gid = pw->pw_gid;
	} else {
		cmd_uid = getuid();
		cmd_gid = getgid();
	}

	/* now bump the priority */
	setpriority (PRIO_PROCESS, 0, -10);

	if (init_staprun()) {
		fprintf(stderr, "Couldn't initialize staprun. Exiting.\n");
		exit(1);
	}

	if (stp_main_loop()) {
		fprintf(stderr,"Couldn't enter main loop. Exiting.\n");
		exit(1);
	}

	return 0;
}
