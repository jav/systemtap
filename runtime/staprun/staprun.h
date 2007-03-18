/* -*- linux-c -*-
 *
 * staprun.h - include file for staprun
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2005-2007 Red Hat Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/fd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <sys/wait.h>
#include <sys/statfs.h>
#include <linux/version.h>

#ifdef DEBUG
#define dbug(args...) {fprintf(stderr,"%s:%d ",__FUNCTION__, __LINE__); fprintf(stderr,args); }
#else
#define dbug(args...) ;
#endif /* DEBUG */

/* we define this so we are compatible with old transport, but we don't have to use it. */
#define STP_OLD_TRANSPORT
#include "../transport/transport_msgs.h"

/* command to check system's kernel version */
/* KERNEL_VERSION(2.6.15) = 132623 */
#define VERSION_CMD "uname -r | awk \'{split($1,a,\".\"); split(a[3],b,\"-\"); exit (a[1]*65536+a[2]*256+b[1] <= 132623)}\'"
extern int use_old_transport;

#define RELAYFS_MAGIC			0xF0B4A981
#define DEBUGFS_MAGIC			0x64626720

/*
 * function prototypes
 */
int init_staprun(void);
int stp_main_loop(void);
int send_request(int type, void *data, int len);
void cleanup_and_exit (int);
int do_module(void *);
void do_kernel_symbols(void);
int init_ctl_channel(void);
void close_ctl_channel(void);
int init_relayfs(struct _stp_msg_trans *);
void close_relayfs(void);
int init_oldrelayfs(struct _stp_msg_trans *);
void close_oldrelayfs(void);

/*
 * variables 
 */
extern int control_channel;
extern int ncpus;

/* flags */
extern int verbose;
extern unsigned int buffer_size;
extern char *modname;
extern char *modpath;
extern char *modoptions[];
extern int target_pid;
extern int driver_pid;
extern char *target_cmd;
extern char *outfile_name;

/* uid/gid to use when execing external programs */
extern uid_t cmd_uid;
extern gid_t cmd_gid;

/* maximum number of CPUs we can handle */
#define NR_CPUS 256

/* relay*.c uses these */
extern int out_fd[NR_CPUS];
