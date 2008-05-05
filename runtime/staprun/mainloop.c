/* -*- linux-c -*-
 *
 * mainloop - staprun main loop
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2005-2008 Red Hat Inc.
 */

#include "staprun.h"
#include <sys/utsname.h>

/* globals */
int ncpus;
static int use_old_transport = 0;
//enum _stp_sig_type { sig_none, sig_done, sig_detach };
//static enum _stp_sig_type got_signal = sig_none;

/**
 *      send_request - send request to kernel over control channel
 *      @type: the relay-app command id
 *      @data: pointer to the data to be sent
 *      @len: length of the data to be sent
 *
 *      Returns 0 on success, negative otherwise.
 */
int send_request(int type, void *data, int len)
{
	char buf[1024];

	/* Before doing memcpy, make sure 'buf' is big enough. */
	if ((len + 4) > (int)sizeof(buf)) {
		_err("exceeded maximum send_request size.\n");
		return -1;
	}
	memcpy(buf, &type, 4);
	memcpy(&buf[4], data, len);
	return write(control_channel, buf, len + 4);
}

static void *signal_thread(void *arg)
{
	sigset_t *s = (sigset_t *) arg;
	int signum;

	while (1) {
		if (sigwait(s, &signum) < 0) {
			_perr("sigwait");
			continue;
		}
		dbug(2, "sigproc %d (%s)\n", signum, strsignal(signum));
		if (signum == SIGCHLD) {
			pid_t pid = waitpid(-1, NULL, WNOHANG);
			if (pid == target_pid) {
				send_request(STP_EXIT, NULL, 0);
				break;
			}
		} else if (signum == SIGQUIT)
			cleanup_and_exit(1);
		else if (signum == SIGINT || signum == SIGHUP || signum == SIGTERM) {
			send_request(STP_EXIT, NULL, 0);
			break;
		}
	}
	return NULL;
}

static void setup_main_signals(void)
{
	pthread_t tid;
	struct sigaction sa;
	sigset_t *s = malloc(sizeof(*s));
	if (!s) {
		_perr("malloc failed");
		exit(1);
	}
	sigfillset(s);
	pthread_sigmask(SIG_SETMASK, s, NULL);
	memset(&sa, 0, sizeof(sa));
	sigfillset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	sigemptyset(s);
	sigaddset(s, SIGCHLD);
	sigaddset(s, SIGINT);
	sigaddset(s, SIGTERM);
	sigaddset(s, SIGHUP);
	sigaddset(s, SIGQUIT);
	pthread_sigmask(SIG_SETMASK, s, NULL);
	if (pthread_create(&tid, NULL, signal_thread, s) < 0) {
		_perr("failed to create thread");
		exit(1);
	}
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

	dbug(1, "execing target_cmd %s\n", target_cmd);
	if ((pid = fork()) < 0) {
		_perr("fork");
		exit(1);
	} else if (pid == 0) {
		int signum;

		a.sa_handler = SIG_DFL;
		sigaction(SIGINT, &a, NULL);

		/* commands we fork need to run at normal priority */
		setpriority(PRIO_PROCESS, 0, 0);

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

	dbug(2, "system %s\n", cmd);
	if ((pid = fork()) < 0) {
		_perr("fork");
	} else if (pid == 0) {
		setpriority(PRIO_PROCESS, 0, 0);
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

	if (statfs("/sys/kernel/debug", &st) == 0 && (int)st.f_type == (int)DEBUGFS_MAGIC)
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
	use_old_transport = init_ctl_channel(modname, 1);
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

/* cleanup_and_exit() closed channels, frees memory,
 * removes the module (if necessary) and exits. */
void cleanup_and_exit(int detach)
{
	pid_t err;
	static int exiting = 0;

	if (exiting)
		return;
	exiting = 1;

	setup_main_signals();

	dbug(1, "detach=%d\n", detach);

	/* what about child processes? we will wait for them here. */
	err = waitpid(-1, NULL, WNOHANG);
	if (err >= 0)
		err("\nWaiting for processes to exit\n");
	while (wait(NULL) > 0) ;

	if (use_old_transport)
		close_oldrelayfs(detach);
	else
		close_relayfs();

	dbug(1, "closing control channel\n");
	close_ctl_channel();

	if (detach) {
		err("\nDisconnecting from systemtap module.\n" "To reconnect, type \"staprun -A %s\"\n", modname);
	} else {
		dbug(2, "removing %s\n", modname);
		if (execl(BINDIR "/staprun", "staprun", "-d", modname, NULL) < 0) {
			perror(modname);
			_exit(1);
		}
	}
	_exit(0);
}

/**
 *	stp_main_loop - loop forever reading data
 */

int stp_main_loop(void)
{
	ssize_t nb;
	void *data;
	uint32_t type;
	FILE *ofp = stdout;
	char recvbuf[8196];

	setvbuf(ofp, (char *)NULL, _IOLBF, 0);
	setup_main_signals();

	dbug(2, "in main loop\n");
	send_request(STP_READY, NULL, 0);

	/* handle messages from control channel */
	while (1) {
		nb = read(control_channel, recvbuf, sizeof(recvbuf));
		dbug(2, "nb=%d\n", (int)nb);
		if (nb <= 0) {
			if (errno != EINTR)
				_perr("Unexpected EOF in read (nb=%ld)", (long)nb);
			continue;
		}

		type = *(uint32_t *) recvbuf;
		data = (void *)(recvbuf + sizeof(uint32_t));
		nb -= sizeof(uint32_t);

		switch (type) {
#ifdef STP_OLD_TRANSPORT
		case STP_REALTIME_DATA:
			{
				ssize_t bw = write(out_fd[0], data, nb);
				if (bw >= 0 && bw != nb) {
					nb = nb - bw;
					bw = write(out_fd[0], data, nb);
				}
				if (bw != nb) {
					_perr("write error (nb=%ld)", (long)nb);
					cleanup_and_exit(0);
				}
				break;
			}
#endif
		case STP_OOB_DATA:
			fputs((char *)data, stderr);
			break;
		case STP_EXIT:
			{
				/* module asks us to unload it and exit */
				dbug(2, "got STP_EXIT\n");
				cleanup_and_exit(0);
				break;
			}
		case STP_START:
			{
				struct _stp_msg_start *t = (struct _stp_msg_start *)data;
				dbug(2, "probe_start() returned %d\n", t->res);
				if (t->res < 0) {
					if (target_cmd)
						kill(target_pid, SIGKILL);
					cleanup_and_exit(0);
				} else if (target_cmd)
					kill(target_pid, SIGUSR1);
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
						cleanup_and_exit(0);
				} else {
					if (init_relayfs() < 0)
						cleanup_and_exit(0);
				}
				ts.target = target_pid;
				send_request(STP_START, &ts, sizeof(ts));
				if (load_only)
					cleanup_and_exit(1);
				break;
			}
		case STP_UNWIND:
			{
				int len;
				char *ptr = (char *)data;
				while (nb > 0) {
					send_unwind_data(ptr);
					len = strlen(ptr) + 1;
					ptr += len;
					nb -= len;
				}
				break;
			}
		default:
			err("WARNING: ignored message of type %d\n", (type));
		}
	}
	fclose(ofp);
	return 0;
}
