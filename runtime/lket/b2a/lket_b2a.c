// Copyright (C) 2005, 2006 IBM Corp.
// Copyright (C) 2006 Red Hat Inc.
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
#include <time.h>
#include "lket_b2a.h"

/* A flag indicate whether to store the trace
   data into local file/MySQL database */
int into_file, into_db;
int name_flag=1, id_flag=0, appname_flag=1;
#ifdef HAS_MYSQL

#define SQLSIZE 1024*1024
int sql_count;
#define INSERT_THRESHOLD 100
char sql[4096];
char sqlStatement[SQLSIZE];
char sql_col[1024];
char sql_val[2048];

MYSQL mysql;
#endif

/* A FILE handle points to a local file used to
   store the trace data */
FILE *outfp;

#define TIMING_GETCYCLES       0x01
#define TIMING_GETTIMEOFDAY    0x02
#define TIMING_SCHEDCLOCK      0x03

typedef struct _cpufreq_info {
	long		timebase;
	long long	last_cycles;
	long long	last_time;
} cpufreq_info;

#define MAX_CPUS		256
cpufreq_info cpufreq[MAX_CPUS];

static long timing_method = TIMING_GETTIMEOFDAY;

static long long start_timestamp;

GTree *appNameTree;

/* event table */
event_desc *events_des[MAX_EVT_TYPES][MAX_GRPID][MAX_HOOKID]; 

void usage()
{
printf("Usage:\n\
  lket-b2a Options INFILE1 [INFILE2...]\n\
    Options:\n\
       -f     dump the trace data into a local file named \"lket.out\"\n\
       -n     name_flag. name_flag set to 0 means not printing the event\n\
              description string and 1 means printing. Only valid with -f\n\
              option. name_flag is set to 1 by default.\n\
       -i     id_flag. id_flag set to 0 means not printing event groupid and\n\
              hookid and 1 means printing. Only valid with -f option. id_flag\n\
              is set to 0 by default.\n\
       -a     appname_flag. appname_flag set to 0 means not printing process\n\
              name and 1 means printing. Only valid with -f option. appname_flag\n\
              is set to 1 by default.\n\
       -m     dump the trace data into MySQL\n\
   Example:\n\
       lket-b2a -f -a 1 -i 1 -n 0 stpd_cpu*\n\
       lket-b2a -m stpd_cpu*\n");
}

