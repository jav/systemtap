/* -*- linux-c -*-
 *
 * staprun_funcs.c - staprun functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2007-2008 Red Hat Inc.
 */

#include "staprun.h"
#include <sys/mount.h>
#include <sys/utsname.h>
#include <grp.h>
#include <pwd.h>

void cleanup(int rc);

void setup_staprun_signals(void)
{
	struct sigaction a;
	memset(&a, 0, sizeof(a));
	sigfillset(&a.sa_mask);
	a.sa_handler = SIG_IGN;
	sigaction(SIGINT, &a, NULL);
	sigaction(SIGTERM, &a, NULL);
	sigaction(SIGHUP, &a, NULL);
	sigaction(SIGQUIT, &a, NULL);
}

extern long init_module(void *, unsigned long, const char *);

/* Module errors get translated. */
const char *moderror(int err)
{
	switch (err) {
	case ENOEXEC:
		return "Invalid module format";
	case ENOENT:
		return "Unknown symbol in module";
	case ESRCH:
		return "Module has wrong symbol version";
	case EINVAL:
		return "Invalid parameters";
	default:
		return strerror(err);
	}
}

int insert_module(const char *path, const char *special_options, char **options)
{
	int i;
	long ret;
	void *file;
	char *opts;
	int fd, saved_errno;
	struct stat sbuf;
		
	dbug(2, "inserting module\n");

	if (special_options)
		opts = strdup(special_options);
	else
		opts = strdup("");
	if (opts == NULL) {
		_perr("allocating memory failed");
		return -1;
	}
	for (i = 0; options[i] != NULL; i++) {
		opts = realloc(opts, strlen(opts) + strlen(options[i]) + 2);
		if (opts == NULL) {
			_perr("[re]allocating memory failed");
			return -1;
		}
		strcat(opts, " ");
		strcat(opts, options[i]);
	}
	dbug(2, "module options: %s\n", opts);

	/* Open the module file. */
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perr("Error opening '%s'", path);
		return -1;
	}
	
	/* Now that the file is open, figure out how big it is. */
	if (fstat(fd, &sbuf) < 0) {
		_perr("Error stat'ing '%s'", path);
		close(fd);
		return -1;
	}

	/* mmap in the entire module. */
	file = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == MAP_FAILED) {
		_perr("Error mapping '%s'", path);
		close(fd);
		free(opts);
		return -1;
	}
	    
	/* Actually insert the module */
	ret = do_cap(CAP_SYS_MODULE, init_module, file, sbuf.st_size, opts);
	saved_errno = errno;

	/* Cleanup. */
	free(opts);
	munmap(file, sbuf.st_size);
	close(fd);

	if (ret != 0) {
		err("Error inserting module '%s': %s\n", path, moderror(saved_errno));
		return -1; 
	}
	return 0;
}

int mountfs(void)
{
	struct stat sb;
	struct statfs st;
	int rc;

	/* If the debugfs dir is already mounted correctly, we're done. */
 	if (statfs(DEBUGFSDIR, &st) == 0
	    && (int) st.f_type == (int) DEBUGFS_MAGIC)
		return 0;

	/* If DEBUGFSDIR exists (and is a directory), try to mount
	 * DEBUGFSDIR. */
	rc = stat(DEBUGFSDIR, &sb);
	if (rc == 0 && S_ISDIR(sb.st_mode)) {
		/* If we can mount the debugfs dir correctly, we're done. */
		rc = do_cap(CAP_SYS_ADMIN, mount, "debugfs", DEBUGFSDIR,
			    "debugfs", 0, NULL); 
		if (rc == 0)
			return 0;
		/* If we got ENODEV, that means that debugfs isn't
		 * supported, so we'll need try try relayfs.  If we
		 * didn't get ENODEV, we got a real error. */
		else if (errno != ENODEV) {
			perr("Couldn't mount %s", DEBUGFSDIR);
			return -1;
		}
	}
	
	/* DEBUGFSDIR couldn't be mounted.  So, try RELAYFSDIR. */

	/* If the relayfs dir is already mounted correctly, we're done. */
	if (statfs(RELAYFSDIR, &st) == 0
	    && (int)st.f_type == (int)RELAYFS_MAGIC)
		return 0;

	/* Ensure that RELAYFSDIR exists and is a directory. */
	rc = stat(RELAYFSDIR, &sb);
	if (rc == 0 && ! S_ISDIR(sb.st_mode)) {
		err("%s exists but isn't a directory.\n", RELAYFSDIR);
		return -1;
	}
	else if (rc < 0) {
		mode_t old_umask;
		int saved_errno;
		gid_t gid = getgid();
		uid_t uid = getuid();

		/* To ensure the directory gets created with the proper
		 * permissions, set umask to a known value. */
		old_umask = umask(0002);

		/* To ensure the directory gets created with the
		 * proper group, we'll have to temporarily switch to
		 * root. */
		if (do_cap(CAP_SETUID, setuid, 0) < 0) {
			_perr("Couldn't change user while creating %s", RELAYFSDIR);
			return -1;
		}
		if (do_cap(CAP_SETGID, setgid, 0) < 0) {
			_perr("Couldn't change group while creating %s", RELAYFSDIR);
			return -1;
		}

		/* Try to create the directory, saving the return
		 * status and errno value. */
		rc = mkdir(RELAYFSDIR, 0755);
		saved_errno = errno;

		/* Restore everything we changed. */
		if (do_cap(CAP_SETGID, setgid, gid) < 0) {
			_perr("Couldn't restore group while creating %s", RELAYFSDIR);
			return -1;
		}
		if (do_cap(CAP_SETUID, setuid, uid) < 0) {
			_perr("Couldn't restore user while creating %s", RELAYFSDIR);
			return -1;
		}
		umask(old_umask);

		/* If creating the directory failed, error out. */
		if (rc < 0) {
			err("Couldn't create %s: %s\n", RELAYFSDIR, strerror(saved_errno));
			return -1;
		}
	}

	/* Now that we're sure the directory exists, try mounting RELAYFSDIR. */
	if (do_cap(CAP_SYS_ADMIN, mount, "relayfs", RELAYFSDIR, "relayfs", 0, NULL) < 0) {
		perr("Couldn't mount %s", RELAYFSDIR);
		return -1;
	}
	return 0;
}


