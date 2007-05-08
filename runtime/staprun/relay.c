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

int out_fd[NR_CPUS];
static pthread_t reader[NR_CPUS];
static int relay_fd[NR_CPUS];
static int bulkmode = 0;
static int stop_threads = 0;

/*
 * ppoll exists in glibc >= 2.4
 */
#if (__GLIBC__ < 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ < 4))
static int ppoll(struct pollfd *fds, nfds_t nfds,
		 const struct timespec *timeout, const sigset_t *sigmask)
{
	sigset_t origmask;
	int ready;
	int tim;
	if (timeout == NULL)
		tim = -1;
	else
		tim = timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000;

	sigprocmask(SIG_SETMASK, sigmask, &origmask);
	ready = poll(fds, nfds, tim);
	sigprocmask(SIG_SETMASK, &origmask, NULL);
	return ready;
}
#endif

/**
 *	reader_thread - per-cpu channel buffer reader
 */

static void *reader_thread(void *data)
{
        char buf[131072];
        int rc, cpu = (int)(long)data;
        struct pollfd pollfd;
	struct timespec tim = {.tv_sec=0, .tv_nsec=10000000}, *timeout = &tim;
	sigset_t sigs;

	sigemptyset(&sigs);
	sigaddset(&sigs,SIGUSR2);

	if (bulkmode) {
		cpu_set_t cpu_mask;
		CPU_ZERO(&cpu_mask);
		CPU_SET(cpu, &cpu_mask);
		if( sched_setaffinity( 0, sizeof(cpu_mask), &cpu_mask ) < 0 ) {
			perror("sched_setaffinity");
		}
		timeout = NULL;
	}
	
	pollfd.fd = relay_fd[cpu];
	pollfd.events = POLLIN;

        do {
                rc = ppoll(&pollfd, 1, &tim, &sigs);
                if (rc < 0) {
			dbug(3, "poll=%d errno=%d\n", rc, errno);
                        if (errno != EINTR) {
				fprintf(stderr, "poll error: %s\n",strerror(errno));
				pthread_exit(NULL);
                        }
			stop_threads = 1;
                }
		while ((rc = read(relay_fd[cpu], buf, sizeof(buf))) > 0) {
			if (write(out_fd[cpu], buf, rc) != rc) {
				fprintf(stderr, "Couldn't write to output fd %d for cpu %d, exiting: errcode = %d: %s\n", 
					out_fd[cpu], cpu, errno, strerror(errno));
				pthread_exit(NULL);
			}
		}
        } while (!stop_threads);
	dbug(3, "exiting thread\n");
	pthread_exit(NULL);
}

/**
 *	init_relayfs - create files and threads for relayfs processing
 *
 *	Returns 0 if successful, negative otherwise
 */
int init_relayfs(void)
{
	int i;
	struct statfs st;
	char buf[128], relay_filebase[128];

	dbug(1, "initializing relayfs\n");

	reader[0] = (pthread_t)0;
	relay_fd[0] = 0;
	out_fd[0] = 0;

 	if (statfs("/sys/kernel/debug", &st) == 0 && (int) st.f_type == (int) DEBUGFS_MAGIC)
 		sprintf(relay_filebase, "/sys/kernel/debug/systemtap/%s", modname);
 	else {
		fprintf(stderr,"Cannot find relayfs or debugfs mount point.\n");
		return -1;
	}


	for (i = 0; i < NR_CPUS; i++) {
		sprintf(buf, "%s/trace%d", relay_filebase, i);
		dbug(2, "attempting to open %s\n", buf);
		relay_fd[i] = open(buf, O_RDONLY | O_NONBLOCK);
		if (relay_fd[i] < 0)
			break;
	}
	ncpus = i;
	dbug(2, "ncpus=%d\n", ncpus);

	if (ncpus == 0) {
		err("couldn't open %s.\n", buf);
		return -1;
	}
	if (ncpus > 1)
		bulkmode = 1;

	if (bulkmode) {
		for (i = 0; i < ncpus; i++) {
			if (outfile_name) {
				/* special case: for testing we sometimes want to write to /dev/null */
				if (strcmp(outfile_name, "/dev/null") == 0)
					strcpy(buf, outfile_name);
				else
					sprintf(buf, "%s_%d", outfile_name, i);
			} else
				sprintf(buf, "stpd_cpu%d", i);
			
			out_fd[i] = open (buf, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			if (out_fd[i] < 0) {
				fprintf(stderr, "ERROR: couldn't open output file %s.\n", buf);
				return -1;
			}
		}
	} else {
		/* stream mode */
		if (outfile_name) {
			out_fd[0] = open (outfile_name, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			if (out_fd[0] < 0) {
				fprintf(stderr, "ERROR: couldn't open output file %s.\n", outfile_name);
				return -1;
			}
		} else
			out_fd[0] = STDOUT_FILENO;
		
	}
	dbug(2, "starting threads\n");
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

	stop_threads = 1;

	dbug(2, "closing\n");
	for (i = 0; i < ncpus; i++) {
		if (reader[i]) 
			pthread_kill(reader[i], SIGUSR2);
		else
			break;
	}
	dbug(2, "sent SIGUSR2\n");
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
	dbug(2, "done\n");
}