int main(int argc, char *argv[])
{
	lket_pkt_header *hdrs = NULL;
	FILE	**infps = NULL;
	char	outfilename[MAX_STRINGLEN]={0};
	int	i, j, total_infiles = 0;
	long long min;

	char database[18];
	time_t timer;
        struct tm *tm;

        time(&timer);
        tm = localtime(&timer);
        strftime(database, 18,  "DB%Y%m%d%H%M%S", tm);

        while (1) {
                int c = getopt(argc, argv, "mfi:n:a:");
                if (c < 0) // no more options
                        break;
                switch (c) {
		case 'm':
			into_db = 1;
			break;
		case 'f':
			into_file = 1;
			break;
		case 'n':
			name_flag = atoi(optarg);
			if(name_flag!=0 && name_flag!=1) {
				fprintf(stderr, "you must specify 0 or 1 for -n option\n");
				usage();
				exit(-1);
			}
			break;
		case 'i':
			id_flag = atoi(optarg);
			if(id_flag!=0 && id_flag!=1) {
				fprintf(stderr, "you must specify 0 or 1 for -i option\n");
				usage();
				exit(-1);
			}
			break;
		case 'a':
			appname_flag = atoi(optarg);
			if(appname_flag!=0 && appname_flag!=1) {
				fprintf(stderr, "you must specify 0 or 1 for -a option\n");
				usage();
				exit(-1);
			}
			break;

		default:
			printf("Error in options\n");
			usage();
			exit(-1);
			break;
		}
        }

#ifndef HAS_MYSQL
	if(into_db)  {
		fprintf(stderr, "-m option is not supported since lket-b2a is not compiled with mysql support, \n");
		exit(-1);
	}
#endif
	if(into_file==0 && into_db==0)  {
#ifdef HAS_MYSQL
		fprintf(stderr, "At least one of -m/-f option should be specified\n");
#else
		fprintf(stderr, "-f option must be specified\n");
#endif
		usage();
		exit(-1);
	}

	total_infiles = argc - optind;

	// open the input files and the output file
	infps = (FILE **)malloc(total_infiles * sizeof(FILE *));
	if(!infps) {
		printf("Unable to malloc infps\n");
		return 1;
	}

	memset(infps, 0, total_infiles * sizeof(FILE *));
	for(i=0; i < total_infiles; i++) {
		infps[i] = fopen(argv[optind++], "r");
		if(infps[i] == NULL) {
			printf("Unable to open %s\n", argv[optind-1]);
			goto failed;
		}
	}

	if(into_file)  {
		if(strnlen(outfilename, MAX_STRINGLEN) == 0)
			strncpy(outfilename, DEFAULT_OUTFILE_NAME, MAX_STRINGLEN);
	
		outfp = fopen(outfilename, "w");
		if(outfp == NULL) {
			fprintf(stderr,"Unable to create %s\n", outfilename);
			goto failed;
		}
	}
	/* create the search tree */
        appNameTree = g_tree_new_full(compareFunc, NULL, NULL, destroyTreeData);

#ifdef HAS_MYSQL
	if(into_db)  {
		if(!mysql_init(&mysql))  {
			fprintf(stderr, "Failed to Init MySQL: Error: %s\n",
				mysql_error(&mysql));
		}
		if(!mysql_real_connect(&mysql, NULL, NULL, NULL, NULL, 0, NULL, 
			CLIENT_MULTI_STATEMENTS))  {
			fprintf(stderr, "Failed to connect to database: Error: %s\n",
				mysql_error(&mysql));
		}

		snprintf(sql, 64,"create database %s", database);

		if(mysql_query(&mysql, sql))  {
			fprintf(stderr, "Failed create database %s, Error: %s\n",
				database, mysql_error(&mysql));
		}

		if(!mysql_real_connect(&mysql, NULL, NULL, NULL, database, 0, NULL, 
			CLIENT_MULTI_STATEMENTS))  {
			fprintf(stderr, "Failed to connect to database %s: Error: %s\n",
				database, mysql_error(&mysql));
		}
	}
#endif
	
	// find the lket header
	find_init_header(infps, total_infiles);

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
		if( (hdrs[i].microsecond < start_timestamp && hdrs[i].microsecond >0)
		       || (start_timestamp == 0)) {
			start_timestamp = hdrs[i].microsecond;
			j = i;
		}
	}

	// initialize the start cycles
	if(timing_method == TIMING_GETCYCLES) {
		for(i=0; i<MAX_CPUS; i++) {
			cpufreq[i].last_cycles = start_timestamp;
			cpufreq[i].last_time   = 0;
		}
	}

	// main loop of parsing & merging
	min = start_timestamp;
	do {
		// j is the next
		if(min) {
			if(HDR_GroupID(&hdrs[j])==_GROUP_REGEVT)  {
				if(HDR_HookID(&hdrs[j]) == _HOOKID_REGEVTDESC)
					register_evt_desc(infps[j],hdrs[j].sys_size);
				else
					register_events(HDR_HookID(&hdrs[j]), infps[j], 
						hdrs[j].sys_size);
			} else  {

				if(HDR_GroupID(&hdrs[j])==_GROUP_PROCESS &&
					(HDR_HookID(&hdrs[j])==_HOOKID_PROCESS_SNAPSHOT 
					|| HDR_HookID(&hdrs[j])==_HOOKID_PROCESS_EXECVE
					|| HDR_HookID(&hdrs[j])==_HOOKID_PROCESS_FORK))
				{
					register_appname(j, infps[j], &hdrs[j]);
				}

				if(HDR_GroupID(&hdrs[j])==_GROUP_CPUFREQ
					&& HDR_HookID(&hdrs[j])==_HOOKID_SWITCH_CPUFREQ
					&& timing_method == TIMING_GETCYCLES) 
				{
					int64_t new_timebase;
					fread(&new_timebase, sizeof(new_timebase), 1, infps[j]);

					cpufreq[HDR_CpuID(&hdrs[j])].last_time += 
						(hdrs[j].microsecond 
						- cpufreq[HDR_CpuID(&hdrs[j])].last_cycles)
						/ cpufreq[HDR_CpuID(&hdrs[j])].timebase;
					cpufreq[j].last_cycles = hdrs[j].microsecond;
					cpufreq[HDR_CpuID(&hdrs[j])].timebase = new_timebase;

					fseek(infps[j], -sizeof(new_timebase), SEEK_CUR);
				}

				dump_data(hdrs[j], infps[j]);
			}
			// update hdr[j]
#ifdef DEBUG_OUTPUT
			fprintf(stderr, "File %d, Offset: %ld\n", j, ftell(infps[j]));
#endif
			get_pkt_header(infps[j], &hdrs[j]);
		}
		// recalculate the smallest timestamp
		min = hdrs[0].microsecond;
		j = 0;
		for(i=1; i < total_infiles ; i++) {
			if((min == 0) || 
				(hdrs[i].microsecond < min && hdrs[i].microsecond > 0)) {
				min = hdrs[i].microsecond;
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

	for(i=1; i<MAX_GRPID; i++)
	for(j=1; j<MAX_HOOKID; j++) {
		if(events_des[_HOOKID_REGSYSEVT][i][j] != NULL) {
#ifdef HAS_MYSQL
			if(events_des[_HOOKID_REGSYSEVT][i][j]->flag == 0 && into_db)  {
				snprintf(sql, 256, "drop table %d_%d",i,j);
				if(mysql_query(&mysql,sql))  {
					fprintf(stderr, "Failed to exec sql: %s, Error: %s\n",
						sql, mysql_error(&mysql));
					exit(-1);
				}
				snprintf(sql, 256, "delete from table_desc where table_name='%d_%d'",i,j);
				if(mysql_query(&mysql,sql))  {
					fprintf(stderr, "Failed to exec sql: %s, Error: %s\n",
						sql, mysql_error(&mysql));
					exit(-1);
				}
			}
			/* destroy entrytime tree */
			if(events_des[_HOOKID_REGSYSEVT][i][j]->entrytime)
				g_tree_destroy(events_des[_HOOKID_REGSYSEVT][i][j]->entrytime);
#endif
		}
	}

#ifdef HAS_MYSQL
	if(into_db)  {
		mysql_close(&mysql);
	}
#endif
	if (appNameTree)
		g_tree_destroy(appNameTree);

	return 0;
}

/* register newly found process name for addevent.process.snapshot
  and addevent.process.execve */
void register_appname(int i, FILE *fp, lket_pkt_header *phdr)
{
	int pid, tid, ppid;
	char *appname=NULL;
	int count;
	int len;
	int c;
	int location;
	len=0;
	count=0;

#ifdef HAS_MYSQL
	static int flag = 0;

	if(into_db)  {
		if(flag==0)  {
			if(mysql_query(&mysql, "create table appNameMap ( pid INT, pname varchar(20))"))  {
				fprintf(stderr, "Failed to create appNameMap table, Error: %s\n",
					mysql_error(&mysql));
				exit(-1);
			}
		}
		flag=1;
	}
#endif
	appname = (char *)malloc(512);
	location = ftell(fp);

	if(HDR_HookID(phdr) == _HOOKID_PROCESS_SNAPSHOT )  {  /* process_snapshot */
		fread(&tid, 1, 4, fp); /* read tid */
		fread(&pid, 1, 4, fp); /* read pid */
		fread(&ppid, 1, 4, fp); /* read ppid */
		c = fgetc_unlocked(fp);
		len+=13;
		while (c && len < 1024) {
			appname[count++] = (char)c;	
			c = fgetc_unlocked(fp);
			++len;
		}
		appname[count]='\0';
	} else if (HDR_HookID(phdr) == _HOOKID_PROCESS_EXECVE)  { /* process.execve */
		fread(&tid, 1, 4, fp); /* read tid */
		fread(&pid, 1, 4, fp); /* read pid */
		fread(&ppid, 1, 4, fp); /* read ppid */
		c = fgetc_unlocked(fp);
		len+=5;
		while (c && len < 1024) {
			appname[count++] = (char)c;	
			c = fgetc_unlocked(fp);
			++len;
		}
		appname[count]='\0';
	} else  if (HDR_HookID(phdr) == _HOOKID_PROCESS_FORK) {
		fread(&tid, 1, 4, fp); /* read tid */
		fread(&pid, 1, 4, fp); /* read pid */
		fread(&ppid, 1, 4, fp); /* read ppid */

		strncpy(appname, (char *)(g_tree_lookup(appNameTree,(gconstpointer)((long)ppid))), 256);

	} else {
		free(appname);
		return;
	}
	fseek(fp, location, SEEK_SET);
#ifdef HAS_MYSQL
		if(into_db)  {
			snprintf(sql, 256,"insert into appNameMap values ( %d, \"%s\")", pid, appname);
			if(mysql_query(&mysql,sql))  {
				fprintf(stderr, "Failed to exec SQL: %s, Error: %s\n",
					sql, mysql_error(&mysql));
			exit(-1);
			}
		}
#endif
	g_tree_insert(appNameTree, (gpointer)((long)pid), (gpointer)appname);
}


gint compareFunc(gconstpointer a, gconstpointer b, gpointer user_data)
{
        if((long)(a) > (long)(b)) return 1;
        else if ((long)(a) < (long)(b)) return -1;
        else return 0;
}

void destroyTreeData(gpointer data)
{
        free(data);
}

/* 
 * search the LKET init header in a set of input files, 
 * and the header structure is defined in tapsets/lket_trace.stp
 */
void find_init_header(FILE **infps, const int total_infiles)
{
	int 	 i, j, k;
	int32_t magic;

	/* information from lket_init_header */
	int16_t inithdr_len;
	int8_t ver_major;
	int8_t ver_minor;
	int8_t big_endian;
	int8_t timing_field; 
	int8_t bits_width;
	int32_t init_timebase;
	char  timing_methods_str[128];

	if(total_infiles <= 0 )
		return;
	j = total_infiles;
	for(i=0; i<total_infiles; i++) {
		if(fread(&magic, 1, sizeof(magic), infps[i]) < sizeof(magic))
			continue;
		if(magic == (int32_t)LKET_MAGIC) {
			//found
			j = i;
			if(into_file)
				fprintf(outfp, "LKET Magic:\t0x%X\n", magic);
			//read other content of lket_int_header
			if(fread(&inithdr_len, 1, sizeof(inithdr_len), infps[i]) 
				< sizeof(inithdr_len))
				break;
			if(into_file)
				fprintf(outfp, "InitHdrLen:\t%d\n", inithdr_len);
			if(fread(&ver_major, 1, sizeof(ver_major), infps[i]) < sizeof(ver_major))
				break;
			if(into_file)
				fprintf(outfp, "Version Major:\t%d\n", ver_major);
			if(fread(&ver_minor, 1, sizeof(ver_minor), infps[i]) < sizeof(ver_minor))
				break;
			if(into_file)
				fprintf(outfp, "Version Minor:\t%d\n", ver_minor);
			if(fread(&big_endian, 1, sizeof(big_endian), infps[i]) < sizeof(big_endian))
				break;
			if(into_file)
				fprintf(outfp, "Big endian:\t%s\n", big_endian ? "YES":"NO");
			if(fread(&timing_field, 1, sizeof(timing_field), infps[i]) 
				< sizeof(timing_field))
				break;
			timing_method = timing_field;
			if(into_file)
				fprintf(outfp, "Timing method:\t");
			switch(timing_method) {
				case TIMING_GETCYCLES: 
					snprintf(timing_methods_str, 128, "get_cycles");
					if(into_file)
						fprintf(outfp, "get_cycles()\n"); 
					break;
				case TIMING_GETTIMEOFDAY:
					snprintf(timing_methods_str, 128,"do_gettimeofday");
					if(into_file)
						fprintf(outfp, "do_gettimeofday()\n"); 	
					break;
				case TIMING_SCHEDCLOCK:
					snprintf(timing_methods_str, 128, "sched_clock");
					if(into_file)
						fprintf(outfp, "sched_clock()\n"); 
					break;
				default:
					snprintf(timing_methods_str,128,
						"Unsupported timing method");
					if(into_file)
						fprintf(outfp, "Unsupported timging method\n");
			}
			if(fread(&bits_width, 1, sizeof(bits_width), infps[i]) < sizeof(bits_width))
				break;
			if(into_file)
				fprintf(outfp, "Bits width:\t%d\n", bits_width);
			if(fread(&init_timebase, 1, sizeof(init_timebase), infps[i]) 
				< sizeof(init_timebase))
				break;
			if(into_file)
				fprintf(outfp, 
					"Initial CPU timebase:\t%d (cycles per microsecond)\n", 
					init_timebase);
			if(timing_method == TIMING_GETCYCLES) {
				for(k = 0; k < MAX_CPUS; k++)
					cpufreq[k].timebase = init_timebase;
			}
			break;
		}
	}

#ifdef HAS_MYSQL
	if(into_db)  {
		if(mysql_query(&mysql, "create table trace_header ( Major_Ver TINYINT, Minor_Ver TINYINT, Big_Endian TINYINT, Timing_Method varchar(20), Bits_Width TINYINT)" )) {
			fprintf(stderr, "Failed to create trace_header table, Error: %s\n",
				mysql_error(&mysql));
			exit(-1);
		}
		snprintf(sql, 256, "insert into trace_header value ( %d, %d, %d, \"%s\", %d )",
			ver_major, ver_minor, big_endian, timing_methods_str, bits_width);	

		if(mysql_query(&mysql, sql)) {
			fprintf(stderr, "Failed exec SQL %d: \n %s \n, Error: %s\n",
				__LINE__, sql, mysql_error(&mysql));
			exit(-1);
		}
	}
#endif
	for(i=0; i<total_infiles && i!=j; i++)
		fseek(infps[i], 0LL, SEEK_SET);
	return;
}

/* 
 * read the lket_pkt_header structure at the begining of the input file 
 */
int get_pkt_header(FILE *fp, lket_pkt_header *phdr)
{
	if(fread(phdr, 1, sizeof(lket_pkt_header), fp) < sizeof(lket_pkt_header))
		goto bad;

	phdr->sys_size -= sizeof(lket_pkt_header)-sizeof(phdr->total_size)-sizeof(phdr->sys_size);
	phdr->total_size -= sizeof(lket_pkt_header)-sizeof(phdr->total_size)-sizeof(phdr->sys_size);
	return 0;
bad:
	memset(phdr, 0, sizeof(lket_pkt_header));
	return -1;
}	

void print_pkt_header(lket_pkt_header *phdr)
{
	long long usecs;
	int sec, usec;
	int grpid, hookid, pid, tid, ppid;

	if(!phdr)
		return;

	if(timing_method == TIMING_GETCYCLES)
		usecs = (phdr->microsecond - cpufreq[HDR_CpuID(phdr)].last_cycles)
			/ cpufreq[HDR_CpuID(phdr)].timebase + cpufreq[HDR_CpuID(phdr)].last_time;
	else if(timing_method == TIMING_SCHEDCLOCK)
		usecs = (phdr->microsecond - start_timestamp) / 1000;
	else
		usecs = phdr->microsecond - start_timestamp;
	
	sec = usecs/1000000;
	usec = usecs%1000000;

	grpid = HDR_GroupID(phdr);	
	hookid = HDR_HookID(phdr);
	pid = HDR_PID(phdr);
	tid = HDR_TID(phdr);
	ppid = HDR_PPID(phdr);

	if(into_file) {
		fprintf(outfp, "\n%d.%d CPU:%d TID:%d, PID:%d, PPID:%d, ", sec, usec, 
			HDR_CpuID(phdr), tid, pid, ppid);
		if(appname_flag==1)
			fprintf(outfp, "APPNAME:%s ", (char *)(g_tree_lookup(appNameTree,(gconstpointer)((long)pid))));
		if(name_flag==1)
			fprintf(outfp, "EVT_NAME:%s ", events_des[_HOOKID_REGSYSEVT][grpid][hookid]->description);
		if(id_flag==1)
			fprintf(outfp, "HOOKGRP:%d HOOKID:%d ", grpid, hookid);
	}

#ifdef HAS_MYSQL
	if(into_db)  {
		if(!(hookid%2)) { // return type event
			long long *entrytime;
			long long entryusecs;
			entrytime = g_tree_lookup(events_des[_HOOKID_REGSYSEVT][grpid][hookid-1]->entrytime,
                                        (gconstpointer)((long)tid));
			if(entrytime==NULL)  // key not found
				entryusecs = 0;
			else
				entryusecs = *entrytime;
			snprintf(sql_col, 128, "groupid, hookid, usec, thread_id, process_id, parentprocess_id, \
				cpu_id, entry_usec,");
			snprintf(sql_val, 256, "%d, %d, %lld, %d, %d, %d, %d, %lld,", grpid,
				hookid, usecs, tid, pid, ppid, HDR_CpuID(phdr), 
				entryusecs);
		}  else  {
			snprintf(sql_col, 128, "groupid, hookid, usec, thread_id, process_id, parentprocess_id, cpu_id,");
			snprintf(sql_val, 256, "%d, %d, %lld, %d, %d, %d, %d, ", grpid, 
				hookid, usecs, tid, pid, ppid, HDR_CpuID(phdr));
		}
		if(hookid%2) {
			char *entrytime = malloc(sizeof(long long));
			*((long long *)entrytime) = usecs;
			g_tree_insert(events_des[_HOOKID_REGSYSEVT][grpid][hookid]->entrytime, 
				(gpointer)((long)tid), (gpointer)entrytime);
		}
	}
#endif

}

#ifdef HAS_MYSQL
char *get_sqltype(char *fmt)
{
        if(strncmp(fmt, "INT8", 4) == 0)
		return "TINYINT";
        if(strncmp(fmt, "INT16", 5) == 0)
		return "SMALLINT";
        if(strncmp(fmt, "INT32", 5) == 0)
		return "INT";
        if(strncmp(fmt, "INT64", 5) == 0)
		return "BIGINT";
        if(strncmp(fmt, "STRING", 6) == 0)
		return "VARCHAR(20)";
	return "";
}
#endif

void register_evt_desc(FILE *infp, size_t size)
{
#ifdef HAS_MYSQL
	static int has_table = 0;
#endif
	int grpid, hookid;
	int len = 0;
	char *evt_body;
	evt_body = malloc(size);
	fread(evt_body, size, 1, infp);
	grpid = *(int8_t *)evt_body;
        hookid  = *(int8_t *)(evt_body+1);
	len = strlen(evt_body+2)+2;
	if(!events_des[_HOOKID_REGSYSEVT][grpid][hookid])
		events_des[_HOOKID_REGSYSEVT][grpid][hookid] = malloc(sizeof(event_desc));
	events_des[_HOOKID_REGSYSEVT][grpid][hookid]->description = malloc(len);

	strncpy(events_des[_HOOKID_REGSYSEVT][grpid][hookid]->description, evt_body+2, len);
#ifdef HAS_MYSQL
	events_des[_HOOKID_REGSYSEVT][grpid][hookid]->entrytime = g_tree_new_full(
		compareFunc, NULL, NULL, destroyTreeData);
	if(into_db)  {
		if(!has_table)  {
			snprintf(sql, 1024, "create table table_desc ( table_name varchar(6), table_desc varchar(32))");
			if(mysql_query(&mysql, sql))  {
				fprintf(stderr, "Failed exec SQL %d: \n %s \n, Error: %s\n",
					__LINE__, sql, mysql_error(&mysql));
				exit(-1);
			}
			has_table = 1;
		} 

		snprintf(sql, 1024, "insert into table_desc ( table_name, table_desc) values ( \"%d_%d\", \"%s\")", grpid, hookid,
			evt_body+2);

		if(mysql_query(&mysql, sql))  {
			fprintf(stderr, "Failed exec SQL:%d \n %s \n, Error: %s\n",
				__LINE__, sql, mysql_error(&mysql));
			exit(-1);
		}
	}
#endif
	free(evt_body);
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

	if(!events_des[evt_type][grpid][hookid])
		events_des[evt_type][grpid][hookid] = malloc(sizeof(event_desc));
	if(!events_des[evt_type][grpid][hookid])  {
		fprintf(stderr, "error when malloc for event_des[%d][%d][%d]\n",
			evt_type, grpid, hookid);
	}

#ifdef HAS_MYSQL
	if(into_db)  {
		if(evt_type==_HOOKID_REGSYSEVT)  {  /* if sys event, create a table */
			if(!(hookid%2))  {/* if this is a return type event, should record 
					     the entry time of this event */
				snprintf(sql, 1024, "create table %d_%d ( groupid TINYINT, hookid TINYINT, usec BIGINT, thread_id INT, process_id INT, parentprocess_id INT, cpu_id TINYINT, entry_usec BIGINT,", grpid, hookid);
			} else  {
				snprintf(sql, 1024, "create table %d_%d ( groupid TINYINT, hookid TINYINT, usec BIGINT, thread_id INT, process_id INT, parentprocess_id INT, cpu_id TINYINT,", grpid, hookid);
			}
		}
		if(evt_type==_HOOKID_REGUSREVT)  { /* if user event, alter an existing table */
			snprintf(sql, 1024, "alter table %d_%d ", grpid, hookid);
		}
	}

	if(size == 2) // skip if no event format is provided
		goto gen_sql;
#endif

	evt_fmt = evt_body+2;
	
	for(tmp=evt_fmt; *tmp!=0; tmp++);

	evt_names = tmp+1;

	fmt = strsep(&evt_fmt, ":");
	name = strsep(&evt_names, ":");

	if(fmt==NULL || name==NULL)  {
		printf("error in event format/names string\n");
		exit(-1);
	}

	while(fmt!=NULL && name!=NULL)  {
#ifdef HAS_MYSQL
		if(into_db)  {
			if(evt_type==_HOOKID_REGSYSEVT)  {
				strcat(sql, "`");
				strcat(sql, name);
				strcat(sql, "` ");
				strcat(sql, get_sqltype(fmt));
				strcat(sql, ",");
			}
			if(evt_type==_HOOKID_REGUSREVT)  {
				strcat(sql, "add ");
				strcat(sql, "`");
				strcat(sql, name);
				strcat(sql, "` ");
				strcat(sql, get_sqltype(fmt));
				strcat(sql, ",");
			}
		}
#endif
		strncpy(events_des[evt_type][grpid][hookid]->evt_fmt[cnt], fmt, 7);
		strncpy(events_des[evt_type][grpid][hookid]->evt_names[cnt], 
			name, MAX_FIELDNAME_LEN);
		strncpy(events_des[evt_type][grpid][hookid]->fmt+len, get_fmtstr(fmt), 8);
		len+=strlen(get_fmtstr(fmt));
		fmt = strsep(&evt_fmt, ":");
		name = strsep(&evt_names, ":");
		cnt++;
	}
	events_des[evt_type][grpid][hookid]->count = cnt;
	*(events_des[evt_type][grpid][hookid]->fmt+len)='\0';

#ifdef HAS_MYSQL
gen_sql:
	if(into_db)  {
		if(evt_type==_HOOKID_REGSYSEVT)
			sql[strlen(sql)-1]=')';
		if(evt_type==_HOOKID_REGUSREVT)
			sql[strlen(sql)-1]='\0';

		if(mysql_query(&mysql, sql))  {
			fprintf(stderr, "Failed exec SQL %d: \n %s \n, Error: %s\n",
				__LINE__, sql, mysql_error(&mysql));
			exit(-1);
		}
	}
#endif
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

int dump_data(lket_pkt_header header, FILE *infp)
{
	int i, c, j;
	int16_t stemp;
	int32_t ntemp;
	long long lltemp;
	int readbytes = 0;
	int total_bytes = 0;
	int size = 0;
	int evt_num = 1;

	char tmp_int[32];

	char *fmt, *name, *buffer;
	int grpid = HDR_GroupID(&header);
	int hookid = HDR_HookID(&header);

	print_pkt_header(&header);

	/* if the data contains user appended extra data */
	if(header.total_size != header.sys_size)
		evt_num = 3;

	/* iterate the sys and user event */
	for(j=1; j<= evt_num; j+=2)  {
		
		readbytes = 0;

		if(j == 1) /* if current one is a sys event */
			size = header.sys_size;
		if(j == 2) /* if current one is a user event */
			size = header.total_size - header.sys_size;

		if(into_file && (events_des[j][grpid][hookid] == NULL ||
			events_des[j][grpid][hookid]->count <= 0)) {
			//no format is provided, dump in hex
			buffer = malloc(size);
			fread(buffer, size, 1, infp);
			fwrite(buffer, size, 1, outfp);
			free(buffer);
			total_bytes += size;
			continue;
		}

		events_des[j][grpid][hookid]->flag = 1;

		for(i=0; i<events_des[j][grpid][hookid]->count; i++)  {
			fmt = events_des[j][grpid][hookid]->evt_fmt[i];
			name = events_des[j][grpid][hookid]->evt_names[i];
#ifdef HAS_MYSQL
			if(into_db) {
				strcat(sql_col, "`");
				strcat(sql_col, name);
				strcat(sql_col, "`,");
			}
#endif

			if(into_file)  {
				fwrite(name, strlen(name), 1, outfp);
				fwrite(":", 1, 1, outfp);
			}
			if(strncmp(fmt, "INT8", 4)==0)  {
				c = fgetc_unlocked(infp);
				if(into_file)
					fprintf(outfp, "%d,", (int8_t)c);
				sprintf(tmp_int, "%d,", (int8_t)c);
#ifdef HAS_MYSQL
				if(into_db)
					strcat(sql_val, tmp_int);
#endif
				readbytes+=1;
			} else if(strncmp(fmt, "INT16", 5)==0)  {
				fread(&stemp, 2, 1, infp);
				if(into_file)
					fprintf(outfp, "%d,", (int16_t)stemp);
				sprintf(tmp_int, "%d,", (int16_t)stemp);
#ifdef HAS_MYSQL
				if(into_db)
					strcat(sql_val, tmp_int);
#endif
				readbytes+=2;
			} else if(strncmp(fmt, "INT32", 5)==0) {
				fread(&ntemp, 4, 1, infp);
				if(into_file)
					fprintf(outfp, "%d,", (int32_t)ntemp);
				snprintf(tmp_int, 20, "%d,", (int32_t)ntemp);
#ifdef HAS_MYSQL
				if(into_db)
					strcat(sql_val, tmp_int);
#endif
				readbytes+=4;
			} else if(strncmp(fmt, "INT64", 5)==0) {
				fread(&lltemp, 8, 1, infp);
				if(into_file)
					fprintf(outfp, "%lld,",lltemp);
				snprintf(tmp_int, 30, "%lld,", lltemp);
#ifdef HAS_MYSQL
				if(into_db)
					strcat(sql_val, tmp_int);
#endif
				readbytes+=8;
			} else if(strncmp(fmt, "STRING", 6)==0)  {

#ifdef HAS_MYSQL
				int tmplen=0;
				if(into_db) {
					tmplen=strlen(sql_val);
					sql_val[tmplen++]='"';
				}
#endif
				c = fgetc_unlocked(infp);
				++readbytes;
				while (c && readbytes < size) {
					if(into_file)
						fputc_unlocked(c, outfp);
#ifdef HAS_MYSQL
					if(into_db)
						sql_val[tmplen++]=c;
#endif
					c = fgetc_unlocked(infp);
					++readbytes;
				}
				if(!c) {
					if(into_file)
						fputc_unlocked(',', outfp);
#ifdef HAS_MYSQL
					if(into_db) {
						sql_val[tmplen++]='"';
						sql_val[tmplen++]=',';
						sql_val[tmplen]='\0';
					}
#endif
					continue;
				}
				else {
					return -1;
				}
			}
		}
		total_bytes += readbytes;
	}

#ifdef HAS_MYSQL
	if(into_db) {
		sql_col[strlen(sql_col)-1] = '\0';
		sql_val[strlen(sql_val)-1] = '\0';
		snprintf(sql, 1024, "insert into %d_%d (%s) values (%s)", 
			grpid, hookid, sql_col, sql_val);

		if(sql_count >= INSERT_THRESHOLD)  {
			if(mysql_query(&mysql, sqlStatement))  {
				fprintf(stderr, "Failed exec SQL %d:\n%s\n, Error:\n%s\n",
					__LINE__, sqlStatement, mysql_error(&mysql));
				exit(-1);
			}
			while(!mysql_next_result(&mysql));
			sql_count=0;
			sqlStatement[0]='\0';
		}  else {
			//strncpy(sqlStatement, sql, 2048);
			strcat(sqlStatement, sql);
			strcat(sqlStatement, ";");
			sql_count++;
		}
	}
#endif

	return total_bytes;
}
