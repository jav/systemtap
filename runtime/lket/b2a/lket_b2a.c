// Copyright (C) 2005, 2006 IBM Corp.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lket_b2a.h"

static long long start_timestamp;

/* mapping table for [group, id, fmt] */
int **groups[MAX_HOOKGROUP+1] = {NULL};

/* Balanced binary search tree to store the 
   mapping of <pid, process name> */
GTree *appNameTree;

/* 
 * default hook format table,
 * based on tapsets/hookid_defs.stp and other hook specific files
 */
static hook_fmt default_hook_fmts[] = {
	//_GROUP_SYSCALL
	{1, 1, "%s"}, 	//HOOKID_SYSCALL_ENTRY
	{1, 2, "%s"}, 	//HOOKID_SYSCALL_RETURN
	//_GROUP_PROCESS
	{2, 1, "%4b%s"},//HOOKID_PROCESS_SNAPSHOT
	{2, 2, "%s"}, 	//HOOKID_PROCESS_EXECVE
	{2, 3, "%4b"}, 	//HOOKID_PROCESS_FORK
	//_GROUP_IOSCHED
	{3, 1, "%s%1b%1b"}, //HOOKID_IOSCHED_NEXT_REQ
	{3, 2, "%s%1b%1b"}, //HOOKID_IOSCHED_ADD_REQ
	{3, 3, "%s%1b%1b"}, //HOOKID_IOSCHED_REMOVE_REQ
	//_GROUP_TASK
	{4, 1, "%4b%4b%1b"},//HOOKID_TASK_CTXSWITCH
	{4, 2, "%4b"},      //HOOKID_TASK_CPUIDLE
	//_GROUP_SCSI
	{5, 1, "%1b%1b%1b"}, 		//HOOKID_SCSI_IOENTRY
	{5, 2, "%1b%4b%1b%8b%4b%8b"}, 	//HOOKID_SCSI_IO_TO_LLD
	{5, 3, "%4b%1b%8b"}, 		//HOOKID_SCSI_IODONE_BY_LLD
	{5, 4, "%4b%1b%8b%4b"}, 	//HOOKID_SCSI_IOCOMP_BY_MIDLEVEL
	//_GROUP_PAGEFAULT
	{6, 1, "%8b%1b"},  	//HOOKID_PAGEFAULT
	//_GROUP_NETDEV
	{7, 1, "%s%4b%2b%4b"}, 	//HOOKID_NETDEV_RECEIVE
	{7, 2, "%s%4b%2b%4b"}  	//HOOKID_NETDEV_TRANSMIT
};

int main(int argc, char *argv[])
{
	lket_pkt_header *hdrs = NULL;
	FILE	**infps = NULL;
	FILE 	*outfp = NULL;
	char	outfilename[MAX_STRINGLEN]={0};
	int	i, j, total_infiles = 0;
	long long min;

	if(argc < 2) {
		printf("Usage: %s inputfile1 [inputfile2...]\n", argv[0]);
		return 1;
	}
	total_infiles = argc - 1;
	
	// open the input files and the output file
	infps = (FILE **)malloc(total_infiles * sizeof(FILE *));
	if(!infps) {
		printf("Unable to malloc infps\n");
		return 1;
	}
	memset(infps, 0, total_infiles * sizeof(FILE *));
	for(i=0; i < total_infiles; i++) {
		infps[i] = fopen(argv[i+1], "r");
		if(infps[i] == NULL) {
			printf("Unable to open %s\n", argv[i+1]);
			goto failed;
		}
	}
#if !defined(DEBUG_OUTPUT)
	if(strnlen(outfilename, MAX_STRINGLEN) == 0)
		strncpy(outfilename, DEFAULT_OUTFILE_NAME, MAX_STRINGLEN);
	outfp = fopen(outfilename, "w");
	if(outfp == NULL) {
		printf("Unable to create %s\n", outfilename);
		goto failed;
	}
#else
	outfp = stdout;
#endif
	
	/* create the search tree */
	appNameTree = g_tree_new_full(compareFunc, NULL, NULL, destroyAppName);

	// register all hookdata formats here
	register_formats();

	// find the lket header
	find_init_header(infps, total_infiles, outfp);

	// allocate packet headers array
	hdrs = malloc(total_infiles * sizeof(lket_pkt_header));
	if(!hdrs) {
		printf("Unable to malloc hdrs \n");
		goto failed;
	}
	memset(hdrs, 0, total_infiles * sizeof(lket_pkt_header));

	// initialize packet headers array
	start_timestamp = 0;
	j = 0;
	for(i=0; i < total_infiles; i++) {
		get_pkt_header(infps[i], &hdrs[i]);
		if((hdrs[i].sec*1000000LL + hdrs[i].usec) < start_timestamp
		       || (start_timestamp == 0)) {
			start_timestamp = hdrs[i].sec*1000000LL + hdrs[i].usec;
			j = i;
		}
	}

	// main loop of parsing & merging
	min = start_timestamp;
	do {
		// j is the next
		if(min) {
			print_pkt_header(outfp, &hdrs[j]);

			if(hdrs[j].hookgroup==2 &&
				(hdrs[j].hookid==1 || hdrs[j].hookid==2))
				register_appname(j, infps[j], &hdrs[j]);

			// write the remaining content from infd[j] to outfile
			b2a_vsnprintf(get_fmt(hdrs[j].hookgroup, hdrs[j].hookid), 
					infps[j], outfp, hdrs[j].size);
			// update hdr[j]
			get_pkt_header(infps[j], &hdrs[j]);
		}
		// recalculate the smallest timestamp
		min = hdrs[0].sec*1000000LL + hdrs[0].usec;
		j = 0;
		for(i=1; i < total_infiles ; i++) {
			if((min == 0) || 
				((hdrs[i].sec*1000000LL + hdrs[i].usec) < min)) {
				min = hdrs[i].sec*1000000LL + hdrs[i].usec;
				j = i;
			}
		}
	} while(min != 0);

failed:
	// close all opened files
	for(i=0; i < total_infiles; i++)
		if(infps[i])
			fclose(infps[i]);
	if(outfp)
		fclose(outfp);
	
	// free all allocated memory space
	for(i=0; i <= MAX_HOOKGROUP; i++) {
		if(groups[i]) {
			for(j=0; j <= MAX_HOOKID; j++)
				if(groups[i][j]) {
					free(groups[i][j]);
					groups[i][j] = NULL;
				}
		}
	}
	if(infps)
		free(infps);
	if(hdrs)
		free(hdrs);

	g_tree_destroy(appNameTree);

	return 0;
}

