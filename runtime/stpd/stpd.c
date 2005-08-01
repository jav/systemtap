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
 * Copyright (C) Redhat Inc, 2005
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "librelay.h"

extern char *optarg;
extern int optopt;
extern int optind;

int print_only = 0;
int quiet = 0;
int merge = 1;
unsigned int opt_subbuf_size = 0;
unsigned int opt_n_subbufs = 0;
char *modname = NULL;

 /* relayfs base file name */
static char stpd_filebase[1024];

static void usage(char *prog)
{
	fprintf(stderr, "\n%s [-m] [-p] [-q] [-b bufsize] [-n num_subbufs] kmod-name\n", prog);
	fprintf(stderr, "-m  Don't merge per-cpu files.\n");
	fprintf(stderr, "-p  Print only.  Don't log to files.\n");
	fprintf(stderr, "-q  Quiet. Don't display trace to stdout.\n");
	fprintf(stderr, "-b subbuf_size  (override the value in the module)\n");
	fprintf(stderr, "-n subbufs  (override the value in the module)\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "mpqb:n:")) != EOF) 
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
		case 'b':
			opt_subbuf_size = (unsigned)atoi(optarg);
			if (!opt_subbuf_size)
				usage(argv[0]);
			break;
		case 'n':
			opt_n_subbufs = (unsigned)atoi(optarg);
			if (!opt_n_subbufs)
				usage(argv[0]);
			break;
		default:
			usage(argv[0]);
		}
	}
	
	if ((opt_n_subbufs && !opt_subbuf_size) || (opt_subbuf_size && !opt_n_subbufs)) {
		fprintf (stderr, "You must specify both the number of subbufs and their size.\n");
		usage(argv[0]);
	}
	if (optind < argc)
		modname = argv[optind++];
  
	if (!modname) {
		fprintf (stderr, "Cannot invoke daemon without probe module\n");
		usage(argv[0]);
	}

	if (print_only && quiet) {
		fprintf (stderr, "Cannot do \"-p\" and \"-q\" both.\n");
		usage(argv[0]);
	}

	sprintf(stpd_filebase, "/mnt/relay/%d/cpu", getpid());
	if (init_stp(stpd_filebase, !quiet)) {
		fprintf(stderr, "Couldn't initialize stpd. Exiting.\n");
		exit(1);
	}

	if (stp_main_loop()) {
		printf("Couldn't enter main loop. Exiting.\n");
		exit(1);
	}
	
	return 0;
}
