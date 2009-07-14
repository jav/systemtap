// build/run probes
// Copyright (C) 2005-2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "buildrun.h"
#include "session.h"
#include "util.h"
#if HAVE_NSS
#include "modsign.h"
#endif

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
#include <glob.h>
}


using namespace std;

static int uprobes_pass (systemtap_session& s);

/* Adjust and run make_cmd to build a kernel module. */
static int
run_make_cmd(systemtap_session& s, string& make_cmd)
{
  // Before running make, fix up the environment a bit.  PATH should
  // already be overridden.  Clean out a few variables that
  // s.kernel_build_tree/Makefile uses.
  int rc = unsetenv("ARCH") || unsetenv("KBUILD_EXTMOD")
      || unsetenv("CROSS_COMPILE") || unsetenv("KBUILD_IMAGE")
      || unsetenv("KCONFIG_CONFIG") || unsetenv("INSTALL_PATH");
  if (rc)
    {
      const char* e = strerror (errno);
      cerr << "unsetenv failed: " << e << endl;
    }

  // Disable ccache to avoid saving files that will never be reused.
  // (ccache is useless to us, because our compiler commands always
  // include the randomized tmpdir path.)
  // It's not critical if this fails, so the return is ignored.
  (void) setenv("CCACHE_DISABLE", "1", 0);

  if (s.verbose > 2)
    make_cmd += " V=1";
  else if (s.verbose > 1)
    make_cmd += " >/dev/null";
  else
    make_cmd += " -s >/dev/null 2>&1";

  if (s.verbose > 1) clog << "Running " << make_cmd << endl;
  rc = stap_system (make_cmd.c_str());
  if (rc && s.verbose > 1)
    clog << "Error " << rc << " " << strerror(rc) << endl;
  return rc;
}

