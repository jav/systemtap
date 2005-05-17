/*
 * libstp - stpd 'library'
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
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Redhat Inc, 2005
 *
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/fd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>
#include "librelay.h"

/* maximum number of CPUs we can handle - change if more */
#define NR_CPUS 256

 /* internal variables */
static unsigned subbuf_size;
static unsigned n_subbufs;
static int ncpus;
static int print_totals;
static int logging;

static int relay_file[NR_CPUS];
static int out_file[NR_CPUS];
static char *relay_buffer[NR_CPUS];

 /* netlink control channel */
static int control_channel;

/* flags */
extern int print_only;
extern int quiet;
extern int streaming;

/* used to communicate with kernel over control channel */

struct buf_info
{
	int cpu;
	unsigned produced;
	unsigned consumed;
};

struct consumed_info
{
	int cpu;
	unsigned consumed;
};

struct app_msg
{
	struct nlmsghdr nlh;
	struct buf_info info;
};

static char *recvbuf[8192];

/* per-cpu buffer info */
static struct buf_status
{
	pthread_mutex_t ready_mutex;
	pthread_cond_t ready_cond;
	struct buf_info info;
	unsigned max_backlog; /* max # sub-buffers ready at one time */
} status[NR_CPUS];

/* colors for printing */
static char *color[] = {
  "\033[31m", /* red */
  "\033[32m", /* green */
  "\033[33m", /* yellow */
  "\033[34m", /* blue */
  "\033[35m", /* magenta */
  "\033[36m", /* cyan */
};

/**
 *	send_request - send request to kernel over control channel
 *	@type: the relay-app command id
 *	@data: pointer to the data to be sent
 *	@len: length of the data to be sent
 *
 *	Returns 0 on success, negative otherwise.
 */
int send_request(int type, void *data, int len)
{
	struct nlmsghdr *req;
	int err;

	req = (struct nlmsghdr *)malloc(NLMSG_SPACE(len));
	if (req == 0) {
	  fprintf(stderr, "send_request malloc failed\n");
	  return -1;
	}
	memset(req, 0, NLMSG_SPACE(len));
	req->nlmsg_len = NLMSG_LENGTH(len);
	req->nlmsg_type = type;
	req->nlmsg_flags = NLM_F_REQUEST;
	req->nlmsg_pid = getpid();
	memcpy(NLMSG_DATA(req), data, len);
	
	err = send(control_channel, req, req->nlmsg_len, MSG_DONTWAIT);
	return err;
}

/**
 *	open_control_channel - create netlink channel
 */
static int open_control_channel()
{
	struct sockaddr_nl snl;
	int channel;

	channel = socket(AF_NETLINK, SOCK_RAW, NETLINK_USERSOCK);
	if (channel < 0) {
		printf("socket() failed\n");
		return channel;
	}

	memset(&snl, 0, sizeof snl);
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 0;

	if (bind (channel, (struct sockaddr *) &snl, sizeof snl))
		printf("bind() failed\n");

	return channel;
}

/**
 *	process_subbufs - write ready subbufs to disk and/or screen
 */
static int process_subbufs(struct buf_info *info)
{
	unsigned subbufs_ready, start_subbuf, end_subbuf, subbuf_idx;
	int i, len, cpu = info->cpu;
	char *subbuf_ptr;
	int subbufs_consumed = 0;
	unsigned padding;
  
	subbufs_ready = info->produced - info->consumed;
	start_subbuf = info->consumed % n_subbufs;
	end_subbuf = start_subbuf + subbufs_ready;

	if (!quiet)
		fputs ( color[cpu % 4], stdout);

	for (i = start_subbuf; i < end_subbuf; i++) {
		subbuf_idx = i % n_subbufs;
		subbuf_ptr = relay_buffer[cpu] + subbuf_idx * subbuf_size;
		padding = *((unsigned *)subbuf_ptr);
		subbuf_ptr += sizeof(padding);
		len = (subbuf_size - sizeof(padding)) - padding;

		if (!print_only)
		{
			if (write(out_file[cpu], subbuf_ptr, len) < 0) 
			{
				printf("Couldn't write to output file for cpu %d, exiting: errcode = %d: %s\n", cpu, errno, strerror(errno));
				exit(1);
			}
		}
		
		if (!quiet)
			fwrite (subbuf_ptr, len, 1, stdout);
		
		subbufs_consumed++;
	}
	
	return subbufs_consumed;
}