/*
 * Members of the 'stapusr' group can only use "blessed" modules -
 * ones in the '/lib/modules/KVER/systemtap' directory.  Make sure the
 * module path is in that directory.
 *
 * Returns: -1 on errors, 0 on failure, 1 on success.
 */
static int
check_path(void)
{
	struct utsname utsbuf;
	struct stat sb;
	char staplib_dir_path[PATH_MAX];
	char staplib_dir_realpath[PATH_MAX];
	char module_realpath[PATH_MAX];

	/* First, we need to figure out what the kernel
	 * version is and build the '/lib/modules/KVER/systemtap' path. */
	if (uname(&utsbuf) != 0) {
		_perr("ERROR: Unable to determine kernel version, uname failed");
		return -1;
	}
	if (sprintf_chk(staplib_dir_path, "/lib/modules/%s/systemtap", utsbuf.release))
		return -1;

	/* Validate /lib/modules/KVER/systemtap. */
	if (stat(staplib_dir_path, &sb) < 0) {
		perr("Members of the \"stapusr\" group can only use modules within\n"
		     "  the \"%s\" directory.\n"
		     "  Error getting information on that directory", staplib_dir_path);
		return -1;
	}
	/* Make sure it is a directory. */
	if (! S_ISDIR(sb.st_mode)) {
		err("ERROR: Members of the \"stapusr\" group can only use modules within\n"
		    "  the \"%s\" directory.\n"
		    "  That path must refer to a directory.\n", staplib_dir_path);
		return -1;
	}
	/* Make sure it is owned by root. */
	if (sb.st_uid != 0) {
		err("ERROR: Members of the \"stapusr\" group can only use modules within\n"
		    "  the \"%s\" directory.\n"
		    "  That directory should be owned by root.\n", staplib_dir_path);
		return -1;
	}
	/* Make sure it isn't world writable. */
	if (sb.st_mode & S_IWOTH) {
		err("ERROR: Members of the \"stapusr\" group can only use modules within\n"
		    "  the \"%s\" directory.\n"
		    "  That directory should not be world writable.\n", staplib_dir_path);
		return -1;
	}

	/* Use realpath() to canonicalize the module directory
	 * path. */
	if (realpath(staplib_dir_path, staplib_dir_realpath) == NULL) {
		perr("Members of the \"stapusr\" group can only use modules within\n"
		     "  the \"%s\" directory.\n"
		     "  Unable to canonicalize that directory",	staplib_dir_path);
		return -1;
	}
	
	/* Use realpath() to canonicalize the module path. */
	if (realpath(modpath, module_realpath) == NULL) {
		perr("Unable to canonicalize path \"%s\"",modpath);
		return -1;
	}
	
	/* Now we've got two canonicalized paths.  Make sure
	 * module_realpath starts with staplib_dir_realpath. */
	if (strncmp(staplib_dir_realpath, module_realpath,
		    strlen(staplib_dir_realpath)) != 0) {
		err("ERROR: Members of the \"stapusr\" group can only use modules within\n"
		    "  the \"%s\" directory.\n"
		    "  Module \"%s\" does not exist within that directory.\n",
		    staplib_dir_path, modpath);
		return 0;
	}
	return 1;
}

/*
 * Check the user's permissions.  Is he allowed to run staprun (or is
 * he limited to "blessed" modules)?
 *
 * Returns: -1 on errors, 0 on failure, 1 on success.
 */
