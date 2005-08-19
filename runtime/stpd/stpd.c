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
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Red Hat Inc, 2005
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <sys/wait.h>
#include "librelay.h"

extern char *optarg;
extern int optopt;
extern int optind;

int print_only = 0;
int quiet = 0;
int merge = 1;
int verbose = 0;
unsigned int buffer_size = 0;
char *modname = NULL;
char *modpath = NULL;

 /* relayfs base file name */
static char stpd_filebase[1024];

/* stp_check script */
#ifdef PKGLIBDIR
char *stp_check=PKGLIBDIR "/stp_check";
#else
char *stp_check="stp_check";
#endif

static void usage(char *prog)
{
	fprintf(stderr, "\n%s [-m] [-p] [-q] [-b bufsize] [-n num_subbufs] kmod-name\n", prog);
	fprintf(stderr, "-m  Don't merge per-cpu files.\n");
	fprintf(stderr, "-p  Print only.  Don't log to files.\n");
	fprintf(stderr, "-q  Quiet. Don't display trace to stdout.\n");
	fprintf(stderr, "-b buffer size. The systemtap module will specify a buffer size.\n");
	fprintf(stderr, "   Setting one here will override that value. The value should be\n");
	fprintf(stderr, "   an integer between 1 and 64 which be assumed to be the\n");
	fprintf(stderr, "   buffer size in MB. That value will be per-cpu if relayfs is used.\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int c, status;
	pid_t pid;

	while ((c = getopt(argc, argv, "mpqb:n:v")) != EOF) 
	{
		switch (c) {
		case 'm':
			merge = 0;
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
  
	if (!modname) {
		fprintf (stderr, "Cannot invoke daemon without probe module\n");
		usage(argv[0]);
	}

	if (print_only && quiet) {
		fprintf (stderr, "Cannot do \"-p\" and \"-q\" both.\n");
		usage(argv[0]);
	}

	/* now run the _stp_check script */
	if ((pid = vfork()) < 0) {
		perror ("vfork");
		exit(-1);
	} else if (pid == 0) {
		if (execlp(stp_check, stp_check, NULL) < 0)
			exit (-1);
	}
	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		exit(-1);
	}
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		perror (stp_check);
		fprintf(stderr, "Could not execute %s\n", stp_check);
		exit(1);
	}
	
	sprintf(stpd_filebase, "/mnt/relay/%d/cpu", getpid());
	if (init_stp(stpd_filebase, !quiet)) {
		//fprintf(stderr, "Couldn't initialize stpd. Exiting.\n");
		exit(1);
	}

	if (stp_main_loop()) {
		fprintf(stderr,"Couldn't enter main loop. Exiting.\n");
		exit(1);
	}
	
	return 0;
}