/**
 *	reader_thread - per-cpu channel buffer reader
 */
static void *reader_thread(void *data)
{
	int rc;
	long cpu = (long)data;
	struct pollfd pollfd;
	struct consumed_info consumed_info;
	unsigned subbufs_consumed;

	do {
		pollfd.fd = relay_file[cpu];
		pollfd.events = POLLIN;
		rc = poll(&pollfd, 1, -1);
		if (rc < 0) {
			if (errno != EINTR) {
				printf("poll error: %s\n",strerror(errno));
				exit(1);
			}
			printf("poll warning: %s\n",strerror(errno));
			rc = 0;
		}

		send_request(STP_BUF_INFO, &status[cpu].info,
			     sizeof(struct buf_info));
		if (status[cpu].info.produced == status[cpu].info.consumed)
			pthread_cond_wait(&status[cpu].ready_cond,
					  &status[cpu].ready_mutex);
		pthread_mutex_unlock(&status[cpu].ready_mutex);
		
		subbufs_consumed = process_subbufs(&status[cpu].info);
		if (subbufs_consumed) {
			if (subbufs_consumed == n_subbufs)
				fprintf(stderr, "cpu %ld buffer full.  Consider using a larger buffer size.\n", cpu);
			if (subbufs_consumed > status[cpu].max_backlog)
				status[cpu].max_backlog = subbufs_consumed;
			status[cpu].info.consumed += subbufs_consumed;
			consumed_info.cpu = cpu;
			consumed_info.consumed = subbufs_consumed;
			send_request(STP_SUBBUFS_CONSUMED,
				     &consumed_info,
				     sizeof(struct consumed_info));
		}
	} while (1);
}

static void summarize(void)
{
	int i;
	
	if (streaming)
		return;
	
	printf("summary:\n");
	for (i = 0; i < ncpus; i++) {
		printf("%s  cpu %u:\n", color[i % 4], i);
		printf("    %u sub-buffers processed\n",
		       status[i].info.consumed);
		printf("    %u max backlog\n", status[i].max_backlog);
	}
}

/**
 *	close_files - close and munmap buffer and open output file
 */
static void close_files(int cpu)
{
	size_t total_bufsize = subbuf_size * n_subbufs;

	munmap(relay_buffer[cpu], total_bufsize);
	close(relay_file[cpu]);
	if (!print_only) close(out_file[cpu]);
}

static void close_all_files(void)
{
	int i;
	close(control_channel);
	for (i = 0; i < ncpus; i++)
		close_files(i);
}

static void sigalarm(int signum)
{
	if (print_totals)
		summarize();
	close_all_files();
	exit(0);
}

static void sigproc(int signum)
{
  while (send_request(STP_EXIT, NULL, 0) < 0)
    usleep (10000);
}

/**
 *	open_files - open and mmap buffer and open output file
 */
