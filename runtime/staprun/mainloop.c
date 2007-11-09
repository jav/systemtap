/* -*- linux-c -*-
 *
 * mainloop - staprun main loop
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2005-2007 Red Hat Inc.
 */

#include "staprun.h"
#include <sys/utsname.h>

/* globals */
int ncpus;
int use_old_transport = 0;

static void sigproc(int signum)
{
	dbug(2, "sigproc %d (%s)\n", signum, strsignal(signum));

	if (signum == SIGCHLD) {
		pid_t pid = waitpid(-1, NULL, WNOHANG);
		if (pid != target_pid)
			return;
		send_request(STP_EXIT, NULL, 0);
	} else if (signum == SIGQUIT)
		cleanup_and_exit(2);	
	else if (signum == SIGINT || signum == SIGHUP || signum == SIGTERM)
		send_request(STP_EXIT, NULL, 0);
}

static void setup_main_signals(int cleanup)
{
	struct sigaction a;
	memset(&a, 0, sizeof(a));
	sigfillset(&a.sa_mask);
	if (cleanup == 0) {
		a.sa_handler = sigproc;
		sigaction(SIGCHLD, &a, NULL);
	} else 
		a.sa_handler = SIG_IGN;
	sigaction(SIGINT, &a, NULL);
	sigaction(SIGTERM, &a, NULL);
	sigaction(SIGHUP, &a, NULL);
	sigaction(SIGQUIT, &a, NULL);
}


/* 
 * start_cmd forks the command given on the command line
 * with the "-c" option. It will not exec that command
 * until it received signal SIGUSR1. We do it this way because 
 * we must have the pid of the forked command so it can be set to 
 * the module and made available internally as _stp_target.
 * SIGUSR1 is sent from stp_main_loop() below when it receives
 * STP_START from the module.
 */
void start_cmd(void)
{
	pid_t pid;
	sigset_t usrset;
	struct sigaction a;

	sigemptyset(&usrset);
	sigaddset(&usrset, SIGUSR1);
	pthread_sigmask(SIG_BLOCK, &usrset, NULL);

	/* if we are execing a target cmd, ignore ^C in stapio */
	/* and let the target cmd get it. */
	sigemptyset(&a.sa_mask);
	a.sa_flags = 0;
	a.sa_handler = SIG_IGN;
	sigaction(SIGINT, &a, NULL);

	dbug (1, "execing target_cmd %s\n", target_cmd);
	if ((pid = fork()) < 0) {
		_perr("fork");
		exit(1);
	} else if (pid == 0) {
		int signum;

		a.sa_handler = SIG_DFL;
		sigaction(SIGINT, &a, NULL);

		/* commands we fork need to run at normal priority */
		setpriority (PRIO_PROCESS, 0, 0);
		
		/* wait here until signaled */
		sigwait(&usrset, &signum);

		if (execl("/bin/sh", "sh", "-c", target_cmd, NULL) < 0)
			perror(target_cmd);
		_exit(1);
	}
	target_pid = pid;
}

/** 
 * system_cmd() executes system commands in response
 * to an STP_SYSTEM message from the module. These
 * messages are sent by the system() systemtap function.
 */
void system_cmd(char *cmd)
{
	pid_t pid;

	dbug (2, "system %s\n", cmd);
	if ((pid = fork()) < 0) {
		_perr("fork");
	} else if (pid == 0) {
		setpriority (PRIO_PROCESS, 0, 0);
		if (execl("/bin/sh", "sh", "-c", cmd, NULL) < 0)
			perr("%s", cmd);
		_exit(1);
	}
}

/* This is only used in the old relayfs code */
static void read_buffer_info(void)
{
	char buf[PATH_MAX];
	struct statfs st;
	int fd, len, ret;

	if (!use_old_transport)
		return;

 	if (statfs("/sys/kernel/debug", &st) == 0 && (int) st.f_type == (int) DEBUGFS_MAGIC)
		return;

	if (sprintf_chk(buf, "/proc/systemtap/%s/bufsize", modname))
		return;
	fd = open(buf, O_RDONLY);
	if (fd < 0)
		return;

	len = read(fd, buf, sizeof(buf));
	if (len <= 0) {
		perr("Couldn't read bufsize");
		close(fd);
		return;
	}
	ret = sscanf(buf, "%u,%u", &n_subbufs, &subbuf_size);
	if (ret != 2)
		perr("Couldn't read bufsize");

	dbug(2, "n_subbufs= %u, size=%u\n", n_subbufs, subbuf_size);
	close(fd);
	return;
}


/**
 *	init_stapio - initialize the app
 *	@print_summary: boolean, print summary or not at end of run
 *
 *	Returns 0 on success, negative otherwise.
 */
