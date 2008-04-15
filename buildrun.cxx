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
#include <signal.h>
#include <sys/wait.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
}


using namespace std;

static int uprobes_pass (systemtap_session& s);

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

  if (s.verbose > 2)
    make_cmd += " V=1";
  else if (s.verbose > 1)
    make_cmd += " >/dev/null";
  else
    make_cmd += " -s >/dev/null 2>&1";
  
  if (s.verbose > 1) clog << "Running " << make_cmd << endl;
  rc = system (make_cmd.c_str());
  
  return rc;
}

int
compile_pass (systemtap_session& s)
{
  int rc = uprobes_pass (s);
  if (rc)
    return rc;

  // fill in a quick Makefile
  string makefile_nm = s.tmpdir + "/Makefile";
  ofstream o (makefile_nm.c_str());

  // Create makefile

  // Clever hacks copied from vmware modules
  string superverbose;
  if (s.verbose > 3)
    superverbose = "set -x;";

  o << "stap_check_gcc = $(shell " << superverbose << " if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo \"$(1)\"; else echo \"$(2)\"; fi)" << endl;
  o << "stap_check_build = $(shell " << superverbose << " if $(CC) $(KBUILD_CPPFLAGS) $(CPPFLAGS) $(KBUILD_CFLAGS) $(CFLAGS_KERNEL) $(EXTRA_CFLAGS) $(CFLAGS) -DKBUILD_BASENAME=\\\"" << s.module_name << "\\\" -Werror -S -o /dev/null -xc $(1) > /dev/null 2>&1 ; then echo \"$(2)\"; else echo \"$(3)\"; fi)" << endl;

  o << "SYSTEMTAP_RUNTIME = \"" << s.runtime_path << "\"" << endl;

  // "autoconf" options go here

  string module_cflags = "EXTRA_CFLAGS";
  o << module_cflags << " :=" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-hrtimer-rel.c, -DSTAPCONF_HRTIMER_REL,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-inode-private.c, -DSTAPCONF_INODE_PRIVATE,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-constant-tsc.c, -DSTAPCONF_CONSTANT_TSC,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-tsc-khz.c, -DSTAPCONF_TSC_KHZ,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-ktime-get-real.c, -DSTAPCONF_KTIME_GET_REAL,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-x86-uniregs.c, -DSTAPCONF_X86_UNIREGS,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-nameidata.c, -DSTAPCONF_NAMEIDATA_CLEANUP,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-unregister-kprobes.c, -DSTAPCONF_UNREGISTER_KPROBES,)" << endl;
  o << module_cflags << " += $(call stap_check_build, $(SYSTEMTAP_RUNTIME)/autoconf-module-nsections.c, -DSTAPCONF_MODULE_NSECTIONS,)" << endl;

  for (unsigned i=0; i<s.macros.size(); i++)
    o << "EXTRA_CFLAGS += -D " << lex_cast_qstring(s.macros[i]) << endl;

  if (s.verbose > 3)
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

static const string uprobes_home = string(PKGDATADIR "/runtime/uprobes");

/*
 * If uprobes was built as part of the kernel build (either built-in
 * or as a module), the uprobes exports should show up in
 * /lib/modules/`uname -r`/build/Module.symvers.  Return true if so.
 */
static bool
kernel_built_uprobes (systemtap_session& s)
{
  string grep_cmd = string ("/bin/grep -q unregister_uprobe /lib/modules/")
    + s.kernel_release + string ("/build/Module.symvers");
  int rc = system (grep_cmd.c_str());
  return (rc == 0);
}

static bool
verify_uprobes_uptodate (systemtap_session& s)
{
  if (s.verbose)
    clog << "Pass 4, preamble: "
	 << "verifying that SystemTap's version of uprobes is up to date."
	 << endl;

  string make_cmd = string("make -q -C ") + uprobes_home
    + string(" uprobes.ko");
  int rc = run_make_cmd(s, make_cmd);
  if (rc) {
    clog << "SystemTap's version of uprobes is out of date." << endl;
    clog << "As root, run \"make\" in " << uprobes_home << "." << endl;
  }

  return rc;
}

static int
make_uprobes (systemtap_session& s)
{
  if (s.verbose)
    clog << "Pass 4, preamble: "
	 << "(re)building SystemTap's version of uprobes."
	 << endl;

  string make_cmd = string("make -C ") + uprobes_home;
  int rc = run_make_cmd(s, make_cmd);
  if (s.verbose) {
    if (rc)
      clog << "Uprobes (re)build failed." << endl;
    else
      clog << "Uprobes (re)build complete." << endl;
  }

  return rc;
}

/*
 * Copy uprobes' exports (in Module.symvers) into the temporary directory
 * so the script-module build can find them.
 */
static int
copy_uprobes_symbols (systemtap_session& s)
{
  string cp_cmd = string("/bin/cp ") + uprobes_home +
    string("/Module.symvers ") + s.tmpdir;
  int rc = system (cp_cmd.c_str());
  return rc;
}

static int
uprobes_pass (systemtap_session& s)
{
  if (!s.need_uprobes || kernel_built_uprobes(s))
    return 0;
  /*
   * We need to use the version of uprobes that comes with SystemTap, so
   * we may need to rebuild uprobes.ko there.  Unfortunately, this is
   * never a no-op; e.g., the modpost step gets run every time.  We don't
   * want non-root users modifying uprobes, so we keep the uprobes
   * directory writable only by root.  But that means a non-root member
   * of group stapdev can't run the make even if everything's up to date.
   *
   * So for non-root users, we just use "make -q" with a fake target to
   * verify that uprobes doesn't need to be rebuilt.  If that's not so,
   * stap must fail.
   */
  int rc;
  if (geteuid() == 0) {
    rc = make_uprobes(s);
    if (rc == 0)
      rc = copy_uprobes_symbols(s);
  } else
    rc = verify_uprobes_uptodate(s);
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
  
  rc = system (staprun_cmd.c_str ());
  return rc;
}
