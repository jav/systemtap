/* -*- linux-c -*-
 *
 * staprun_funcs.c - staprun functions
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2007-2012 Red Hat Inc.
 */

#include "config.h"
#include "staprun.h"

#include <sys/mount.h>
#include <sys/utsname.h>
#include <grp.h>
#include <pwd.h>
#include <assert.h>

/* The module-renaming facility only works with new enough
   elfutils: 0.142+. */
#ifdef HAVE_LIBELF_H
#include <libelf.h>
#include <gelf.h>
#endif

#include <math.h>

#include "modverify.h"
#include "../../privilege.h"

typedef int (*check_module_path_func)(const char *module_path, int module_fd);

extern long init_module(void *, unsigned long, const char *);
extern gid_t get_gid (const char *group_name);

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

int insert_module(
  const char *path,
  const char *special_options,
  char **options,
  assert_permissions_func assert_permissions
) {
	int i;
	long ret, module_read;
	void *module_file;
	char *opts;
	int saved_errno;
	char module_realpath[PATH_MAX];
	int module_fd;
	struct stat sbuf;
	int rename_this_module;

	dbug(2, "inserting module %s\n", path);

	/* Rename the script module if '-R' was passed, but not other modules
	 * like uprobes.  We can tell which this is by comparing to the global
	 * modpath, but we must do it before it's transformed by realpath.  */
	rename_this_module = rename_mod && (strcmp(path, modpath) == 0);

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
		/* Note that these strcat() calls are OK, since we just
		 * allocated space for the resulting string. */
		strcat(opts, " ");
		strcat(opts, options[i]);
	}
	dbug(2, "module options: %s\n", opts);

	/* Use realpath() to canonicalize the module path. */
	if (realpath(path, module_realpath) == NULL) {
		perr("Unable to canonicalize path \"%s\"", path);
		free(opts);
		return -1;
	}
	dbug(2, "module path canonicalized to '%s'\n", module_realpath);

        /* Use module_realpath from this point on. "Poison" 'path'
	   by setting it to NULL so that it doesn't get used again by
	   mistake.  */
        path = NULL;

	/* Open the module file. Work with the open file descriptor from this
	   point on to avoid TOCTOU problems. */
	module_fd = open(module_realpath, O_RDONLY);
	if (module_fd < 0) {
		perr("Error opening '%s'", module_realpath);
		free(opts);
		return -1;
	}

	/* Now that the file is open, figure out how big it is. */
	if (fstat(module_fd, &sbuf) < 0) {
		_perr("Error stat'ing '%s'", module_realpath);
		close(module_fd);
		free(opts);
		return -1;
	}

        /* Allocate memory for the entire module. */
        module_file = calloc(1, sbuf.st_size);
        if (module_file == NULL) {
                _perr("Error allocating memory to read '%s'", module_realpath);
		close(module_fd);
		free(opts);
		return -1;
	}

        /* Read in the entire module.  Work with this copy of the data from this
           point on to avoid a TOCTOU race between path and signature checking
           below and module loading.  */
        module_read = 0;
        while (module_read < sbuf.st_size) {
                ret = read(module_fd, module_file + module_read,
                           sbuf.st_size - module_read);
                if (ret > 0)
                        module_read += ret;
                else if (ret == 0) {
                        _err("Unexpected EOF reading '%s'", module_realpath);
                        free(module_file);
                        close(module_fd);
                        free(opts);
                        return -1;
                } else if (errno != EINTR) {
                        _perr("Error reading '%s'", module_realpath);
                        free(module_file);
                        close(module_fd);
                        free(opts);
                        return -1;
                }
        }

	/* Check whether this module can be loaded by the current user.
	 * check_permissions will exit(-1) if permissions are insufficient*/
	assert_permissions (module_realpath, module_fd, module_file, sbuf.st_size);

	/* Rename Module if '-R' was passed */
	if (rename_this_module) {
		dbug(2,"Renaming module '%s'\n", modname);
		if(rename_module(module_file, sbuf.st_size) < 0) {

			  _err("Error renaming module");
			  close(module_fd);
			  free(opts);
			  return -1;
		}
		dbug(2,"Renamed module to '%s'\n", modname);
	}

        /* As a debugging aid for testing risky stap modules. */
        if (getenv ("SYSTEMTAP_SYNC") != NULL)
                sync();

	PROBE1(staprun, insert__module, (char*)module_realpath);
	/* Actually insert the module */
	ret = init_module(module_file, sbuf.st_size, opts);
	saved_errno = errno;

	/* Cleanup. */
	free(opts);
	free(module_file);
	close(module_fd);

	if (ret != 0) {
		errno = saved_errno;
		return -errno;
	}
	return 0;
}

