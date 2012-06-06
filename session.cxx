// session functions
// Copyright (C) 2010-2012 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "session.h"
#include "cache.h"
#include "elaborate.h"
#include "translate.h"
#include "buildrun.h"
#include "coveragedb.h"
#include "hash.h"
#include "task_finder.h"
#include "csclient.h"
#include "rpm_finder.h"
#include "util.h"
#include "cmdline.h"
#include "git_version.h"

#include <cerrno>
#include <cstdlib>

extern "C" {
#include <getopt.h>
#include <limits.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <elfutils/libdwfl.h>
#include <unistd.h>
}

#if HAVE_NSS
extern "C" {
#include <nspr.h>
}
#endif

#include <string>

using namespace std;

/* getopt variables */
extern int optind;

#define PATH_TBD string("__TBD__")

#if HAVE_NSS
bool systemtap_session::NSPR_Initialized = false;
#endif

systemtap_session::systemtap_session ():
  // NB: pointer members must be manually initialized!
  // NB: don't forget the copy constructor too!
  base_hash(0),
  pattern_root(new match_node),
  user_file (0),
  be_derived_probes(0),
  dwarf_derived_probes(0),
  kprobe_derived_probes(0),
  hwbkpt_derived_probes(0),
  perf_derived_probes(0),
  uprobe_derived_probes(0),
  utrace_derived_probes(0),
  itrace_derived_probes(0),
  task_finder_derived_probes(0),
  timer_derived_probes(0),
  netfilter_derived_probes(0),
  profile_derived_probes(0),
  mark_derived_probes(0),
  tracepoint_derived_probes(0),
  hrtimer_derived_probes(0),
  procfs_derived_probes(0),
  op (0), up (0),
  sym_kprobes_text_start (0),
  sym_kprobes_text_end (0),
  sym_stext (0),
  module_cache (0),
  last_token (0)
{
  struct utsname buf;
  (void) uname (& buf);
  kernel_release = string (buf.release);
  release = kernel_release;
  kernel_build_tree = "/lib/modules/" + kernel_release + "/build";
  architecture = machine = normalize_machine(buf.machine);

  for (unsigned i=0; i<5; i++) perpass_verbose[i]=0;
  verbose = 0;

  have_script = false;
  runtime_specified = false;
  include_arg_start = -1;
  timing = false;
  guru_mode = false;
  bulk_mode = false;
  unoptimized = false;
  suppress_warnings = false;
  panic_warnings = false;
  listing_mode = false;
  listing_mode_vars = false;
  dump_probe_types = false;

#ifdef ENABLE_PROLOGUES
  prologue_searching = true;
#else
  prologue_searching = false;
#endif

  buffer_size = 0;
  last_pass = 5;
  module_name = "stap_" + lex_cast(getpid());
  stapconf_name = "stapconf_" + lex_cast(getpid()) + ".h";
  output_file = ""; // -o FILE
  tmpdir_opt_set = false;
  save_module = false;
  modname_given = false;
  keep_tmpdir = false;
  cmd = "";
  target_pid = 0;
  use_cache = true;
  use_script_cache = true;
  poison_cache = false;
  tapset_compile_coverage = false;
  need_uprobes = false;
  need_unwind = false;
  need_symbols = false;
  uprobes_path = "";
  consult_symtab = false;
  ignore_vmlinux = false;
  ignore_dwarf = false;
  load_only = false;
  skip_badvars = false;
  privilege = pr_stapdev;
  privilege_set = false;
  omit_werror = false;
  compatible = VERSION; // XXX: perhaps also process GIT_SHAID if available?
  unwindsym_ldd = false;
  client_options = false;
  server_cache = NULL;
  automatic_server_mode = false;
  use_server_on_error = false;
  try_server_status = try_server_unset;
  use_remote_prefix = false;
  systemtap_v_check = false;
  download_dbinfo = 0;
  suppress_handler_errors = false;
  native_build = true; // presumed
  sysroot = "";
  update_release_sysroot = false;

  /*  adding in the XDG_DATA_DIRS variable path,
   *  this searches in conjunction with SYSTEMTAP_TAPSET
   *  to locate stap scripts, either can be disabled if 
   *  needed using env $PATH=/dev/null where $PATH is the 
   *  path you want disabled
   */  
  const char* s_p1 = getenv ("XDG_DATA_DIRS");
  if ( s_p1 != NULL )
  {
    vector<string> dirs;
    tokenize(s_p1, dirs, ":");
    for(vector<string>::iterator i = dirs.begin(); i != dirs.end(); ++i)
    {
      include_path.push_back(*i + "/systemtap/tapset");
    }
  }

  const char* s_p = getenv ("SYSTEMTAP_TAPSET");
  if (s_p != NULL)
  {
    include_path.push_back (s_p);
  }
  else
  {
    include_path.push_back (string(PKGDATADIR) + "/tapset");
  }

  const char* s_r = getenv ("SYSTEMTAP_RUNTIME");
  if (s_r != NULL)
    runtime_path = s_r;
  else
    runtime_path = string(PKGDATADIR) + "/runtime";

  const char* s_d = getenv ("SYSTEMTAP_DIR");
  if (s_d != NULL)
    data_path = s_d;
  else
    data_path = get_home_directory() + string("/.systemtap");
  if (create_dir(data_path.c_str()) == 1)
    {
      const char* e = strerror (errno);
        print_warning("failed to create systemtap data directory \"" + data_path + "\" " + e + ", disabling cache support.");
      use_cache = use_script_cache = false;
    }

  if (use_cache)
    {
      cache_path = data_path + "/cache";
      if (create_dir(cache_path.c_str()) == 1)
        {
	  const char* e = strerror (errno);
            print_warning("failed to create cache directory (\" " + cache_path + " \") " + e + ", disabling cache support.");
	  use_cache = use_script_cache = false;
	}
    }

  const char* s_tc = getenv ("SYSTEMTAP_COVERAGE");
  if (s_tc != NULL)
    tapset_compile_coverage = true;

  const char* s_kr = getenv ("SYSTEMTAP_RELEASE");
  if (s_kr != NULL) {
    setup_kernel_release(s_kr);
  }
  create_tmp_dir();
}

systemtap_session::systemtap_session (const systemtap_session& other,
                                      const string& arch,
                                      const string& kern):
  // NB: pointer members must be manually initialized!
  // NB: this needs to consider everything that the base ctor does,
  //     plus copying any wanted implicit fields (strings, vectors, etc.)
  base_hash(0),
  pattern_root(new match_node),
  user_file (other.user_file),
  be_derived_probes(0),
  dwarf_derived_probes(0),
  kprobe_derived_probes(0),
  hwbkpt_derived_probes(0),
  perf_derived_probes(0),
  uprobe_derived_probes(0),
  utrace_derived_probes(0),
  itrace_derived_probes(0),
  task_finder_derived_probes(0),
  timer_derived_probes(0),
  netfilter_derived_probes(0),
  profile_derived_probes(0),
  mark_derived_probes(0),
  tracepoint_derived_probes(0),
  hrtimer_derived_probes(0),
  procfs_derived_probes(0),
  op (0), up (0),
  sym_kprobes_text_start (0),
  sym_kprobes_text_end (0),
  sym_stext (0),
  module_cache (0),
  last_token (0)
{
  release = kernel_release = kern;
  kernel_build_tree = "/lib/modules/" + kernel_release + "/build";
  architecture = machine = normalize_machine(arch);
  setup_kernel_release(kern.c_str());
  native_build = false; // assumed; XXX: could be computed as in check_options()

  // These are all copied in the same order as the default ctor did above.

  copy(other.perpass_verbose, other.perpass_verbose + 5, perpass_verbose);
  verbose = other.verbose;

  have_script = other.have_script;
  runtime_specified = other.runtime_specified;
  include_arg_start = other.include_arg_start;
  timing = other.timing;
  guru_mode = other.guru_mode;
  bulk_mode = other.bulk_mode;
  unoptimized = other.unoptimized;
  suppress_warnings = other.suppress_warnings;
  panic_warnings = other.panic_warnings;
  listing_mode = other.listing_mode;
  listing_mode_vars = other.listing_mode_vars;
  dump_probe_types = other.dump_probe_types;

  prologue_searching = other.prologue_searching;

  buffer_size = other.buffer_size;
  last_pass = other.last_pass;
  module_name = other.module_name;
  stapconf_name = other.stapconf_name;
  output_file = other.output_file; // XXX how should multiple remotes work?
  tmpdir_opt_set = false;
  save_module = other.save_module;
  modname_given = other.modname_given;
  keep_tmpdir = other.keep_tmpdir;
  cmd = other.cmd;
  target_pid = other.target_pid; // XXX almost surely nonsense for multiremote
  use_cache = other.use_cache;
  use_script_cache = other.use_script_cache;
  poison_cache = other.poison_cache;
  tapset_compile_coverage = other.tapset_compile_coverage;
  need_uprobes = false;
  need_unwind = false;
  need_symbols = false;
  uprobes_path = "";
  consult_symtab = other.consult_symtab;
  ignore_vmlinux = other.ignore_vmlinux;
  ignore_dwarf = other.ignore_dwarf;
  load_only = other.load_only;
  skip_badvars = other.skip_badvars;
  privilege = other.privilege;
  privilege_set = other.privilege_set;
  omit_werror = other.omit_werror;
  compatible = other.compatible;
  unwindsym_ldd = other.unwindsym_ldd;
  client_options = other.client_options;
  server_cache = NULL;
  use_server_on_error = other.use_server_on_error;
  try_server_status = other.try_server_status;
  use_remote_prefix = other.use_remote_prefix;
  systemtap_v_check = other.systemtap_v_check;
  download_dbinfo = other.download_dbinfo;
  suppress_handler_errors = other.suppress_handler_errors;
  sysroot = other.sysroot;
  update_release_sysroot = other.update_release_sysroot;
  sysenv = other.sysenv;

  include_path = other.include_path;
  runtime_path = other.runtime_path;

  // NB: assuming that "other" created these already
  data_path = other.data_path;
  cache_path = other.cache_path;

  tapset_compile_coverage = other.tapset_compile_coverage;


  // These are fields that were left to their default ctor, but now we want to
  // copy them from "other".  In the same order as declared...
  script_file = other.script_file;
  cmdline_script = other.cmdline_script;
  macros = other.macros;
  args = other.args;
  kbuildflags = other.kbuildflags;
  globalopts = other.globalopts;
  modinfos = other.modinfos;

  client_options_disallowed_for_unprivileged = other.client_options_disallowed_for_unprivileged;
  server_status_strings = other.server_status_strings;
  specified_servers = other.specified_servers;
  server_trust_spec = other.server_trust_spec;
  server_args = other.server_args;

  unwindsym_modules = other.unwindsym_modules;
  automatic_server_mode = other.automatic_server_mode;

  create_tmp_dir();
}

