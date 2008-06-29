/* -*- linux-c -*-
 *
 * common.c - staprun suid/user common code
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2007 Red Hat Inc.
 */

#include "staprun.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <assert.h>


/* variables needed by parse_args() */
int verbose;
int target_pid;
unsigned int buffer_size;
char *target_cmd;
char *outfile_name;
int attach_mod;
int delete_mod;
int load_only;
int need_uprobes;

/* module variables */
char *modname = NULL;
char *modpath = "";
char *modoptions[MAXMODOPTIONS];

int control_channel = -1; /* NB: fd==0 possible */

void parse_args(int argc, char **argv)
{
	int c;

	/* Initialize option variables. */
	verbose = 0;
	target_pid = 0;
	buffer_size = 0;
	target_cmd = NULL;
	outfile_name = NULL;
	attach_mod = 0;
	delete_mod = 0;
	load_only = 0;
	need_uprobes = 0;

	while ((c = getopt(argc, argv, "ALuvb:t:dc:o:x:")) != EOF) {
		switch (c) {
		case 'u':
			need_uprobes = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'b':
			buffer_size = (unsigned)atoi(optarg);
			if (buffer_size < 1 || buffer_size > 4095) {
				err("Invalid buffer size '%d' (should be 1-4095).\n", buffer_size);
				usage(argv[0]);
			}
			break;
		case 't':
		case 'x':
			target_pid = atoi(optarg);
			break;
		case 'd':
			/* delete module */
			delete_mod = 1;
			break;
		case 'c':
			target_cmd = optarg;
			break;
		case 'o':
			outfile_name = optarg;
			break;
		case 'A':
			attach_mod = 1;
			break;
		case 'L':
			load_only = 1;
			break;
		default:
			usage(argv[0]);
		}
	}

	if (attach_mod && load_only) {
		err("You can't specify the '-A' and '-L' options together.\n");
		usage(argv[0]);
	}

	if (attach_mod && buffer_size) {
		err("You can't specify the '-A' and '-b' options together.  The '-b'\n"
		    "buffer size option only has an effect when the module is inserted.\n");
		usage(argv[0]);
	}

	if (attach_mod && target_cmd) {
		err("You can't specify the '-A' and '-c' options together.  The '-c cmd'\n"
		    "option used to start a command only has an effect when the module\n"
		    "is inserted.\n");
		usage(argv[0]);
	}

	if (attach_mod && target_pid) {
		err("You can't specify the '-A' and '-x' options together.  The '-x pid'\n"
		    "option only has an effect when the module is inserted.\n");
		usage(argv[0]);
	}

	if (target_cmd && target_pid) {
		err("You can't specify the '-c' and '-x' options together.\n");
		usage(argv[0]);
	}
}

void usage(char *prog)
{
	err("\n%s [-v]  [-c cmd ] [-x pid] [-u user]\n"
                "\t[-A|-L] [-b bufsize] [-o FILE] MODULE [module-options]\n", prog);
	err("-v              Increase verbosity.\n");
	err("-c cmd          Command \'cmd\' will be run and staprun will\n");
	err("                exit when it does.  The '_stp_target' variable\n");
	err("                will contain the pid for the command.\n");
	err("-x pid          Sets the '_stp_target' variable to pid.\n");
	err("-o FILE         Send output to FILE.\n");
	err("-b buffer size  The systemtap module specifies a buffer size.\n");
	err("                Setting one here will override that value.  The\n");
	err("                value should be an integer between 1 and 4095 \n");
	err("                which be assumed to be the buffer size in MB.\n");
	err("                That value will be per-cpu in bulk mode.\n");
	err("-L              Load module and start probes, then detach.\n");
	err("-A              Attach to loaded systemtap module.\n");
	err("-d              Delete a module.  Only detached or unused modules\n");
	err("                the user has permission to access will be deleted. Use \"*\"\n");
	err("                (quoted) to delete all unused modules.\n");
	err("MODULE can be either a module name or a module path.  If a\n");
	err("module name is used, it is looked for in the following\n");
	err("directory: /lib/modules/`uname -r`/systemtap\n");
	exit(1);
}

/*
 * parse_modpath.  Here's how this code interprets the global modpath:
 *
 * (1) If modpath contains a '/', it is assumed to be an absolute or
 * relative file path to a module (such as "../foo.ko" or
 * "/tmp/stapXYZ/stap_foo.ko").
 *
 * (2) If modpath doesn't contain a '/' and ends in '.ko', it is a file
 * path to a module in the current directory (such as "foo.ko").
 *
 * (3) If modpath doesn't contain a '/' and doesn't end in '.ko', then
 * it is a module name and the full pathname of the module is
 * '/lib/modules/`uname -r`/systemtap/PATH.ko'.  For instance, if
 * modpath was "foo", the full module pathname would be
 * '/lib/modules/`uname -r`/systemtap/foo.ko'.
 */
