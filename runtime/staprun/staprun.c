/* -*- linux-c -*-
 *
 * staprun.c - SystemTap module loader
 *
 * Copyright (C) 2005-2010 Red Hat, Inc.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include "staprun.h"
#include <string.h>
#include <sys/uio.h>
#include <glob.h>
#include <time.h>
#include <sys/prctl.h>

/* used in dbug, _err and _perr */
char *__name__ = "staprun";

extern long delete_module(const char *, unsigned int);

int send_relocations ();
int send_tzinfo ();


static int run_as(int exec_p, uid_t uid, gid_t gid, const char *path, char *const argv[])
{
	pid_t pid;
	int rstatus;

	if (verbose >= 2) {
		int i = 0;
		err(exec_p ? "execing: ": "spawning: ");
		while (argv[i]) {
			err("%s ", argv[i]);
			i++;
		}
		err("\n");
	}

        if (exec_p)
          pid = 0;
        else
          pid = fork();

        if (pid < 0)
          {
            _perr("fork");
            return -1;
          }

	if (pid == 0) /* child process, or exec_p */
          {
            /* Make sure we run as the full user.  If we're
             * switching to a non-root user, this won't allow
             * that process to switch back to root (since the
             * original process is setuid). */
            if (setresgid (gid, gid, gid) < 0) {
              _perr("setresgid");
              exit(1);
            }
            if (setresuid (uid, uid, uid) < 0) {
              _perr("setresuid");
              exit(1);
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
	char runtimeko[2048];
	uid_t uid = getuid();
	gid_t gid = getgid();

	i = 0;
	argv[i++] = "/bin/grep";
	argv[i++] = "-q";
	argv[i++] = "unregister_uprobe";
	argv[i++] = "/proc/kallsyms";
	argv[i] = NULL;
	if (run_as(0, uid, gid, argv[0], argv) == 0)
		return 0;

        /* NB: don't use /sbin/modprobe, without more env. sanitation. */

	/* Try the specified module or the one from the runtime.  */
	if (uprobes_path)
	  snprintf (runtimeko, sizeof(runtimeko), "%s", uprobes_path);
	else
	  snprintf (runtimeko, sizeof(runtimeko), "%s/uprobes/uprobes.ko",
		    (getenv("SYSTEMTAP_RUNTIME") ?: PKGDATADIR "/runtime"));
	dbug(2, "Inserting uprobes module from %s.\n", runtimeko);
	/* This module may be signed, so use insert_module to load it.  */
	argv[0] = NULL;
	if (insert_module(runtimeko, NULL, argv, assert_uprobes_module_permissions) == 0)
		return 0;

	return 1; /* failure */
}

static int insert_stap_module(void)
{
	char special_options[128];

	/* Add the _stp_bufsize option.  */
	if (snprintf_chk(special_options, sizeof (special_options), "_stp_bufsize=%d", buffer_size))
		return -1;

	return insert_module(modpath, special_options, modoptions, assert_stap_module_permissions);
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

#ifdef PR_SET_NAME
        /* Make self easier to identify in vmcrash images */
        prctl (PR_SET_NAME, "staprun-d");
#endif

        (void) verb; /* XXX: ignore */

	if (strcmp(name, "*") == 0) {
		remove_all_modules();
		return 0;
	}

        /* We call init_ctl_channel/close_ctl_channel to check whether
           the module is a systemtap-built one (having the right files),
           and that it's already unattached (because otherwise it'd EBUSY
           the opens. */
        ret = init_ctl_channel (name, 0);
        if (ret < 0) {
                err("Error, '%s' is not a zombie systemtap module.\n", name);
                return ret;
        }
        close_ctl_channel ();

	dbug(2, "removing module %s\n", name);
	PROBE1(staprun, remove__module, name);
	ret = delete_module (name, O_NONBLOCK);
	if (ret != 0) {
                /* XXX: maybe we should just accept this, with a
                   diagnostic, but without an error.  Might it be
                   possible for the same module to be started up just
                   as we're shutting down?  */
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

	if (delete_mod)
		exit(remove_module(modname, 1));
	else if (!attach_mod) {
		if (need_uprobes && enable_uprobes() != 0)
			return -1;
		if (insert_stap_module() < 0) {
#ifdef HAVE_ELF_GETSHDRSTRNDX
			if(!rename_mod && errno == EEXIST)
				err("Rerun with staprun option '-R' to rename this module.\n");
#endif
                        /* Without a working rename_module(), we shan't
                           advise people to use -R. */
			return -1;
		}
		if (send_relocations() < 0)
			return -1;
                if (send_tzinfo() < 0)
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

	if (getuid() != geteuid()) { /* setuid? */
		rc = unsetenv("SYSTEMTAP_STAPRUN") ||
                  unsetenv("SYSTEMTAP_STAPIO") ||
                  unsetenv("SYSTEMTAP_RUNTIME");

		if (rc) {
			_perr("unsetenv failed");
			exit(-1);
		}
	}

	setup_signals();

	parse_args(argc, argv);

	if (buffer_size)
		dbug(2, "Using a buffer of %u MB.\n", buffer_size);

	int mod_optind = optind;
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

	if (init_staprun())
		exit(1);

	argv[0] = getenv ("SYSTEMTAP_STAPIO") ?: PKGLIBDIR "/stapio";

	/* Copy nenamed modname into argv */
	if(rename_mod)
		argv[mod_optind] = modname;

	/* Run stapio */
	if (run_as (1, getuid(), getgid(), argv[0], argv) < 0) {
		perror(argv[0]);
		goto err;
	}

	free(modname);
	return 0;

err:
	remove_module(modname, 1);
	free(modname);
	return 1;
}



/* Send a variety of relocation-related data to the kernel: for the
   kernel proper, just the "_stext" symbol address; for all loaded
   modules, a variety of symbol base addresses.

   We do this under protest.  The kernel ought expose this data to
   modules such as ourselves, but instead the upstream community
   continually shrinks its module-facing interfaces, including this
   stuff, even when users exist.
*/


void send_a_relocation (const char* module, const char* reloc, unsigned long long address)
{
  struct _stp_msg_relocation msg;

  if (strlen(module) >= STP_MODULE_NAME_LEN-1) {
          dbug (1, "module name too long: %s", module);
          return; 
  }
  strncpy (msg.module, module, STP_MODULE_NAME_LEN);
  
  if (strlen(reloc) >= STP_SYMBOL_NAME_LEN-1) {
          dbug (1, "reloc name too long: %s", module);
          return; 
  }
  strncpy (msg.reloc, reloc, STP_MODULE_NAME_LEN);

  msg.address = address;

  send_request (STP_RELOCATION, & msg, sizeof (msg));
  /* XXX: verify send_request RC */
}


#ifdef __powerpc64__
#define KERNEL_RELOC_SYMBOL ".__start"
#else
#define KERNEL_RELOC_SYMBOL "_stext"
#endif

int send_relocation_kernel ()
{
  FILE* kallsyms = fopen ("/proc/kallsyms", "r");
  if (kallsyms == NULL)
    {
      perror("cannot open /proc/kallsyms");
      // ... and the kernel module will almost certainly fail to initialize.
    }
  else
    {
      int done_with_kallsyms = 0;
      char *line = NULL;
      size_t linesz = 0;
      while (! feof(kallsyms) && !done_with_kallsyms)
        {
          ssize_t linesize = getline (& line, & linesz, kallsyms);
          if (linesize > 0)
            {
              unsigned long long address;
	      int pos = -1;
	      if (sscanf (line, "%llx %*c %n", &address, &pos) == 1
		  && pos != -1
		  && linesize - pos == sizeof KERNEL_RELOC_SYMBOL
		  && !strcmp(line + pos, KERNEL_RELOC_SYMBOL "\n"))
                {
                  /* NB: even on ppc, we use the _stext relocation name. */
                  send_a_relocation ("kernel", "_stext", address);

                  /* We need nothing more from the kernel. */
                  done_with_kallsyms=1;
                }
            }
        }
      free (line);
      fclose (kallsyms);
      if (!done_with_kallsyms)
	return -1;

      /* detect note section, send flag if there
       * NB: address=2 represents existed note, the real one in _stp_module
       */
      if (!access("/sys/kernel/notes", R_OK))
	 send_a_relocation ("kernel", ".note.gnu.build-id", 2);
    }

  return 0;
}


void send_relocation_modules ()
{
  unsigned i = 0;
  glob_t globbuf;
  globbuf.gl_pathc = 0;
  int r = glob("/sys/module/*/sections/*", GLOB_PERIOD, NULL, &globbuf);

  if (r == GLOB_NOSPACE || r == GLOB_ABORTED)
    return;

  for (i=0; i<globbuf.gl_pathc; i++)
    {
      char *module_section_file;
      char *section_name;
      char *module_name;
      char *module_name_end;
      FILE* secfile;
      unsigned long long section_address;

      module_section_file = globbuf.gl_pathv[i];

      /* Tokenize the file name.
         Sample gl_pathv[]: /sys/modules/zlib_deflate/sections/.text
         Pieces:                         ^^^^^^^^^^^^          ^^^^^
      */
      section_name = strrchr (module_section_file, '/');
      if (! section_name) continue;
      section_name ++;

      if (!strcmp (section_name, ".")) continue;
      if (!strcmp (section_name, "..")) continue;

      module_name = strchr (module_section_file, '/');
      if (! module_name) continue;
      module_name ++;
      module_name = strchr (module_name, '/');
      if (! module_name) continue;
      module_name ++;
      module_name = strchr (module_name, '/');
      if (! module_name) continue;
      module_name ++;

      module_name_end = strchr (module_name, '/');
      if (! module_name_end) continue;

      secfile = fopen (module_section_file, "r");
      if (! secfile) continue;

      if (1 == fscanf (secfile, "0x%llx", &section_address))
        {
          /* Now we destructively modify the string, but by now the file
             is open so we won't need the full name again. */
          *module_name_end = '\0';

          send_a_relocation (module_name, section_name, section_address);
        }

      if (strcmp (section_name, ".gnu.linkonce.this_module"))
        fclose (secfile);
      else
        {
          (void)set_clexec (fileno (secfile));
          /* NB: don't fclose this arbitrarily-chosen section file.
             This forces the kernel to keep a nonzero reference count
             on the subject module, until staprun exits, by which time
             the kernel module will have inserted its separate claws
             into the probeworthy modules.  This prevents a race
             condition where a probe may be just starting up at the
             same time that a probeworthy module is being unloaded. */
        }
    }

  globfree (& globbuf);
}



int send_relocations ()
{
  int rc;
  rc = init_ctl_channel (modname, 0);
  if (rc < 0) goto out;
  rc = send_relocation_kernel ();
  send_relocation_modules ();
  close_ctl_channel ();
 out:
  return rc;
}


int send_tzinfo ()
{
  int rc;
  struct _stp_msg_tzinfo tzi;
  time_t now_t;
  struct tm* now;

  rc = init_ctl_channel (modname, 0);
  if (rc < 0) goto out;

  /* NB: This is not good enough; it sends DST-unaware numbers. */
#if 0
  tzset ();
  tzi.tz_gmtoff = timezone;
  strncpy (tzi.tz_name, tzname[0], STP_TZ_NAME_LEN);
#endif

  time (& now_t);
  now = localtime (& now_t);
  tzi.tz_gmtoff = - now->tm_gmtoff;
  strncpy (tzi.tz_name, now->tm_zone, STP_TZ_NAME_LEN);

  send_request(STP_TZINFO, & tzi, sizeof(tzi));
  close_ctl_channel ();
 out:
  return rc;
}