int init_stapio(void)
{
	dbug(2, "init_stapio\n");

	/* create control channel */
	use_old_transport = init_ctl_channel(0);
	if (use_old_transport < 0) {
		err("Failed to initialize control channel.\n");
		return -1;
	}
	read_buffer_info();

	if (attach_mod) {
		dbug(2, "Attaching\n");
		if (use_old_transport) {
			if (init_oldrelayfs() < 0) {
				close_ctl_channel();
				return -1;
			} 
		} else {
			if (init_relayfs() < 0) {
				close_ctl_channel();
				return -1;
			}
		}
		return 0;
	}

	/* fork target_cmd if requested. */
	/* It will not actually exec until signalled. */
	if (target_cmd)
		start_cmd();


	return 0;
}

/* cleanup_and_exit() closed channels and frees memory
 * then exits with the following status codes:
 * 1 - failed to initialize.
 * 2 - disconnected
 * 3 - initialized
 */
void cleanup_and_exit (int closed)
{
	pid_t err;
	static int exiting = 0;

	if (exiting)
		return;
	exiting = 1;

	setup_main_signals(1);

	dbug(1, "CLEANUP AND EXIT  closed=%d\n", closed);

	/* what about child processes? we will wait for them here. */
	err = waitpid(-1, NULL, WNOHANG);
	if (err >= 0)
		err("\nWaiting for processes to exit\n");
	while(wait(NULL) > 0) ;

	if (use_old_transport)
		close_oldrelayfs(closed == 2);
	else
		close_relayfs();

	dbug(1, "closing control channel\n");
	close_ctl_channel();

	if (initialized == 2 && closed == 2) {
		err("\nDisconnecting from systemtap module.\n"		\
		    "To reconnect, type \"staprun -A %s\"\n", modname);
	} else if (initialized)
		closed = 3;
	else
		closed = 1;
	exit(closed);
}

/**
 *	stp_main_loop - loop forever reading data
 */

int stp_main_loop(void)
{
	ssize_t nb;
	void *data;
	int type;
	FILE *ofp = stdout;
	char recvbuf[8196];

	setvbuf(ofp, (char *)NULL, _IOLBF, 0);
	setup_main_signals(0);

	dbug(2, "in main loop\n");

	while (1) { /* handle messages from control channel */
		nb = read(control_channel, recvbuf, sizeof(recvbuf));
		if (nb <= 0) {
			if (errno != EINTR)
				_perr("Unexpected EOF in read (nb=%ld)", (long)nb);
			continue;
		}
		
		type = *(int *)recvbuf;
		data = (void *)(recvbuf + sizeof(int));

		switch (type) { 
#ifdef STP_OLD_TRANSPORT
		case STP_REALTIME_DATA:
		{
			ssize_t bw = write(out_fd[0], data, nb - sizeof(int));
			if (bw >= 0 && bw != (nb - (ssize_t)sizeof(int))) {
				nb = nb - bw; 
				bw = write(out_fd[0], data, nb - sizeof(int));
			}
			if (bw != (nb - (ssize_t)sizeof(int))) {
				_perr("write error (nb=%ld)", (long)nb);
				cleanup_and_exit(1);
			}
                        break;
		}
#endif
		case STP_OOB_DATA:
			fputs ((char *)data, stderr);
			break;
		case STP_EXIT: 
		{
			/* module asks us to unload it and exit */
			int *closed = (int *)data;
			dbug(2, "got STP_EXIT, closed=%d\n", *closed);
			cleanup_and_exit(*closed);
			break;
		}
		case STP_START: 
		{
			struct _stp_msg_start *t = (struct _stp_msg_start *)data;
			dbug(2, "probe_start() returned %d\n", t->res);
			if (t->res < 0) {
				if (target_cmd)
					kill (target_pid, SIGKILL);
				cleanup_and_exit(1);
			} else if (target_cmd)
				kill (target_pid, SIGUSR1);
			break;
		}
		case STP_SYSTEM:
		{
			struct _stp_msg_cmd *c = (struct _stp_msg_cmd *)data;
			dbug(2, "STP_SYSTEM: %s\n", c->cmd);
			system_cmd(c->cmd);
			break;
		}
		case STP_TRANSPORT:
		{
			struct _stp_msg_start ts;
			if (use_old_transport) {
				if (init_oldrelayfs() < 0)
					cleanup_and_exit(1);
			} else {
				if (init_relayfs() < 0)
					cleanup_and_exit(1);
			}
			ts.target = target_pid;
			initialized = 2;
			send_request(STP_START, &ts, sizeof(ts));
			if (load_only)
				cleanup_and_exit(2);
			break;
		}
		default:
			err("WARNING: ignored message of type %d\n", (type));
		}
	}
	fclose(ofp);
	return 0;
}
