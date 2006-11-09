/*
 * stp.c - stp 'daemon'
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
 * Copyright (C) 2005 IBM Corporation
 * Copyright (C) 2005-2006 Red Hat, Inc.
 *
 */

#include "librelay.h"
#include <pwd.h>

extern char *optarg;
extern int optopt;
extern int optind;

int print_only = 0;
int quiet = 0;
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
	fprintf(stderr, "\n%s [-m] [-p] [-q] [-r] [-c cmd ] [-t pid]\n"
                "\t[-b bufsize] [-o FILE] kmod-name [kmod-options]\n", prog);
	fprintf(stderr, "-p  Print only.  Don't log to files.\n");
	fprintf(stderr, "-q  Quiet. Don't display trace to stdout.\n");
	fprintf(stderr, "-c cmd.  Command \'cmd\' will be run and staprun will exit when it does.\n");
	fprintf(stderr, "   _stp_target will contain the pid for the command.\n");
	fprintf(stderr, "-t pid.  Sets _stp_target to pid.\n");
	fprintf(stderr, "-d pid.  Pass the systemtap driver's pid.\n");
	fprintf(stderr, "-o FILE. Send output to FILE.\n");
	fprintf(stderr, "-u username. Run commands as username.\n");
	fprintf(stderr, "-b buffer size. The systemtap module will specify a buffer size.\n");
	fprintf(stderr, "   Setting one here will override that value. The value should be\n");
	fprintf(stderr, "   an integer between 1 and 64 which be assumed to be the\n");
	fprintf(stderr, "   buffer size in MB. That value will be per-cpu if relayfs is used.\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int c;
	
	while ((c = getopt(argc, argv, "mpqrb:n:t:d:c:vo:u:")) != EOF)
	{
		switch (c) {
		case 'm':
			fprintf(stderr, "Warning: -m option deprecated. Ignoring...\n");
			break;
		case 'p':
			print_only = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'r':
			fprintf(stderr, "Warning: -r option deprecated. Ignoring...\n");
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
			target_pid = atoi(optarg);
			break;
		case 'd':
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

	if (optind < argc)
          {
            /* Collect both full path and just the trailing module name.  */
            modpath = argv[optind++];
            modname = rindex (modpath, '/');
            if (modname == NULL)
              modname = modpath;
            else
              modname++; /* skip over / */
          }

        if (optind < argc)
          {
            unsigned start_idx = 3; /* reserve three slots in modoptions[] */
            while (optind < argc && start_idx+1 < MAXMODOPTIONS)
              modoptions[start_idx++] = argv[optind++];
            /* Redundantly ensure that there is a NULL pointer at the end
               of modoptions[]. */
            modoptions[start_idx] = NULL;
          }

	if (!modname) {
		fprintf (stderr, "Cannot invoke daemon without probe module\n");
		usage(argv[0]);
	}

	if (print_only && quiet) {
		fprintf (stderr, "Cannot do \"-p\" and \"-q\" both.\n");
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

	if (init_stp(!quiet)) {
		//fprintf(stderr, "Couldn't initialize staprun. Exiting.\n");
		exit(1);
	}

	if (stp_main_loop()) {
		fprintf(stderr,"Couldn't enter main loop. Exiting.\n");
		exit(1);
	}

	return 0;
}