systemtap_session::~systemtap_session ()
{
  remove_tmp_dir();
  delete_map(subsessions);
  delete pattern_root;
}

#if HAVE_NSS
void
systemtap_session::NSPR_init ()
{
  if (! NSPR_Initialized)
    {
      PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
      NSPR_Initialized = true;
    }
}
#endif // HAVE_NSS

systemtap_session*
systemtap_session::clone(const string& arch, const string& release)
{
  const string norm_arch = normalize_machine(arch);
  if (this->architecture == norm_arch && this->kernel_release == release)
    return this;

  systemtap_session*& s = subsessions[make_pair(norm_arch, release)];
  if (!s)
    s = new systemtap_session(*this, norm_arch, release);
  return s;
}

void
systemtap_session::version ()
{
  clog << _F("Systemtap translator/driver (version %s/%s %s)\n"
             "Copyright (C) 2005-2012 Red Hat, Inc. and others\n"
             "This is free software; see the source for copying conditions.",
             VERSION, dwfl_version(NULL), GIT_MESSAGE) << endl;
  clog << _("enabled features:")
#ifdef HAVE_AVAHI
       << " AVAHI"
#endif
#ifdef HAVE_LIBRPM
       << " LIBRPM"
#endif
#ifdef HAVE_LIBSQLITE3
       << " LIBSQLITE3"
#endif
#ifdef HAVE_NSS
       << " NSS"
#endif
#ifdef HAVE_BOOST_SHARED_PTR_HPP
       << " BOOST_SHARED_PTR"
#endif
#ifdef HAVE_TR1_UNORDERED_MAP
       << " TR1_UNORDERED_MAP"
#endif
#ifdef ENABLE_PROLOGUES
       << " PROLOGUES"
#endif
#ifdef ENABLE_NLS
       << " NLS"
#endif
       << endl;
}

void
systemtap_session::usage (int exitcode)
{
  version ();
  clog
    << endl
    << _F("Usage: stap [options] FILE         Run script in file.\n"
     "   or: stap [options] -            Run script on stdin.\n"
     "   or: stap [options] -e SCRIPT    Run given script.\n"
     "   or: stap [options] -l PROBE     List matching probes.\n"
     "   or: stap [options] -L PROBE     List matching probes and local variables.\n\n"
     "Options (in %s/rc and on command line):\n"
     "   --         end of translator options, script options follow\n"
     "   -h --help  show help\n"
     "   -V --version  show version\n"
     "   -p NUM     stop after pass NUM 1-5, instead of %d\n"
     "              (parse, elaborate, translate, compile, run)\n"
     "   -v         add verbosity to all passes\n"
     "   --vp {N}+  add per-pass verbosity [", data_path.c_str(), last_pass);
  for (unsigned i=0; i<5; i++)
    clog << (perpass_verbose[i] <= 9 ? perpass_verbose[i] : 9);
  clog 
    << "]" << endl;
    clog << _F("   -k         keep temporary directory\n"
     "   -u         unoptimized translation %s\n"
     "   -w         suppress warnings %s\n"
     "   -W         turn warnings into errors %s\n"
     "   -g         guru mode %s\n"
     "   -P         prologue-searching for function probes %s\n"
     "   -b         bulk (percpu file) mode %s\n"
     "   -s NUM     buffer size in megabytes, instead of %d\n"
     "   -I DIR     look in DIR for additional .stp script files", (unoptimized ? _(" [set]") : ""),
         (suppress_warnings ? _(" [set]") : ""), (panic_warnings ? _(" [set]") : ""),
         (guru_mode ? _(" [set]") : ""), (prologue_searching ? _(" [set]") : ""),
         (bulk_mode ? _(" [set]") : ""), buffer_size);
  if (include_path.size() == 0)
    clog << endl;
  else
    clog << _(", in addition to") << endl;
  for (unsigned i=0; i<include_path.size(); i++)
    clog << "              " << include_path[i].c_str() << endl;
  clog
    << _F("   -D NM=VAL  emit macro definition into generated C code\n"
    "   -B NM=VAL  pass option to kbuild make\n"
    "   --modinfo NM=VAL\n"
    "              include a MODULE_INFO(NM,VAL) in the generated C code\n"
    "   -G VAR=VAL set global variable to value\n"
    //TRANSLATORS: translating 'runtime' is not advised 
    "   -R DIR     look in DIR for runtime, instead of\n"
    "              %s\n"
    "   -r DIR     cross-compile to kernel with given build tree; or else\n"
    "   -r RELEASE cross-compile to kernel /lib/modules/RELEASE/build, instead of\n"
    "              %s\n" 
    "   -a ARCH    cross-compile to given architecture, instead of %s\n"
    "   -m MODULE  set probe module name, instead of \n"
    "              %s\n"
    "   -o FILE    send script output to file, instead of stdout. This supports\n" 
    "              strftime(3) formats for FILE\n"
    "   -c CMD     start the probes, run CMD, and exit when it finishes\n"
    "   -x PID     sets target() to PID\n"
    "   -F         run as on-file flight recorder with -o.\n"
    "              run as on-memory flight recorder without -o.\n"
    "   -S size[,n] set maximum of the size and the number of files.\n"
    "   -d OBJECT  add unwind/symbol data for OBJECT file", runtime_path.c_str(), kernel_build_tree.c_str(), architecture.c_str(), module_name.c_str());
  if (unwindsym_modules.size() == 0)
    clog << endl;
  else
    clog << _(", in addition to") << endl;
  {
    vector<string> syms (unwindsym_modules.begin(), unwindsym_modules.end());
    for (unsigned i=0; i<syms.size(); i++)
      clog << "              " << syms[i].c_str() << endl;
  }
  clog
    << _F("   --ldd      add unwind/symbol data for all referenced object files.\n"
    "   --all-modules\n"
    "              add unwind/symbol data for all loaded kernel objects.\n"
    "   -t         collect probe timing information\n"
#ifdef HAVE_LIBSQLITE3
    "   -q         generate information on tapset coverage\n"
#endif /* HAVE_LIBSQLITE3 */
    "   --privilege=PRIVILEGE_LEVEL\n"
    "              check the script for constructs not allowed at the given privilege level\n"
    "   --unprivileged\n"
    "              equivalent to --privilege=stapusr\n"
#if 0 /* PR6864: disable temporarily; should merge with -d somehow */
    "   --kelf     make do with symbol table from vmlinux\n"
    "   --kmap[=FILE]\n"
    "              make do with symbol table from nm listing\n"
#endif
  // Formerly present --ignore-{vmlinux,dwarf} options are for testsuite use
  // only, and don't belong in the eyesight of a plain user.
    "   --compatible=VERSION\n"
    "              suppress incompatible language/tapset changes beyond VERSION,\n"
    "              instead of %s\n"
    "   --check-version\n"
    "              displays warnings where a syntax element may be \n"
    "              version dependent\n"
    "   --skip-badvars\n"
    "              substitute zero for bad context $variables\n"
    "   --suppress-handler-errors\n"
    "              catch all runtime errors, quietly skip probe handlers\n"
    "   --use-server[=SERVER-SPEC]\n"
    "              specify systemtap compile-servers\n"
    "   --list-servers[=PROPERTIES]\n"
    "              report on the status of the specified compile-servers:\n"
    "              all,specified,online,trusted,signer,compatible\n"
#if HAVE_NSS
    "   --trust-servers[=TRUST-SPEC]\n"
    "              add/revoke trust of specified compile-servers:\n"
    "              ssl,signer,all-users,revoke,no-prompt\n"
    "   --use-server-on-error[=yes/no]\n"
    "              retry compilation using a compile server upon compilation error\n"
#endif
    "   --remote=HOSTNAME\n"
    "              run pass 5 on the specified ssh host.\n"
    "              may be repeated for targeting multiple hosts.\n"
    "   --remote-prefix\n"
    "              prefix each line of remote output with a host index.\n"
    "   --tmpdir=NAME\n"
    "              specify name of temporary directory to be used.\n"
    "   --download-debuginfo[=OPTION]\n"
    "              automatically download debuginfo using ABRT.\n"
    "              yes,no,ask,<timeout value>\n"
    "   --dump-probe-types\n"
    "              show a list of available probe types.\n"
    "   --sysroot=DIR\n"
    "              specify sysroot directory where target files (executables,\n"    "              libraries, etc.) are located.\n"
    "   --sysenv=VAR=VALUE\n"
    "              provide an alternate value for an environment variable\n"
    "              where the value on a remote system differs.  Path\n"
    "              variables (e.g. PATH, LD_LIBRARY_PATH) are assumed to be\n"
    "              relative to the sysroot.\n"
    , compatible.c_str()) << endl
  ;

  time_t now;
  time (& now);
  struct tm* t = localtime (& now);
  if (t && t->tm_mon*3 + t->tm_mday*173 == 0xb6)
    clog << morehelp << endl;

  exit (exitcode);
}

