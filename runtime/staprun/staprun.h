/* -*- linux-c -*-
 *
 * staprun.h - include file for staprun and stapio
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2005-2008 Red Hat Inc.
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
#include <sys/capability.h>

#define dbug(level, args...) {if (verbose>=level) {fprintf(stderr,"%s:%s:%d ",__name__,__FUNCTION__, __LINE__); fprintf(stderr,args);}}

extern char *__name__;

/* print to stderr */
#define err(args...) fprintf(stderr,args)

/* better perror() */
#define perr(args...) do {					\
		int _errno = errno;				\
		fputs("ERROR: ", stderr);			\
		fprintf(stderr, args);				\
		fprintf(stderr, ": %s\n", strerror(_errno));	\
	} while (0)

/* Error messages. Use these for serious errors, not informational messages to stderr. */
#define _err(args...) do {fprintf(stderr,"%s:%s:%d: ERROR: ",__name__, __FUNCTION__, __LINE__); fprintf(stderr,args);} while(0)
#define _perr(args...) do {					\
		int _errno = errno;				\
		_err(args);					\
		fprintf(stderr, ": %s\n", strerror(_errno));	\
	} while (0)
#define overflow_error() _err("Internal buffer overflow. Please file a bug report.\n")
		
#define do_cap(cap,func,args...) ({			\
			int _rc, _saved_errno;		\
			add_cap(cap);			\
			_rc = func(args);		\
			_saved_errno = errno;		\
			del_cap(cap);			\
			errno = _saved_errno;		\
			_rc;				\
		})					\


/* Error checking version of sprintf() - returns 1 if overflow error */
#define sprintf_chk(str, args...) ({			\
	int _rc;					\
	_rc = snprintf(str, sizeof(str), args);		\
	if (_rc >= (int)sizeof(str)) {			\
		overflow_error();			\
		_rc = 1;				\
	}						\
	else						\
		_rc = 0;				\
	_rc;						\
})

/* Error checking version of snprintf() - returns 1 if overflow error */
#define snprintf_chk(str, size, args...) ({		\
	int _rc;					\
	_rc = snprintf(str, size, args);		\
	if (_rc >= (int)size) {				\
		overflow_error();			\
		_rc = 1;				\
	}						\
	else						\
		_rc = 0;				\
	_rc;						\
})

/* Grabbed from linux/module.h kernel include. */
#define MODULE_NAME_LEN (64 - sizeof(unsigned long))

/* we define this so we are compatible with old transport, but we don't have to use it. */
#define STP_OLD_TRANSPORT
#include "../transport/transport_msgs.h"

#define RELAYFS_MAGIC	0xF0B4A981
#define DEBUGFS_MAGIC	0x64626720
#define DEBUGFSDIR	"/sys/kernel/debug"
#define RELAYFSDIR	"/mnt/relay"

/*
 * function prototypes
 */
int init_staprun(void);
int init_stapio(void);
int stp_main_loop(void);
int send_request(int type, void *data, int len);
void cleanup_and_exit (int);
void send_unwind_data(const char *name);
int init_ctl_channel(const char *name, int verb);
void close_ctl_channel(void);
int init_relayfs(void);
void close_relayfs(void);
int init_oldrelayfs(void);
void close_oldrelayfs(int);
void setup_signals(void);
/* cap.c */
void print_cap(char *text);
int init_cap(void);
void add_cap(cap_value_t cap);
void del_cap(cap_value_t cap);
void drop_cap(cap_value_t cap);
/* staprun_funcs.c */
void setup_staprun_signals(void);
const char *moderror(int err);
int insert_module(const char *path, const char *special_options,
	char **options);
int mountfs(void);
int check_permissions(void);
void start_symbol_thread(void);
void stop_symbol_thread(void);

/* common.c functions */
void parse_args(int argc, char **argv);
void usage(char *prog);
void parse_modpath(const char *);
void setup_signals(void);
int set_clexec(int fd);

/*
 * variables 
 */
extern int control_channel;
extern int ncpus;
extern int initialized;
extern int kernel_ptr_size;

/* flags */
extern int verbose;
extern unsigned int buffer_size;
extern char *modname;
extern char *modpath;
#define MAXMODOPTIONS 64
extern char *modoptions[MAXMODOPTIONS];
extern int target_pid;
extern char *target_cmd;
extern char *outfile_name;
extern int attach_mod;
extern int delete_mod;
extern int load_only;
extern int need_uprobes;

/* getopt variables */
extern char *optarg;
extern int optopt;
extern int optind;

/* maximum number of CPUs we can handle */
#define NR_CPUS 256

/* relay*.c uses these */
extern int out_fd[NR_CPUS];

/* relay_old uses these. Set in ctl.c */
extern unsigned subbuf_size;
extern unsigned n_subbufs;