/* register newly found process name for addevent.process.snapshot
  and addevent.process.execve */
void register_appname(int i, FILE *fp, lket_pkt_header *phdr)
{
	int pid;
	char *appname;
	int count;
	int len;
	int c;
	len=0;
	count=0;

	appname = (char *)malloc(1024);

	if(phdr->hookid ==1 )  {  /* process_snapshot */
		len = fread(&pid, 1, 4, fp);
		c = fgetc_unlocked(fp);
		++len;
		while (c && len < 1024) {
			appname[count++] = (char)c;	
			c = fgetc_unlocked(fp);
			++len;
		}
		appname[count]='\0';
		fseek(fp, 0-len, SEEK_CUR);
	} else if (phdr->hookid == 2)  { /* process.execve */
		pid = phdr->pid;

		c = fgetc_unlocked(fp);
		++len;
		while (c && len < 1024) {
			appname[count++] = (char)c;	
			c = fgetc_unlocked(fp);
			++len;
		}
		appname[count]='\0';
		fseek(fp, 0-len, SEEK_CUR);
	} else  {
		free(appname);
		return;
	}
	g_tree_insert(appNameTree, (gpointer)pid, (gpointer)appname);
}


gint compareFunc(gconstpointer a, gconstpointer b, gpointer user_data)
{
	if((int)(a) > (int)(b)) return 1;
	else if ((int)(a) < (int)(b)) return -1;
	else return 0;
}

void destroyAppName(gpointer data)
{
	free(data);
}

/* The following are all supporting sub-functions */

/*
 * register one hookdata fmt string for a [hookgroup, hookid] pair
 */
int register_one_fmt(int hookgroup, int hookid, const char *fmt, size_t maxlen)
{
	void *ptr;

	if(hookgroup < 0 || hookgroup > MAX_HOOKGROUP)
		return -1;
	if(hookid < 0 || hookid > MAX_HOOKID)
		return -1;
	if(!fmt || maxlen <= 0)
		return -1;
	if(groups[hookgroup] == NULL) {
		// allocate hook aray for new group
		ptr = malloc((MAX_HOOKID+1) * sizeof(char *));
		if(!ptr)
			return -1;
		memset(ptr,0,(MAX_HOOKID+1) * sizeof(char *));
		groups[hookgroup] = ptr;
	}
	if(groups[hookgroup][hookid] != NULL) {
		free(groups[hookgroup][hookid]);
		groups[hookgroup][hookid] = NULL;
	}
	assert(groups[hookgroup][hookid] == NULL);
	ptr = malloc(maxlen);
	if(!ptr)
		return -1;
	memset(ptr, 0, maxlen);
	strncpy(ptr, fmt, maxlen);
	groups[hookgroup][hookid] = ptr;
	return 0;
}

