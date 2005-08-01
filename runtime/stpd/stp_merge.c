/*
 * stp_merge.c - stp merge program
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
 * Copyright (C) Red Hat Inc, 2005
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void usage (char *prog)
{
	fprintf(stderr, "%s [-o output_filename] input_files ...\n", prog);
	exit(1);
}

#define TIMESTAMP_SIZE 11
#define NR_CPUS 256

int main (int argc, char *argv[])
{
	char *outfile_name = NULL;
	char buf[32];
	int c, i, j, dropped=0;
	long count=0, min, num[NR_CPUS];
	FILE *ofp, *fp[NR_CPUS];
	int ncpus;

	while ((c = getopt (argc, argv, "o:")) != EOF)  {
		switch (c) {
		case 'o':
			outfile_name = optarg;
			break;
		default:
			usage(argv[0]);
		}
	}
	
	if (optind == argc)
		usage (argv[0]);

	i = 0;
	while (optind < argc) {
		fp[i] = fopen(argv[optind++], "r");
		if (!fp[i]) {
			fprintf(stderr, "error opening file %s.\n", argv[optind - 1]);
			return -1;
		}
		if (fread (buf, TIMESTAMP_SIZE, 1, fp[i]))
			num[i] = strtoul (buf, NULL, 10);
		else
			num[i] = 0;
		i++;
	}
	ncpus = i;

	if (!outfile_name)
		ofp = stdout;
	else {
		ofp = fopen(outfile_name, "w");	
		if (!ofp) {
			fprintf(stderr, "ERROR: couldn't open output file %s: errcode = %s\n", 
				outfile_name, strerror(errno));
			return -1;
		}
	}
	
	do {
		min = num[0];
		j = 0;
		for (i = 1; i < ncpus; i++) {
			if (min == 0 || (num[i] && num[i] < min)) {
				min = num[i];
				j = i;
			}
		}

		while (1) {
			c = fgetc_unlocked(fp[j]);
			if (c == 0 || c == EOF)
				break;
			fputc_unlocked (c, ofp);
		}

		if (min && ++count != min) {
			fprintf(stderr, "got %ld. expected %ld\n", min, count);
			dropped += min - count ;
			count = min;
		}

		if (fread (buf, TIMESTAMP_SIZE, 1, fp[j]))
			num[j] = strtoul (buf, NULL, 10);
		else
			num[j] = 0;
	} while (min);

	fputs ("\n", ofp);

	for (i = 0; i < ncpus; i++)
		fclose (fp[i]);
	fclose (ofp);
	printf ("sequence had %d drops\n", dropped);
	return 0;
}