int
systemtap_session::parse_cmdline (int argc, char * const argv [])
{
  client_options_disallowed_for_unprivileged = "";
  struct rlimit our_rlimit;
  while (true)
    {
      char * num_endptr;
      int grc = getopt_long (argc, argv, STAP_SHORT_OPTIONS, stap_long_options, NULL);

      // NB: when adding new options, consider very carefully whether they
      // should be restricted from stap clients (after --client-options)!

      if (grc < 0)
        break;
      switch (grc)
        {
        case 'V':
          version ();
          exit (0);

        case 'v':
	  server_args.push_back (string ("-") + (char)grc);
          for (unsigned i=0; i<5; i++)
            perpass_verbose[i] ++;
	  verbose ++;
	  break;

        case 'G':
          // Make sure the global option is only composed of the
          // following chars: [_=a-zA-Z0-9]
          assert_regexp_match("-G parameter", optarg, "^[a-z_][a-z0-9_]*=[a-z0-9_-]+$");
          globalopts.push_back (string(optarg));
          break;

        case 't':
	  server_args.push_back (string ("-") + (char)grc);
	  timing = true;
	  break;

        case 'w':
	  server_args.push_back (string ("-") + (char)grc);
	  suppress_warnings = true;
	  break;

        case 'W':
	  server_args.push_back (string ("-") + (char)grc);
	  panic_warnings = true;
	  break;

        case 'p':
          last_pass = (int)strtoul(optarg, &num_endptr, 10);
          if (*num_endptr != '\0' || last_pass < 1 || last_pass > 5)
            {
              cerr << _("Invalid pass number (should be 1-5).") << endl;
              return 1;
            }
          if (listing_mode && last_pass != 2)
            {
              cerr << _("Listing (-l) mode implies pass 2.") << endl;
              return 1;
            }
	  server_args.push_back (string ("-") + (char)grc + optarg);
          break;

        case 'I':
	  if (client_options)
	    client_options_disallowed_for_unprivileged += client_options_disallowed_for_unprivileged.empty () ? "-I" : ", -I";
	  if (include_arg_start == -1)
	    include_arg_start = include_path.size ();
          include_path.push_back (string (optarg));
          break;

        case 'd':
	  server_args.push_back (string ("-") + (char)grc + optarg);
          {
            // At runtime user module names are resolved through their
            // canonical (absolute) path.
            const char *mpath = canonicalize_file_name (optarg);
            if (mpath == NULL) // Must be a kernel module name
              mpath = optarg;
            unwindsym_modules.insert (string (mpath));
            // PR10228: trigger vma tracker logic early if -d /USER-MODULE/
            // given. XXX This is actually too early. Having a user module
            // is a good indicator that something will need vma tracking.
            // But it is not 100%, this really should only trigger through
            // a user mode tapset /* pragma:vma */ or a probe doing a
            // variable lookup through a dynamic module.
            if (mpath[0] == '/')
              enable_vma_tracker (*this);
            break;
          }

        case 'e':
	  if (have_script)
	    {
	      cerr << _("Only one script can be given on the command line.")
		   << endl;
              return 1;
	    }
	  server_args.push_back (string ("-") + (char)grc + optarg);
          cmdline_script = string (optarg);
          have_script = true;
          break;

        case 'o':
          // NB: client_options not a problem, since pass 1-4 does not use output_file.
	  server_args.push_back (string ("-") + (char)grc + optarg);
          output_file = string (optarg);
          break;

        case 'R':
          if (client_options) { cerr << _F("ERROR: %s invalid with %s", "-R", "--client-options") << endl; return 1; }
	  runtime_specified = true;
          runtime_path = string (optarg);
          break;

        case 'm':
	  if (client_options)
	    client_options_disallowed_for_unprivileged += client_options_disallowed_for_unprivileged.empty () ? "-m" : ", -m";
          module_name = string (optarg);
	  save_module = true;
	  modname_given = true;
	  {
	    // If the module name ends with '.ko', chop it off since
	    // modutils doesn't like modules named 'foo.ko.ko'.
	    if (endswith(module_name, ".ko"))
	      {
		module_name.erase(module_name.size() - 3);
		cerr << _F("Truncating module name to '%s'", module_name.c_str()) << endl;
	      }

	    // Make sure an empty module name wasn't specified (-m "")
	    if (module_name.empty())
	    {
		cerr << _("Module name cannot be empty.") << endl;
		return 1;
	    }

	    // Make sure the module name is only composed of the
	    // following chars: [a-z0-9_]
            assert_regexp_match("-m parameter", module_name, "^[a-z0-9_]+$");

	    // Make sure module name isn't too long.
	    if (module_name.size() >= (MODULE_NAME_LEN - 1))
	      {
		module_name.resize(MODULE_NAME_LEN - 1);
		cerr << _F("Truncating module name to '%s'", module_name.c_str()) << endl;
	      }
	  }

	  server_args.push_back (string ("-") + (char)grc + optarg);
	  use_script_cache = false;
          break;

        case 'r':
          if (client_options) // NB: no paths!
            assert_regexp_match("-r parameter from client", optarg, "^[a-z0-9_.-]+$");
	  server_args.push_back (string ("-") + (char)grc + optarg);
          setup_kernel_release(optarg);
          break;

        case 'a':
          assert_regexp_match("-a parameter", optarg, "^[a-z0-9_-]+$");
	  server_args.push_back (string ("-") + (char)grc + optarg);
          architecture = string(optarg);
          break;

        case 'k':
          if (client_options) { cerr << _F("ERROR: %s invalid with %s", "-k", "--client-options") << endl; return 1; } 
          keep_tmpdir = true;
          use_script_cache = false; /* User wants to keep a usable build tree. */
          break;

        case 'g':
	  server_args.push_back (string ("-") + (char)grc);
          guru_mode = true;
          break;

        case 'P':
	  server_args.push_back (string ("-") + (char)grc);
          prologue_searching = true;
          break;

        case 'b':
	  server_args.push_back (string ("-") + (char)grc);
          bulk_mode = true;
          break;

	case 'u':
	  server_args.push_back (string ("-") + (char)grc);
	  unoptimized = true;
	  break;

        case 's':
          buffer_size = (int) strtoul (optarg, &num_endptr, 10);
          if (*num_endptr != '\0' || buffer_size < 1 || buffer_size > 4095)
            {
              cerr << _("Invalid buffer size (should be 1-4095).") << endl;
	      return 1;
            }
	  server_args.push_back (string ("-") + (char)grc + optarg);
          break;

	case 'c':
	  cmd = string (optarg);
          if (cmd == "")
            {
              // This would mess with later code deciding to pass -c
              // through to staprun
              cerr << _("Empty CMD string invalid.") << endl;
              return 1;
            }
	  server_args.push_back (string ("-") + (char)grc + optarg);
	  break;

	case 'x':
	  target_pid = (int) strtoul(optarg, &num_endptr, 10);
	  if (*num_endptr != '\0')
	    {
	      cerr << _("Invalid target process ID number.") << endl;
	      return 1;
	    }
	  server_args.push_back (string ("-") + (char)grc + optarg);
	  break;

	case 'D':
          assert_regexp_match ("-D parameter", optarg, "^[a-z_][a-z_0-9]*(=-?[a-z_0-9]+)?$");
	  if (client_options)
	    client_options_disallowed_for_unprivileged += client_options_disallowed_for_unprivileged.empty () ? "-D" : ", -D";
	  server_args.push_back (string ("-") + (char)grc + optarg);
	  macros.push_back (string (optarg));
	  break;

	case 'S':
          assert_regexp_match ("-S parameter", optarg, "^[0-9]+(,[0-9]+)?$");
	  server_args.push_back (string ("-") + (char)grc + optarg);
	  size_option = string (optarg);
	  break;

	case 'q':
          if (client_options) { cerr << _F("ERROR: %s invalid with %s", "-q", "--client-options") << endl; return 1; } 
	  server_args.push_back (string ("-") + (char)grc);
	  tapset_compile_coverage = true;
	  break;

        case 'h':
          usage (0);
          break;

        case 'L':
          listing_mode_vars = true;
          unoptimized = true; // This causes retention of variables for listing_mode
          // fallthrough
        case 'l':
	  suppress_warnings = true;
          listing_mode = true;
          last_pass = 2;
          if (have_script)
            {
	      cerr << _("Only one script can be given on the command line.")
		   << endl;
	      return 1;
            }
	  server_args.push_back (string ("-") + (char)grc + optarg);
          cmdline_script = string("probe ") + string(optarg) + " {}";
          have_script = true;
          break;

        case 'F':
	  server_args.push_back (string ("-") + (char)grc);
          load_only = true;
	  break;

	case 'B':
          if (client_options) { cerr << _F("ERROR: %s invalid with %s", "-B", "--client-options") << endl; return 1; } 
	  server_args.push_back (string ("-") + (char)grc + optarg);
          kbuildflags.push_back (string (optarg));
	  break;

	case LONG_OPT_VERSION:
	  version ();
	  exit (0);
	  break;

	case LONG_OPT_KELF:
	  server_args.push_back ("--kelf");
	  consult_symtab = true;
	  break;

	case LONG_OPT_KMAP:
	  // Leave consult_symtab unset for now, to ease error checking.
	  if (!kernel_symtab_path.empty())
	    {
	      cerr << _("You can't specify multiple --kmap options.") << endl;
	      return 1;
	    }
	  if (optarg) {
	    kernel_symtab_path = optarg;
	    server_args.push_back ("--kmap=" + string(optarg));
	  }
	  else {
	    kernel_symtab_path = PATH_TBD;
	    server_args.push_back ("--kmap");
	  }
	  break;

	case LONG_OPT_IGNORE_VMLINUX:
	  server_args.push_back ("--ignore-vmlinux");
	  ignore_vmlinux = true;
	  break;

	case LONG_OPT_IGNORE_DWARF:
	  server_args.push_back ("--ignore-dwarf");
	  ignore_dwarf = true;
	  break;

	case LONG_OPT_VERBOSE_PASS:
	  {
	    bool ok = true;
	    if (strlen(optarg) < 1 || strlen(optarg) > 5)
	      ok = false;
	    if (ok)
	      for (unsigned i=0; i<strlen(optarg); i++)
		if (isdigit (optarg[i]))
		  perpass_verbose[i] += optarg[i]-'0';
		else
		  ok = false;
                
	    if (! ok)
	      {
		cerr << _("Invalid --vp argument: it takes 1 to 5 digits.") << endl;
		return 1;
	      }
	    // NB: we don't do this: last_pass = strlen(optarg);
	    server_args.push_back ("--vp=" + string(optarg));
	    break;
	  }

	case LONG_OPT_SKIP_BADVARS:
	  server_args.push_back ("--skip-badvars");
	  skip_badvars = true;
	  break;

	case LONG_OPT_PRIVILEGE:
	  {
	    // We allow only multiple privilege-setting options if they all specify the same
	    // privilege level. The server also expects and depends on this behaviour when
	    // examining the client-side options passed to it.
	    privilege_t newPrivilege;
	    if (strcmp (optarg, "stapdev") == 0)
	      newPrivilege = pr_stapdev;
	    else if (strcmp (optarg, "stapsys") == 0)
	      newPrivilege = pr_stapsys;
	    else if (strcmp (optarg, "stapusr") == 0)
	      newPrivilege = pr_stapusr;
	    else
	      {
		cerr << _F("Invalid argument '%s' for --privilege.", optarg) << endl;
		return 1;
	      }
	    if (privilege_set && newPrivilege != privilege)
	      {
		cerr << _("Privilege level may be set only once.") << endl;
		return 1;
	      }
	    privilege = newPrivilege;
	    privilege_set = true;
	    server_args.push_back ("--privilege=" + string(optarg));
	  }
	  /* NB: for server security, it is essential that once this flag is
	     set, no future flag be able to unset it. */
	  break;

	case LONG_OPT_UNPRIVILEGED:
	  // We allow only multiple privilege-setting options if they all specify the same
	  // privilege level. The server also expects and depends on this behaviour when
	  // examining the client-side options passed to it.
	  if (privilege_set && pr_unprivileged != privilege)
	    {
	      cerr << _("Privilege level may be set only once.") << endl;
	      return 1;
	    }
	  privilege = pr_unprivileged;
	  privilege_set = true;
	  server_args.push_back ("--unprivileged");
	  /* NB: for server security, it is essential that once this flag is
	     set, no future flag be able to unset it. */
	  break;

	case LONG_OPT_OMIT_WERROR:
	  server_args.push_back (OMIT_WERROR_NAME);
	  omit_werror = true;
	  break;

	case LONG_OPT_CLIENT_OPTIONS:
	  client_options = true;
	  break;

	case LONG_OPT_TMPDIR:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--tmpdir", "--client-options") << endl;
	    return 1;
	  }
	  tmpdir_opt_set = true;
	  tmpdir = optarg;
	  break;

	case LONG_OPT_DOWNLOAD_DEBUGINFO:
	  if(optarg)
	    {
	      if(strcmp(optarg, "no") == 0)
		download_dbinfo = 0; //Disable feature
	      else if (strcmp(optarg, "yes") == 0)
		download_dbinfo = INT_MAX; //Enable, No Timeout
	      /* NOTE: Timeout and Asking for Confirmation features below are not supported yet by abrt
	       * in version abrt-2.0.3-1.fc15.x86_64, Bugzilla: BZ730107 (timeout), BZ726192 ('-y') */
	      else if(atoi(optarg) > 0)
		download_dbinfo = atoi(optarg); //Enable, Set timeout to optarg
	      else if (strcmp(optarg, "ask") == 0)
		download_dbinfo = -1; //Enable, Ask for confirmation
	      else
		{
		  cerr << _F("ERROR: %s is not a valid value. Use 'yes', 'no', 'ask' or a timeout value.", optarg) << endl;
		  return 1;
		}
	    }
	  else
	    download_dbinfo = INT_MAX; //Enable, No Timeout
	  break;

	case LONG_OPT_USE_SERVER:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--use-server", "--client-options") << endl;
	    return 1;
	  }
	  if (optarg)
	    specified_servers.push_back (optarg);
	  else
	    specified_servers.push_back ("");
	  break;

	case LONG_OPT_USE_SERVER_ON_ERROR:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--use-server-on-error", "--client-options") << endl;
	    return 1;
	  }
	  if (optarg)
	    {
	      string arg = optarg;
	      for (unsigned i = 0; i < arg.size (); ++i)
		arg[i] = tolower (arg[i]);
	      if (arg == "yes" || arg == "ye" || arg == "y")
		use_server_on_error = true;
	      else if (arg == "no" || arg == "n")
		use_server_on_error = false;
	      else
		cerr << _F("Invalid argument '%s' for --use-server-on-error.", optarg) << endl;
	    }
	  else
	    use_server_on_error = true;
	  break;

	case LONG_OPT_LIST_SERVERS:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--list-servers", "--client-options") << endl;
	    return 1;
	  }
	  if (optarg)
	    server_status_strings.push_back (optarg);
	  else
	    server_status_strings.push_back ("");
	  break;

	case LONG_OPT_TRUST_SERVERS:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--trust-servers", "--client-options") << endl;
	    return 1;
	  }
	  if (optarg)
	    server_trust_spec = optarg;
	  else
	    server_trust_spec = "ssl";
	  break;

	case LONG_OPT_HELP:
	  usage (0);
	  break;

	  // The caching options should not be available to server clients
	case LONG_OPT_DISABLE_CACHE:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--disable-cache", "--client-options") << endl;
	    return 1;
	  }
	  use_cache = use_script_cache = false;
	  break;

	case LONG_OPT_POISON_CACHE:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--poison-cache", "--client-options") << endl;
	    return 1;
	  }
	  poison_cache = true;
	  break;

	case LONG_OPT_CLEAN_CACHE:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--clean-cache", "--client-options") << endl;
	    return 1;
	  }
	  clean_cache(*this);
	  exit(0);

	case LONG_OPT_COMPATIBLE:
	  server_args.push_back ("--compatible=" + string(optarg));
	  compatible = optarg;
	  break;

	case LONG_OPT_LDD:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--ldd", "--client-options") << endl;
	    return 1;
	  }
	  unwindsym_ldd = true;
	  break;

	case LONG_OPT_ALL_MODULES:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--all-modules", "--client-options") << endl;
	    return 1;
	  }
	  insert_loaded_modules();
	  break;

	case LONG_OPT_REMOTE:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--remote", "--client-options") << endl;
	    return 1;
	  }

	  remote_uris.push_back(optarg);
	  break;

	case LONG_OPT_REMOTE_PREFIX:
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--remote-prefix", "--client-options") << endl;
	    return 1;
	  }

	  use_remote_prefix = true;
	  break;

	case LONG_OPT_CHECK_VERSION:
	  server_args.push_back ("--check-version");
	  systemtap_v_check = true;
	  break;

	case LONG_OPT_DUMP_PROBE_TYPES:
	  server_args.push_back ("--dump-probe-types");
	  dump_probe_types = true;
	  break;

	case LONG_OPT_SUPPRESS_HANDLER_ERRORS:
	  suppress_handler_errors = true;
	  break;

	case LONG_OPT_MODINFO:
	  // Make sure the global option is only composed of the
	  // following chars: [_=a-zA-Z0-9]
	  if (client_options) {
	    cerr << _F("ERROR: %s is invalid with %s", "--modinfo", "--client-options") << endl;
	    return 1;
	  }
	  assert_regexp_match("--modinfo parameter", optarg, "^[a-z_][a-z0-9_]*=.+$");
	  modinfos.push_back (string(optarg));
	  break;

	case LONG_OPT_RLIMIT_AS:
	  if(getrlimit(RLIMIT_AS, & our_rlimit))
	    cerr << _F("Unable to obtain resource limits for rlimit_as : %s", strerror (errno)) << endl;
	  our_rlimit.rlim_max = our_rlimit.rlim_cur = strtoul (optarg, &num_endptr, 0);
	  if(*num_endptr || setrlimit (RLIMIT_AS, & our_rlimit))
	    cerr << _F("Unable to set resource limits for rlimit_as : %s", strerror (errno)) << endl;
          /* Disable core dumps, since exhaustion results in uncaught bad_alloc etc. exceptions */
	  our_rlimit.rlim_max = our_rlimit.rlim_cur = 0;
	  (void) setrlimit (RLIMIT_CORE, & our_rlimit);
	  break;

	case LONG_OPT_RLIMIT_CPU:
	  if(getrlimit(RLIMIT_CPU, & our_rlimit))
	    cerr << _F("Unable to obtain resource limits for rlimit_cpu : %s", strerror (errno)) << endl;
	  our_rlimit.rlim_max = our_rlimit.rlim_cur = strtoul (optarg, &num_endptr, 0);
	  if(*num_endptr || setrlimit (RLIMIT_CPU, & our_rlimit))
	    cerr << _F("Unable to set resource limits for rlimit_cpu : %s", strerror (errno)) << endl;
	  break;

	case LONG_OPT_RLIMIT_NPROC:
	  if(getrlimit(RLIMIT_NPROC, & our_rlimit))
	    cerr << _F("Unable to obtain resource limits for rlimit_nproc : %s", strerror (errno)) << endl;
	  our_rlimit.rlim_max = our_rlimit.rlim_cur = strtoul (optarg, &num_endptr, 0);
	  if(*num_endptr || setrlimit (RLIMIT_NPROC, & our_rlimit))
	    cerr << _F("Unable to set resource limits for rlimit_nproc : %s", strerror (errno)) << endl;
	  break;

	case LONG_OPT_RLIMIT_STACK:
	  if(getrlimit(RLIMIT_STACK, & our_rlimit))
	    cerr << _F("Unable to obtain resource limits for rlimit_stack : %s", strerror (errno)) << endl;
	  our_rlimit.rlim_max = our_rlimit.rlim_cur = strtoul (optarg, &num_endptr, 0);
	  if(*num_endptr || setrlimit (RLIMIT_STACK, & our_rlimit))
	    cerr << _F("Unable to set resource limits for rlimit_stack : %s", strerror (errno)) << endl;
          /* Disable core dumps, since exhaustion results in SIGSEGV */
	  our_rlimit.rlim_max = our_rlimit.rlim_cur = 0;
	  (void) setrlimit (RLIMIT_CORE, & our_rlimit);
	  break;

	case LONG_OPT_RLIMIT_FSIZE:
	  if(getrlimit(RLIMIT_FSIZE, & our_rlimit))
	    cerr << _F("Unable to obtain resource limits for rlimit_fsize : %s", strerror (errno)) << endl;
	  our_rlimit.rlim_max = our_rlimit.rlim_cur = strtoul (optarg, &num_endptr, 0);
	  if(*num_endptr || setrlimit (RLIMIT_FSIZE, & our_rlimit))
	    cerr << _F("Unable to set resource limits for rlimit_fsize : %s", strerror (errno)) << endl;
	  break;

	case LONG_OPT_SYSROOT:
	  if (client_options) {
	      cerr << _F("ERROR: %s invalid with %s", "--sysroot", "--client-options") << endl;
	      return 1;
	  } else if (!sysroot.empty()) {
	      cerr << "ERROR: multiple --sysroot options not supported" << endl;
	      return 1;
	  } else {
	      const char *spath = canonicalize_file_name (optarg);
	      if (spath == NULL) {
		  cerr << _F("ERROR: %s is an invalid directory for --sysroot", optarg) << endl;
		  return 1;
	      }

	      sysroot = string(spath);
	      if (sysroot[sysroot.size() - 1] != '/')
		  sysroot.append("/");

	      break;
	  }

	case LONG_OPT_SYSENV:
	  if (client_options) {
	      cerr << _F("ERROR: %s invalid with %s", "--sysenv", "--client-options") << endl;
	      return 1;
	  } else {
	      string sysenv_str = optarg;
	      string value;
	      size_t pos;
	      if (sysroot.empty()) {
		  cerr << "ERROR: --sysenv must follow --sysroot" << endl;
		  return 1;
	      }

	      pos = sysenv_str.find("=");
	      if (pos == string::npos) {
		  cerr << _F("ERROR: %s is an invalid argument for --sysenv", optarg) << endl;
		  return 1;
	      }

	      value = sysenv_str.substr(pos + 1);
	      sysenv[sysenv_str.substr(0, pos)] = value;

	      break;
	  }

	case '?':
	  // Invalid/unrecognized option given or argument required, but
	  // not given. In both cases getopt_long() will have printed the
	  // appropriate error message to stderr already.
	  return 1;
	  break;

        default:
          // NOTREACHED unless one added a getopt option but not a corresponding case:
          cerr << _F("Unhandled argument code %d", (char)grc) << endl;
          return 1;
          break;
        }
    }

  return 0;
}

