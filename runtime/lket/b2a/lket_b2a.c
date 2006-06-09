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

/* Balanced binary search tree to store the 
   mapping of <pid, process name> */
GTree *appNameTree;

/* 
 * default hook format table,
 * based on tapsets/hookid_defs.stp and other hook specific files
 */
//event_desc events_des[MAX_EVT_TYPES][MAX_GRPID][MAX_HOOKID]; 
event_desc **events_des[MAX_EVT_TYPES][MAX_GRPID]; 

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

			if(hdrs[j].hookgroup==_GROUP_PROCESS &&
				(hdrs[j].hookid==_HOOKID_PROCESS_SNAPSHOT 
				|| hdrs[j].hookid==_HOOKID_PROCESS_EXECVE))
			{
				register_appname(j, infps[j], &hdrs[j]);
			} else if(hdrs[j].hookgroup==_GROUP_REGEVT)  {
				register_events(hdrs[j].hookid, infps[j], 
					hdrs[j].sys_size);
			} else  {
				print_pkt_header(outfp, &hdrs[j]);
				ascii_print(hdrs[j], infps[j], outfp, EVT_SYS);
				if(hdrs[j].total_size != hdrs[j].sys_size)
					ascii_print(hdrs[j], infps[j], outfp, EVT_USER);
			}
			fgetc_unlocked(infps[j]);
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
		//fseek(fp, 0-len, SEEK_CUR);
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
		//fseek(fp, 0-len, SEEK_CUR);
	} else  {
		free(appname);
		return;
	}
	g_tree_insert(appNameTree, (gpointer)((long)pid), (gpointer)appname);
}


gint compareFunc(gconstpointer a, gconstpointer b, gpointer user_data)
{
	if((long)(a) > (long)(b)) return 1;
	else if ((long)(a) < (long)(b)) return -1;
	else return 0;
}

void destroyAppName(gpointer data)
{
	free(data);
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

	phdr->sys_size -= sizeof(lket_pkt_header)-sizeof(phdr->total_size)-sizeof(phdr->sys_size);
	phdr->total_size -= sizeof(lket_pkt_header)-sizeof(phdr->total_size)-sizeof(phdr->sys_size);

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
	fprintf(fp, "\n%lld.%lld APPNAME: %s PID:%d PPID:%d TID:%d CPU:%d HOOKGRP:%d HOOKID:%d -- ",
		(phdr->sec*1000000LL + phdr->usec - start_timestamp)/1000000LL,
		(phdr->sec*1000000LL + phdr->usec- start_timestamp)%1000000LL,
		(char *)(g_tree_lookup(appNameTree, (gconstpointer)((long)phdr->pid))),
		phdr->pid,
		phdr->ppid,
		phdr->tid,
		phdr->cpu,
		phdr->hookgroup,
		phdr->hookid);
}

void register_events(int evt_type, FILE *infp, size_t size)
{
	int cnt=0, len=0;

	char *evt_body, *evt_fmt, *evt_names, *tmp, *fmt, *name;
	int8_t grpid, hookid;

	evt_body = malloc(size);

	fread(evt_body, size, 1, infp);

	grpid = *(int8_t *)evt_body;
	hookid  = *(int8_t *)(evt_body+1);
	
	evt_fmt = evt_body+2;
	
	for(tmp=evt_fmt; *tmp!=0; tmp++);

	evt_names = tmp+1;

	fmt = strsep(&evt_fmt, ":");
	name = strsep(&evt_names, ":");

	if(fmt==NULL || name==NULL)  {
		printf("error in event format/names string\n");
		exit(-1);
	}

	if(events_des[evt_type][grpid] == NULL)  {
		events_des[evt_type][grpid] 
		= malloc(sizeof(event_desc *)*MAX_HOOKID);
	}
	if(events_des[evt_type][grpid][hookid] == NULL)  {
		events_des[evt_type][grpid][hookid]
		= malloc(sizeof(event_desc));
	}

	while(fmt!=NULL && name!=NULL)  {
		strncpy(events_des[evt_type][grpid][hookid]->evt_fmt[cnt], fmt, 7);
		strncpy(events_des[evt_type][grpid][hookid]->evt_names[cnt], name, 64);
		strncpy(events_des[evt_type][grpid][hookid]->fmt+len, get_fmtstr(fmt), 8);
		len+=strlen(get_fmtstr(fmt));
		fmt = strsep(&evt_fmt, ":");
		name = strsep(&evt_names, ":");
		cnt++;
	}
	events_des[evt_type][grpid][hookid]->count = cnt;
	*(events_des[evt_type][grpid][hookid]->fmt+len)='\0';
	free(evt_body);
}

char *get_fmtstr(char *fmt)
{
        if(strncmp(fmt, "INT8", 4) == 0)
		return "%1b";
        if(strncmp(fmt, "INT16", 5) == 0)
		return "%2b";
        if(strncmp(fmt, "INT32", 5) == 0)
		return "%4b";
        if(strncmp(fmt, "INT64", 5) == 0)
		return "%8b";
        if(strncmp(fmt, "STRING", 6) == 0)
		return "%0s";
	return "";
}

void ascii_print(lket_pkt_header header, FILE *infp, FILE *outfile, int evt_type)
{
	int i, c;
	int16_t stemp;
	int32_t ntemp;
	long long lltemp;
	int	readbytes = 0;
	int size;

	char *fmt, *name, *buffer;
	int grpid = header.hookgroup;
	int hookid = header.hookid;


	if(evt_type == EVT_SYS) 
		size = header.sys_size;
	else
		size = header.total_size - header.sys_size;

	if(events_des[evt_type][grpid] == NULL)
		return;
	if(events_des[evt_type][grpid][hookid] == NULL)
		return;

	if(events_des[evt_type][grpid][hookid]->count <= 0 || !outfile)
		return;

	if(events_des[evt_type][grpid][hookid]->evt_fmt[0][0] == '\0')  {
		//no format is provided, dump in hex
		buffer = malloc(size);
		fread(buffer, size, 1, infp);
		fwrite(buffer, size, 1, outfile);
		return;
	}

	for(i=0; i<events_des[evt_type][grpid][hookid]->count; i++)  {
		fmt = events_des[evt_type][grpid][hookid]->evt_fmt[i];
		name = events_des[evt_type][grpid][hookid]->evt_names[i];
		fwrite(name, strlen(name), 1, outfile);
		fwrite(":", 1, 1, outfile);
		if(strncmp(fmt, "INT8", 4)==0)  {
			c = fgetc_unlocked(infp);
			fprintf(outfile, "%d,", (int8_t)c);
			readbytes+=1;
		} else if(strncmp(fmt, "INT16", 5)==0)  {
			fread(&stemp, 2, 1, infp);
			fprintf(outfile, "%d,", (int16_t)stemp);
			readbytes+=2;
		} else if(strncmp(fmt, "INT32", 5)==0) {
			fread(&ntemp, 4, 1, infp);
			fprintf(outfile, "%d,", (int32_t)ntemp);
			readbytes+=4;
		} else if(strncmp(fmt, "INT64", 5)==0) {
			fread(&lltemp, 8, 1, infp);
			fprintf(outfile, "%lld,",lltemp);
			readbytes+=8;
		} else if(strncmp(fmt, "STRING", 6)==0)  {
			c = fgetc_unlocked(infp);
			++readbytes;
			while (c && readbytes < size) {
				fputc_unlocked(c, outfile);
				c = fgetc_unlocked(infp);
				++readbytes;
			}
			if(!c) {
				fputc_unlocked(',', outfile);
				continue;
			}
			else
				return;
		}
	}
}
