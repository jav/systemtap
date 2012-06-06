// Setup routines for creating fully populated DWFLs. Used in pass 2 and 3.
// Copyright (C) 2009-2011 Red Hat, Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "setupdwfl.h"

#include "dwarf_wrappers.h"
#include "dwflpp.h"
#include "session.h"
#include "util.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <string>

extern "C" {
#include <fnmatch.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/utsname.h>
#include <unistd.h>
}

// XXX: also consider adding $HOME/.debug/ for perf build-id-cache
static const char *debuginfo_path_arr = "+:.debug:/usr/lib/debug:/var/cache/abrt-di/usr/lib/debug:build";
static const char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
static char *debuginfo_path = (char *)(debuginfo_env_arr ?: debuginfo_path_arr);

// NB: kernel_build_tree doesn't enter into this, as it's for
// kernel-side modules only.
// XXX: also consider adding $HOME/.debug/ for perf build-id-cache
static const char *debuginfo_usr_path_arr = "+:.debug:/usr/lib/debug:/var/cache/abrt-di/usr/lib/debug";
static char *debuginfo_usr_path = (char *)(debuginfo_env_arr
					   ?: debuginfo_usr_path_arr);

// A pointer to the current systemtap session for use only by a few
// dwfl calls. DO NOT rely on this, as it is cleared after use.
// This is a kludge.
static systemtap_session* current_session_for_find_debuginfo;

static const Dwfl_Callbacks kernel_callbacks =
  {
    dwfl_linux_kernel_find_elf,
    internal_find_debuginfo,
    dwfl_offline_section_address,
    (char **) & debuginfo_path
  };

static const Dwfl_Callbacks user_callbacks =
  {
    NULL,
    internal_find_debuginfo,
    NULL, /* ET_REL not supported for user space, only ET_EXEC and ET_DYN.
	     dwfl_offline_section_address, */
    (char **) & debuginfo_usr_path
  };

using namespace std;

// Store last kernel and user Dwfl for reuse since they are often
// re-requested (in phase 2 and then in phase 3).
static DwflPtr kernel_dwfl;
static DwflPtr user_dwfl;

// Setup in setup_dwfl_kernel(), for use in setup_dwfl_report_kernel_p().
// Either offline_search_modname or offline_search_names is
// used. When offline_search_modname is not NULL then
// offline_search_names is ignored.
static const char *offline_search_modname;
static set<string> offline_search_names;
static unsigned offline_modules_found;

// Whether or not we are done reporting kernel modules in
// set_dwfl_report_kernel_p().
static bool setup_dwfl_done;

// Kept for user_dwfl cache, user modules don't allow wildcards, so
// just keep the set of module strings.
static set<string> user_modset;

// Determines whether or not we will make setup_dwfl_report_kernel_p
// report true for all module dependencies. This is necessary for
// correctly resolving some dwarf constructs that relocate against
// symbols in vmlinux and/or other modules they depend on. See PR10678.
static const bool setup_all_deps = true;

// Where to find the kernel (and the Modules.dep file).  Setup in
// setup_dwfl_kernel(), used by dwfl_linux_kernel_report_offline() and
// setup_mod_deps().
static string elfutils_kernel_path;

static bool is_comma_dash(const char c) { return (c == ',' || c == '-'); }

// The path to the abrt-action-install-debuginfo-to-abrt-cache program.
static const string abrt_path =
                    (access ("/usr/bin/abrt-action-install-debuginfo-to-abrt-cache", X_OK) == 0
                      ? "/usr/bin/abrt-action-install-debuginfo-to-abrt-cache"
                    : (access ("/usr/libexec/abrt-action-install-debuginfo-to-abrt-cache", X_OK) == 0
                      ? "/usr/libexec/abrt-action-install-debuginfo-to-abrt-cache"
                    : ""));

// The module name is the basename (without the extension) of the
// module path, with ',' and '-' replaced by '_'.
static string
modname_from_path(const string &path)
{
  size_t dot = path.rfind('.');
  size_t slash = path.rfind('/');
  if (dot == string::npos || slash == string::npos || dot < slash)
    return "";
  string name = path.substr(slash + 1, dot - slash - 1);
  replace_if(name.begin(), name.end(), is_comma_dash, '_');
  return name;
}

