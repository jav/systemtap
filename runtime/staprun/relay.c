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
static volatile int stop_threads = 0;

/*
 * ppoll exists in glibc >= 2.4
 */
#if (__GLIBC__ < 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ < 4))
#define NEED_PPOLL
#endif

#ifdef NEED_PPOLL
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

int make_outfile_name(char *buf, int max, int fnum, int cpu)
{
	if (bulkmode) {
		/* special case: for testing we sometimes want to write to /dev/null */
		if (strcmp(outfile_name, "/dev/null") == 0) {
			strcpy(buf, "/dev/null");
		} else {
			if (snprintf_chk(buf, max, "%s_cpu%d.%d",
					 outfile_name, cpu, fnum))
				return -1;
		}
	} else {
		/* stream mode */
		if (snprintf_chk(buf, max, "%s.%d", outfile_name, fnum))
			return -1;
	}
	return 0;
}

static int open_outfile(int fnum, int cpu, int remove_file)
{
	char buf[PATH_MAX];
	if (!outfile_name) {
		_err("-S is set without -o. Please file a bug report.\n");
		return -1;
	}

	if (remove_file) {
		 /* remove oldest file */
		if (make_outfile_name(buf, PATH_MAX, fnum - fnum_max, cpu) < 0)
			return -1;
		remove(buf); /* don't care */
	}

	if (make_outfile_name(buf, PATH_MAX, fnum, cpu) < 0)
		return -1;
	out_fd[cpu] = open (buf, O_CREAT|O_TRUNC|O_WRONLY, 0666);
	if (out_fd[cpu] < 0) {
		perr("Couldn't open output file %s", buf);
		return -1;
	}
	if (set_clexec(out_fd[cpu]) < 0)
		return -1;
	return 0;
}

/**
 *	reader_thread - per-cpu channel buffer reader
 */
static void empty_handler(int __attribute__((unused)) sig) { /* do nothing */  }

