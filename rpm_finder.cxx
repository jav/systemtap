// systemtap debuginfo rpm finder
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "session.h"
#include "rpm_finder.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstdlib>

using namespace std;

#ifdef HAVE_LIBRPM

extern "C" {

#define _RPM_4_4_COMPAT
#include <string.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>
#include <rpm/header.h>

#ifndef xfree
#define xfree free
#endif

}

/* Returns the count of newly added rpms.  */
/* based on the code in F11 gdb-6.8.50.20090302 source rpm */

static int
missing_rpm_enlist (systemtap_session& sess, const char *filename)
{
  static int rpm_init_done = 0;
  rpmts ts;
  rpmdbMatchIterator mi;
  int count = 0;

  if (filename == NULL)
    return 0;

  if (!rpm_init_done)
    {
      static int init_tried;

      /* Already failed the initialization before?  */
      if (init_tried)
      	return 0;
      init_tried = 1;

      if (rpmReadConfigFiles(NULL, NULL) != 0)
	{
	  cerr << "Error reading the rpm configuration files" << endl;
	  return 0;
	}

      rpm_init_done = 1;
    }

  ts = rpmtsCreate();

  mi = rpmtsInitIterator(ts, RPMTAG_BASENAMES, filename, 0);
  if (mi != NULL)
    {
      for (;;)
	{
	  Header h;
	  char *debuginfo, *s, *s2;
	  errmsg_t err;
	  size_t srcrpmlen = sizeof (".src.rpm") - 1;
	  size_t debuginfolen = sizeof ("-debuginfo") - 1;
	  rpmdbMatchIterator mi_debuginfo;

	  h = rpmdbNextIterator(mi);
	  if (h == NULL)
	    break;

	  /* Verify the debuginfo file is not already installed.  */

	  debuginfo = headerSprintf(h, "%{sourcerpm}-debuginfo.%{arch}",
				      rpmTagTable, rpmHeaderFormats, &err);

	  if (!debuginfo)
	    {
	      cerr << "Error querying the rpm file `" << filename << "': "
		   << err << endl;
	      continue;
	    }
	  /* s = `.src.rpm-debuginfo.%{arch}' */
	  s = strrchr (debuginfo, '-') - srcrpmlen;
	  s2 = NULL;
	  if (s > debuginfo && memcmp (s, ".src.rpm", srcrpmlen) == 0)
	    {
	      /* s2 = `-%{release}.src.rpm-debuginfo.%{arch}' */
	      s2 = (char *) memrchr (debuginfo, '-', s - debuginfo);
	    }
	  if (s2)
	    {
	      /* s2 = `-%{version}-%{release}.src.rpm-debuginfo.%{arch}' */
	      s2 = (char *) memrchr (debuginfo, '-', s2 - debuginfo);
	    }
	  if (!s2)
	    {
	      cerr << "Error querying the rpm file `" << filename 
		   << "': " << debuginfo << endl;
	      xfree (debuginfo);
	      continue;
	    }
	  /* s = `.src.rpm-debuginfo.%{arch}' */
	  /* s2 = `-%{version}-%{release}.src.rpm-debuginfo.%{arch}' */
	  memmove (s2 + debuginfolen, s2, s - s2);
	  memcpy (s2, "-debuginfo", debuginfolen);
	  /* s = `XXXX.%{arch}' */
	  /* strlen ("XXXX") == srcrpmlen + debuginfolen */
	  /* s2 = `-debuginfo-%{version}-%{release}XX.%{arch}' */
	  /* strlen ("XX") == srcrpmlen */
	  memmove (s + debuginfolen, s + srcrpmlen + debuginfolen,
		   strlen (s + srcrpmlen + debuginfolen) + 1);
	  /* s = `-debuginfo-%{version}-%{release}.%{arch}' */

	  /* RPMDBI_PACKAGES requires keylen == sizeof (int).  */
	  /* RPMDBI_LABEL is an interface for NVR-based dbiFindByLabel().  */
	  mi_debuginfo = rpmtsInitIterator(ts, (rpmTag)  RPMDBI_LABEL,
					      debuginfo, 0);
	  xfree (debuginfo);
	  if (mi_debuginfo)
	    {
	      rpmdbFreeIterator(mi_debuginfo);
	      count = 0;
	      break;
	    }

	  /* The allocated memory gets utilized below for MISSING_RPM_HASH.  */
	  debuginfo = headerSprintf(h,
				      "%{name}-%{version}-%{release}.%{arch}",
				      rpmTagTable, rpmHeaderFormats, &err);
	  if (!debuginfo)
	    {
	      cerr << "Error querying the rpm file `" << filename 
		   << "': " << err << endl;
	      continue;
	    }

	  /* Base package name for `debuginfo-install'.  We do not use the
	     `yum' command directly as the line
		 yum --enablerepo='*-debuginfo' install NAME-debuginfo.ARCH
	     would be more complicated than just:
		 debuginfo-install NAME-VERSION-RELEASE.ARCH
	     Do not supply the rpm base name (derived from .src.rpm name) as
	     debuginfo-install is unable to install the debuginfo package if
	     the base name PKG binary rpm is not installed while for example
	     PKG-libs would be installed (RH Bug 467901).
	     FUTURE: After multiple debuginfo versions simultaneously installed
	     get supported the support for the VERSION-RELEASE tags handling
	     may need an update.  */

	  sess.rpms_to_install.insert(debuginfo);
	  count++;
	}

      rpmdbFreeIterator(mi);
    }

  rpmtsFree(ts);

  return count;
}

#endif	/* HAVE_LIBRPM */

void
missing_rpm_list_print (systemtap_session &sess)
{
#ifdef HAVE_LIBRPM
  if (sess.rpms_to_install.size() > 0) {
    cerr << "Missing separate debuginfos, use: debuginfo-install ";
    for (set<std::string>::iterator it=sess.rpms_to_install.begin();
	 it !=sess.rpms_to_install.end(); it++)
      cerr <<  *it << " ";
    cerr << endl;
  }
#endif
}

int
find_debug_rpms (systemtap_session &sess, const char * filename)
{
#ifdef HAVE_LIBRPM
  return missing_rpm_enlist (sess, filename);
#else
  return 0;
#endif
}