// Try to parse modules.dep file,
// Simple format: module path (either full or relative), colon,
// (possibly empty) space delimited list of module (path)
// dependencies.
static void
setup_mod_deps()
{
  string modulesdep;
  ifstream in;
  string l;

  if (elfutils_kernel_path[0] == '/')
    {
      modulesdep = elfutils_kernel_path;
      modulesdep += "/modules.dep";
    }
  else
    {
      string sysroot = "";
      if (current_session_for_find_debuginfo)
        sysroot = current_session_for_find_debuginfo->sysroot;
      modulesdep = sysroot + "/lib/modules/";
      modulesdep += elfutils_kernel_path;
      modulesdep += "/modules.dep";
    }
  in.open(modulesdep.c_str());
  if (in.fail ())
    return;

  while (getline (in, l))
    {
      size_t off = l.find (':');
      if (off != string::npos)
	{
	  string modpath, modname;
	  modpath = l.substr (0, off);
	  modname = modname_from_path (modpath);
	  if (modname == "")
	    continue;

	  bool dep_needed;
	  if (offline_search_modname != NULL)
	    {
	      if (dwflpp::name_has_wildcard (offline_search_modname))
		{
		  dep_needed = !fnmatch (offline_search_modname,
					 modname.c_str (), 0);
		  if (dep_needed)
		    offline_search_names.insert (modname);
		}
	      else
		{
		  dep_needed = ! strcmp(modname.c_str (),
					offline_search_modname);
		  if (dep_needed)
		    offline_search_names.insert (modname);
		}
	    }
	  else
	    dep_needed = (offline_search_names.find (modname)
			  != offline_search_names.end ());

	  if (! dep_needed)
	    continue;

	  string depstring = l.substr (off + 1);
	  if (depstring.size () > 0)
	    {
	      stringstream ss (depstring);
	      string deppath;
	      while (ss >> deppath)
		offline_search_names.insert (modname_from_path(deppath));

	    }
	}
    }

  // We always want kernel (needed in list so size checks match).
  // Everything needed now stored in offline_search_names.
  offline_search_names.insert ("kernel");
  offline_search_modname = NULL;
}

// Set up our offline search for kernel modules.  We don't want the
// offline search iteration to do a complete search of the kernel
// build tree, since that's wasteful, so create a predicate that
// filters and stops reporting as soon as we got everything.
static int
setup_dwfl_report_kernel_p(const char* modname, const char* filename)
{
  assert_no_interrupts();
  if (setup_dwfl_done)
    return -1;

  // elfutils sends us NULL filenames sometimes if it can't find dwarf
  if (filename == NULL)
    return 0;

  // Check kernel first since it is often the only thing needed,
  // then we never have to parse and setup the module deps map.
  // It will be reported as the very first thing.
  if (setup_all_deps && ! strcmp (modname, "kernel"))
    {
      if ((offline_search_modname != NULL
	   && ! strcmp (offline_search_modname, "kernel"))
	  || (offline_search_names.size() == 1
	      && *offline_search_names.begin() == "kernel"))
	setup_dwfl_done = true;
      else
	setup_mod_deps();

      offline_modules_found++;
      return 1;
    }

  // If offline_search_modname is setup use it (either as regexp or
  // explicit module/kernel name) and ignore offline_search_names.
  // Otherwise use offline_search_names exclusively.
  if (offline_search_modname != NULL)
    {
      if (dwflpp::name_has_wildcard (offline_search_modname))
	{
	  int match_p = !fnmatch(offline_search_modname, modname, 0);
	  // In the wildcard case, we don't short-circuit (return -1)
	  // analogously to dwflpp::module_name_final_match().
	  if (match_p)
	    offline_modules_found++;
	  return match_p;
	}
      else
	{ /* non-wildcard mode, reject mismatching module names */
	  if (strcmp(modname, offline_search_modname))
	    return 0;
	  else
	    {
	      // Done, only one name needed and found it.
	      offline_modules_found++;
	      setup_dwfl_done = true;
	      return 1;
	    }
	}
    }
  else
    { /* find all in set mode, reject mismatching module names */
      if (offline_search_names.find(modname) == offline_search_names.end())
	return 0;
      else
	{
	  offline_modules_found++;
	  if (offline_search_names.size() == offline_modules_found)
	    setup_dwfl_done = true;
	  return 1;
	}
    }
}

static char * path_insert_sysroot(string sysroot, string path)
{
  char * path_new;
  size_t pos = 1;
  if (path[0] == '/')
    path.replace(0, 1, sysroot);
  while (true) {
    pos = path.find(":/", pos);
    if (pos == string::npos)
      break;
    path.replace(pos, 2, ":" + sysroot);
    ++pos;
  }
  path_new = new char[path.size()+1];
  strcpy (path_new, path.c_str());
  return path_new;
}

