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
 * Copyright (C) Red Hat Inc, 2005
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
#include <linux/limits.h>
#include "librelay.h"

/* maximum number of CPUs we can handle - change if more */
#define NR_CPUS 256

/* relayfs parameters */
static struct params
{
	unsigned subbuf_size;
	unsigned n_subbufs;
	char relay_filebase[256];
} params;

/* temporary per-cpu output written here, filebase0...N */
static char *percpu_tmpfilebase = "stpd_cpu";

/* probe output written here  */
static char *outfile_name = "probe.out";

/* internal variables */
static int transport_mode;
static int ncpus;
static int print_totals;
static int final_cpus_processed;
static int exiting;

/* per-cpu data */
static int relay_file[NR_CPUS];
static FILE *percpu_tmpfile[NR_CPUS];
static char *relay_buffer[NR_CPUS];
static pthread_t reader[NR_CPUS];

/* netlink control channel */
static int control_channel;

/* flags */
extern int print_only, quiet, merge, verbose;
extern unsigned int opt_subbuf_size;
extern unsigned int opt_n_subbufs;
extern char *modname;

/* used to communicate with kernel over control channel */

struct buf_msg
{
	struct nlmsghdr nlh;
	struct buf_info info;
};

struct transport_msg
{
	struct nlmsghdr nlh;
	struct transport_info info;
};

struct start_msg
{
	struct nlmsghdr nlh;
	struct transport_start ts;
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

/**
 *	streaming - is the current transport mode streaming or not?
 *
 *	Returns 1 if in streaming mode, 0 otherwise.
 */
static int streaming(void)
{
	if (transport_mode == STP_TRANSPORT_NETLINK)
		return 1;

	return 0;
}

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
	int err, trylimit = 50;
	req = (struct nlmsghdr *)malloc(NLMSG_SPACE(len));
	if (req == 0) {
		fprintf(stderr, "WARNING: send_request malloc failed\n");
		return -1;
	}
	memset(req, 0, NLMSG_SPACE(len));
	req->nlmsg_len = NLMSG_LENGTH(len);
	req->nlmsg_type = type;
	req->nlmsg_flags = NLM_F_REQUEST;
	req->nlmsg_pid = getpid();
	memcpy(NLMSG_DATA(req), data, len);
	while ((err = send(control_channel, req, req->nlmsg_len, MSG_DONTWAIT)) < 0 && trylimit-- )
		usleep (5000);
	return err;
}

#if 0
static unsigned int get_rmem(void)
{
	char buf[32];
	FILE *fp = fopen ("/proc/sys/net/core/rmem_max", "r");
	if (fp == NULL)
		return 0;
	if (fgets (buf, 32, fp) == NULL) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	return atoi(buf);
}
#endif

/**
 *	open_control_channel - create netlink channel
 */
static int open_control_channel()
{
	struct sockaddr_nl snl;
	int channel, rcvsize = 512*1024;

	channel = socket(AF_NETLINK, SOCK_RAW, NETLINK_USERSOCK);
	if (channel < 0) {
		fprintf(stderr, "ERROR: socket() failed\n");
		return channel;
	}

	if (setsockopt(channel, SOL_SOCKET, SO_RCVBUF, &rcvsize, sizeof(int))) 
		fprintf(stderr, "WARNING: failed to set socket receive buffer to %d\n", rcvsize);

	memset(&snl, 0, sizeof snl);
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 0;

	if (bind (channel, (struct sockaddr *) &snl, sizeof snl))
		fprintf(stderr, "ERROR: bind() failed\n");

	return channel;
}

/**
 *	summarize - print a summary if applicable
 */
static void summarize(void)
{
	int i;

	if (transport_mode != STP_TRANSPORT_RELAYFS)
		return;

	printf("summary:\n");
	for (i = 0; i < ncpus; i++) {
		printf("cpu %u:\n", i);
		printf("    %u sub-buffers processed\n",
		       status[i].info.consumed);
		printf("    %u max backlog\n", status[i].max_backlog);
	}
}

/**
 *	close_relayfs_files - close and munmap buffer and open output file
 */
