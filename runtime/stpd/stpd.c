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

 /* packet logging output written here, filebase0...N */
static char *stpd_outfilebase = "stpd_cpu";

#define DEFAULT_SUBBUF_SIZE (262144)
#define DEFAULT_N_SUBBUFS (4)
static unsigned subbuf_size = DEFAULT_SUBBUF_SIZE;
static unsigned n_subbufs = DEFAULT_N_SUBBUFS;

extern char *optarg;
extern int optopt;
extern int optind;

int print_only = 0;
int quiet = 0;
int streaming = 1;

 /* relayfs base file name */
static char stpd_filebase[1024];

static void usage(char *prog)
{
	fprintf(stderr, "%s [-p] [-q] [-b subbuf_size -n n_subbufs] kmod-name\n", prog);
	fprintf(stderr, "-p  Print only.  Don't log to files.\n");
	fprintf(stderr, "-q  Quiet. Don't display trace to stdout.\n");
	fprintf(stderr, "-r  Use relayfs for buffering i.e. non-streaming mode.\n");
	fprintf(stderr, "-b subbuf_size  (default is %d)\n", DEFAULT_SUBBUF_SIZE);
	fprintf(stderr, "-n subbufs  (default is %d)\n", DEFAULT_N_SUBBUFS);
	exit(1);
}

int main(int argc, char **argv)
{
	int c;
	unsigned opt_subbuf_size = 0;
	unsigned opt_n_subbufs = 0;
	char *modname = NULL;

	while ((c = getopt(argc, argv, "b:n:pqr")) != EOF) 
	{
		switch (c) {
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
		case 'p':
			print_only = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'r':
			streaming = 0;
			break;
		default:
			usage(argv[0]);
		}
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
  
	if ((opt_n_subbufs && !opt_subbuf_size) ||
	    (!opt_n_subbufs && opt_subbuf_size))
		usage(argv[0]);
	
	if (opt_n_subbufs && opt_n_subbufs) {
		subbuf_size = opt_subbuf_size;
		n_subbufs = opt_n_subbufs;
	}

	sprintf(stpd_filebase, "/mnt/relay/%d/cpu", getpid());
	if (init_stp(modname, stpd_filebase, stpd_outfilebase,
		     subbuf_size, n_subbufs, 1)) {
		fprintf(stderr, "Couldn't initialize stpd. Exiting.\n");
		exit(1);
	}

	if (!streaming)
		printf("Creating channel with %u sub-buffers of size %u.\n",
		       n_subbufs, subbuf_size);

	if (quiet)
		printf("Logging... Press Control-C to stop.\n");
	else
		printf("Press Control-C to stop.\n");

	if (stp_main_loop()) {
		printf("Couldn't enter main loop. Exiting.\n");
		exit(1);
	}
	
	return 0;
}