void
systemtap_session::check_options (int argc, char * const argv [])
{
  for (int i = optind; i < argc; i++)
    {
      if (! have_script)
        {
          script_file = string (argv[i]);
          have_script = true;
        }
      else
        args.push_back (string (argv[i]));
    }

  // need a user file
  // NB: this is also triggered if stap is invoked with no arguments at all
  if (! have_script)
    {
      // We don't need a script if --list-servers, --trust-servers or --dump-probe-types was
      // specified.
      if (server_status_strings.empty () && server_trust_spec.empty () && ! dump_probe_types)
	{
	  cerr << _("A script must be specified.") << endl;
	  usage(1);
	}
    }

#if ! HAVE_NSS
  if (client_options)
    print_warning("--client-options is not supported by this version of systemtap");

  if (! server_status_strings.empty ())
    {
      print_warning("--list-servers is not supported by this version of systemtap");
      server_status_strings.clear ();
    }

  if (! server_trust_spec.empty ())
    {
      print_warning("--trust-servers is not supported by this version of systemtap");
      server_trust_spec.clear ();
    }
#endif

  if (runtime_specified && ! specified_servers.empty ())
    {
      print_warning("Ignoring --use-server due to the use of -R");
      specified_servers.clear ();
    }

  if (client_options && last_pass > 4)
    {
      last_pass = 4; /* Quietly downgrade.  Server passed through -p5 naively. */
    }

  // If phase 5 has been requested, automatically adjust the --privilege setting to match the
  // user's actual privilege level and add --use-server, if necessary.
  // Do this only if we have a script and we are not the server.
  // XXX Eventually we could check remote hosts, but disable that case for now.
  if (last_pass > 4 && have_script && ! client_options && remote_uris.empty())
    {
      // What is the user's privilege level?
      privilege_t credentials = get_privilege_credentials ();
      // Don't alter specifically-requested privilege levels
      if (! privilege_set && ! pr_contains (credentials, privilege))
	{
	  // We do not have the default privilege credentials (stapdev). Lower
	  // the privilege level to match our credentials.
	  if (pr_contains (credentials, pr_stapsys))
	    {
	      if (perpass_verbose[0] > 1)
		cerr << _("Using --privilege=stapsys for member of the group stapsys") << endl;
	      privilege = pr_stapsys;
	      server_args.push_back ("--privilege=stapsys");
	    }
	  else if (pr_contains (credentials, pr_stapusr))
	    {
	      if (perpass_verbose[0] > 1)
		cerr << _("Using --privilege=stapusr for member of the group stapusr") << endl;
	      privilege = pr_stapusr;
	      server_args.push_back ("--privilege=stapusr");
	    }
	  else
	    {
	      // Completely unprivileged user.
	      cerr << _("You are trying to run systemtap as a normal user.\n"
			"You should either be root, or be part of "
			"the group \"stapusr\" and possibly one of the groups \"stapsys\" or \"stapdev\".\n");
	      usage (1); // does not return.
	    }
	}
      // Add --use-server if not already specified and the user's (lack of) credentials require
      // it for pass 5.
      if (! pr_contains (credentials, pr_stapdev))
	{
	  if (specified_servers.empty ())
	    {
	      if (perpass_verbose[0] > 1)
		cerr << _F("Using --use-server for user with privilege level %s",
			   pr_name (privilege))
		     << endl;
	      specified_servers.push_back ("");
	    }
	}
    }

  if (client_options && ! pr_contains (privilege, pr_stapdev) && ! client_options_disallowed_for_unprivileged.empty ())
    {
      cerr << _F("You can't specify %s when --privilege=%s is specified.",
                 client_options_disallowed_for_unprivileged.c_str(),
		 pr_name (privilege))
	   << endl;
      usage (1);
    }
  if ((cmd != "") && (target_pid))
    {
      cerr << _F("You can't specify %s and %s together.", "-c", "-x") << endl;
      usage (1);
    }
  if (! pr_contains (privilege, pr_stapdev) && guru_mode)
    {
      cerr << _F("You can't specify %s and --privilege=%s together.", "-g", pr_name (privilege))
	   << endl;
      usage (1);
    }
  if (!kernel_symtab_path.empty())
    {
      if (consult_symtab)
      {
        cerr << _F("You can't specify %s and %s together.", "--kelf", "--kmap") << endl;
        usage (1);
      }
      consult_symtab = true;
      if (kernel_symtab_path == PATH_TBD)
        kernel_symtab_path = string("/boot/System.map-") + kernel_release;
    }
  // Can't use --remote and --tmpdir together because with --remote,
  // there may be more than one tmpdir needed.
  if (!remote_uris.empty() && tmpdir_opt_set)
    {
      cerr << _F("You can't specify %s and %s together.", "--remote", "--tmpdir") << endl;
      usage(1);
    }
  // Warn in case the target kernel release doesn't match the running one.
  native_build = (release == kernel_release &&
                  machine == architecture); // NB: squashed ARCH by PR4186 logic

  // Non-native builds can't be loaded locally, but may still work on remotes
  if (last_pass > 4 && !native_build && remote_uris.empty())
    {
      print_warning("kernel release/architecture mismatch with host forces last-pass 4.");
      last_pass = 4;
    }
  if(download_dbinfo != 0 && access ("/usr/bin/abrt-action-install-debuginfo-to-abrt-cache", X_OK) < 0
                          && access ("/usr/libexec/abrt-action-install-debuginfo-to-abrt-cache", X_OK) < 0)
    {
      print_warning("abrt-action-install-debuginfo-to-abrt-cache is not installed. Continuing without downloading debuginfo.");
      download_dbinfo = 0;
    }

  // translate path of runtime to absolute path
  if (runtime_path[0] != '/')
    {
      char cwd[PATH_MAX];
      if (getcwd(cwd, sizeof(cwd)))
        {
          runtime_path = string(cwd) + "/" + runtime_path;
        }
    }

  // Abnormal characters in our temp path can break us, including parts out
  // of our control like Kbuild.  Let's enforce nice, safe characters only.
  const char *tmpdir = getenv("TMPDIR");
  if (tmpdir != NULL)
    assert_regexp_match("TMPDIR", tmpdir, "^[-/._0-9a-z]+$");
}