/*
 * initialize all the hookdata fmt strings as required
 * called at the beginning of main()
 */
void register_formats(void)
{
	int i, total;
       
	total = sizeof(default_hook_fmts)/sizeof(default_hook_fmts[0]);
	if(total <= 0)
		return;
	for(i=0; i<total; i++)
		register_one_fmt( default_hook_fmts[i].hookgrp,
			default_hook_fmts[i].hookid, 
			default_hook_fmts[i].fmt,
			strlen(default_hook_fmts[i].fmt) + 1 );
}

/*
 * get the format string with [hookgroup, hookid] pair
 */
const char *get_fmt(int hookgroup, int hookid)
{
	assert(hookgroup >= 0 && hookgroup <= MAX_HOOKGROUP );
	assert(hookid >= 0 && hookid <= MAX_HOOKID );
	if(groups[hookgroup] && groups[hookgroup][hookid])
		return (const char *)groups[hookgroup][hookid];
	else
		return "<Bad Fmt>";
}

/* 
 * handle the bothering sequence id generated by systemtap
 */
int skip_sequence_id(FILE *fp)
{
	return (fseek(fp, (off_t)SEQID_SIZE, SEEK_CUR) == -1);
}

/* 
 * search the LKET init header in a set of input files, 
 * and the header structure is defined in tapsets/lket_trace.stp
 */
void find_init_header(FILE **infps, const int total_infiles, FILE *outfp)
{
	int 	 i, j;
	int32_t magic;

	/* information from lket_init_header */
	int16_t inithdr_len;
	int8_t ver_major;
	int8_t ver_minor;
	int8_t big_endian;
	int8_t bits_width;

	if(total_infiles <= 0 )
		return;
	j = total_infiles;
	for(i=0; i<total_infiles; i++) {
		if(skip_sequence_id(infps[i]))
			continue;
		if(fread(&magic, 1, sizeof(magic), infps[i]) < sizeof(magic))
			continue;
		if(magic == (int32_t)LKET_MAGIC) {
			//found
			j = i;
			fprintf(outfp, "LKET Magic:\t0x%X\n", magic);
			//read other content of lket_int_header
			if(fread(&inithdr_len, 1, sizeof(inithdr_len), infps[i]) < sizeof(inithdr_len))
				break;
			fprintf(outfp, "InitHdrLen:\t%d\n", inithdr_len);
			if(fread(&ver_major, 1, sizeof(ver_major), infps[i]) < sizeof(ver_major))
				break;
			fprintf(outfp, "Version Major:\t%d\n", ver_major);
			if(fread(&ver_minor, 1, sizeof(ver_minor), infps[i]) < sizeof(ver_minor))
				break;
			fprintf(outfp, "Version Minor:\t%d\n", ver_minor);
			if(fread(&big_endian, 1, sizeof(big_endian), infps[i]) < sizeof(big_endian))
				break;
			fprintf(outfp, "Big endian:\t%s\n", big_endian ? "YES":"NO");
			if(fread(&bits_width, 1, sizeof(bits_width), infps[i]) < sizeof(bits_width))
				break;
			fprintf(outfp, "Bits width:\t%d\n", bits_width);
			// skip the null terminater
			fseek(infps[i], 1LL, SEEK_CUR);
			break;
		}
	}
	for(i=0; i<total_infiles && i!=j; i++)
		fseek(infps[i], 0LL, SEEK_SET);
	return;
}

/* 
 * read the lket_pkt_header structure at the begining of the input file 
 */
int get_pkt_header(FILE *fp, lket_pkt_header *phdr)
{
	if(skip_sequence_id(fp))
		goto bad;

	if(fread(phdr, 1, sizeof(lket_pkt_header), fp) < sizeof(lket_pkt_header))
		goto bad;

	phdr->size -= sizeof(lket_pkt_header)-sizeof(phdr->flag)-sizeof(phdr->size)-1;

	return 0;

bad:
	memset(phdr, 0, sizeof(lket_pkt_header));
	return -1;
}	

/* 
 * print the lket_pkt_header structure into the output file
 */
void print_pkt_header(FILE *fp, lket_pkt_header *phdr)
{
	if(!fp || !phdr)
		return;
	fprintf(fp, "%lld.%lld APPNAME: %s PID:%d PPID:%d TID:%d CPU:%d HOOKGRP:%d HOOKID:%d HOOKDATA:",
		(phdr->sec*1000000LL + phdr->usec - start_timestamp)/1000000LL,
		(phdr->sec*1000000LL + phdr->usec- start_timestamp)%1000000LL,
		(char *)(g_tree_lookup(appNameTree, (gconstpointer)phdr->pid)),
		phdr->pid,
		phdr->ppid,
		phdr->tid,
		phdr->cpu,
		phdr->hookgroup,
		phdr->hookid);
}
static int skip_atoi(const char **s)
{
	int i=0;
	while (isdigit(**s))
		i = i*10 + *((*s)++) - '0';
	return i;
}

