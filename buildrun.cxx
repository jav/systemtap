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

/* Adjust and run make_cmd to build a kernel module. */
static int
run_make_cmd(systemtap_session& s, string& make_cmd)
{
  // Before running make, fix up the environment a bit.  PATH should
  // already be overridden.  Clean out a few variables that
  // /lib/modules/${KVER}/build/Makefile uses.
  int rc = unsetenv("ARCH") || unsetenv("KBUILD_EXTMOD")
      || unsetenv("CROSS_COMPILE") || unsetenv("KBUILD_IMAGE")
      || unsetenv("KCONFIG_CONFIG") || unsetenv("INSTALL_PATH");
  if (rc)
    {
      const char* e = strerror (errno);
      cerr << "unsetenv failed: " << e << endl;
    }

  if (s.verbose > 1)
    make_cmd += " V=1";
  else
    make_cmd += " -s >/dev/null 2>&1";
  
  if (s.verbose > 1) clog << "Running " << make_cmd << endl;
  rc = system (make_cmd.c_str());
  
  return rc;
}

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
  o << "stap_check_build = $(shell " << "set -x; " << " if $(CC) $(KBUILD_CPPFLAGS) $(CPPFLAGS) $(KBUILD_CFLAGS) $(CFLAGS_KERNEL) $(EXTRA_CFLAGS) $(CFLAGS) -DKBUILD_BASENAME=\\\"" << s.module_name << "\\\" -Werror -S -o /dev/null -xc $(1) > /dev/null ; then echo \"$(2)\"; else echo \"$(3)\"; fi)" << endl;


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
    o << "EXTRA_CFLAGS += -D " << lex_cast_qstring(s.macros[i]) << endl;

  if (s.verbose > 2)
    o << "EXTRA_CFLAGS += -ftime-report -Q" << endl;

  // XXX: unfortunately, -save-temps can't work since linux kbuild cwd
  // is not writeable.
  //
  // if (s.keep_tmpdir)
  // o << "CFLAGS += -fverbose-asm -save-temps" << endl;

  o << "EXTRA_CFLAGS += -freorder-blocks" << endl; // improve on -Os

  // o << "CFLAGS += -fno-unit-at-a-time" << endl;
    
  // Assumes linux 2.6 kbuild
  o << "EXTRA_CFLAGS += -Wno-unused -Werror" << endl;
  o << "EXTRA_CFLAGS += -I\"" << s.runtime_path << "\"" << endl;
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

  // Run make
  string make_cmd = string("make")
    + string (" -C \"") + module_dir + string("\"");
  make_cmd += string(" M=\"") + s.tmpdir + string("\" modules");

  rc = run_make_cmd(s, make_cmd);
  
  return rc;
}


bool
uprobes_enabled (void)
{
  int rc = system ("/bin/grep -q unregister_uprobe /proc/kallsyms");
  return (rc == 0);
}

int
make_uprobes (systemtap_session& s)
{
  string uprobes_home = string(PKGDATADIR "/runtime/uprobes");

  // Quietly skip the build if the Makefile has been removed.
  string makefile = uprobes_home + string("/Makefile");
  struct stat buf;
  if (stat(makefile.c_str(), &buf) != 0)
  	return 2;	// make's exit value for No such file or directory.

  if (s.verbose)
    clog << "Pass 4, overtime: "
	 << "(re)building SystemTap's version of uprobes."
	 << endl;

  string make_cmd = string("make -C ") + uprobes_home;
  int rc = run_make_cmd(s, make_cmd);
  if (rc && s.verbose)
    clog << "Uprobes build failed. "
    	 << "Hope uprobes is available at run time."
	 << endl;

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
  
  if (s.need_uprobes)
    staprun_cmd += "-u ";

  staprun_cmd += s.tmpdir + "/" + s.module_name + ".ko";
  
  if (s.verbose>1) clog << "Running " << staprun_cmd << endl;
  
  signal (SIGHUP, SIG_IGN);
  signal (SIGINT, SIG_IGN);
  rc = system (staprun_cmd.c_str ());

  return rc;
}