void
systemtap_session::init_try_server ()
{
#if HAVE_NSS
  // If the option is disabled or we are a server or we are already using a
  // server, then never retry compilation using a server.
  if (! use_server_on_error || client_options || ! specified_servers.empty ())
    try_server_status = dont_try_server;
  else
    try_server_status = try_server_unset;
#else
  // No client, so don't bother.
  try_server_status = dont_try_server;
#endif
}

void
systemtap_session::set_try_server (int t)
{
  if (try_server_status != dont_try_server)
    try_server_status = t;
}


void systemtap_session::insert_loaded_modules()
{
  char line[1024];
  ifstream procmods ("/proc/modules");
  while (procmods.good()) {
    procmods.getline (line, sizeof(line));
    strtok(line, " \t");
    if (line[0] == '\0')
      break;  // maybe print a warning?
    unwindsym_modules.insert (string (line));
  }
  procmods.close();
  unwindsym_modules.insert ("kernel");
}

void
systemtap_session::setup_kernel_release (const char* kstr) 
{
  // Sometimes we may get dupes here... e.g. a server may have a full
  // -r /path/to/kernel followed by a client's -r kernel.
  if (kernel_release == kstr)
    return; // nothing new here...

  kernel_release = kernel_build_tree = kernel_source_tree = "";
  if (kstr[0] == '/') // fully specified path
    {
      kernel_build_tree = kstr;
      kernel_release = kernel_release_from_build_tree (kernel_build_tree, verbose);

      // PR10745
      // Maybe it's a full kernel source tree, for purposes of PR10745.
      // In case CONFIG_DEBUG_INFO was set, we'd find it anyway with the
      // normal search in tapsets.cxx.  Without CONFIG_DEBUG_INFO, we'd
      // need heuristics such as this one:

      string some_random_source_only_file = kernel_build_tree + "/COPYING";
      ifstream epic (some_random_source_only_file.c_str());
      if (! epic.fail())
        {
          kernel_source_tree = kernel_build_tree;
          if (verbose > 2)
            clog << _F("Located kernel source tree (COPYING) at '%s'", kernel_source_tree.c_str()) << endl;
        }
    }
  else
    {
      update_release_sysroot = true;
      kernel_release = string (kstr);
      if (!kernel_release.empty())
        kernel_build_tree = "/lib/modules/" + kernel_release + "/build";

      // PR10745
      // Let's not look for the kernel_source_tree; it's definitely
      // not THERE.  tapsets.cxx might try to find it later if tracepoints
      // need it.
    }
}


// Register all the aliases we've seen in library files, and the user
// file, as patterns.
void
systemtap_session::register_library_aliases()
{
  vector<stapfile*> files(library_files);
  files.push_back(user_file);

  for (unsigned f = 0; f < files.size(); ++f)
    {
      stapfile * file = files[f];
      for (unsigned a = 0; a < file->aliases.size(); ++a)
	{
	  probe_alias * alias = file->aliases[a];
          try
            {
              for (unsigned n = 0; n < alias->alias_names.size(); ++n)
                {
                  probe_point * name = alias->alias_names[n];
                  match_node * mn = pattern_root;
                  for (unsigned c = 0; c < name->components.size(); ++c)
                    {
                      probe_point::component * comp = name->components[c];
                      // XXX: alias parameters
                      if (comp->arg)
                        throw semantic_error(_F("alias component %s contains illegal parameter",
                                                comp->functor.c_str()));
                      mn = mn->bind(comp->functor);
                    }
		  // PR 12916: All probe aliases are OK for all users. The actual
		  // referenced probe points will be checked when the alias is resolved.
		  mn->bind_privilege (pr_all);
                  mn->bind(new alias_expansion_builder(alias));
                }
            }
          catch (const semantic_error& e)
            {
              semantic_error* er = new semantic_error (_("while registering probe alias"),
                                                       alias->tok);
              er->chain = & e;
              print_error (* er);
              delete er;
            }
	}
    }
}