void debuginfo_path_insert_sysroot(string sysroot)
{
  debuginfo_path = path_insert_sysroot(sysroot, debuginfo_path);
  debuginfo_usr_path = path_insert_sysroot(sysroot, debuginfo_usr_path);
}

static DwflPtr
setup_dwfl_kernel (unsigned *modules_found, systemtap_session &s)
{
  Dwfl *dwfl = dwfl_begin (&kernel_callbacks);
  dwfl_assert ("dwfl_begin", dwfl);
  dwfl_report_begin (dwfl);

  // We have a problem with -r REVISION vs -r BUILDDIR here.  If
  // we're running against a fedora/rhel style kernel-debuginfo
  // tree, s.kernel_build_tree is not the place where the unstripped
  // vmlinux will be installed.  Rather, it's over yonder at
  // /usr/lib/debug/lib/modules/$REVISION/.  It seems that there is
  // no way to set the dwfl_callback.debuginfo_path and always
  // passs the plain kernel_release here.  So instead we have to
  // hard-code this magic here.
  if (s.kernel_build_tree == string(s.sysroot + "/lib/modules/"
				    + s.kernel_release
				    + "/build"))
    elfutils_kernel_path = s.kernel_release;
  else
    elfutils_kernel_path = s.kernel_build_tree;

  offline_modules_found = 0;

  // First try to report full path modules.
  set<string>::iterator it = offline_search_names.begin();
  int kernel = 0;
  while (it != offline_search_names.end())
    {
      if ((*it)[0] == '/')
        {
          const char *cname = (*it).c_str();
          Dwfl_Module *mod = dwfl_report_offline (dwfl, cname, cname, -1);
          if (mod)
            offline_modules_found++;
        }
      else if ((*it) == "kernel")
        kernel = 1;
      it++;
    }

    // We always need this, even when offline_search_modname is NULL
    // and offline_search_names is empty because we still might want
    // the kernel vmlinux reported.
  setup_dwfl_done = false;
  int rc = dwfl_linux_kernel_report_offline (dwfl,
                                             elfutils_kernel_path.c_str(),
					     &setup_dwfl_report_kernel_p);

  (void) rc; /* Ignore since the predicate probably returned -1 at some point,
                And libdwfl interprets that as "whole query failed" rather than
                "found it already, stop looking". */

  // NB: the result of an _offline call is the assignment of
  // virtualized addresses to relocatable objects such as
  // modules.  These have to be converted to real addresses at
  // run time.  See the dwarf_derived_probe ctor and its caller.

  // If no modules were found, and we are probing the kernel,
  // attempt to download the kernel debuginfo.
  if(kernel)
    {
      // Get the kernel build ID. We still need to call this even if we
      // already have the kernel debuginfo installed as it adds the
      // build ID to the script hash.
      string hex = get_kernel_build_id(s);
      if (offline_modules_found == 0 && s.download_dbinfo != 0 && !hex.empty())
        {
          rc = download_kernel_debuginfo(s, hex);
          if(rc >= 0)
            return setup_dwfl_kernel (modules_found, s);
        }
    }

  dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
  *modules_found = offline_modules_found;

  StapDwfl *stap_dwfl = new StapDwfl(dwfl);
  kernel_dwfl = DwflPtr(stap_dwfl);

  return kernel_dwfl;
}

DwflPtr
setup_dwfl_kernel(const std::string &name,
		  unsigned *found,
		  systemtap_session &s)
{
  current_session_for_find_debuginfo = &s;
  const char *modname = name.c_str();
  set<string> names; // Default to empty

  /* Support full path kernel modules, these cannot be regular
     expressions, so just put them in the search set. */
  if (name[0] == '/' || ! dwflpp::name_has_wildcard (modname))
    {
      names.insert(name);
      modname = NULL;
    }

  if (kernel_dwfl != NULL
      && offline_search_modname == modname
      && offline_search_names == names)
    {
      *found = offline_modules_found;
      return kernel_dwfl;
    }

  offline_search_modname = modname;
  offline_search_names = names;

  return setup_dwfl_kernel(found, s);
}

DwflPtr
setup_dwfl_kernel(const std::set<std::string> &names,
		  unsigned *found,
		  systemtap_session &s)
{
  if (kernel_dwfl != NULL
      && offline_search_modname == NULL
      && offline_search_names == names)
    {
      *found = offline_modules_found;
      return kernel_dwfl;
    }

  offline_search_modname = NULL;
  offline_search_names = names;
  return setup_dwfl_kernel(found, s);
}

