// Setup routines for creating fully populated DWFLs. Used in pass 2 and 3.
// Copyright (C) 2009 Red Hat, Inc.
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

#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <string>

extern "C" {
#include <fnmatch.h>
#include <stdlib.h>
}

// XXX: also consider adding $HOME/.debug/ for perf build-id-cache
static const char *debuginfo_path_arr = "+:.debug:/usr/lib/debug:/var/cache/abrt-di/usr/lib/debug:build";
static const char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
static const char *debuginfo_path = (debuginfo_env_arr ?: debuginfo_path_arr);

// NB: kernel_build_tree doesn't enter into this, as it's for
// kernel-side modules only.
// XXX: also consider adding $HOME/.debug/ for perf build-id-cache
static const char *debuginfo_usr_path_arr = "+:.debug:/usr/lib/debug:/var/cache/abrt-di/usr/lib/debug";
static const char *debuginfo_usr_path = (debuginfo_env_arr
					 ?: debuginfo_usr_path_arr);

static const Dwfl_Callbacks kernel_callbacks =
  {
    dwfl_linux_kernel_find_elf,
    dwfl_standard_find_debuginfo,
    dwfl_offline_section_address,
    (char **) & debuginfo_path
  };

static const Dwfl_Callbacks user_callbacks =
  {
    NULL,
    dwfl_standard_find_debuginfo,
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
      modulesdep = "/lib/modules/";
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
  if (pending_interrupts || setup_dwfl_done)
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
  if (s.kernel_build_tree == string("/lib/modules/"
				    + s.kernel_release
				    + "/build"))
    elfutils_kernel_path = s.kernel_release;
  else
    elfutils_kernel_path = s.kernel_build_tree;

  offline_modules_found = 0;

  // First try to report full path modules.
  set<string>::iterator it = offline_search_names.begin();
  while (it != offline_search_names.end())
    {
      if ((*it)[0] == '/')
	{
	  const char *cname = (*it).c_str();
	  Dwfl_Module *mod = dwfl_report_offline (dwfl, cname, cname, -1);
	  if (mod)
	    offline_modules_found++;
	}
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
		bool all_needed)
{
  // See if we have this dwfl already cached
  set<string> modset(begin, end);
  if (user_dwfl != NULL && modset == user_modset)
    return user_dwfl;

  user_modset = modset;

  Dwfl *dwfl = dwfl_begin (&user_callbacks);
  dwfl_assert("dwfl_begin", dwfl);
  dwfl_report_begin (dwfl);

  // XXX: should support buildid-based naming
  while (begin != end && dwfl != NULL)
    {
      const char *cname = (*begin).c_str();
      Dwfl_Module *mod = dwfl_report_offline (dwfl, cname, cname, -1);
      if (! mod && all_needed)
	{
	  dwfl_end(dwfl);
	  dwfl = NULL;
	}
      begin++;
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