static int open_files(int cpu, const char *relay_filebase,
		      const char *out_filebase)
{
	size_t total_bufsize;
	char tmp[4096];

	memset(&status[cpu], 0, sizeof(struct buf_status));
	pthread_mutex_init(&status[cpu].ready_mutex, NULL);
	pthread_cond_init(&status[cpu].ready_cond, NULL);
	status[cpu].info.cpu = cpu;

	sprintf(tmp, "%s%d", relay_filebase, cpu);
	relay_file[cpu] = open(tmp, O_RDONLY | O_NONBLOCK);
	if (relay_file[cpu] < 0) {
		printf("Couldn't open relayfs file %s: errcode = %s\n",
		       tmp, strerror(errno));
		return -1;
	}

	if (!print_only) {
		sprintf(tmp, "%s%d", out_filebase, cpu);
		if((out_file[cpu] = open(tmp, O_CREAT | O_RDWR | O_TRUNC,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
			printf("Couldn't open output file %s: errcode = %s\n",
			       tmp, strerror(errno));
			close(relay_file[cpu]);
			return -1;
		}
	}

	total_bufsize = subbuf_size * n_subbufs;
	relay_buffer[cpu] = mmap(NULL, total_bufsize, PROT_READ,
				 MAP_PRIVATE | MAP_POPULATE, relay_file[cpu],
				 0);
	if(relay_buffer[cpu] == MAP_FAILED)
	{
		printf("Couldn't mmap relay file, total_bufsize (%d) = subbuf_size (%d) * n_subbufs(%d), error = %s \n", (int)total_bufsize, (int)subbuf_size, (int)n_subbufs, strerror(errno));
		close(relay_file[cpu]);
		if (!print_only) close(out_file[cpu]);
		return -1;
	}
	
	return 0;
}

/**
 *	init_stp - initialize the app
 *	@relay_filebase: full path of base name of the per-cpu relayfs files
 *	@out_filebase: base name of the per-cpu files data will be written to
 *	@sub_buf_size: relayfs sub-buffer size of channel to be created
 *	@n_sub_bufs: relayfs number of sub-buffers of channel to be created
 *	@print_summary: boolean, print summary or not at end of run
 *
 *	Returns 0 on success, negative otherwise.
 */
int init_stp(const char *modname,
	     const char *relay_filebase,
	     const char *out_filebase,
	     unsigned sub_buf_size,
	     unsigned n_sub_bufs,
	     int print_summary)
{
	int i;
	int daemon_pid;
	char buf[1024];
	
	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	subbuf_size = sub_buf_size;
	n_subbufs = n_sub_bufs;
	print_totals = print_summary;


	daemon_pid = getpid();
	sprintf(buf, "insmod %s pid=%d", modname, daemon_pid);
	if (system(buf)) {
		printf("Couldn't insmod probe module %s\n", modname);
		return -1;
	}

	control_channel = open_control_channel();
	if (control_channel < 0)
		return -1;
	
	if (streaming)
		return 0;
	
	for (i = 0; i < ncpus; i++) {
		if (open_files(i, relay_filebase, out_filebase) < 0) {
			printf("Couldn't open files\n");
			close (control_channel);
			return -1;
		}
	}

	return 0;
}

/**
 *	stp_main_loop - loop forever reading data
 */
int stp_main_loop(void)
{
	pthread_t thread;
	int cpu, nb;
	long i;
	struct app_msg *msg;
	unsigned short *ptr;
	char tmpbuf[128];

	signal(SIGINT, sigproc);
	signal(SIGTERM, sigproc);
	signal(SIGALRM, sigalarm);

	if (!streaming) {
		for (i = 0; i < ncpus; i++) {
			/* create a thread for each per-cpu buffer */
			if (pthread_create(&thread, NULL, reader_thread, (void *)i) < 0) {
				printf("Couldn't create thread\n");
				return -1;
			}
		}
	}

	logging = 1;
	
	while (1) { /* handle messages from control channel */
		nb = recv(control_channel, recvbuf, sizeof(recvbuf), 0);
		struct nlmsghdr *nlh = (struct nlmsghdr *)recvbuf;
		
		if (nb < 0) {
			if (errno == EINTR)
				continue;
			printf("recv() failed\n");
		} else if (nb == 0)
			printf("unexpected EOF on netlink socket\n");
		if (!NLMSG_OK(nlh, nb)) {
			printf("netlink message not ok, nb = %d\n", nb);
			continue;
		}
		switch (nlh->nlmsg_type) {
		case STP_BUF_INFO:
		  msg = (struct app_msg *)nlh;
		  cpu = msg->info.cpu;
		  memcpy(&status[cpu].info, &msg->info, sizeof (struct buf_info));
		  pthread_mutex_lock(&status[cpu].ready_mutex);
		  if (status[cpu].info.produced > status[cpu].info.consumed)
		    pthread_cond_signal(&status[cpu].ready_cond);
		  pthread_mutex_unlock(&status[cpu].ready_mutex);
		  break;
		case STP_REALTIME_DATA:
		  ptr = NLMSG_DATA(nlh);
		  fputs ( color[5], stdout);
		  fputs ((char *)ptr, stdout);
		  break;
		case STP_EXIT:
		  /* module asks us to unload it and exit */
		  ptr = NLMSG_DATA(nlh);
		  /* FIXME. overflow check */
		  strcpy (tmpbuf, "/sbin/rmmod ");
		  strcpy (tmpbuf + strlen(tmpbuf), (char *)ptr);
#if 0
		  printf ("Executing \"system %s\"\n", tmpbuf);
#endif
		  system (tmpbuf);
		  if (print_totals)
			  summarize();
		  if (!streaming)
			  close_all_files();
		  exit(0);
		  break;
		default:
		  fprintf(stderr, "WARNING: ignored netlink message of type %d\n", (nlh->nlmsg_type));
		}
	}
	return 0;
}