DwflPtr
setup_dwfl_user(const std::string &name)
{
  if (user_dwfl != NULL
      && user_modset.size() == 1
      && (*user_modset.begin()) == name)
    return user_dwfl;

  user_modset.clear();
  user_modset.insert(name);

  Dwfl *dwfl = dwfl_begin (&user_callbacks);
  dwfl_assert("dwfl_begin", dwfl);
  dwfl_report_begin (dwfl);

  // XXX: should support buildid-based naming
  const char *cname = name.c_str();
  Dwfl_Module *mod = dwfl_report_offline (dwfl, cname, cname, -1);
  dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
  if (! mod)
    {
      dwfl_end(dwfl);
      dwfl = NULL;
    }

  StapDwfl *stap_dwfl = new StapDwfl(dwfl);
  user_dwfl = DwflPtr(stap_dwfl);

  return user_dwfl;
}

DwflPtr
setup_dwfl_user(std::vector<std::string>::const_iterator &begin,
		const std::vector<std::string>::const_iterator &end,
		bool all_needed, systemtap_session &s)
{
  current_session_for_find_debuginfo = &s;
  // See if we have this dwfl already cached
  set<string> modset(begin, end);
  if (user_dwfl != NULL && modset == user_modset)
    return user_dwfl;

  user_modset = modset;

  Dwfl *dwfl = dwfl_begin (&user_callbacks);
  dwfl_assert("dwfl_begin", dwfl);
  dwfl_report_begin (dwfl);
  Dwfl_Module *mod = NULL;
  // XXX: should support buildid-based naming
  while (begin != end && dwfl != NULL)
    {
      const char *cname = (*begin).c_str();
      mod = dwfl_report_offline (dwfl, cname, cname, -1);
      if (! mod && all_needed)
	{
	  dwfl_end(dwfl);
	  dwfl = NULL;
	}
      begin++;
    }

  /* Extract the build id and add it to the session variable
   * so it will be added to the script hash */
  if (mod)
    {
      const unsigned char *bits;
      GElf_Addr vaddr;
      if(s.verbose > 2)
        clog << _("Extracting build ID.") << endl;
      int bits_length = dwfl_module_build_id(mod, &bits, &vaddr);

      /* Convert the binary bits to a hex string */
      string hex = hex_dump(bits, bits_length);

      //Store the build ID in the session
      s.build_ids.push_back(hex);
    }

  if (dwfl)
    dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));

  StapDwfl *stap_dwfl = new StapDwfl(dwfl);
  user_dwfl = DwflPtr(stap_dwfl);

  return user_dwfl;
}

bool
is_user_module(const std::string &m)
{
  return m[0] == '/' && m.rfind(".ko", m.length() - 1) != m.length() - 3;
}