#ifdef HAVE_ELF_GETSHDRSTRNDX
static Elf_Scn *
find_section_in_module(const void* module_file, const __off_t st_size, const char *section_name)
{
	char *name;
	size_t shstrndx;
	Elf* elf;
	Elf_Scn *scn = 0;
	GElf_Shdr shdr_mem;

  	/* Create descriptor for memory region.  */
  	if((elf = elf_memory ((void *)module_file, st_size))== NULL) {
  		_err("Error creating Elf object.\n");
  		return NULL;
  	}

  	/* Get the string section index */
  	if(elf_getshdrstrndx (elf, &shstrndx) < 0) {
  		_err("Error getting section index.\n");
  		return NULL;
  	}

  	/* Go through the sections looking for the given section" */
  	while ((scn = elf_nextscn (elf, scn))) {
  		 if((gelf_getshdr (scn, &shdr_mem))==NULL) {
  		 	 _err("Error getting section header.\n");
  		 	 return NULL;
  		 }
  		 name = elf_strptr (elf, shstrndx, shdr_mem.sh_name);
  		 if (name == NULL) {
  			 _err("Error getting section name.\n");
  			 return NULL;
  		 }
  		 if(strcmp(name, section_name) == 0) {
  			 break;
  		 }
  	}
	return scn;
}
#endif /* HAVE_ELF_GETSHDRSTRNDX */

