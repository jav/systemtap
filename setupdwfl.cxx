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

#include <set>
#include <string>

extern "C" {
#include <fnmatch.h>
#include <stdlib.h>
}

static const char *debuginfo_path_arr = "+:.debug:/usr/lib/debug:build";
static const char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
static const char *debuginfo_path = (debuginfo_env_arr ?: debuginfo_path_arr);

// NB: kernel_build_tree doesn't enter into this, as it's for
// kernel-side modules only.
static const char *debuginfo_usr_path_arr = "+:.debug:/usr/lib/debug";
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

// Kept for user_dwfl cache, user modules don't allow wildcards, so
// just keep the set of module strings.
static set<string> user_modset;

// Set up our offline search for kernel modules.  We don't want the
// offline search iteration to do a complete search of the kernel
// build tree, since that's wasteful, so create a predicate that
// filters and stops reporting as soon as we got everything.
static int
setup_dwfl_report_kernel_p(const char* modname, const char* filename)
{
  if (pending_interrupts)
    return -1;

  // elfutils sends us NULL filenames sometimes if it can't find dwarf
  if (filename == NULL)
    return 0;

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
	{ /* non-wildcard mode */
	  if (offline_modules_found)
	    return -1; // Done, only one name needed and found it.
	  
	  /* Reject mismatching module names */
	  if (strcmp(modname, offline_search_modname))
	    return 0;
	  else
	    {
	      offline_modules_found++;
	      return 1;
	    }
	}
    }
  else
    { /* find all in set mode */
      if (offline_search_names.size() == offline_modules_found)
	return -1;

      /* Reject mismatching module names */
      if (offline_search_names.find(modname) == offline_search_names.end())
	return 0;
      else
	{
	  offline_modules_found++;
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
  string elfutils_kernel_path;
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

bool is_user_module(const std::string &m)
{
  return m[0] == '/' && m.rfind(".ko", m.length() - 1) != m.length() - 3;
}
