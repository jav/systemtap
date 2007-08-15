/*
 * stap_merge.c - systemtap merge program
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
 * Copyright (C) Red Hat Inc, 2005-2007
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void usage (char *prog)
{
	fprintf(stderr, "%s [-v] [-o output_filename] input_files ...\n", prog);
	exit(-1);
}

#define TIMESTAMP_SIZE (sizeof(int))
#define NR_CPUS 256

int main (int argc, char *argv[])
{
	char *buf, *outfile_name = NULL;
	int c, i, j, rc, dropped=0;
	long count=0, min, num[NR_CPUS];
	FILE *ofp, *fp[NR_CPUS];
	int ncpus, len, verbose = 0;
	int bufsize = 65536;

	buf = malloc(bufsize);
	if (buf == NULL) {
		fprintf(stderr, "Memory allocation failed.\n");
		exit(-2);
	}

	while ((c = getopt (argc, argv, "vo:")) != EOF)  {
		switch (c) {
		case 'v':
			verbose = 1;
			break;
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
			num[i] = *((int *)buf);
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

		if (fread(&len, sizeof(int), 1, fp[j])) {
			if (verbose)
				fprintf(stdout, "[CPU:%d, seq=%ld, length=%d]\n", j, min, len);
			if (len > bufsize) {
				bufsize = len * 2;
				if (verbose) fprintf(stderr, "reallocating %d bytes\n", bufsize);
				buf = realloc(buf, bufsize);
				if (buf == NULL) {
					fprintf(stderr, "Memory allocation failed.\n");
					exit(-2);
				}
			}
			if ((rc = fread(buf, len, 1, fp[j]) <= 0)) {
				fprintf(stderr, "fread error: got %d\n", rc);
				exit(-3);
			}
			if ((rc = fwrite(buf, len, 1, ofp)) <= 0) {
				fprintf(stderr, "fread error: got %d\n", rc);
				exit(-3);
			}
		}

		if (min && ++count != min) {
			fprintf(stderr, "got %ld. expected %ld\n", min, count);
			dropped += min - count ;
			count = min;
		}

		if (fread (buf, TIMESTAMP_SIZE, 1, fp[j]))
			num[j] = *((int *)buf);
		else
			num[j] = 0;
	} while (min);

	for (i = 0; i < ncpus; i++)
		fclose (fp[i]);
	fclose (ofp);
	printf ("sequence had %d drops\n", dropped);
	return 0;
}