int check_permissions(void)
{
	gid_t gid, gidlist[NGROUPS_MAX];
	gid_t stapdev_gid, stapusr_gid;
	int i, ngids;
	struct group *stgr;
	int path_check = 0;

	/* If we're root, we can do anything. */
	if (geteuid() == 0)
		return 1;

	/* Lookup the gid for group "stapdev" */
	errno = 0;
	stgr = getgrnam("stapdev");
	/* If we couldn't find the group, just set the gid to an
	 * invalid number.  Just because this group doesn't exist
	 * doesn't mean the other group doesn't exist. */
	if (stgr == NULL)
		stapdev_gid = (gid_t)-1;
	else
		stapdev_gid = stgr->gr_gid;

	/* Lookup the gid for group "stapusr" */
	errno = 0;
	stgr = getgrnam("stapusr");
	/* If we couldn't find the group, just set the gid to an
	 * invalid number.  Just because this group doesn't exist
	 * doesn't mean the other group doesn't exist. */
	if (stgr == NULL)
		stapusr_gid = (gid_t)-1;
	else
		stapusr_gid = stgr->gr_gid;

	/* If neither group was found, just return an error. */
	if (stapdev_gid == (gid_t)-1 && stapusr_gid == (gid_t)-1) {
		err("ERROR: unable to find either group \"stapdev\" or group \"stapusr\"\n");
		return -1;
	}

	/* According to the getgroups() man page, getgroups() may not
	 * return the effective gid, so try to match it first. */
	gid = getegid();
	if (gid == stapdev_gid)
		return 1;
	else if (gid == stapusr_gid)
		path_check = 1;

	/* Get the list of the user's groups. */
	ngids = getgroups(NGROUPS_MAX, gidlist);
	if (ngids < 0) {
		perr("Unable to retrieve group list");
		return -1;
	}

	for (i = 0; i < ngids; i++) {
		/* If the user is a member of 'stapdev', then we're
		 *  done, since he can use staprun without any
		 *  restrictions. */
		if (gidlist[i] == stapdev_gid)
			return 1;

		/* If the user is a member of 'stapusr', then we'll
		 * need to check the module path.  However, we'll keep
		 * checking groups since it is possible the user is a
		 * member of both groups and we haven't seen the
		 * 'stapdev' group yet. */
		if (gidlist[i] == stapusr_gid)
			path_check = 1;
	}

	/* If path_check is 0, then the user isn't a member of either
	 * group.  Error out. */
	if (path_check == 0) {
		err("ERROR: you must be a member of either group \"stapdev\" or group \"stapusr\"\n");
		return 0;
	}

	/* At this point the user is only a member of the 'stapusr'
	 * group.  Members of the 'stapusr' group can only use modules
	 * in /lib/modules/KVER/systemtap.  Make sure the module path
	 * is in that directory. */
	return check_path();
}

pthread_t symbol_thread_id = (pthread_t)0;
int kernel_ptr_size = 0;

/* Symbol handling thread */
void *handle_symbols(void __attribute__((unused)) *arg)
{
	ssize_t nb;
	void *data;
	int32_t type;
	char recvbuf[8192];

	dbug(2, "waiting for symbol requests\n");

	/* handle messages from control channel */
	while (1) {
		nb = read(control_channel, recvbuf, sizeof(recvbuf));
		if (nb <= 0) {
			if (errno != EINTR)
				_perr("Unexpected EOF in read (nb=%ld)", (long)nb);
			continue;
		}
		
		type = *(int32_t *)recvbuf;
		data = (void *)(recvbuf + sizeof(int32_t));
		
		switch (type) { 
		case STP_MODULE:
		{
			dbug(2, "STP_MODULES request received\n");
			if (do_module(data) < 0)
				goto done;
			break;
		}		
		case STP_SYMBOLS:
		{
			struct _stp_msg_symbol *req = (struct _stp_msg_symbol *)data;
			dbug(2, "STP_SYMBOLS request received\n");
			if (req->endian != 0x1234) {
				err("ERROR: staprun is compiled with different endianess than the kernel!\n");
				goto done;
			}
			kernel_ptr_size = req->ptr_size;
			if (kernel_ptr_size != 4 && kernel_ptr_size != 8) {
				err("ERROR: invalid kernel pointer size %d\n", kernel_ptr_size);
				goto done;
			}
			if (do_kernel_symbols() < 0)
				goto done;
			break;
		}
		default:
			err("WARNING: ignored message of type %d\n", (type));
		}
	}

done:
	/* signal stapio we're done */
	kill(0, SIGINT);

	return NULL;
}

void start_symbol_thread(void)
{
	int status;

	/* create symbol control channel */
	status = do_cap(CAP_DAC_OVERRIDE, init_ctl_channel, 1);
	drop_cap(CAP_DAC_OVERRIDE);
	if (status < 0) {
		err("Failed to initialize control channel.\n");
		exit(1);
	}
	status = pthread_create(&symbol_thread_id, NULL, handle_symbols, NULL);
	if (status) {
		perr("Failed to create symbol thread.\n");
		exit(1);
	}
}

void stop_symbol_thread(void)
{

	if (symbol_thread_id) {
		dbug(2, "Stopping symbol thread.\n");
		pthread_cancel(symbol_thread_id);
		pthread_join(symbol_thread_id, NULL);
	}
	close_ctl_channel();
}

