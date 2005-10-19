/*
 * stp_dump.c - stp data dump program
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
	fprintf(stderr, "%s input_file \n", prog);
	exit(1);
}

#define TIMESTAMP_SIZE (sizeof(int))

int main (int argc, char *argv[])
{
	char buf[32];
	int c, seq, lastseq = 0;
	FILE *fp;

	if (argc != 2)
	  usage(argv[0]);

	fp = fopen(argv[1], "r");
	if (!fp) {
	  fprintf(stderr, "ERROR: couldn't open input file %s: errcode = %s\n",
		  argv[1], strerror(errno));
	  return -1;
	}
	
	while (1) {
	  int numbytes = 0;

	  if (fread (buf, TIMESTAMP_SIZE, 1, fp))
	    seq = *((int *)buf);
	  else
	    break;

	  if (seq < lastseq)
	    fprintf(stderr, "WARNING: seq %d followed by %d\n", lastseq, seq);
	  lastseq = seq;

	  while (1) {
	    c = fgetc_unlocked(fp);
	    if (c == 0 || c == EOF)
	      break;
	    numbytes++;
	  }
	  printf ("<%d><%d BYTES>", seq, numbytes);
	  if (c == 0)
	    printf ("<0>\n");
	  else {
	    printf ("<EOF>\n");
	    break;
	  }
	}

	printf ("DONE\n");
	fclose (fp);
	return 0;
}