/* 
 * read fixed-length from the input binary file and write into 
 * the output file, based on the fmt string
 */
void b2a_vsnprintf(const char *fmt, FILE *infp, FILE *outfile, size_t size)
{

	int	field_width, qualifier;
	int	readbytes = 0;
	int	c;
	int16_t stemp;
	int32_t ntemp;
	long 	ltemp;
	long long lltemp;
	short	length;
	char	format[128];

	if(size <= 0 || !outfile)
		return;

	for(; *fmt; ++fmt) {
		
		if (*fmt != '%') {
			if (readbytes < size) {
				c = fgetc_unlocked(infp);
				++readbytes;
				fputc_unlocked(*fmt, outfile);
				continue;
			}
			goto filled;
		}

		++fmt;
		qualifier = -1;
		if (*fmt == 'l' || *fmt == 'L') {
			qualifier = *fmt;
			++fmt;
			if (qualifier == 'l' && *fmt == 'l') {
				qualifier = 'L';
				++fmt;
			}
		}

		field_width = -1;
		if (isdigit(*fmt))
			field_width = skip_atoi(&fmt);
			
		switch (*fmt) {
			case 's':
				c = fgetc_unlocked(infp);
				++readbytes;
				while (c && readbytes < size) {
					fputc_unlocked(c, outfile);
					c = fgetc_unlocked(infp);
					++readbytes;
				}
				if(!c) {
					fputc_unlocked(' ', outfile);
					continue;
				}
				else
					goto filled;
			case 'd':
			case 'i':
				if (qualifier == 'l') {
					if(readbytes + sizeof(long) > size)
						goto filled;
					fread(&ltemp, sizeof(long), 1, infp);
					readbytes += sizeof(long);
					fprintf(outfile,"%ld ", (long)ltemp);
				}
				else if (qualifier == 'L') {
					if(readbytes + sizeof(long long) > size)
						goto filled;
					fread(&lltemp, sizeof(long long), 1, infp);
					readbytes += sizeof(long long);
					fprintf(outfile,"%lld ", (long long)lltemp);
				}
				else {
					if(readbytes + 4 > size)
						goto filled;
					fread(&ntemp, 4, 1, infp);
					readbytes += 4;
					fprintf(outfile,"%d ", (int32_t)ntemp);
				}
				break;
			case 'b':
				if(field_width != 1 && field_width != 2
					&& field_width != 4 && field_width != 8)
					field_width = 4;
				if(readbytes + field_width > size)
					goto filled;

				//read(infd, &temp, field_width);
				switch(field_width) {
					case 1: 
						c = fgetc_unlocked(infp);
						fprintf(outfile, "%d ", (int8_t)c);
						break;
					case 2: 
						fread(&stemp, 2, 1, infp);
						fprintf(outfile, "%d ", (int16_t)stemp);
						break;
					case 8: 
						fread(&lltemp, 8, 1, infp);
						fprintf(outfile, "%lld ",(int64_t)lltemp);
						break;
					case 4:
					default:
						fread(&ntemp, 4, 1, infp);
						fprintf(outfile, "%d ", (int32_t)ntemp);
						break;
					}
				readbytes += field_width;
				break;
			default:
				if(readbytes >= size)
					goto filled;
				c = fgetc_unlocked(infp);
				++readbytes;
				fputc_unlocked(c, outfile);
				if (*fmt) {
					if( readbytes >= size)
						goto filled;
					c = fgetc_unlocked(infp);
					++readbytes;
					fputc_unlocked(c, outfile);
				} else {
					--fmt;
				}
				continue;
		}
	}

filled:

	readbytes = 0;

	c=fgetc_unlocked(infp);

	if(c == LKET_PKT_BT)  {
		fread(&length, 2, 1, infp);
		strncpy(format, "BACKTRACE: ", 12);
		fwrite(format, 11, 1, outfile);
		strncpy(format, "%0s", 4);		
		b2a_vsnprintf(format, infp, outfile, length);
	}  else if(c == LKET_PKT_USER)  {
		fread(&length, 2, 1, infp);
		strncpy(format, "USER: ", 6);
		fwrite(format, 6, 1, outfile);
		do {
			c = fgetc_unlocked(infp);
			format[readbytes++] = c;
		} while(c);
		b2a_vsnprintf(format, infp, outfile, length - readbytes);
	} else  {
		fputc_unlocked('\n', outfile);
	}
}