int
internal_find_debuginfo (Dwfl_Module *mod,
      void **userdata __attribute__ ((unused)),
      const char *modname __attribute__ ((unused)),
      GElf_Addr base __attribute__ ((unused)),
      const char *file_name,
      const char *debuglink_file,
      GElf_Word debuglink_crc,
      char **debuginfo_file_name)
{

  int bits_length;
  string hex;

  /* To Keep track of whether the abrt successfully installed the debuginfo */
  static int install_dbinfo_failed = 0;

  /* Make sure the current session variable is not null */
  if(current_session_for_find_debuginfo == NULL)
    goto call_dwfl_standard_find_debuginfo;

  /* Check to see if download-debuginfo=0 was set */
  if(!current_session_for_find_debuginfo->download_dbinfo || abrt_path.empty())
    goto call_dwfl_standard_find_debuginfo;

  /* Check that we haven't already run this */
  if (install_dbinfo_failed < 0)
    {
      if(current_session_for_find_debuginfo->verbose > 1)
        current_session_for_find_debuginfo->print_warning(_F("We already tried running '%s'", abrt_path.c_str()));
      goto call_dwfl_standard_find_debuginfo;
    }

  /* Extract the build ID */
  const unsigned char *bits;
  GElf_Addr vaddr;
  if(current_session_for_find_debuginfo->verbose > 2)
    clog << _("Extracting build ID.") << endl;
  bits_length = dwfl_module_build_id(mod, &bits, &vaddr);

  /* Convert the binary bits to a hex string */
  hex = hex_dump(bits, bits_length);

  /* Search for the debuginfo with the build ID */
  if(current_session_for_find_debuginfo->verbose > 2)
    clog << _F("Searching for debuginfo with build ID: '%s'.", hex.c_str()) << endl;
  if (bits_length > 0)
    {
      int fd = dwfl_build_id_find_debuginfo(mod,
             NULL, NULL, 0,
             NULL, NULL, 0,
             debuginfo_file_name);
      if (fd >= 0)
        return fd;
    }

  /* The above failed, so call abrt-action-install-debuginfo-to-abrt-cache
  to download and install the debuginfo */
  if(current_session_for_find_debuginfo->verbose > 1)
    clog << _F("Downloading and installing debuginfo with build ID: '%s' using %s.",
            hex.c_str(), abrt_path.c_str()) << endl;

  struct tms tms_before;
  times (& tms_before);
  struct timeval tv_before;
  struct tms tms_after;
  unsigned _sc_clk_tck;
  struct timeval tv_after;
  gettimeofday (&tv_before, NULL);

  if(execute_abrt_action_install_debuginfo_to_abrt_cache (hex) < 0)
    {
      install_dbinfo_failed = -1;
      current_session_for_find_debuginfo->print_warning(_F("%s failed.", abrt_path.c_str()));
      goto call_dwfl_standard_find_debuginfo;
    }

  _sc_clk_tck = sysconf (_SC_CLK_TCK);
  times (& tms_after);
  gettimeofday (&tv_after, NULL);
  if(current_session_for_find_debuginfo->verbose > 1)
    clog << _("Download completed in ")
              << ((tms_after.tms_cutime + tms_after.tms_utime
              - tms_before.tms_cutime - tms_before.tms_utime) * 1000 / (_sc_clk_tck)) << "usr/"
              << ((tms_after.tms_cstime + tms_after.tms_stime
              - tms_before.tms_cstime - tms_before.tms_stime) * 1000 / (_sc_clk_tck)) << "sys/"
              << ((tv_after.tv_sec - tv_before.tv_sec) * 1000 +
              ((long)tv_after.tv_usec - (long)tv_before.tv_usec) / 1000) << "real ms"<< endl;

  call_dwfl_standard_find_debuginfo:

  /* Call the original dwfl_standard_find_debuginfo */
  return dwfl_standard_find_debuginfo(mod, userdata, modname, base,
              file_name, debuglink_file,
              debuglink_crc, debuginfo_file_name);

}

int
execute_abrt_action_install_debuginfo_to_abrt_cache (string hex)
{
  /* Be sure that abrt exists */
  if (abrt_path.empty())
    return -1;

  int timeout = current_session_for_find_debuginfo->download_dbinfo;;
  vector<string> cmd;
  cmd.push_back ("/bin/sh");
  cmd.push_back ("-c");
  
  /* NOTE: abrt does not currently work with asking for confirmation
   * in version abrt-2.0.3-1.fc15.x86_64, Bugzilla: BZ726192 */
  if(current_session_for_find_debuginfo->download_dbinfo == -1)
    {
      cmd.push_back ("echo " + hex + " | " + abrt_path + " --ids=-");
      timeout = INT_MAX; 
      current_session_for_find_debuginfo->print_warning(_("Due to bug in abrt, it may continue downloading anyway without asking for confirmation."));
    }
  else
    cmd.push_back ("echo " + hex + " | " + abrt_path + " -y --ids=-");
 
  /* NOTE: abrt does not allow canceling the download process at the moment
   * in version abrt-2.0.3-1.fc15.x86_64, Bugzilla: BZ730107 */
  if(timeout != INT_MAX)
    current_session_for_find_debuginfo->print_warning(_("Due to a bug in abrt, it  may continue downloading after stopping stap if download times out."));
  
  int pid;
  if(current_session_for_find_debuginfo->verbose > 1 ||  current_session_for_find_debuginfo->download_dbinfo == -1)
    /* Execute abrt-action-install-debuginfo-to-abrt-cache, 
     * showing output from abrt */
    pid = stap_spawn(current_session_for_find_debuginfo->verbose, cmd, NULL);
  else
    {
      /* Execute abrt-action-install-debuginfo-to-abrt-cache,
       * without showing output from abrt */
      posix_spawn_file_actions_t fa;
      if (posix_spawn_file_actions_init(&fa) != 0)
        return -1;
      if(posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0) != 0)
        {
          posix_spawn_file_actions_destroy(&fa);
          return -1;
        }
      pid = stap_spawn(current_session_for_find_debuginfo->verbose, cmd, &fa);
      posix_spawn_file_actions_destroy(&fa);
    }

  /* Check to see if either the program successfully completed, or if it timed out. */
  int rstatus = 0;
  int timer = 0;
  int rc = 0;
  while(timer < timeout)
    {
      sleep(1); 
      rc = waitpid(pid, &rstatus, WNOHANG);
      if(rc < 0)
        return -1;
      if (rc > 0 && WIFEXITED(rstatus)) 
        break;
      assert_no_interrupts();
      timer++;
    }
  if(timer == timeout)
    {
      /* Timed out! */
      kill(-pid, SIGINT);
      current_session_for_find_debuginfo->print_warning(_("Aborted downloading debuginfo: timed out."));
      return -1;
    }

  /* Successfully finished downloading! */
  #if 0 // Should not print this until BZ733690 is fixed as abrt could fail to download
        // and it would still print success.
  if(current_session_for_find_debuginfo->verbose > 1 || current_session_for_find_debuginfo->download_dbinfo == -1)
     clog << _("Download Completed Successfully!") << endl;
  #endif
  if(current_session_for_find_debuginfo->verbose > 1 || current_session_for_find_debuginfo->download_dbinfo == -1)
    clog << _("ABRT finished attempting to download debuginfo.") << endl;

  return 0;
}