static void
output_autoconf(systemtap_session& s, ofstream& o, const char *autoconf_c,
                const char *deftrue, const char *deffalse)
{
  o << "\t";
  if (s.verbose < 4)
    o << "@";
  o << "if $(CHECK_BUILD) $(SYSTEMTAP_RUNTIME)/" << autoconf_c;
  if (s.verbose < 5)
    o << " > /dev/null 2>&1";
  o << "; then ";
  if (deftrue)
    o << "echo \"#define " << deftrue << " 1\"";
  if (deffalse)
    o << "; else echo \"#define " << deffalse << " 1\"";
  o << "; fi >> $@" << endl;
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

  string redirecterrors = "> /dev/null 2>&1";
  if (s.verbose > 6)
    redirecterrors = "";

  // Support O= (or KBUILD_OUTPUT) option
  o << "_KBUILD_CFLAGS := $(call flags,KBUILD_CFLAGS)" << endl;

  o << "stap_check_gcc = $(shell " << superverbose << " if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo \"$(1)\"; else echo \"$(2)\"; fi)" << endl;
  o << "CHECK_BUILD := $(CC) $(KBUILD_CPPFLAGS) $(CPPFLAGS) $(LINUXINCLUDE) $(_KBUILD_CFLAGS) $(CFLAGS_KERNEL) $(EXTRA_CFLAGS) $(CFLAGS) -DKBUILD_BASENAME=\\\"" << s.module_name << "\\\" -Werror -S -o /dev/null -xc " << endl;
  o << "stap_check_build = $(shell " << superverbose << " if $(CHECK_BUILD) $(1) " << redirecterrors << " ; then echo \"$(2)\"; else echo \"$(3)\"; fi)" << endl;

  o << "SYSTEMTAP_RUNTIME = \"" << s.runtime_path << "\"" << endl;

  // "autoconf" options go here

  string module_cflags = "EXTRA_CFLAGS";
  o << module_cflags << " :=" << endl;

  // XXX: This gruesome hack is needed on some kernels built with separate O=directory,
  // where files like 2.6.27 x86's asm/mach-*/mach_mpspec.h are not found on the cpp path.
  // This could be a bug in arch/x86/Makefile that names
  //      mflags-y += -Iinclude/asm-x86/mach-default
  // but that path does not exist in an O= build tree.
  o << module_cflags << " += -Iinclude2/asm/mach-default" << endl;

  // NB: don't try
  // o << module_cflags << " += -Iusr/include" << endl;
  // since such headers are cleansed of _KERNEL_ pieces that we need

  o << "STAPCONF_HEADER := " << s.tmpdir << "/" << s.stapconf_name << endl;
  o << s.translated_source << ": $(STAPCONF_HEADER)" << endl;
  o << "$(STAPCONF_HEADER):" << endl;
  o << "\t@echo -n > $@" << endl;
  output_autoconf(s, o, "autoconf-hrtimer-rel.c", "STAPCONF_HRTIMER_REL", NULL);
  output_autoconf(s, o, "autoconf-hrtimer-getset-expires.c", "STAPCONF_HRTIMER_GETSET_EXPIRES", NULL);
  output_autoconf(s, o, "autoconf-inode-private.c", "STAPCONF_INODE_PRIVATE", NULL);
  output_autoconf(s, o, "autoconf-constant-tsc.c", "STAPCONF_CONSTANT_TSC", NULL);
  output_autoconf(s, o, "autoconf-tsc-khz.c", "STAPCONF_TSC_KHZ", NULL);
  output_autoconf(s, o, "autoconf-ktime-get-real.c", "STAPCONF_KTIME_GET_REAL", NULL);
  output_autoconf(s, o, "autoconf-x86-uniregs.c", "STAPCONF_X86_UNIREGS", NULL);
  output_autoconf(s, o, "autoconf-nameidata.c", "STAPCONF_NAMEIDATA_CLEANUP", NULL);
  output_autoconf(s, o, "autoconf-unregister-kprobes.c", "STAPCONF_UNREGISTER_KPROBES", NULL);
  output_autoconf(s, o, "autoconf-real-parent.c", "STAPCONF_REAL_PARENT", NULL);
  output_autoconf(s, o, "autoconf-uaccess.c", "STAPCONF_LINUX_UACCESS_H", NULL);
  output_autoconf(s, o, "autoconf-oneachcpu-retry.c", "STAPCONF_ONEACHCPU_RETRY", NULL);
  output_autoconf(s, o, "autoconf-dpath-path.c", "STAPCONF_DPATH_PATH", NULL);
  output_autoconf(s, o, "autoconf-synchronize-sched.c", "STAPCONF_SYNCHRONIZE_SCHED", NULL);
  output_autoconf(s, o, "autoconf-task-uid.c", "STAPCONF_TASK_UID", NULL);
  output_autoconf(s, o, "autoconf-vm-area.c", "STAPCONF_VM_AREA", NULL);
  output_autoconf(s, o, "autoconf-procfs-owner.c", "STAPCONF_PROCFS_OWNER", NULL);
  output_autoconf(s, o, "autoconf-alloc-percpu-align.c", "STAPCONF_ALLOC_PERCPU_ALIGN", NULL);
  output_autoconf(s, o, "autoconf-find-task-pid.c", "STAPCONF_FIND_TASK_PID", NULL);
  output_autoconf(s, o, "autoconf-x86-gs.c", "STAPCONF_X86_GS", NULL);

#if 0
  /* NB: For now, the performance hit of probe_kernel_read/write (vs. our
   * homegrown safe-access functions) is deemed undesireable, so we'll skip
   * this autoconf. */
  output_autoconf(s, o, "autoconf-probe-kernel.c", "STAPCONF_PROBE_KERNEL", NULL);
#endif
  output_autoconf(s, o, "autoconf-save-stack-trace.c",
                  "STAPCONF_KERNEL_STACKTRACE", NULL);
  output_autoconf(s, o, "autoconf-asm-syscall.c",
		  "STAPCONF_ASM_SYSCALL_H", NULL);
  output_autoconf(s, o, "autoconf-ring_buffer-flags.c", "STAPCONF_RING_BUFFER_FLAGS", NULL);

  o << module_cflags << " += -include $(STAPCONF_HEADER)" << endl;

  for (unsigned i=0; i<s.macros.size(); i++)
    o << "EXTRA_CFLAGS += -D " << lex_cast_qstring(s.macros[i]) << endl;

  if (s.verbose > 3)
    o << "EXTRA_CFLAGS += -ftime-report -Q" << endl;

  // XXX: unfortunately, -save-temps can't work since linux kbuild cwd
  // is not writeable.
  //
  // if (s.keep_tmpdir)
  // o << "CFLAGS += -fverbose-asm -save-temps" << endl;

  // Kernels can be compiled with CONFIG_CC_OPTIMIZE_FOR_SIZE to select
  // -Os, otherwise -O2 is the default.
  o << "EXTRA_CFLAGS += -freorder-blocks" << endl; // improve on -Os

  // We used to allow the user to override default optimization when so
  // requested by adding a -O[0123s] so they could determine the
  // time/space/speed tradeoffs themselves, but we cannot guantantee that
  // the (un)optimized code actually compiles and/or generates functional
  // code, so we had to remove it.
  // o << "EXTRA_CFLAGS += " << s.gcc_flags << endl; // Add -O[0123s]

  // o << "CFLAGS += -fno-unit-at-a-time" << endl;

  // Assumes linux 2.6 kbuild
  o << "EXTRA_CFLAGS += -Wno-unused -Werror" << endl;
  #if CHECK_POINTER_ARITH_PR5947
  o << "EXTRA_CFLAGS += -Wpointer-arith" << endl;
  #endif
  o << "EXTRA_CFLAGS += -I\"" << s.runtime_path << "\"" << endl;
  // XXX: this may help ppc toc overflow
  // o << "CFLAGS := $(subst -Os,-O2,$(CFLAGS)) -fminimal-toc" << endl;
  o << "obj-m := " << s.module_name << ".o" << endl;

  o.close ();

  // Generate module directory pathname and make sure it exists.
  string module_dir;
  module_dir = s.kernel_build_tree;
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

#if HAVE_NSS
  // If a certificate database was specified, then try to sign the module.
  // Failure to do so is not a fatal error. If the signature is actually needed,
  // staprun will complain at that time.
  assert (! s.cert_db_path.empty());
  if (!rc)
    sign_module (s);
#endif

  return rc;
}