static void *reader_thread(void *data)
{
        char buf[131072];
        int rc, cpu = (int)(long)data;
        struct pollfd pollfd;
	struct timespec tim = {.tv_sec=0, .tv_nsec=200000000}, *timeout = &tim;
	sigset_t sigs;
	struct sigaction sa;
	off_t wsize = 0;
	int fnum = 0;
	int remove_file = 0;

	sigemptyset(&sigs);
	sigaddset(&sigs,SIGUSR2);
	pthread_sigmask(SIG_BLOCK, &sigs, NULL);

	sigfillset(&sigs);
	sigdelset(&sigs,SIGUSR2);
	
	sa.sa_handler = empty_handler;
        sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR2, &sa, NULL);

	if (bulkmode) {
		cpu_set_t cpu_mask;
		CPU_ZERO(&cpu_mask);
		CPU_SET(cpu, &cpu_mask);
		if( sched_setaffinity( 0, sizeof(cpu_mask), &cpu_mask ) < 0 )
			_perr("sched_setaffinity");
#ifdef NEED_PPOLL
		/* Without a real ppoll, there is a small race condition that could */
		/* block ppoll(). So use a timeout to prevent that. */
		timeout->tv_sec = 10;
		timeout->tv_nsec = 0;
#else
		timeout = NULL;
#endif
	}
	
	pollfd.fd = relay_fd[cpu];
	pollfd.events = POLLIN;

        do {
                rc = ppoll(&pollfd, 1, timeout, &sigs);
                if (rc < 0) {
			dbug(3, "cpu=%d poll=%d errno=%d\n", cpu, rc, errno);
                        if (errno != EINTR) {
				_perr("poll error");
				return(NULL);
                        }
                }
		while ((rc = read(relay_fd[cpu], buf, sizeof(buf))) > 0) {
			wsize += rc;
			/* Switching file */
			if (fsize_max && wsize > fsize_max) {
				close(out_fd[cpu]);
				fnum++;
				if (fnum_max && fnum == fnum_max)
					remove_file = 1;
				if (open_outfile(fnum, cpu, remove_file) < 0) {
					perr("Couldn't open file for cpu %d, exiting.", cpu);
					return(NULL);
				}
				wsize = 0;
			}
			if (write(out_fd[cpu], buf, rc) != rc) {
				perr("Couldn't write to output %d for cpu %d, exiting.", out_fd[cpu], cpu);
				return(NULL);
			}
		}
        } while (!stop_threads);
	dbug(3, "exiting thread %d\n", cpu);
	return(NULL);
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
	char rqbuf[128];
	char buf[PATH_MAX], relay_filebase[PATH_MAX];

	dbug(2, "initializing relayfs\n");

	reader[0] = (pthread_t)0;
	relay_fd[0] = 0;
	out_fd[0] = 0;

 	if (statfs("/sys/kernel/debug", &st) == 0
	    && (int) st.f_type == (int) DEBUGFS_MAGIC) {
		if (sprintf_chk(relay_filebase,
				"/sys/kernel/debug/systemtap/%s",
				modname))
			return -1;
	}
 	else {
		err("Cannot find relayfs or debugfs mount point.\n");
		return -1;
	}

	if (send_request(STP_BULK, rqbuf, sizeof(rqbuf)) > 0)
		bulkmode = 1;

	for (i = 0; i < NR_CPUS; i++) {
		if (sprintf_chk(buf, "%s/trace%d", relay_filebase, i))
			return -1;
		dbug(2, "attempting to open %s\n", buf);
		relay_fd[i] = open(buf, O_RDONLY | O_NONBLOCK);
		if (relay_fd[i] < 0 || set_clexec(relay_fd[i]) < 0)
			break;
	}
	ncpus = i;
	dbug(2, "ncpus=%d, bulkmode = %d\n", ncpus, bulkmode);

	if (ncpus == 0) {
		_err("couldn't open %s.\n", buf);
		return -1;
	}
	if (ncpus > 1 && bulkmode == 0) {
		_err("ncpus=%d, bulkmode = %d\n", ncpus, bulkmode);
		_err("This is inconsistent! Please file a bug report. Exiting now.\n");
		return -1;
	}

	if (fsize_max) {
		/* switch file mode */
		for (i = 0; i < ncpus; i++)
			if (open_outfile(0, i, 0) < 0)
				return -1;
	} else if (bulkmode) {
		for (i = 0; i < ncpus; i++) {
			if (outfile_name) {
				/* special case: for testing we sometimes want to write to /dev/null */
				if (strcmp(outfile_name, "/dev/null") == 0) {
					strcpy(buf, "/dev/null");
				} else {
					if (sprintf_chk(buf, "%s_%d", outfile_name, i))
						return -1;
				}
			} else {
				if (sprintf_chk(buf, "stpd_cpu%d", i))
					return -1;
			}
			
			out_fd[i] = open (buf, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			if (out_fd[i] < 0) {
				perr("Couldn't open output file %s", buf);
				return -1;
			}
			if (set_clexec(out_fd[i]) < 0)
				return -1;
		}
	} else {
		/* stream mode */
		if (outfile_name) {
			out_fd[0] = open (outfile_name, O_CREAT|O_TRUNC|O_WRONLY, 0666);
			if (out_fd[0] < 0) {
				perr("Couldn't open output file %s", outfile_name);
				return -1;
			}
			if (set_clexec(out_fd[i]) < 0)
				return -1;
		} else
			out_fd[0] = STDOUT_FILENO;
		
	}
	if (!load_only) {
		dbug(2, "starting threads\n");
		for (i = 0; i < ncpus; i++) {
			if (pthread_create(&reader[i], NULL, reader_thread,
					   (void *)(long)i) < 0) {
				_perr("failed to create thread");
				return -1;
			}
		}
	}
	
	return 0;
}

void close_relayfs(void)
{
	int i;
	stop_threads = 1;
	dbug(2, "closing\n");
	for (i = 0; i < ncpus; i++) {
		if (reader[i]) 
			pthread_kill(reader[i], SIGUSR2);
		else
			break;
	}
	for (i = 0; i < ncpus; i++) {
		if (reader[i])
			pthread_join(reader[i], NULL);
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

