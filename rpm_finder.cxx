// systemtap debuginfo rpm finder
// Copyright (C) 2009-2011 Red Hat Inc.
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

#if ! HAVE_LIBRPMIO && HAVE_NSS
extern "C" {
#include <nss.h>
}
#include "nsscommon.h"
#endif

/* Returns the count of newly added rpms.  */
/* based on the code in F11 gdb-6.8.50.20090302 source rpm */
/* Added in the rpm_type parameter to specify what rpm to look for */

static int
missing_rpm_enlist (systemtap_session& sess, const char *filename, const char *rpm_type)
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
	  cerr << _("Error reading the rpm configuration files") << endl;
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
	  char *rpminfo, *s, *s2;
	  char header[31] = {};
	  const char* arch = ".%{arch}";
          sprintf(header, "%%{sourcerpm}%s%s", rpm_type, arch);
	  errmsg_t err;
	  size_t rpminfolen = strlen(rpm_type);
	  size_t srcrpmlen = sizeof (".src.rpm") - 1;
	  rpmdbMatchIterator mi_rpminfo;
	  h = rpmdbNextIterator(mi);
	  if (h == NULL)
	    break;
	  /* Verify the kernel file is not already installed.  */

	  rpminfo = headerSprintf(h, header,
			      rpmTagTable, rpmHeaderFormats, &err);

	  if (!rpminfo)
	    {
	      cerr << _("Error querying the rpm file `") << filename << "': "
		   << err << endl;
	      continue;
	    }
	  /* s = `.src.rpm-debuginfo.%{arch}' */
	  s = strrchr (rpminfo, '-') - srcrpmlen;
	  s2 = NULL;
	  if (s > rpminfo && memcmp (s, ".src.rpm", srcrpmlen) == 0)
	    {
	      /* s2 = `-%{release}.src.rpm-debuginfo.%{arch}' */
	      s2 = (char *) memrchr (rpminfo, '-', s - rpminfo);
	    }
	  if (s2)
	    {
	      /* s2 = `-%{version}-%{release}.src.rpm-debuginfo.%{arch}' */
	      s2 = (char *) memrchr (rpminfo, '-', s2 - rpminfo);
	    }
	  if (!s2)
	    {
	      cerr << _("Error querying the rpm file `") << filename 
		   << "': " << rpminfo << endl;
	      xfree (rpminfo);
	      continue;
	    }
	  /* s = `.src.rpm-debuginfo.%{arch}' */
	  /* s2 = `-%{version}-%{release}.src.rpm-debuginfo.%{arch}' */
	  memmove (s2 + rpminfolen, s2, s - s2);
	  memcpy (s2, rpm_type, rpminfolen);
	  /* s = `XXXX.%{arch}' */
	  /* strlen ("XXXX") == srcrpmlen + debuginfolen */
	  /* s2 = `-debuginfo-%{version}-%{release}XX.%{arch}' */
	  /* strlen ("XX") == srcrpmlen */
	  memmove (s + rpminfolen, s + srcrpmlen + rpminfolen,
		   strlen (s + srcrpmlen + rpminfolen) + 1);
	  /* s = `-debuginfo-%{version}-%{release}.%{arch}' */

	  /* RPMDBI_PACKAGES requires keylen == sizeof (int).  */
	  /* RPMDBI_LABEL is an interface for NVR-based dbiFindByLabel().  */
	  mi_rpminfo = rpmtsInitIterator(ts, (rpmTag)  RPMDBI_LABEL,
					      rpminfo, 0);
	  if (mi_rpminfo)
	    {
	      rpmdbFreeIterator(mi_rpminfo);
	      count = 0;
	      break;
	    }
	  /* The allocated memory gets utilized below for MISSING_RPM_HASH.  */
          if(strcmp(rpm_type,"-debuginfo")==0){
	    xfree(rpminfo);
	    rpminfo = headerSprintf(h,
		      "%{name}-%{version}-%{release}.%{arch}",
		      rpmTagTable, rpmHeaderFormats, &err);
	  }
	  if (!rpminfo)
	    {
	      cerr << _("Error querying the rpm file `") << filename 
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
	  sess.rpms_to_install.insert(rpminfo);
 	}
      count++;
      rpmdbFreeIterator(mi);
    }

  rpmtsFree(ts);

#if HAVE_NSS
  // librpm uses NSS cryptography but doesn't shut down NSS when it is done.
  // If NSS is available, it will be used by the compile server client on
  // specific certificate databases and thus, it must be shut down first.
  // Get librpm to do it if we can. Otherwise do it ourselves.
#if HAVE_LIBRPMIO
  rpmFreeCrypto (); // Shuts down NSS within librpm
#else
  nssCleanup (NULL); // Shut down NSS ourselves
#endif
#endif

  return count;
}
#endif	/* HAVE_LIBRPM */

void
missing_rpm_list_print (systemtap_session &sess, const char* rpm_type)
{
#ifdef HAVE_LIBRPM
  if (sess.rpms_to_install.size() > 0 && ! sess.suppress_warnings) {

    if(strcmp(rpm_type,"-devel")==0)
	cerr << _("Incorrect version or missing kernel-devel package, use: yum install ");

    else if(strcmp(rpm_type,"-debuginfo")==0)
	cerr << _("Missing separate debuginfos, use: debuginfo-install ");

    else{
        cerr << _("Incorrect parameter passed, please report this error.") << endl;
	_exit(1);
	}

    for (set<std::string>::iterator it=sess.rpms_to_install.begin();
	 it !=sess.rpms_to_install.end(); it++)
    {
      cerr <<  *it << " ";
    }
    cerr << endl;
  }
#endif
}

int
find_debug_rpms (systemtap_session &sess, const char * filename)
{
#ifdef HAVE_LIBRPM
  const char *rpm_type = "-debuginfo";
  return missing_rpm_enlist(sess, filename, rpm_type);
#else
  return 0;
#endif
}

int find_devel_rpms(systemtap_session &sess, const char * filename)
{
#ifdef HAVE_LIBRPM
  const char *rpm_type = "-devel";
  return missing_rpm_enlist(sess, filename, rpm_type);
#else
  return 0;
#endif
}
