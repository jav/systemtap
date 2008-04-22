/* -*- linux-c -*-
 *
 * staprun.c - SystemTap module loader 
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
 * Copyright (C) 2005-2008 Red Hat, Inc.
 *
 */

#include "staprun.h"

/* used in dbug, _err and _perr */
char *__name__ = "staprun";

extern long delete_module(const char *, unsigned int);

static int run_as(uid_t uid, gid_t gid, const char *path, char *const argv[])
{
	pid_t pid;
	int rstatus;

	if (verbose >= 2) {
		int i = 0;
		err("execing: ");
		while (argv[i]) {
			err("%s ", argv[i]);
			i++;
		}
		err("\n");
	}

	if ((pid = fork()) < 0) {
		_perr("fork");
		return -1;
	} else if (pid == 0) {
		/* Make sure we run as the full user.  If we're
		 * switching to a non-root user, this won't allow
		 * that process to switch back to root (since the
		 * original process is setuid). */
		if (uid != getuid()) {
			if (do_cap(CAP_SETGID, setresgid, gid, gid, gid) < 0) {
				_perr("setresgid");
				exit(1);
			}
			if (do_cap(CAP_SETUID, setresuid, uid, uid, uid) < 0) {
				_perr("setresuid");
				exit(1);
			}
		}

		/* Actually run the command. */
		if (execv(path, argv) < 0)
			perror(path);
		_exit(1);
	}

	if (waitpid(pid, &rstatus, 0) < 0)
		return -1;

	if (WIFEXITED(rstatus))
		return WEXITSTATUS(rstatus);
	return -1;
}

/*
 * Module to be inserted has one or more user-space probes.  Make sure
 * uprobes is enabled.
 * If /proc/kallsyms lists a symbol in uprobes (e.g. unregister_uprobe),
 * we're done.
 * Else try "modprobe uprobes" to load the uprobes module (if any)
 * built with the kernel.
 * If that fails, load the uprobes module built in runtime/uprobes.
 */
static int enable_uprobes(void)
{
	int i;
	char *argv[10];
	uid_t uid = getuid();
	gid_t gid = getgid();

	i = 0;
	argv[i++] = "/bin/grep";
	argv[i++] = "-q";
	argv[i++] = "unregister_uprobe";
	argv[i++] = "/proc/kallsyms";
	argv[i] = NULL;
	if (run_as(uid, gid, argv[0], argv) == 0)
		return 0;

	/*
	 * TODO: If user can't setresuid to root here, staprun will exit.
	 * Is there a situation where that would fail but the subsequent
	 * attempt to use CAP_SYS_MODULE privileges (in insert_module())
	 * would succeed?
	 */
	dbug(2, "Inserting uprobes module from /lib/modules, if any.\n");
	i = 0;
	argv[i++] = "/sbin/modprobe";
	argv[i++] = "-q";
	argv[i++] = "uprobes";
	argv[i] = NULL;
	if (run_as(0, 0, argv[0], argv) == 0)
		return 0;

	dbug(2, "Inserting uprobes module from SystemTap runtime.\n");
	argv[0] = NULL;
	return insert_module(PKGDATADIR "/runtime/uprobes/uprobes.ko", NULL, argv);
}

static int insert_stap_module(void)
{
	char bufsize_option[128];
	if (snprintf_chk(bufsize_option, 128, "_stp_bufsize=%d", buffer_size))
		return -1;
	return insert_module(modpath, bufsize_option, modoptions);
}

static int remove_module(const char *name, int verb);

static void remove_all_modules(void)
{
	char *base;
	struct statfs st;
	struct dirent *d;
	DIR *moddir;

	if (statfs("/sys/kernel/debug", &st) == 0 && (int)st.f_type == (int)DEBUGFS_MAGIC)
		base = "/sys/kernel/debug/systemtap";
	else
		base = "/proc/systemtap";

	moddir = opendir(base);
	if (moddir) {
		while ((d = readdir(moddir)))
			if (remove_module(d->d_name, 0) == 0)
				printf("Module %s removed.\n", d->d_name);
		closedir(moddir);
	}
}