// Print this given token, but abbreviate it if the last one had the
// same file name.
void
systemtap_session::print_token (ostream& o, const token* tok)
{
  assert (tok);

  if (last_token && last_token->location.file == tok->location.file)
    {
      stringstream tmpo;
      tmpo << *tok;
      string ts = tmpo.str();
      // search & replace the file name with nothing
      size_t idx = ts.find (tok->location.file->name);
      if (idx != string::npos)
          ts.replace (idx, tok->location.file->name.size(), "");

      o << ts;
    }
  else
    o << *tok;

  last_token = tok;
}



void
systemtap_session::print_error (const semantic_error& e)
{
  string message_str[2];
  string align_semantic_error ("        ");

  // We generate two messages.  The second one ([1]) is printed
  // without token compression, for purposes of duplicate elimination.
  // This way, the same message that may be generated once with a
  // compressed and once with an uncompressed token still only gets
  // printed once.
  for (int i=0; i<2; i++)
    {
      stringstream message;

      message << _F("semantic error: %s", e.what ());
      if (e.tok1 || e.tok2)
        message << ": ";
      if (e.tok1)
        {
          if (i == 0)
            {
              print_token (message, e.tok1);
              message << endl;
              print_error_source (message, align_semantic_error, e.tok1);
            }
          else message << *e.tok1;
        }
      if (e.tok2)
        {
          if (i == 0)
            {
              print_token (message, e.tok2);
              message << endl;
              print_error_source (message, align_semantic_error, e.tok2);
            }
          else message << *e.tok2;
        }
      message << endl;
      message_str[i] = message.str();
    }

  // Duplicate elimination
  if (seen_errors.find (message_str[1]) == seen_errors.end())
    {
      seen_errors.insert (message_str[1]);
      cerr << message_str[0];
    }

  if (e.chain)
    print_error (* e.chain);
}

void
systemtap_session::print_error_source (std::ostream& message,
                                       std::string& align, const token* tok)
{
  unsigned i = 0;

  assert (tok);
  if (!tok->location.file)
    //No source to print, silently exit
    return;

  unsigned line = tok->location.line;
  unsigned col = tok->location.column;
  const string &file_contents = tok->location.file->file_contents;

  size_t start_pos = 0, end_pos = 0;
  //Navigate to the appropriate line
  while (i != line && end_pos != std::string::npos)
    {
      start_pos = end_pos;
      end_pos = file_contents.find ('\n', start_pos) + 1;
      i++;
    }
  //TRANSLATORS:  Here were are printing the source string of the error
  message << align << _("source: ") << file_contents.substr (start_pos, end_pos-start_pos-1) << endl;
  message << align << "        ";
  //Navigate to the appropriate column
  for (i=start_pos; i<start_pos+col-1; i++)
    {
      if(isspace(file_contents[i]))
	message << file_contents[i];
      else
	message << ' ';
    }
  message << "^" << endl;
}

void
systemtap_session::print_warning (const string& message_str, const token* tok)
{
  if(suppress_warnings)
    return;

  // Duplicate elimination
  string align_warning (" ");
  if (seen_warnings.find (message_str) == seen_warnings.end())
    {
      seen_warnings.insert (message_str);
      clog << _("WARNING: ") << message_str;
      if (tok) { clog << ": "; print_token (clog, tok); }
      clog << endl;
      if (tok) { print_error_source (clog, align_warning, tok); }
    }
}

void
systemtap_session::create_tmp_dir()
{
  if (!tmpdir.empty())
    return;

  // Create the temp directory
  const char * tmpdir_env = getenv("TMPDIR");
  if (!tmpdir_env)
    tmpdir_env = "/tmp";

  string stapdir = "/stapXXXXXX";
  string tmpdirt = tmpdir_env + stapdir;
  const char *tmpdir_name = mkdtemp((char *)tmpdirt.c_str());
  if (! tmpdir_name)
    {
      const char* e = strerror(errno);
      //TRANSLATORS: we can't make the directory due to the error
      throw runtime_error(_F("cannot create temporary directory (\" %s \"): %s", tmpdirt.c_str(), e));
    }
  else
    tmpdir = tmpdir_name;
}

void
systemtap_session::remove_tmp_dir()
{
  if(tmpdir.empty())
    return;

  // Remove temporary directory
  if (keep_tmpdir && !tmpdir_opt_set)
      clog << _F("Keeping temporary directory \"%s\"", tmpdir.c_str()) << endl;
  else if (!tmpdir_opt_set)
    {
      // Mask signals while we're deleting the temporary directory.
      stap_sigmasker masked;

      // Remove the temporary directory.
      vector<string> cleanupcmd;
      cleanupcmd.push_back("rm");
      cleanupcmd.push_back("-rf");
      cleanupcmd.push_back(tmpdir);

      (void) stap_system(verbose, cleanupcmd);
      if (verbose>1)
        clog << _F("Removed temporary directory \"%s\"", tmpdir.c_str()) << endl;
      tmpdir.clear();

    }
}

void
systemtap_session::reset_tmp_dir()
{
  remove_tmp_dir();
  create_tmp_dir();
}

translator_output* systemtap_session::op_create_auxiliary()
{
  static int counter = 0;
  string tmpname = this->tmpdir + "/" + this->module_name + "_aux_" + lex_cast(counter++) + ".c";
  translator_output* n = new translator_output (tmpname);
  auxiliary_outputs.push_back (n);
  return n;
}

// Wrapper for checking if there are pending interrupts
void
assert_no_interrupts() {if(pending_interrupts) throw interrupt_exception();};

// --------------------------------------------------------------------------