static void close_relayfs_files(int cpu)
{
	size_t total_bufsize = params.subbuf_size * params.n_subbufs;

	munmap(relay_buffer[cpu], total_bufsize);
	close(relay_file[cpu]);
	fclose(percpu_tmpfile[cpu]);
}

/**
 *	close_all_relayfs_files - close and munmap buffers and output files
 */
static void close_all_relayfs_files(void)
{
	int i;

	if (!streaming()) {
		for (i = 0; i < ncpus; i++)
			close_relayfs_files(i);
	}
}

/**
 *	open_relayfs_files - open and mmap buffer and open output file
 */
static int open_relayfs_files(int cpu, const char *relay_filebase)
{
	size_t total_bufsize;
	char tmp[PATH_MAX];

	memset(&status[cpu], 0, sizeof(struct buf_status));
	pthread_mutex_init(&status[cpu].ready_mutex, NULL);
	pthread_cond_init(&status[cpu].ready_cond, NULL);
	status[cpu].info.cpu = cpu;

	sprintf(tmp, "%s%d", relay_filebase, cpu);
	relay_file[cpu] = open(tmp, O_RDONLY | O_NONBLOCK);
	if (relay_file[cpu] < 0) {
		fprintf(stderr, "ERROR: couldn't open relayfs file %s: errcode = %s\n", tmp, strerror(errno));
		return -1;
	}

	sprintf(tmp, "%s%d", percpu_tmpfilebase, cpu);
	if((percpu_tmpfile[cpu] = fopen(tmp, "w+")) == NULL) {
		fprintf(stderr, "ERROR: Couldn't open output file %s: errcode = %s\n", tmp, strerror(errno));
		close(relay_file[cpu]);
		return -1;
	}

	total_bufsize = params.subbuf_size * params.n_subbufs;
	relay_buffer[cpu] = mmap(NULL, total_bufsize, PROT_READ,
				 MAP_PRIVATE | MAP_POPULATE, relay_file[cpu],
				 0);
	if(relay_buffer[cpu] == MAP_FAILED)
	{
		fprintf(stderr, "ERROR: couldn't mmap relay file, total_bufsize (%d) = subbuf_size (%d) * n_subbufs(%d), error = %s \n", (int)total_bufsize, (int)params.subbuf_size, (int)params.n_subbufs, strerror(errno));
		close(relay_file[cpu]);
		fclose(percpu_tmpfile[cpu]);
		return -1;
	}

	return 0;
}

/**
 *	delete_percpu_files - delete temporary per-cpu output files
 *
 *	Returns 0 if successful, -1 otherwise.
 */
static int delete_percpu_files(void)
{
	int i;
	char tmp[PATH_MAX];

	for (i = 0; i < ncpus; i++) {
		sprintf(tmp, "%s%d", percpu_tmpfilebase, i);
		if (unlink(tmp) < 0) {
			fprintf(stderr, "ERROR: couldn't unlink percpu file %s: errcode = %s\n", tmp, strerror(errno));
			return -1;
		}
	}
	return 0;
}

/**
 *	kill_percpu_threads - kill per-cpu threads 0->n-1
 *	@n: number of threads to kill
 *
 *	Returns number of threads killed.
 */
static int kill_percpu_threads(int n)
{
	int i, killed = 0;

	for (i = 0; i < n; i++) {
		if (pthread_cancel(reader[i]) == 0)
			killed++;
	}
	if (killed != n)
		fprintf(stderr, "WARNING: couldn't kill all per-cpu threads:  %d killed, %d total\n", killed, n);

	return killed;
}

/**
 *	request_last_buffers - request end-of-trace last buffer processing
 *
 *	Returns 0 if successful, negative otherwise
 */
static int request_last_buffers(void)
{
	int cpu;
	for (cpu = 0; cpu < ncpus; cpu++) {
		if (send_request(STP_BUF_INFO, &cpu, sizeof(cpu)) < 0) {
			fprintf(stderr, "WARNING: couldn't request last buffers for cpu %d\n", cpu);
			return -1;
		}
	}
	return 0;
}

/**
 *	process_subbufs - write ready subbufs to disk
 */