/* Get the kernel build ID */
string
get_kernel_build_id(systemtap_session &s)
{
  bool found = false;
  string hex;

  // Try to find BuildID from vmlinux.id
  string kernel_buildID_path = s.kernel_build_tree + "/vmlinux.id";
  if(s.verbose > 1)
    clog << _F("Attempting to extract kernel debuginfo build ID from %s", kernel_buildID_path.c_str()) << endl;
  ifstream buildIDfile;
  buildIDfile.open(kernel_buildID_path.c_str());
  if(buildIDfile.is_open())
    {
      getline(buildIDfile, hex);
      if(buildIDfile.good())
        {
          found = true;
        }
      buildIDfile.close();
    }

  // Try to find BuildID from the notes file if the above didn't work and we are
  // building a native module
  if(found == false && s.native_build)
    {
      if(s.verbose > 1)
        clog << _("Attempting to extract kernel debuginfo build ID from /sys/kernel/notes") << endl;

      const char *notesfile = "/sys/kernel/notes";
      int fd = open64 (notesfile, O_RDONLY);
      if (fd < 0)
      return "";

      assert (sizeof (Elf32_Nhdr) == sizeof (GElf_Nhdr));
      assert (sizeof (Elf64_Nhdr) == sizeof (GElf_Nhdr));

      union
      {
        GElf_Nhdr nhdr;
        unsigned char data[8192];
      } buf;

      ssize_t n = read (fd, buf.data, sizeof buf);
      close (fd);

      if (n <= 0)
        return "";

      unsigned char *p = buf.data;
      while (p < &buf.data[n])
        {
          /* No translation required since we are reading the native kernel.  */
          GElf_Nhdr *nhdr = (GElf_Nhdr *) p;
          p += sizeof *nhdr;
          unsigned char *name = p;
          p += (nhdr->n_namesz + 3) & -4U;
          unsigned char *bits = p;
          p += (nhdr->n_descsz + 3) & -4U;

          if (p <= &buf.data[n]
              && nhdr->n_type == NT_GNU_BUILD_ID
              && nhdr->n_namesz == sizeof "GNU"
              && !memcmp (name, "GNU", sizeof "GNU"))
            {
              // Found it.
              hex = hex_dump(bits, nhdr->n_descsz);
              found = true;
            }
        }
    }
  if(found)
    {
      return hex;
    }
  else
    return "";
}

/* Find the kernel build ID and attempt to download the matching debuginfo */
int download_kernel_debuginfo (systemtap_session &s, string hex)
{
  // NOTE: At some point we want to base the
  // already_tried_downloading_kernel_debuginfo flag on the build ID rather
  // than just the stap process.

  // Don't try this again if we already did.
  static int already_tried_downloading_kernel_debuginfo = 0;
  if(already_tried_downloading_kernel_debuginfo)
    return -1;

  // Attempt to download the debuginfo
  if(s.verbose > 1)
    clog << _F("Success! Extracted kernel debuginfo build ID: %s", hex.c_str()) << endl;
  int rc = execute_abrt_action_install_debuginfo_to_abrt_cache(hex);
  already_tried_downloading_kernel_debuginfo = 1;
  if (rc < 0)
    return -1;

  // Success!
  return 0;
}