int
rename_module(void* module_file, const __off_t st_size)
{
#ifdef HAVE_ELF_GETSHDRSTRNDX
	int length_to_replace;
	char newname[MODULE_NAME_LEN];
	char *p;
	pid_t pid;
	Elf_Scn *scn = 0;
	Elf_Data *data = 0;

	scn = find_section_in_module (module_file, st_size, ".gnu.linkonce.this_module");
  	if(!scn) {
  		_err("Section name \".gnu.linkonce.this_module\" not found in module.\n");
  		return -1;
  	}

  	/* Get access to raw data from section; do not translate/copy. */
  	if ((data = elf_rawdata (scn, data)) == NULL) {
  		_err("Error getting Elf data from section.\n");
  		return -1;
  	}

  	/* Generate new module name with the same length as the old name.
    	   The new name is of the form: stap_<FirstPartOfOldname>_<pid>*/
  	if (strlen(modname) >= MODULE_NAME_LEN) {
  		_err("Old module name is too long.\n");
  		return -1;
  	}
  	pid = getpid();
  	length_to_replace = (int)strlen(modname)-((int)log10(pid)+1) - 1;
  	if(length_to_replace < 0 || length_to_replace > (int)strlen(modname)) {
  		_err("Error getting length of oldname to replace in newname.\n");
  		return -1;
  	}
  	if (snprintf(newname, sizeof(newname), "%.*s_%d", length_to_replace, modname, pid) < 0) {
  	    	_err("Creating newname failed./n");
  	    	return -1;
  	}

	/* Find where it is in the module structure.
	   To our knowledge, this section is always completely zeroed apart
	   from the module name, so a simple search and replace should suffice.
	   A signed module from any stapusr or stapsys user will already have been proven
	   untampered.  An unsigned module from a stapdev user could try to do
	   naughty things, but we're already trusting these users so much
	   that they can shoot their feet however they like.
	   */
	for (p = data->d_buf; p < (char *)data->d_buf + data->d_size - strlen(modname); p++) {
		if (memcmp(p, modname, strlen(modname)) == 0) {
			strncpy(p, newname, strlen(p)); /* Actually replace the oldname in memory with the newname */
			modname = strdup(newname); /* This is just to update the global variable containing the current module name */
			if (modname == NULL) {
				_perr("allocating memory failed");
				return -1;
			}
			return 0;
		}
	}
	_err("Could not find old name to replace!\n");
	return -1;
#else
        /* Old or no elfutils?  Pretend to have renamed.  This means a
           greater likelihood for module-name collisions, but so be
           it. */
        (void) module_file;
        (void) st_size;
        return 0; 
#endif
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
          	rc = mount ("debugfs", DEBUGFSDIR, "debugfs", 0, NULL);
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
                /* XXX: Why not just chown() the thing? */
		if (setuid (0) < 0) {
			_perr("Couldn't change user while creating %s", RELAYFSDIR);
			return -1;
		}
		if (setgid (0) < 0) {
			_perr("Couldn't change group while creating %s", RELAYFSDIR);
			return -1;
		}

		/* Try to create the directory, saving the return
		 * status and errno value. */
		rc = mkdir(RELAYFSDIR, 0755);
		saved_errno = errno;

		/* Restore everything we changed. */
		if (setgid (gid) < 0) {
			_perr("Couldn't restore group while creating %s", RELAYFSDIR);
			return -1;
		}
		if (setuid (uid) < 0) {
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
	if (mount ("relayfs", RELAYFSDIR, "relayfs", 0, NULL) < 0) {
		perr("Couldn't mount %s", RELAYFSDIR);
		return -1;
	}
	return 0;
}

/*
 * Check the signature of the given module.
 *
 * Returns: -1 on errors, 0 on failure, 1 on success.
 */
#if ! HAVE_NSS
static int
check_signature(const char *path __attribute__((unused)),
		const void *module_data __attribute__((unused)),
		off_t module_size __attribute__((unused)))
{
  return MODULE_UNTRUSTED;
}
#else
static int
check_signature(const char *path, const void *module_data, off_t module_size)
{
  char signature_realpath[PATH_MAX];
  int rc;

  dbug(2, "checking signature for %s\n", path);

  /* Add the .sgn suffix to the canonicalized module path to get the signature
     file path.  */
  if (strlen (path) >= PATH_MAX - 5) {
    err("Path \"%s.sgn\" is too long.", path);
    return -1;
  }
  /* This use of sprintf() is OK, since we just checked the final
   * string's length. */
  sprintf (signature_realpath, "%s.sgn", path);

  rc = verify_module (signature_realpath, path, module_data, module_size);
  dbug(2, "verify_module returns %d\n", rc);

  return rc;
}
#endif /* HAVE_NSS */

/*
 * For stap modules which have not been signed by a trusted signer,
 * members of the 'stapusr' group can only use the "blessed" modules -
 * ones in the '/lib/modules/KVER/systemtap' directory.  Make sure the
 * module path is in that directory.
 *
 * Returns: -1 on errors, 1 on success.
 */
static int
check_stap_module_path(const char *module_path, int module_fd)
{
	char staplib_dir_path[PATH_MAX];
	char staplib_dir_realpath[PATH_MAX];
	struct utsname utsbuf;
	struct stat sb;
	int rc = 1;

	/* First, we need to figure out what the kernel
	 * version is and build the '/lib/modules/KVER/systemtap' path. */
	if (uname(&utsbuf) != 0) {
		_perr("ERROR: Unable to determine kernel version, uname failed");
		return -1;
	}
	if (sprintf_chk(staplib_dir_path, "/lib/modules/%s/systemtap", utsbuf.release))
		return -1;
	/* Use realpath() to canonicalize the module directory path. */
	if (realpath(staplib_dir_path, staplib_dir_realpath) == NULL) {
		perr("Members of the \"stapusr\" and \"stapsys\" groups can only use unsigned modules within\n"
		     "  the \"%s\" directory.\n"
		     "  Unable to canonicalize that directory",
		     staplib_dir_path);
		return -1;
	}

	/* To make sure the user can't specify something like
	 * /lib/modules/`uname -r`/systemtapmod.ko, put a '/' on the
	 * end of staplib_dir_realpath. */
	if (strlen(staplib_dir_realpath) < (PATH_MAX - 1))
		/* Note that this strcat() is OK, since we just
		 * checked the length of the resulting string.  */
		strcat(staplib_dir_realpath, "/");
	else {
		err("ERROR: Path \"%s\" is too long.", staplib_dir_realpath);
		return -1;
	}

	/* Validate /lib/modules/KVER/systemtap. No need to use fstat on
	   an open file descriptor to avoid TOCTOU, since the path will
	   not be used to access the file system.  */
	if (stat(staplib_dir_path, &sb) < 0) {
		perr("Members of the \"stapusr\" and \"stapsys\" groups can only use unsigned modules within\n"
		     "  the \"%s\" directory.\n"
		     "  Error getting information on that directory",
		     staplib_dir_path);
		return -1;
	}
	/* Make sure it is a directory. */
	if (! S_ISDIR(sb.st_mode)) {
		err("ERROR: Members of the \"stapusr\" and \"stapsys\" groups can only use unsigned modules within\n"
		    "  the \"%s\" directory.\n"
		    "  That path must refer to a directory.\n",
		    staplib_dir_path);
		return -1;
	}
	/* Make sure it is owned by root. */
	if (sb.st_uid != 0) {
		err("ERROR: Members of the \"stapusr\" and \"stapsys\" groups can only use unsigned modules within\n"
		    "  the \"%s\" directory.\n"
		    "  That directory should be owned by root.\n",
		    staplib_dir_path);
		return -1;
	}
	/* Make sure it isn't world writable. */
	if (sb.st_mode & S_IWOTH) {
		err("ERROR: Members of the \"stapusr\" and \"stapsys\" groups can only use unsigned modules within\n"
		    "  the \"%s\" directory.\n"
		    "  That directory should not be world writable.\n",
		    staplib_dir_path);
		return -1;
	}

	/* Now we've got two canonicalized paths.  Make sure
	 * module_path starts with staplib_dir_realpath. */
	if (strncmp(staplib_dir_realpath, module_path,
		    strlen(staplib_dir_realpath)) != 0) {
		err("ERROR: Members of the \"stapusr\" and \"stapsys\" groups can only use unsigned modules within\n"
		    "  the \"%s\" directory.\n"
		    "  Module \"%s\" does not exist within that directory.\n",
		    staplib_dir_path, module_path);
		return -1;
	}

	/* Validate the module permisions. */
	if (fstat(module_fd, &sb) < 0) {
		perr("Error getting information on the module\"%s\"", module_path);
		return -1;
	}
	/* Make sure it is owned by root. */
	if (sb.st_uid != 0) {
		err("ERROR: The module \"%s\" must be owned by root.\n", module_path);
		rc = -1;
	}
	/* Make sure it isn't world writable. */
	if (sb.st_mode & S_IWOTH) {
		err("ERROR: The module \"%s\" must not be world writable.\n", module_path);
		rc = -1;
	}

	return rc;
}

/*
 * Don't allow path-based authorization for the uprobes module at all.
 * Members of the 'stapusr' group can load a signed uprobes module, but
 * nothing else.  Later we could consider allowing specific paths, like
 * the installed runtime or /lib/modules/...
 *
 * Returns: -1 on errors, 0 on failure, 1 on success.
 */
static int
check_uprobes_module_path (
  const char *module_path __attribute__ ((unused)),
  int module_fd __attribute__ ((unused))
)
{
  return 0;
}

/*
 * Obtain the privilege credentials required to load the given module from the module itself.
 *
 * Returns the required credentials if they can be determined or the default safe required
 * credentials otherwise.
 */
static privilege_t get_module_required_credentials (
  const char *module_path,
  const void* module_file __attribute__ ((unused)),
  const __off_t st_size __attribute__ ((unused))
)
{
#ifndef HAVE_ELF_GETSHDRSTRNDX
  /* Without the proper ELF support, we can't determine the credentials required to run this
     module. It may have some future privilege level higher than stapsys, which we don't know about.
     We are forced to assume that requires the highest privilege level. */
  if (verbose >= 1) {
    err ("Unable to determine the privilege level required for the module %s. Assuming %s.",
	 module_path, pr_name (pr_highest));
  }
  return pr_highest;
#else
  Elf_Scn *scn = 0;
  Elf_Data *data = 0;
  GElf_Shdr shdr;
  privilege_t privilege;

  /* Look for the section containing the privilege information in the module. If we can't
     find the section, then we can assume that it is an old stap module, correctly signed, because:
       1) We only check privilege credentials for correctly signed modules
       2) A newer module would contain the required section.
     We can therefore assume that pr_stapusr is the required privilege level.
  */
  scn = find_section_in_module (module_file, st_size, STAP_PRIVILEGE_SECTION);
  if (! scn) {
    if (verbose >= 1) {
      err ("Section name \"%s\" not found in module %s.\n", STAP_PRIVILEGE_SECTION,
	   module_path);
      err ("Assuming required privilege level of %s.\n", pr_name (pr_unprivileged));
    }
    return pr_unprivileged;
  }

  /* From here on if there is an error in the data, then it is most likely caused by a newer
     module containing data that we don't understand. We must then assume the highest privilege
     requirement.
     Get the section header. */
  if (gelf_getshdr (scn, & shdr) == NULL) {
    if (verbose >= 1) {
      err ("Error getting section header from section %s in module %s.\n", STAP_PRIVILEGE_SECTION,
	   module_path);
      err ("Assuming required privilege level of %s.", pr_name (pr_highest));
    }
    return pr_highest;
  }

  /* The section should have at least one data item. */
  if (shdr.sh_size < 1) {
    if (verbose >= 1) {
      err ("Section header from section %s in module %s has no items\n", STAP_PRIVILEGE_SECTION,
	   module_path);
      err ("Assuming required privilege level of %s.", pr_name (pr_highest));
    }
    return pr_highest;
  }

  /* The first data item contains the privilege requirement of the module. */
  if ((data = elf_getdata (scn, data)) == NULL) {
    if (verbose >= 1) {
      err ("Error getting data from section %s in module %s\n", STAP_PRIVILEGE_SECTION,
	   module_path);
      err ("Assuming required privilege level of %s.", pr_name (pr_highest));
    }
    return pr_highest;
  }

  /* Make sure the data is the correct size. */
  if (data->d_size != sizeof (privilege)) {
    if (verbose >= 1) {
      err ("Data in section %s is in module %s not the correct size\n", STAP_PRIVILEGE_SECTION,
	   module_path);
      err ("Assuming required privilege level of %s.", pr_name (pr_highest));
    }
    return pr_highest;
  }

  /* Obtain the data. It should already be in host byte order. */
  privilege = *(privilege_t*)data->d_buf;

  /* Make sure that the data is coherent. */
  switch (privilege) {
  case pr_stapusr:
  case pr_stapsys:
  case pr_stapdev:
    break; /* ok */
  default:
    if (verbose >= 1) {
      err ("Unknown privilege data, 0x%x in section %s in module %s\n",
	   (int)privilege, STAP_PRIVILEGE_SECTION, module_path);
      err ("Assuming required privilege level of %s.", pr_name (pr_highest));
    }
    return pr_highest;
  }

  /* ALl is ok. Return the extracted privilege data. */
  return privilege;
#endif /* HAVE_ELF_GETSHDRSTRNDX */
}

/*
 * Check the user's group membership.
 *
 * o root and members of stapdev can do anything
 * o members of stapsys and stapusr can load signed modules compiled for their privilege levels
 * o all users can load modules from certain module-specific paths
 *
 * Returns: 
 *          -2 user has no privilege credentials
 *           0 on failure
 *           1 on success
 */
static int
check_groups (
  const char *module_path,
  int module_fd,
  int module_signature_status,
  check_module_path_func check_path,
  const void *module_data,
  off_t module_size
)
{
  privilege_t user_credentials, module_required_credentials;

  /* Lookup the user's privilege credentials. */
  user_credentials = get_privilege_credentials ();

  /* Users with stapdev credentials (includes root) can do anything. */
  if (pr_contains (user_credentials, pr_stapdev))
    return 1;

  /* Users with stapsys and stapusr credentials may be able to load a signed module. */
  if (module_signature_status == MODULE_OK) {
    /* We must verify that the module was compiled for that privilege level. */
    module_required_credentials = get_module_required_credentials (module_path, module_data, module_size);
    if (pr_contains (user_credentials, module_required_credentials)) {
      /* Our credentials are sufficient */
      return 1;
    }

    /* Our credentials are insufficient to load this module. */
    err("ERROR: Your privilege credentials (%s) are insufficient to load the module %s (%s required)\n",
	pr_name (user_credentials), module_path, pr_name (module_required_credentials));

    if (user_credentials == pr_none)
      return -2;

    return 0;
  }

  /* Not fatal. The module could still be on a blessed path. */
  assert (module_signature_status == MODULE_UNTRUSTED ||
	  module_signature_status == MODULE_CHECK_ERROR);
  err("Unable to verify the signature for the module %s.\n", module_path);

  /* Check whether this module can be loaded based on its path. check_path is a pointer to a
     module-specific function which will do this.  */
  return check_path (module_path, module_fd);
}

/*
 * Check the user's permissions.  Is he allowed to run staprun, or is
 * he limited to "blessed" modules?
 *
 * There are several levels of possible permission:
 *
 * 1) root can do anything
 * 2) members of stapdev can do anything
 * 3) members of stapsys and stapusr can load signed modules compiled for their privilege levels
 * 4) all users can load unsigned modules from /lib/modules/KVER/systemtap
 *
 * It is only an error if all 4 levels of checking fail
 */
void assert_stap_module_permissions(
  const char *module_path,
  int module_fd,
  const void *module_data,
  off_t module_size
) {
	int check_groups_rc;
	int check_signature_rc;

	/* Attempt to verify the module against its signature. Exit
	   immediately if the module has been tampered with (altered).  */
	check_signature_rc = check_signature (module_path, module_data, module_size);
	if (check_signature_rc == MODULE_ALTERED)
		exit(-1);

	/* If we're root, we can do anything. */
	if (getuid() == 0) {
		/* ... like overriding the real UID */
		const char *env_id = getenv("SYSTEMTAP_REAL_UID");
		if (env_id && setreuid(atoi(env_id), -1))
			err("WARNING: couldn't set staprun UID to '%s': %s",
					env_id, strerror(errno));

		/* ... or overriding the real GID */
		env_id = getenv("SYSTEMTAP_REAL_GID");
		if (env_id && setregid(atoi(env_id), -1))
			err("WARNING: couldn't set staprun GID to '%s': %s",
					env_id, strerror(errno));

		return;
	}

	/* Check permissions for group membership.  */
	check_groups_rc = check_groups (module_path, module_fd, check_signature_rc,
					check_stap_module_path, module_data, module_size);
	if (check_groups_rc == 1)
		return;

	/* Are we are an ordinary user?.  */
	if (check_groups_rc == -2) {
		err("ERROR: You are trying to run systemtap as a normal user.\n"
		    "You should either be root, or be part of "
		    "group \"stapusr\" and possibly the groups \"stapsys\" or \"stapdev\".\n");
	}

	exit(-1);
}

/*
 * Check the user's permissions.  Is he allowed to load the uprobes module?
 *
 * There are several levels of possible permission:
 *
 * 1) root can do anything
 * 2) members of stapdev can do anything
 * 3) members of stapusr can load a uprobes module which has been signed by a
 *    trusted signer
 *
 * It is only an error if all 3 levels of checking fail
 */
void assert_uprobes_module_permissions(
  const char *module_path,
  int module_fd,
  const void *module_data,
  off_t module_size
) {
  int check_groups_rc;
  int check_signature_rc;
  
	/* Attempt to verify the module against its signature. Return failure
	   if the module has been tampered with (altered).  */
	check_signature_rc = check_signature (module_path, module_data, module_size);
	if (check_signature_rc == MODULE_ALTERED)
		exit(-1);

	/* root can still load this module.  */
	if (getuid() == 0)
		return;

	/* Members of the groups stapdev and stapusr can still load this module. */
	check_groups_rc = check_groups (module_path, module_fd, check_signature_rc,
					check_uprobes_module_path, module_data, module_size);
	if (check_groups_rc == 1)
		return;

	/* Check permissions for group membership.  */
	if (check_groups_rc == -2) {
		err("ERROR: You are trying to load the module %s as a normal user.\n"
		    "You should either be root, or be part of "
		    "group \"stapusr\" and possibly the groups \"stapsys\" or \"stapdev\".\n",
		    module_path);
	}

	exit(-1);
}