/*
 * If uprobes was built as part of the kernel build (either built-in
 * or as a module), the uprobes exports should show up in either
 * s.kernel_build_tree / Module.symvers.  Return true if so.
 */
static bool
kernel_built_uprobes (systemtap_session& s)
{
  string grep_cmd = string ("/bin/grep -q unregister_uprobe ") + 
    s.kernel_build_tree + string ("/Module.symvers");
  int rc = stap_system (grep_cmd.c_str());
  if (rc && s.verbose > 1)
    clog << "Error " << rc << " " << strerror(rc) << endl;
  return (rc == 0);
}

static bool
verify_uprobes_uptodate (systemtap_session& s)
{
  if (s.verbose)
    clog << "Pass 4, preamble: "
	 << "verifying that SystemTap's version of uprobes is up to date."
	 << endl;

  string uprobes_home = s.runtime_path + "/uprobes";
  string make_cmd = string("make -q -C ") + uprobes_home
    + string(" uprobes.ko");
  int rc = run_make_cmd(s, make_cmd);
  if (rc) {
    clog << "SystemTap's version of uprobes is out of date." << endl;
    clog << "As root, run \"make -C " << uprobes_home << "\"." << endl;
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

  string uprobes_home = s.runtime_path + "/uprobes";
  string make_cmd = string("make -C ") + uprobes_home;
  int rc = run_make_cmd(s, make_cmd);
  if (s.verbose > 1)
    clog << "uprobes rebuild rc=" << rc << endl;

  return rc;
}

/*
 * Copy uprobes' exports (in Module.symvers) into the temporary directory
 * so the script-module build can find them.
 */
static int
copy_uprobes_symbols (systemtap_session& s)
{
  string uprobes_home = s.runtime_path + "/uprobes";
  string cp_cmd = string("/bin/cp ") + uprobes_home +
    string("/Module.symvers ") + s.tmpdir;
  int rc = stap_system (cp_cmd.c_str());
  if (rc && s.verbose > 1)
    clog << "Error " << rc << " " << strerror(rc) << endl;
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
  if (geteuid() == 0)
    rc = make_uprobes(s);
  else
    rc = verify_uprobes_uptodate(s);
  if (rc == 0)
    rc = copy_uprobes_symbols(s);
  return rc;
}

int
run_pass (systemtap_session& s)
{
  int rc = 0;

  // for now, just spawn staprun
  string staprun_cmd = string(getenv("SYSTEMTAP_STAPRUN") ?: BINDIR "/staprun")
    + " "
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

  if (s.load_only)
    staprun_cmd += (s.output_file.empty() ? "-L " : "-D ");

  if (!s.size_option.empty())
    staprun_cmd += "-S " + s.size_option + " ";

  staprun_cmd += s.tmpdir + "/" + s.module_name + ".ko";

  if (s.verbose>1) clog << "Running " << staprun_cmd << endl;

  rc = stap_system (staprun_cmd.c_str ());
  if (rc && s.verbose > 1)
    clog << "Error " << rc << " " << strerror(rc) << endl;
  return rc;
}


// Build a tiny kernel module to query tracepoints
int
make_tracequery(systemtap_session& s, string& name, const vector<string>& extra_headers)
{
  // create a subdirectory for the module
  string dir(s.tmpdir + "/tracequery");
  if (create_dir(dir.c_str()) != 0)
    {
      if (! s.suppress_warnings)
        cerr << "Warning: failed to create directory for querying tracepoints." << endl;
      return 1;
    }

  name = dir + "/tracequery.ko";

  // create a simple Makefile
  string makefile(dir + "/Makefile");
  ofstream omf(makefile.c_str());
  // force debuginfo generation, and relax implicit functions
  omf << "EXTRA_CFLAGS := -g -Wno-implicit-function-declaration" << endl;
  omf << "obj-m := tracequery.o" << endl;
  omf.close();

  // create our source file
  string source(dir + "/tracequery.c");
  ofstream osrc(source.c_str());
  osrc << "#ifdef CONFIG_TRACEPOINTS" << endl;
  osrc << "#include <linux/tracepoint.h>" << endl;

  // override DECLARE_TRACE to synthesize probe functions for us
  osrc << "#undef DECLARE_TRACE" << endl;
  osrc << "#define DECLARE_TRACE(name, proto, args) \\" << endl;
  osrc << "  void stapprobe_##name(proto) {}" << endl;

  // older tracepoints used DEFINE_TRACE, so redirect that too
  osrc << "#undef DEFINE_TRACE" << endl;
  osrc << "#define DEFINE_TRACE(name, proto, args) \\" << endl;
  osrc << "  DECLARE_TRACE(name, TPPROTO(proto), TPARGS(args))" << endl;

  // PR9993: Add extra headers to work around undeclared types in individual
  // include/trace/foo.h files
  for (unsigned z=0; z<extra_headers.size(); z++)
    osrc << "#include <" << extra_headers[z] << ">\n";

  // dynamically pull in all tracepoint headers from include/trace/
  glob_t trace_glob;
  string globs[] = {
      "/include/trace/*.h",
      "/include/trace/events/*.h",
      "/source/include/trace/*.h",
      "/source/include/trace/events/*.h",
  };
  for (unsigned z = 0; z < sizeof(globs) / sizeof(globs[0]); z++)
    {
      string glob_str(s.kernel_build_tree + globs[z]);
      glob(glob_str.c_str(), 0, NULL, &trace_glob);
      for (unsigned i = 0; i < trace_glob.gl_pathc; ++i)
        {
          string header(trace_glob.gl_pathv[i]);
          size_t root_pos = header.rfind("/include/");
          assert(root_pos != string::npos);
          header.erase(0, root_pos + 9);

          // filter out a few known "internal-only" headers
          if (header.find("/ftrace.h") != string::npos)
            continue;
          if (header.find("/trace_events.h") != string::npos)
            continue;
          if (header.find("_event_types.h") != string::npos)
            continue;

          osrc << "#include <" << header << ">" << endl;
        }
      globfree(&trace_glob);
    }

  // finish up the module source
  osrc << "#endif /* CONFIG_TRACEPOINTS */" << endl;
  osrc.close();

  // make the module
  string make_cmd = "make -C '" + s.kernel_build_tree + "'"
    + " M='" + dir + "' modules";
  if (s.verbose < 4)
    make_cmd += " >/dev/null 2>&1";
  return run_make_cmd(s, make_cmd);
}


// Build a tiny kernel module to query type information
static int
make_typequery_kmod(systemtap_session& s, const string& header, string& name)
{
  static unsigned tick = 0;
  string basename("typequery_kmod_" + lex_cast<string>(++tick));

  // create a subdirectory for the module
  string dir(s.tmpdir + "/" + basename);
  if (create_dir(dir.c_str()) != 0)
    {
      if (! s.suppress_warnings)
        cerr << "Warning: failed to create directory for querying types." << endl;
      return 1;
    }

  name = dir + "/" + basename + ".ko";

  // create a simple Makefile
  string makefile(dir + "/Makefile");
  ofstream omf(makefile.c_str());
  omf << "EXTRA_CFLAGS := -g -fno-eliminate-unused-debug-types" << endl;

  // NB: We use -include instead of #include because that gives us more power.
  // Using #include searches relative to the source's path, which in this case
  // is /tmp/..., so that's not helpful.  Using -include will search relative
  // to the cwd, which will be the kernel build root.  This means if you have a
  // full kernel build tree, it's possible to get at types that aren't in the
  // normal include path, e.g.:
  //    @cast(foo, "bsd_acct_struct", "kernel<kernel/acct.c>")->...
  omf << "CFLAGS_" << basename << ".o := -include " << header << endl;

  omf << "obj-m := " + basename + ".o" << endl;
  omf.close();

  // create our empty source file
  string source(dir + "/" + basename + ".c");
  ofstream osrc(source.c_str());
  osrc.close();

  // make the module
  string make_cmd = "make -C '" + s.kernel_build_tree + "'"
    + " M='" + dir + "' modules";
  if (s.verbose < 4)
    make_cmd += " >/dev/null 2>&1";
  return run_make_cmd(s, make_cmd);
}


// Build a tiny user module to query type information
static int
make_typequery_umod(systemtap_session& s, const string& header, string& name)
{
  static unsigned tick = 0;

  name = s.tmpdir + "/typequery_umod_" + lex_cast<string>(++tick) + ".so";

  // make the module
  //
  // NB: As with kmod, using -include makes relative paths more useful.  The
  // cwd in this case will be the cwd of stap itself though, which may be
  // trickier to deal with.  It might be better to "cd `dirname $script`"
  // first...
  string cmd = "gcc -shared -g -fno-eliminate-unused-debug-types -o "
     + name + " -xc /dev/null -include " + header;
  if (s.verbose < 4)
    cmd += " >/dev/null 2>&1";
  int rc = stap_system (cmd.c_str());
  if (rc && s.verbose > 1)
    clog << "Error " << rc << " " << strerror(rc) << endl;
  return rc;
}


int
make_typequery(systemtap_session& s, string& module)
{
  int rc;
  string new_module;

  if (module[module.size() - 1] != '>')
    return -1;

  if (module[0] == '<')
    {
      string header = module.substr(1, module.size() - 2);
      rc = make_typequery_umod(s, header, new_module);
    }
  else if (module.compare(0, 7, "kernel<") == 0)
    {
      string header = module.substr(7, module.size() - 8);
      rc = make_typequery_kmod(s, header, new_module);
    }
  else
    return -1;

  if (!rc)
    module = new_module;

  return rc;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