/*
Perngrq sebz fzvyrlgnc.fit, rkcbegrq gb n 1484k1110 fzvyrlgnc.cat,
gurapr  catgbcnz | cazfpnyr -jvqgu 160 | 
cczqvgure -qvz 4 -erq 2 -terra 2 -oyhr 2  | cczgbnafv -2k4 | bq -i -j19 -g k1 | 
phg -s2- -q' ' | frq -r 'f,^,\\k,' -r 'f, ,\\k,t' -r 'f,^,",'  -r 'f,$,",'
*/
const char*
systemtap_session::morehelp =
"\x1b\x5b\x30\x6d\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x60\x20\x20\x2e\x60\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x1b\x5b"
"\x33\x33\x6d\x20\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x1b\x5b\x33\x33\x6d\x20\x60"
"\x2e\x60\x1b\x5b\x33\x37\x6d\x20\x3a\x2c\x3a\x2e\x60\x20\x60\x20\x60\x20\x60"
"\x2c\x3b\x2c\x3a\x20\x1b\x5b\x33\x33\x6d\x60\x2e\x60\x20\x1b\x5b\x33\x37\x6d"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x1b\x5b\x33"
"\x33\x6d\x20\x60\x20\x60\x20\x3a\x27\x60\x1b\x5b\x33\x37\x6d\x20\x60\x60\x60"
"\x20\x20\x20\x60\x20\x60\x60\x60\x20\x1b\x5b\x33\x33\x6d\x60\x3a\x60\x20\x60"
"\x20\x60\x20\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x2e\x1b\x5b\x33\x33\x6d\x60\x2e\x60\x20\x60\x20\x60\x20\x20\x1b\x5b\x33"
"\x37\x6d\x20\x3a\x20\x20\x20\x60\x20\x20\x20\x60\x20\x20\x2e\x1b\x5b\x33\x33"
"\x6d\x60\x20\x60\x2e\x60\x20\x60\x2e\x60\x20\x1b\x5b\x33\x37\x6d\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x2e\x3a\x20\x20"
"\x20\x20\x20\x20\x20\x20\x2e\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x2e\x76\x53\x1b\x5b\x33\x34\x6d\x53\x1b\x5b\x33\x37\x6d\x53\x1b\x5b"
"\x33\x31\x6d\x2b\x1b\x5b\x33\x33\x6d\x60\x20\x60\x20\x60\x20\x20\x20\x20\x1b"
"\x5b\x33\x31\x6d\x3f\x1b\x5b\x33\x30\x6d\x53\x1b\x5b\x33\x33\x6d\x2b\x1b\x5b"
"\x33\x37\x6d\x20\x20\x20\x20\x20\x20\x20\x2e\x1b\x5b\x33\x30\x6d\x24\x1b\x5b"
"\x33\x37\x6d\x3b\x1b\x5b\x33\x31\x6d\x7c\x1b\x5b\x33\x33\x6d\x20\x60\x20\x60"
"\x20\x60\x20\x60\x1b\x5b\x33\x31\x6d\x2c\x1b\x5b\x33\x32\x6d\x53\x1b\x5b\x33"
"\x37\x6d\x53\x53\x3e\x2c\x2e\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x2e"
"\x3b\x27\x20\x20\x20\x20\x20\x20\x20\x20\x20\x60\x3c\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x2e\x2e\x3a\x1b\x5b\x33\x30\x6d\x26\x46\x46\x46\x48\x46\x1b\x5b"
"\x33\x33\x6d\x60\x2e\x60\x20\x60\x20\x60\x20\x60\x1b\x5b\x33\x30\x6d\x4d\x4d"
"\x46\x1b\x5b\x33\x33\x6d\x20\x20\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x1b\x5b"
"\x33\x33\x6d\x20\x3a\x1b\x5b\x33\x30\x6d\x4d\x4d\x46\x1b\x5b\x33\x33\x6d\x20"
"\x20\x20\x60\x20\x60\x2e\x60\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x30\x6d\x46"
"\x46\x46\x24\x53\x46\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x20\x0a\x20\x20\x20"
"\x20\x2e\x3c\x3a\x60\x20\x20\x20\x20\x2e\x3a\x2e\x3a\x2e\x2e\x3b\x27\x20\x20"
"\x20\x20\x20\x20\x2e\x60\x2e\x3a\x60\x60\x3c\x27\x1b\x5b\x33\x31\x6d\x3c\x27"
"\x1b\x5b\x33\x33\x6d\x20\x60\x20\x60\x20\x60\x20\x20\x20\x60\x3c\x1b\x5b\x33"
"\x30\x6d\x26\x1b\x5b\x33\x31\x6d\x3f\x1b\x5b\x33\x33\x6d\x20\x1b\x5b\x33\x37"
"\x6d\x20\x1b\x5b\x33\x33\x6d\x20\x20\x20\x20\x20\x1b\x5b\x33\x37\x6d\x60\x1b"
"\x5b\x33\x30\x6d\x2a\x46\x1b\x5b\x33\x37\x6d\x27\x1b\x5b\x33\x33\x6d\x20\x60"
"\x20\x60\x20\x60\x20\x60\x20\x1b\x5b\x33\x31\x6d\x60\x3a\x1b\x5b\x33\x37\x6d"
"\x27\x3c\x1b\x5b\x33\x30\x6d\x23\x1b\x5b\x33\x37\x6d\x3c\x60\x3a\x20\x20\x20"
"\x0a\x20\x20\x20\x20\x3a\x60\x3a\x60\x20\x20\x20\x60\x3a\x2e\x2e\x2e\x2e\x3c"
"\x3c\x20\x20\x20\x20\x20\x20\x3a\x2e\x60\x3a\x60\x20\x20\x20\x60\x1b\x5b\x33"
"\x33\x6d\x3a\x1b\x5b\x33\x31\x6d\x60\x1b\x5b\x33\x33\x6d\x20\x60\x2e\x60\x20"
"\x60\x20\x60\x20\x60\x20\x60\x1b\x5b\x33\x37\x6d\x20\x20\x1b\x5b\x33\x33\x6d"
"\x20\x60\x20\x20\x20\x60\x1b\x5b\x33\x37\x6d\x20\x60\x20\x60\x1b\x5b\x33\x33"
"\x6d\x20\x60\x2e\x60\x20\x60\x2e\x60\x20\x60\x3a\x1b\x5b\x33\x37\x6d\x20\x20"
"\x20\x60\x3a\x2e\x60\x2e\x20\x0a\x20\x20\x20\x60\x3a\x60\x3a\x60\x20\x20\x20"
"\x20\x20\x60\x60\x60\x60\x20\x3a\x2d\x20\x20\x20\x20\x20\x60\x20\x60\x20\x20"
"\x20\x20\x20\x60\x1b\x5b\x33\x33\x6d\x3a\x60\x2e\x60\x20\x60\x20\x60\x20\x60"
"\x20\x60\x20\x20\x2e\x3b\x1b\x5b\x33\x31\x6d\x76\x1b\x5b\x33\x30\x6d\x24\x24"
"\x24\x1b\x5b\x33\x31\x6d\x2b\x53\x1b\x5b\x33\x33\x6d\x2c\x60\x20\x60\x20\x60"
"\x20\x60\x20\x60\x20\x60\x2e\x1b\x5b\x33\x31\x6d\x60\x1b\x5b\x33\x33\x6d\x3a"
"\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x60\x2e\x60\x20\x20\x0a\x20\x20\x20\x60"
"\x3a\x3a\x3a\x3a\x20\x20\x20\x20\x3a\x60\x60\x60\x60\x3a\x53\x20\x20\x20\x20"
"\x20\x20\x3a\x2e\x60\x2e\x20\x20\x20\x20\x20\x1b\x5b\x33\x33\x6d\x3a\x1b\x5b"
"\x33\x31\x6d\x3a\x1b\x5b\x33\x33\x6d\x2e\x60\x2e\x60\x20\x60\x2e\x60\x20\x60"
"\x20\x3a\x1b\x5b\x33\x30\x6d\x24\x46\x46\x48\x46\x46\x46\x46\x46\x1b\x5b\x33"
"\x31\x6d\x53\x1b\x5b\x33\x33\x6d\x2e\x60\x20\x60\x2e\x60\x20\x60\x2e\x60\x2e"
"\x1b\x5b\x33\x31\x6d\x3a\x1b\x5b\x33\x33\x6d\x3a\x1b\x5b\x33\x37\x6d\x20\x20"
"\x20\x2e\x60\x2e\x3a\x20\x20\x0a\x20\x20\x20\x60\x3a\x3a\x3a\x60\x20\x20\x20"
"\x60\x3a\x20\x2e\x20\x3b\x27\x3a\x20\x20\x20\x20\x20\x20\x3a\x2e\x60\x3a\x20"
"\x20\x20\x20\x20\x3a\x1b\x5b\x33\x33\x6d\x3c\x3a\x1b\x5b\x33\x31\x6d\x60\x1b"
"\x5b\x33\x33\x6d\x2e\x60\x20\x60\x20\x60\x20\x60\x2e\x1b\x5b\x33\x30\x6d\x53"
"\x46\x46\x46\x53\x46\x46\x46\x53\x46\x46\x1b\x5b\x33\x33\x6d\x20\x60\x20\x60"
"\x20\x60\x2e\x60\x2e\x60\x3a\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x37\x6d\x20"
"\x20\x20\x20\x3a\x60\x3a\x60\x20\x20\x0a\x20\x20\x20\x20\x60\x3c\x3b\x3c\x20"
"\x20\x20\x20\x20\x60\x60\x60\x20\x3a\x3a\x20\x20\x20\x20\x20\x20\x20\x3a\x3a"
"\x2e\x60\x20\x20\x20\x20\x20\x3a\x1b\x5b\x33\x33\x6d\x3b\x1b\x5b\x33\x31\x6d"
"\x3c\x3a\x60\x1b\x5b\x33\x33\x6d\x2e\x60\x2e\x60\x20\x60\x3a\x1b\x5b\x33\x30"
"\x6d\x53\x46\x53\x46\x46\x46\x53\x46\x46\x46\x53\x1b\x5b\x33\x33\x6d\x2e\x60"
"\x20\x60\x2e\x60\x2e\x60\x3a\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x33\x6d\x3b"
"\x1b\x5b\x33\x37\x6d\x27\x20\x20\x20\x60\x3a\x3a\x60\x20\x20\x20\x0a\x20\x20"
"\x20\x20\x20\x60\x3b\x3c\x20\x20\x20\x20\x20\x20\x20\x3a\x3b\x60\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x60\x3a\x60\x2e\x20\x20\x20\x20\x20\x3a\x1b\x5b\x33"
"\x33\x6d\x3c\x3b\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x33\x6d\x3a\x1b\x5b\x33"
"\x31\x6d\x3a\x1b\x5b\x33\x33\x6d\x2e\x60\x2e\x60\x20\x1b\x5b\x33\x31\x6d\x3a"
"\x1b\x5b\x33\x30\x6d\x46\x53\x46\x53\x46\x53\x46\x53\x46\x1b\x5b\x33\x31\x6d"
"\x3f\x1b\x5b\x33\x33\x6d\x20\x60\x2e\x60\x2e\x3a\x3a\x1b\x5b\x33\x31\x6d\x3c"
"\x1b\x5b\x33\x33\x6d\x3b\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x37\x6d\x60\x20"
"\x20\x20\x3a\x3a\x3a\x60\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x53\x3c"
"\x20\x20\x20\x20\x20\x20\x3a\x53\x3a\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x60\x3a\x3a\x60\x2e\x20\x20\x20\x20\x60\x3a\x1b\x5b\x33\x31\x6d\x3c\x1b"
"\x5b\x33\x33\x6d\x3b\x1b\x5b\x33\x31\x6d\x3c\x3b\x3c\x1b\x5b\x33\x33\x6d\x3a"
"\x60\x2e\x60\x3c\x1b\x5b\x33\x30\x6d\x53\x46\x53\x24\x53\x46\x53\x24\x1b\x5b"
"\x33\x33\x6d\x60\x3a\x1b\x5b\x33\x31\x6d\x3a\x1b\x5b\x33\x33\x6d\x3a\x1b\x5b"
"\x33\x31\x6d\x3a\x3b\x3c\x1b\x5b\x33\x33\x6d\x3b\x1b\x5b\x33\x31\x6d\x3c\x1b"
"\x5b\x33\x33\x6d\x3a\x1b\x5b\x33\x37\x6d\x60\x20\x20\x2e\x60\x3a\x3a\x60\x20"
"\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x3b\x3c\x2e\x2e\x2c\x2e\x2e\x20"
"\x3a\x3c\x3b\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x60\x3a\x3a\x3a"
"\x60\x20\x20\x20\x20\x20\x60\x3a\x1b\x5b\x33\x33\x6d\x3c\x3b\x1b\x5b\x33\x31"
"\x6d\x3c\x3b\x3c\x1b\x5b\x33\x33\x6d\x3b\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33"
"\x33\x6d\x3b\x1b\x5b\x33\x31\x6d\x3c\x3c\x1b\x5b\x33\x30\x6d\x53\x24\x53\x1b"
"\x5b\x33\x31\x6d\x53\x1b\x5b\x33\x37\x6d\x27\x1b\x5b\x33\x33\x6d\x2e\x3a\x3b"
"\x1b\x5b\x33\x31\x6d\x3c\x3b\x3c\x1b\x5b\x33\x33\x6d\x3a\x1b\x5b\x33\x31\x6d"
"\x3c\x1b\x5b\x33\x33\x6d\x3a\x1b\x5b\x33\x37\x6d\x60\x20\x20\x20\x60\x2e\x3a"
"\x3a\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x2e\x3a\x3a\x3c\x53\x3c\x3a\x60"
"\x3a\x3a\x3a\x3a\x53\x1b\x5b\x33\x32\x6d\x53\x1b\x5b\x33\x37\x6d\x3b\x27\x3a"
"\x3c\x2c\x2e\x20\x20\x20\x20\x20\x20\x20\x20\x20\x60\x3a\x3a\x3a\x3a\x2e\x60"
"\x2e\x60\x2e\x60\x3a\x1b\x5b\x33\x33\x6d\x3c\x3a\x1b\x5b\x33\x31\x6d\x3c\x1b"
"\x5b\x33\x33\x6d\x53\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x33\x6d\x3b\x1b\x5b"
"\x33\x31\x6d\x3c\x2c\x1b\x5b\x33\x33\x6d\x3c\x3b\x3a\x1b\x5b\x33\x31\x6d\x2c"
"\x1b\x5b\x33\x33\x6d\x3c\x3b\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x33\x6d\x53"
"\x1b\x5b\x33\x31\x6d\x3c\x1b\x5b\x33\x33\x6d\x3b\x3c\x1b\x5b\x33\x37\x6d\x3a"
"\x60\x2e\x60\x2e\x3b\x1b\x5b\x33\x34\x6d\x53\x1b\x5b\x33\x37\x6d\x53\x3f\x27"
"\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x2e\x60\x3a\x60\x3a\x3c\x53\x53\x3b\x3c"
"\x3a\x60\x3a\x3a\x53\x53\x53\x3c\x3a\x60\x3a\x1b\x5b\x33\x30\x6d\x53\x1b\x5b"
"\x33\x37\x6d\x2b\x20\x20\x20\x20\x20\x20\x60\x20\x20\x20\x3a\x1b\x5b\x33\x34"
"\x6d\x53\x1b\x5b\x33\x30\x6d\x53\x46\x24\x1b\x5b\x33\x37\x6d\x2c\x60\x3a\x3a"
"\x3a\x3c\x3a\x3c\x1b\x5b\x33\x33\x6d\x53\x1b\x5b\x33\x37\x6d\x3c\x1b\x5b\x33"
"\x33\x6d\x53\x1b\x5b\x33\x31\x6d\x53\x1b\x5b\x33\x33\x6d\x3b\x1b\x5b\x33\x31"
"\x6d\x53\x3b\x53\x1b\x5b\x33\x33\x6d\x3b\x1b\x5b\x33\x31\x6d\x53\x1b\x5b\x33"
"\x33\x6d\x53\x1b\x5b\x33\x37\x6d\x3c\x1b\x5b\x33\x33\x6d\x53\x1b\x5b\x33\x37"
"\x6d\x3c\x53\x3c\x3a\x3a\x3a\x3a\x3f\x1b\x5b\x33\x30\x6d\x53\x24\x48\x1b\x5b"
"\x33\x37\x6d\x27\x60\x20\x60\x20\x20\x20\x20\x20\x20\x0a\x2e\x60\x3a\x60\x2e"
"\x60\x3a\x60\x2e\x60\x3a\x60\x2e\x60\x3a\x60\x2e\x60\x3a\x60\x2e\x1b\x5b\x33"
"\x30\x6d\x53\x46\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x60\x20\x20\x20\x60\x20"
"\x60\x3a\x1b\x5b\x33\x30\x6d\x3c\x46\x46\x46\x1b\x5b\x33\x37\x6d\x3f\x2e\x60"
"\x3a\x60\x3a\x60\x3a\x60\x3a\x60\x3a\x3c\x3a\x60\x3a\x27\x3a\x60\x3a\x60\x3a"
"\x60\x3a\x60\x3b\x1b\x5b\x33\x30\x6d\x53\x46\x48\x46\x1b\x5b\x33\x37\x6d\x27"
"\x20\x60\x20\x60\x20\x60\x20\x20\x20\x20\x0a\x20\x3c\x3b\x3a\x2e\x60\x20\x60"
"\x2e\x60\x20\x60\x2e\x60\x20\x60\x2e\x60\x2c\x53\x1b\x5b\x33\x32\x6d\x53\x1b"
"\x5b\x33\x30\x6d\x53\x1b\x5b\x33\x37\x6d\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x60\x20\x60\x3c\x1b\x5b\x33\x30\x6d\x46\x46\x46\x1b\x5b\x33\x34\x6d"
"\x2b\x1b\x5b\x33\x37\x6d\x3a\x20\x60\x20\x60\x20\x60\x2e\x60\x20\x60\x2e\x60"
"\x20\x60\x2e\x60\x20\x60\x20\x60\x2c\x1b\x5b\x33\x30\x6d\x24\x46\x48\x46\x1b"
"\x5b\x33\x37\x6d\x27\x20\x60\x20\x20\x20\x60\x20\x20\x20\x20\x20\x20\x0a\x20"
"\x60\x3a\x1b\x5b\x33\x30\x6d\x53\x24\x1b\x5b\x33\x37\x6d\x53\x53\x53\x3b\x3c"
"\x2c\x60\x2c\x3b\x3b\x53\x3f\x53\x1b\x5b\x33\x30\x6d\x24\x46\x3c\x1b\x5b\x33"
"\x37\x6d\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x60\x20\x60"
"\x3c\x1b\x5b\x33\x30\x6d\x48\x46\x46\x46\x1b\x5b\x33\x37\x6d\x3f\x2e\x60\x20"
"\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x3b\x76\x1b\x5b\x33\x30\x6d"
"\x48\x46\x48\x46\x1b\x5b\x33\x37\x6d\x27\x20\x60\x20\x20\x20\x60\x20\x20\x20"
"\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x60\x3c\x1b\x5b\x33\x30\x6d\x46\x24\x1b"
"\x5b\x33\x37\x6d\x53\x53\x53\x53\x53\x53\x1b\x5b\x33\x30\x6d\x53\x24\x53\x46"
"\x46\x46\x1b\x5b\x33\x37\x6d\x27\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x60\x3c\x1b\x5b\x33\x30\x6d\x23\x46\x46\x46"
"\x24\x1b\x5b\x33\x37\x6d\x76\x2c\x2c\x20\x2e\x20\x2e\x20\x2c\x2c\x76\x1b\x5b"
"\x33\x30\x6d\x26\x24\x46\x46\x48\x3c\x1b\x5b\x33\x37\x6d\x27\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x60"
"\x3c\x1b\x5b\x33\x30\x6d\x53\x46\x46\x24\x46\x24\x46\x46\x48\x46\x53\x1b\x5b"
"\x33\x37\x6d\x20\x20\x20\x20\x20\x20\x20\x20\x2e\x60\x20\x60\x2e\x60\x2e\x60"
"\x2e\x60\x2e\x60\x3a\x3a\x3a\x3a\x3a\x1b\x5b\x33\x30\x6d\x2a\x46\x46\x46\x48"
"\x46\x48\x46\x48\x46\x46\x46\x48\x46\x48\x46\x48\x1b\x5b\x33\x37\x6d\x3c\x22"
"\x2e\x60\x2e\x60\x2e\x60\x2e\x60\x2e\x60\x20\x20\x20\x20\x20\x20\x20\x20\x0a"
"\x20\x20\x20\x20\x20\x20\x20\x60\x3a\x1b\x5b\x33\x30\x6d\x48\x46\x46\x46\x48"
"\x46\x46\x46\x1b\x5b\x33\x37\x6d\x27\x20\x20\x20\x60\x20\x60\x2e\x60\x20\x60"
"\x2e\x60\x2e\x60\x3a\x60\x3a\x60\x3a\x60\x3a\x60\x3a\x3a\x3a\x60\x3a\x3c\x3c"
"\x1b\x5b\x33\x30\x6d\x3c\x46\x48\x46\x46\x46\x48\x46\x46\x46\x1b\x5b\x33\x37"
"\x6d\x27\x3a\x60\x3a\x60\x3a\x60\x3a\x60\x2e\x60\x2e\x60\x20\x60\x2e\x60\x20"
"\x60\x20\x60\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x20\x20\x20\x60\x22\x1b\x5b"
"\x33\x30\x6d\x2a\x46\x48\x46\x1b\x5b\x33\x37\x6d\x3f\x20\x20\x20\x60\x20\x60"
"\x2e\x60\x20\x60\x2e\x60\x2e\x60\x3a\x60\x2e\x60\x3a\x60\x3a\x60\x3a\x60\x3a"
"\x60\x3a\x60\x3a\x60\x3a\x60\x3a\x1b\x5b\x33\x30\x6d\x46\x46\x48\x46\x48\x46"
"\x1b\x5b\x33\x37\x6d\x27\x3a\x60\x3a\x60\x3a\x60\x3a\x60\x2e\x60\x3a\x60\x2e"
"\x60\x2e\x60\x20\x60\x2e\x60\x20\x60\x20\x60\x0a\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x60\x3c\x1b\x5b\x33\x30\x6d\x48\x46\x46\x1b\x5b\x33\x37\x6d"
"\x2b\x60\x20\x20\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x2e\x60\x20"
"\x60\x2e\x60\x20\x60\x2e\x60\x20\x60\x3a\x60\x2e\x60\x3b\x1b\x5b\x33\x30\x6d"
"\x48\x46\x46\x46\x1b\x5b\x33\x37\x6d\x27\x2e\x60\x2e\x60\x20\x60\x2e\x60\x20"
"\x60\x2e\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x20\x20\x60\x20\x20\x0a\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x22\x1b\x5b\x33\x30\x6d\x3c"
"\x48\x46\x53\x1b\x5b\x33\x37\x6d\x2b\x3a\x20\x20\x20\x60\x20\x60\x20\x60\x20"
"\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x2c\x1b"
"\x5b\x33\x30\x6d\x24\x46\x48\x46\x1b\x5b\x33\x37\x6d\x3f\x20\x60\x20\x60\x20"
"\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x60\x20\x20\x20\x60"
"\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x60\x22\x3c\x1b\x5b\x33\x30\x6d\x48\x24\x46\x46\x1b\x5b\x33\x37\x6d\x3e\x2c"
"\x2e\x2e\x20\x20\x20\x20\x20\x20\x20\x20\x60\x20\x20\x20\x60\x20\x20\x20\x3b"
"\x2c\x2c\x1b\x5b\x33\x30\x6d\x24\x53\x46\x46\x46\x1b\x5b\x33\x37\x6d\x27\x22"
"\x20\x20\x60\x20\x20\x20\x60\x20\x20\x20\x60\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x60\x22\x1b\x5b\x33\x30\x6d\x2a\x3c\x48"
"\x46\x46\x24\x53\x24\x1b\x5b\x33\x37\x6d\x53\x53\x53\x3e\x3e\x3e\x3e\x3e\x53"
"\x3e\x53\x1b\x5b\x33\x30\x6d\x24\x53\x24\x46\x24\x48\x46\x23\x1b\x5b\x33\x37"
"\x6d\x27\x22\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x60\x60\x22\x3c\x1b\x5b\x33\x30\x6d\x2a\x3c\x3c\x3c\x48\x46\x46\x46\x48\x46"
"\x46\x46\x23\x3c\x1b\x5b\x33\x36\x6d\x3c\x1b\x5b\x33\x37\x6d\x3c\x27\x22\x22"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
"\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x0a\x1b"
                                                                "\x5b\x30\x6d";

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