void parse_modpath(const char *inpath)
{
	const char *mptr = rindex(inpath, '/');
	char *ptr;

	dbug(3, "inpath=%s\n", inpath);

	/* If we couldn't find a '/', ... */
	if (mptr == NULL) {
		size_t plen = strlen(inpath);

		/* If the path ends with the '.ko' file extension,
		 * then we've got a module in the current
		 * directory. */
		if (plen > 3 && strcmp(&inpath[plen - 3], ".ko") == 0) {
			mptr = inpath;
			modpath = strdup(inpath);
			if (!modpath) {
				err("Memory allocation failed. Exiting.\n");
				exit(1);
			}
		} else {
			/* If we didn't find the '.ko' file extension, then
			 * we've just got a module name, not a module path.
			 * Look for the module in /lib/modules/`uname
			 * -r`/systemtap. */

			struct utsname utsbuf;
			int len;
			#define MODULE_PATH "/lib/modules/%s/systemtap/%s.ko"

			/* First, we need to figure out what the
			 * kernel version. */
			if (uname(&utsbuf) != 0) {
				perr("Unable to determine kernel version, uname failed");
				exit(-1);
			}

			/* Build the module path, which will look like
			 * '/lib/modules/KVER/systemtap/{path}.ko'. */
			len = sizeof(MODULE_PATH) + sizeof(utsbuf.release) + strlen(inpath);
			modpath = malloc(len);
			if (!modpath) {
				err("Memory allocation failed. Exiting.\n");
				exit(1);
			}
			
			if (snprintf_chk(modpath, len, MODULE_PATH, utsbuf.release, inpath))
				exit(-1);

			dbug(2, "modpath=\"%s\"\n", modpath);

			mptr = rindex(modpath, '/');
			mptr++;
		}
	} else {
		/* We found a '/', so the module name starts with the next
		 * character. */
		mptr++;

		modpath = strdup(inpath);
		if (!modpath) {
			err("Memory allocation failed. Exiting.\n");
			exit(1);
		}
	}

	modname = strdup(mptr);
	if (!modname) {
		err("Memory allocation failed. Exiting.\n");
		exit(1);
	}

	ptr = rindex(modname, '.');
	if (ptr)
		*ptr = '\0';

	/* We've finally got a real modname.  Make sure it isn't too
	 * long.  If it is too long, init_module() will appear to
	 * work, but the module can't be removed (because you end up
	 * with control characters in the module name). */
	if (strlen(modname) > MODULE_NAME_LEN) {
		err("ERROR: Module name ('%s') is too long.\n", modname);
		exit(1);
	}
}

#define ERR_MSG "\nUNEXPECTED FATAL ERROR in staprun. Please file a bug report.\n"
static void fatal_handler (int signum)
{
        int rc;
        char *str = strsignal(signum);
        rc = write (STDERR_FILENO, ERR_MSG, sizeof(ERR_MSG));
        rc = write (STDERR_FILENO, str, strlen(str));
        rc = write (STDERR_FILENO, "\n", 1);
	_exit(1);
}

void setup_signals(void)
{
	sigset_t s;
	struct sigaction a;

	/* blocking all signals while we set things up */
	sigfillset(&s);
#ifdef SINGLE_THREADED
	sigprocmask(SIG_SETMASK, &s, NULL);
#else
	pthread_sigmask(SIG_SETMASK, &s, NULL);
#endif
	/* set some of them to be ignored */
	memset(&a, 0, sizeof(a));
	sigfillset(&a.sa_mask);
	a.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &a, NULL);
	sigaction(SIGUSR2, &a, NULL);

	/* for serious errors, handle them in fatal_handler */
	a.sa_handler = fatal_handler;
	sigaction(SIGBUS, &a, NULL);
	sigaction(SIGFPE, &a, NULL);
	sigaction(SIGILL, &a, NULL);
	sigaction(SIGSEGV, &a, NULL);
	sigaction(SIGXCPU, &a, NULL);
	sigaction(SIGXFSZ, &a, NULL);

	/* unblock all signals */
	sigemptyset(&s);

#ifdef SINGLE_THREADED
	sigprocmask(SIG_SETMASK, &s, NULL);
#else
	pthread_sigmask(SIG_SETMASK, &s, NULL);
#endif
}

/*
 * set FD_CLOEXEC for any file descriptor
 */
int set_clexec(int fd)
{
	int val;
	if ((val = fcntl(fd, F_GETFD, 0)) < 0)
		goto err;
	
	if ((val = fcntl(fd, F_SETFD, val | FD_CLOEXEC)) < 0)
		goto err;	
	
	return 0;
err:
	perr("fcntl failed");
	close(fd);
	return -1;
}


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
        int rc = 0;

	/* Before doing memcpy, make sure 'buf' is big enough. */
	if ((len + 4) > (int)sizeof(buf)) {
		_err("exceeded maximum send_request size.\n");
		return -1;
	}
	memcpy(buf, &type, 4);
	memcpy(&buf[4], data, len);

        assert (control_channel >= 0);
	rc = write (control_channel, buf, len + 4);
        if (rc < 0) return rc;
        return (rc != len+4);
}
