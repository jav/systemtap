/* -*- linux-c -*-
 *
 * relay.c - staprun relayfs functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2007 Red Hat Inc.
 */

#include "staprun.h"

#ifndef STP_OLD_TRANSPORT

int out_fd[NR_CPUS];
static pthread_t reader[NR_CPUS];
static int relay_fd[NR_CPUS];
static int stop_threads = 0;
static int bulkmode = 0;

/**
 *	reader_thread - per-cpu channel buffer reader
 */

static void *reader_thread(void *data)
{
        char buf[131072];
        int rc, cpu = (int)(long)data;
        struct pollfd pollfd;
	int max_rd = 0;

	if (bulkmode) {
		cpu_set_t cpu_mask;
		CPU_ZERO(&cpu_mask);
		CPU_SET(cpu, &cpu_mask);
		if( sched_setaffinity( 0, sizeof(cpu_mask), &cpu_mask ) < 0 ) {
			perror("sched_setaffinity");
		}
	}
	
	pollfd.fd = relay_fd[cpu];
	pollfd.events = POLLIN;

        do {
                rc = poll(&pollfd, 1, 10);
                if (rc < 0) {
                        if (errno != EINTR) {
				fprintf(stderr, "poll error: %s\n",strerror(errno));
				pthread_exit(NULL);
                        }
                        fprintf(stderr, "poll warning: %s\n",strerror(errno));
                }
                rc = read(relay_fd[cpu], buf, sizeof(buf));
                if (!rc) {
                        continue;
		}
                if (rc < 0) {
                        if (errno == EAGAIN)
                                continue;
			fprintf(stderr, "error reading fd %d on cpu %d: %s\n", relay_fd[cpu], cpu, strerror(errno));
			continue;
                }

		if (rc > max_rd)
			max_rd = rc;

                if (write(out_fd[cpu], buf, rc) < 0) {
			fprintf(stderr, "Couldn't write to output fd %d for cpu %d, exiting: errcode = %d: %s\n", 
				out_fd[cpu], cpu, errno, strerror(errno));
			pthread_exit(NULL);
                }

        } while (!stop_threads);
	pthread_exit((void *)(long)max_rd);
}

/**
 *	init_relayfs - create files and threads for relayfs processing
 *
 *	Returns 0 if successful, negative otherwise
 */
int init_relayfs(struct _stp_msg_trans *t)
{
	int i;
	struct statfs st;
	char buf[128], relay_filebase[128];

	bulkmode = t->bulk_mode;
	dbug("initializing relayfs. bulkmode = %d\n", bulkmode);

	reader[0] = (pthread_t)0;
	relay_fd[0] = 0;
	out_fd[0] = 0;

 	if (statfs("/sys/kernel/debug", &st) == 0 && (int) st.f_type == (int) DEBUGFS_MAGIC)
 		sprintf(relay_filebase, "/sys/kernel/debug/systemtap_%d", getpid());
 	else {
		fprintf(stderr,"Cannot find relayfs or debugfs mount point.\n");
		return -1;
	}

	if (bulkmode) {
		for (i = 0; i < ncpus; i++) {
			sprintf(buf, "%s/trace%d", relay_filebase, i);
			dbug("opening %s\n", buf);
			relay_fd[i] = open(buf, O_RDONLY | O_NONBLOCK);
			if (relay_fd[i] < 0) {
				fprintf(stderr, "ERROR: couldn't open relayfs file %s.\n", buf);
				return -1;
			}
			
			if (outfile_name) {
				/* special case: for testing we sometimes want to write to /dev/null */
				if (strcmp(outfile_name, "/dev/null") == 0)
					strcpy(buf, outfile_name);
				else
					sprintf(buf, "%s_%d", outfile_name, i);
			} else
				sprintf(buf, "stpd_cpu%d", i);

			out_fd[i] = open (buf, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			dbug("out_fd[%d] = %d\n", i, out_fd[i]);
			if (out_fd[i] < 0) {
				fprintf(stderr, "ERROR: couldn't open output file %s.\n", buf);
				return -1;
			}
		}
		
	} else {
		/* stream mode */
		ncpus = 1;
		sprintf(buf, "%s/trace0", relay_filebase);
		dbug("opening %s\n", buf);
		relay_fd[0] = open(buf, O_RDONLY | O_NONBLOCK);
		dbug("got fd=%d\n", relay_fd[0]);
		if (relay_fd[0] < 0) {
			fprintf(stderr, "ERROR: couldn't open relayfs file %s.\n", buf);
			return -1;
		}

		if (outfile_name) {
			out_fd[0] = open (outfile_name, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			if (out_fd[0] < 0) {
				fprintf(stderr, "ERROR: couldn't open output file %s.\n", outfile_name);
				return -1;
			}
		} else
			out_fd[0] = STDOUT_FILENO;

	}
	dbug("starting threads\n");
	for (i = 0; i < ncpus; i++) {
		if (pthread_create(&reader[i], NULL, reader_thread, (void *)(long)i) < 0) {
			fprintf(stderr, "failed to create thread\n");
			perror("Error creating thread");
			return -1;
		}
	}		

	return 0;
}

void close_relayfs(void)
{
	int i;
	void *res;
	dbug("closing\n");

	stop_threads = 1;

	for (i = 0; i < ncpus; i++) {
		if (reader[i]) 
			pthread_join(reader[i], &res);
		else
			break;
	}

	for (i = 0; i < ncpus; i++) {
		if (relay_fd[i] >= 0)
			close(relay_fd[i]);
		else
			break;
	}
	dbug("closed files\n");
}

#endif /* !STP_OLD_TRANSPORT */