static int remove_module(const char *name, int verb)
{
	int ret;
	dbug(2, "%s\n", name);

	if (strcmp(name, "*") == 0) {
		remove_all_modules();
		return 0;
	}

	/* Call init_ctl_channel() which actually attempts an open()
	 * of the control channel. This is better than using access() because 
	 * an open on an already open channel will fail, preventing us from attempting
	 * to remove an in-use module. 
	 */
	if (init_ctl_channel(name, 0) < 0) {
		if (verb)
			err("Error accessing systemtap module %s: %s\n", name, strerror(errno));
		return 1;
	}
	close_ctl_channel();

	dbug(2, "removing module %s\n", name);

	/* Don't remove module when priority is elevated. */
	if (setpriority(PRIO_PROCESS, 0, 0) < 0)
		_perr("setpriority");

	ret = do_cap(CAP_SYS_MODULE, delete_module, name, 0);
	if (ret != 0) {
		err("Error removing module '%s': %s.\n", name, strerror(errno));
		return 1;
	}

	dbug(1, "Module %s removed.\n", name);
	return 0;
}

int init_staprun(void)
{
	dbug(2, "init_staprun\n");

	if (mountfs() < 0)
		return -1;

	/* We're done with CAP_SYS_ADMIN. */
	drop_cap(CAP_SYS_ADMIN);

	if (delete_mod)
		exit(remove_module(modname, 1));
	else if (!attach_mod) {
		if (need_uprobes && enable_uprobes() != 0)
			return -1;
		if (insert_stap_module() < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int rc;

	/* NB: Don't do the geteuid()!=0 check here, since we want to
	   test command-line error-handling while running non-root. */
	/* Get rid of a few standard environment variables (which */
	/* might cause us to do unintended things). */
	rc = unsetenv("IFS") || unsetenv("CDPATH") || unsetenv("ENV")
	    || unsetenv("BASH_ENV");
	if (rc) {
		_perr("unsetenv failed");
		exit(-1);
	}

	setup_signals();

	parse_args(argc, argv);

	if (buffer_size)
		dbug(2, "Using a buffer of %u bytes.\n", buffer_size);

	if (optind < argc) {
		parse_modpath(argv[optind++]);
		dbug(2, "modpath=\"%s\", modname=\"%s\"\n", modpath, modname);
	}

	if (optind < argc) {
		if (attach_mod) {
			err("ERROR: Cannot have module options with attach (-A).\n");
			usage(argv[0]);
		} else {
			unsigned start_idx = 0;
			while (optind < argc && start_idx + 1 < MAXMODOPTIONS)
				modoptions[start_idx++] = argv[optind++];
			modoptions[start_idx] = NULL;
		}
	}

	if (modpath == NULL || *modpath == '\0') {
		err("ERROR: Need a module name or path to load.\n");
		usage(argv[0]);
	}

	if (geteuid() != 0) {
		err("ERROR: The effective user ID of staprun must be set to the root user.\n"
		    "  Check permissions on staprun and ensure it is a setuid root program.\n");
		exit(1);
	}

	init_cap();
		
	if (check_permissions() != 1)
		usage(argv[0]);

	/* now bump the priority */
	rc = do_cap(CAP_SYS_NICE, setpriority, PRIO_PROCESS, 0, -10);
	/* failure is not fatal in this case */
	if (rc < 0)
		_perr("setpriority");

	/* We're done with CAP_SYS_NICE. */
	drop_cap(CAP_SYS_NICE);

	if (init_staprun())
		exit(1);

	argv[0] = PKGLIBDIR "/stapio";
	if (execv(argv[0], argv) < 0) {
		perror(argv[0]);
		goto err;
	}
	return 0;

err:
	remove_module(modname, 1);
	return 1;
}
