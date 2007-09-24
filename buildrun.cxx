// build/run probes
// Copyright (C) 2005-2007 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "buildrun.h"
#include "session.h"
#include "util.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

extern "C" {
#include "signal.h"
#include <sys/wait.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
}


using namespace std;


int
compile_pass (systemtap_session& s)
{
  // fill in a quick Makefile
  string makefile_nm = s.tmpdir + "/Makefile";
  ofstream o (makefile_nm.c_str());
  int rc = 0;

  // Create makefile

  // Clever hacks copied from vmware modules
  o << "stap_check_gcc = $(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo \"$(1)\"; else echo \"$(2)\"; fi)" << endl;
  o << "stap_check_build = $(shell " << "set -x; " << " if $(CC) $(CPPFLAGS) $(CFLAGS_KERNEL) $(EXTRA_CFLAGS) $(CFLAGS) -DKBUILD_BASENAME=\\\"" << s.module_name << "\\\" -Werror -S -o /dev/null -xc $(1) > /dev/null ; then echo \"$(2)\"; else echo \"$(3)\"; fi)" << endl;


  o << "SYSTEMTAP_RUNTIME = \"" << s.runtime_path << "\"" << endl;

  // "autoconf" options go here

  // enum hrtimer_mode renaming near 2.6.21; see tapsets.cxx hrtimer_derived_probe_group::emit_module_decls
  string module_cflags = "CFLAGS_" + s.module_name + ".o";
  o << module_cflags << " :=" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-hrtimer-rel.c, -DSTAPCONF_HRTIMER_REL,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-inode-private.c, -DSTAPCONF_INODE_PRIVATE,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-constant-tsc.c, -DSTAPCONF_CONSTANT_TSC,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-tsc-khz.c, -DSTAPCONF_TSC_KHZ,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-ktime-get-real.c, -DSTAPCONF_KTIME_GET_REAL,)" << endl;

  for (unsigned i=0; i<s.macros.size(); i++)
    o << "CFLAGS += -D " << lex_cast_qstring(s.macros[i]) << endl;

  if (s.verbose > 2)
    o << "CFLAGS += -ftime-report -Q" << endl;

  // XXX: unfortunately, -save-temps can't work since linux kbuild cwd
  // is not writeable.
  //
  // if (s.keep_tmpdir)
  // o << "CFLAGS += -fverbose-asm -save-temps" << endl;

  o << "CFLAGS += -freorder-blocks" << endl; // improve on -Os

  // o << "CFLAGS += -fno-unit-at-a-time" << endl;
    
  // Assumes linux 2.6 kbuild
  o << "CFLAGS += -Wno-unused -Werror" << endl;
  o << "CFLAGS += -I\"" << s.runtime_path << "\"" << endl;
  // XXX: this may help ppc toc overflow
  // o << "CFLAGS := $(subst -Os,-O2,$(CFLAGS)) -fminimal-toc" << endl;
  o << "obj-m := " << s.module_name << ".o" << endl;

  o.close ();

  // Generate module directory pathname and make sure it exists.
  string module_dir = string("/lib/modules/")
    + s.kernel_release + "/build";
  struct stat st;
  rc = stat(module_dir.c_str(), &st);
  if (rc != 0)
    {
	clog << "Module directory " << module_dir << " check failed: "
	     << strerror(errno) << endl
	     << "Make sure kernel devel is installed." << endl;
	return rc;
    }  

  // Before running make, fix up the environment a bit.  PATH should
  // already be overridden.  Clean out a few variables that
  // /lib/modules/${KVER}/build/Makefile uses.
  rc = unsetenv("ARCH") || unsetenv("KBUILD_EXTMOD")
      || unsetenv("CROSS_COMPILE") || unsetenv("KBUILD_IMAGE")
      || unsetenv("KCONFIG_CONFIG") || unsetenv("INSTALL_PATH");
  if (rc)
    {
      const char* e = strerror (errno);
      cerr << "unsetenv failed: " << e << endl;
    }

  // Run make
  string make_cmd = string("make")
    + string (" -C \"") + module_dir + string("\"");
  make_cmd += string(" M=\"") + s.tmpdir + string("\" modules");

  if (s.verbose > 1)
    make_cmd += " V=1";
  else
    make_cmd += " -s >/dev/null 2>&1";
  
  if (s.verbose > 1) clog << "Running " << make_cmd << endl;
  rc = system (make_cmd.c_str());
  
  return rc;
}


int
run_pass (systemtap_session& s)
{
  int rc = 0;

  struct passwd *pw = getpwuid(getuid());
  string username = string(pw->pw_name);

  // for now, just spawn staprun
  string staprun_cmd = string(BINDIR) + "/staprun "
    + (s.verbose>1 ? "-v " : "")
    + (s.verbose>2 ? "-v " : "")
    + (s.output_file.empty() ? "" : "-o " + s.output_file + " ");
  
  staprun_cmd += "-d " + stringify(getpid()) + " ";
  
  if (s.cmd != "")
    staprun_cmd += "-c " + cmdstr_quoted(s.cmd) + " ";
  
  if (s.target_pid)
    staprun_cmd += "-t " + stringify(s.target_pid) + " ";
  
  if (s.buffer_size)
    staprun_cmd += "-b " + stringify(s.buffer_size) + " ";
  
  staprun_cmd += s.tmpdir + "/" + s.module_name + ".ko";
  
  if (s.verbose>1) clog << "Running " << staprun_cmd << endl;
  
  signal (SIGHUP, SIG_IGN);
  signal (SIGINT, SIG_IGN);
  rc = system (staprun_cmd.c_str ());

  return rc;
}