static int process_subbufs(struct buf_info *info)
{
	unsigned subbufs_ready, start_subbuf, end_subbuf, subbuf_idx, i;
	int len, cpu = info->cpu;
	char *subbuf_ptr;
	int subbufs_consumed = 0;
	unsigned padding;

	subbufs_ready = info->produced - info->consumed;
	start_subbuf = info->consumed % params.n_subbufs;
	end_subbuf = start_subbuf + subbufs_ready;

	for (i = start_subbuf; i < end_subbuf; i++) {
		subbuf_idx = i % params.n_subbufs;
		subbuf_ptr = relay_buffer[cpu] + subbuf_idx * params.subbuf_size;
		padding = *((unsigned *)subbuf_ptr);
		subbuf_ptr += sizeof(padding);
		len = (params.subbuf_size - sizeof(padding)) - padding;
		if (len) {
			if (fwrite_unlocked (subbuf_ptr, len, 1, percpu_tmpfile[cpu]) != 1) {
				fprintf(stderr, "ERROR: couldn't write to output file for cpu %d, exiting: errcode = %d: %s\n", cpu, errno, strerror(errno));
				exit(1);
			}
		}
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
	int cpu = (long)data;
	struct pollfd pollfd;
	struct consumed_info consumed_info;
	unsigned subbufs_consumed;

	do {
		pollfd.fd = relay_file[cpu];
		pollfd.events = POLLIN;
		rc = poll(&pollfd, 1, -1);
		if (rc < 0) {
			if (errno != EINTR) {
				fprintf(stderr, "ERROR: poll error: %s\n",strerror(errno));
				exit(1);
			}
			fprintf(stderr, "WARNING: poll warning: %s\n",strerror(errno));
			rc = 0;
		}
		send_request(STP_BUF_INFO, &cpu, sizeof(cpu));
		
		pthread_mutex_lock(&status[cpu].ready_mutex);
		if (status[cpu].info.produced == status[cpu].info.consumed)
			pthread_cond_wait(&status[cpu].ready_cond,
					  &status[cpu].ready_mutex);
		pthread_mutex_unlock(&status[cpu].ready_mutex);
		// printf ("produced:%d  consumed:%d\n", status[cpu].info.produced,status[cpu].info.consumed);

		subbufs_consumed = process_subbufs(&status[cpu].info);
		if (subbufs_consumed) {
			if (subbufs_consumed == params.n_subbufs)
				fprintf(stderr, "WARNING: cpu %d buffer full.  Consider using a larger buffer size.\n", cpu);
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

/**
 *	init_relayfs - create files and threads for relayfs processing
 *
 *	Returns 0 if successful, negative otherwise
 */
int init_relayfs(void)
{
	int i, j;

	for (i = 0; i < ncpus; i++) {
		if (open_relayfs_files(i, params.relay_filebase) < 0) {
			fprintf(stderr, "ERROR: couldn't open relayfs files, cpu = %d\n", i);
			goto err;
		}
		/* create a thread for each per-cpu buffer */
		if (pthread_create(&reader[i], NULL, reader_thread, (void *)(long)i) < 0) {
			close_relayfs_files(i);
			fprintf(stderr, "ERROR: Couldn't create reader thread, cpu = %d\n", i);
			goto err;
		}
	}

        if (print_totals)
          printf("Using channel with %u sub-buffers of size %u.\n",
                 params.n_subbufs, params.subbuf_size);

	return 0;
err:
	for (j = 0; j < i; j++)
		close_relayfs_files(j);
	kill_percpu_threads(i);

	return -1;
}

#include <sys/wait.h>
static void cleanup_and_exit (int);

/**
 *	init_stp - initialize the app
 *	@relay_filebase: full path of base name of the per-cpu relayfs files
 *	@print_summary: boolean, print summary or not at end of run
 *
 *	Returns 0 on success, negative otherwise.
 */
int init_stp(const char *relay_filebase, int print_summary)
{
	char buf[1024];
	struct transport_info ti;
	pid_t pid;
	int status;

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	print_totals = print_summary;

	sprintf(buf, "_stp_pid=%d", (int)getpid());
	if ((pid = vfork()) < 0) {
		perror ("vfork");
		exit(-1);
	} else if (pid == 0) {
		if (execl("/sbin/insmod",  "insmod", modname, buf, NULL) < 0)
			exit(-1);
	}
	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		exit(-1);
	}
	if (WIFEXITED(status) && WEXITSTATUS(status)) {
		perror ("insmod");
		fprintf(stderr, "ERROR, couldn't insmod probe module %s\n", modname);
		return -1;
	}

	control_channel = open_control_channel();
	if (control_channel < 0)
		return -1;

	if (relay_filebase)
		strcpy(params.relay_filebase, relay_filebase);

	/* now send TRANSPORT_INFO */
	ti.subbuf_size = opt_subbuf_size;
	ti.n_subbufs = opt_n_subbufs;
	ti.target = 0;  // FIXME.  not implemented yet
	send_request(STP_TRANSPORT_INFO, &ti, sizeof(ti));

	return 0;
}

/* length of timestamp in output field 0 - TODO: make binary, variable */
#define TIMESTAMP_SIZE 11


/**
 *	merge_output - merge per-cpu output
 *
 */
#define MERGE_BUF_SIZE 32768

static int merge_output(void)
{
	int c, i, j, dropped=0;
	long count=0, min, num[ncpus];
	char buf[32], tmp[PATH_MAX];
	FILE *ofp, *fp[ncpus];

	for (i = 0; i < ncpus; i++) {
		sprintf (tmp, "%s%d", percpu_tmpfilebase, i);
		fp[i] = fopen (tmp, "r");
		if (!fp[i]) {
			fprintf (stderr, "error opening file %s.\n", tmp);
			return -1;
		}
		if (fread (buf, TIMESTAMP_SIZE, 1, fp[i]))
			num[i] = strtoul (buf, NULL, 10);
		else
			num[i] = 0;
	}

	ofp = fopen (outfile_name, "w");
	if (!ofp) {
		fprintf (stderr, "ERROR: couldn't open output file %s: errcode = %s\n",
			 outfile_name, strerror(errno));
		return -1;
	}

	do {
		min = num[0];
		j = 0;
		for (i = 1; i < ncpus; i++) {
			if (min == 0 || (num[i] && num[i] < min)) {
				min = num[i];
				j = i;
			}
		}

		while (1) {
			c = fgetc_unlocked (fp[j]);
			if (c == 0 || c == EOF)
				break;
			if (!quiet)
				fputc_unlocked (c, stdout);
			if (!print_only)
				fputc_unlocked (c, ofp);
		}
		if (min && ++count != min) {
			// fprintf(stderr, "got %ld. expected %ld\n", min, count);
			count = min;
			dropped++ ;
		}

		if (fread (buf, TIMESTAMP_SIZE, 1, fp[j]))
			num[j] = strtoul (buf, NULL, 10);
		else
			num[j] = 0;
	} while (min);

	if (!quiet)
		fputs ("\n", stdout);
	if (!print_only)
		fputs ("\n", ofp);

	for (i = 0; i < ncpus; i++)
		fclose (fp[i]);
	fclose (ofp);
	if (dropped)
		printf ("\033[33mSequence had %d drops.\033[0m\n", dropped);
	return 0;
}

/**
 *	postprocess_and_exit - postprocess the output and exit
 */
static void postprocess_and_exit(void)
{
	if (print_totals)
		summarize();

	if (transport_mode == STP_TRANSPORT_RELAYFS && merge) {
		close_all_relayfs_files();
		merge_output();
		delete_percpu_files();
	}

	close(control_channel);

	exit(0);
}

static void cleanup_and_exit (int closed)
{
	char tmpbuf[128];

	if (exiting)
		return;

	exiting = 1;

	if (transport_mode == STP_TRANSPORT_RELAYFS) {
		kill_percpu_threads(ncpus);
		if (request_last_buffers() < 0)
			exit(1);
	}

	if (!closed) {
		/* FIXME. overflow check */
		strcpy (tmpbuf, "/sbin/rmmod ");
		strcpy (tmpbuf + strlen(tmpbuf), modname);
		if (system(tmpbuf)) {
			fprintf(stderr, "ERROR: couldn't rmmod probe module %s.  No output will be written.\n",
				modname);
			close_all_relayfs_files();
			delete_percpu_files();
			exit(1);
		}
	}

	if ( transport_mode == STP_TRANSPORT_RELAYFS && final_cpus_processed < ncpus)
	  return;
	
	postprocess_and_exit();
}

static void sigproc(int signum __attribute__((unused)))
{
  send_request(STP_EXIT, NULL, 0);
}

/**
 *	stp_main_loop - loop forever reading data
 */
int stp_main_loop(void)
{
	int cpu, nb, rc;
	struct transport_start ts;

	unsigned short *ptr;
	unsigned subbufs_consumed;

	signal(SIGINT, sigproc);
	signal(SIGTERM, sigproc);
	
	while (1) { /* handle messages from control channel */
		nb = recv(control_channel, recvbuf, sizeof(recvbuf), 0);
		struct nlmsghdr *nlh = (struct nlmsghdr *)recvbuf;
		if (nb < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "WARNING: recv() failed\n");
		} else if (nb == 0)
			fprintf(stderr, "WARNING: unexpected EOF on netlink socket\n");
		if (!NLMSG_OK(nlh, (unsigned int) nb)) {
			fprintf(stderr, "WARNING: netlink message not ok, nb = %d\n", nb);
			continue;
		}
		if (!transport_mode &&
		    nlh->nlmsg_type != STP_TRANSPORT_INFO &&
		    nlh->nlmsg_type != STP_EXIT)
		{
			fprintf(stderr, "WARNING: invalid stp command: no transport\n");
			continue;
		}
		switch (nlh->nlmsg_type) {
		case STP_BUF_INFO:
		{
			struct buf_msg *buf_msg = (struct buf_msg *)nlh;
			cpu = buf_msg->info.cpu;
			memcpy(&status[cpu].info, &buf_msg->info, sizeof (struct buf_info));
			if (exiting) {
				final_cpus_processed++;
				subbufs_consumed = process_subbufs(&status[cpu].info);
				status[cpu].info.consumed += subbufs_consumed;
				if (final_cpus_processed >= ncpus)
					postprocess_and_exit();
				break;
			}
			pthread_mutex_lock(&status[cpu].ready_mutex);
			if (status[cpu].info.produced > status[cpu].info.consumed)
				pthread_cond_signal(&status[cpu].ready_cond);
			pthread_mutex_unlock(&status[cpu].ready_mutex);
			break;
		}
		case STP_TRANSPORT_INFO:
		{
			struct transport_msg *transport_msg = (struct transport_msg *)nlh;
			transport_mode = transport_msg->info.transport_mode;
			params.subbuf_size = transport_msg->info.subbuf_size;
			params.n_subbufs = transport_msg->info.n_subbufs;
			/*
			printf ("TRANSPORT_INFO recvd: %d bufs of %d bytes. mode=%d\n", 
				params.n_subbufs, 
				params.subbuf_size, 
				transport_mode);
			*/
			if (!streaming()) {
				rc = init_relayfs();
				if (rc < 0) {
					close(control_channel);
					fprintf(stderr, "ERROR: couldn't init relayfs, exiting\n");
					exit(1);
				}
			}
			ts.pid = 0; // FIXME. not implemented yet
			send_request(STP_START, &ts, sizeof(ts));
			break;
		}
		case STP_REALTIME_DATA:
			ptr = NLMSG_DATA(nlh);
			fputs ((char *)ptr, stdout);
			break;
		case STP_EXIT: 
		{
			/* module asks us to unload it and exit */
			int *closed = (int *)NLMSG_DATA(nlh);			
			cleanup_and_exit(*closed);
			break;
		}
		case STP_START: 
		{
			/* we only get this if probe_start() errors */
			struct start_msg *s = (struct start_msg *)nlh;
			struct transport_start *t = &s->ts;
			fprintf(stderr, "probe_start() returned %d\nExiting...\n", t->pid);
			cleanup_and_exit(0);
			break;
		}
		default:
			fprintf(stderr, "WARNING: ignored netlink message of type %d\n", (nlh->nlmsg_type));
		}
	}
	return 0;
}
