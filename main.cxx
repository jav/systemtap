// systemtap translator/driver
// Copyright (C) 2005-2008 Red Hat Inc.
// Copyright (C) 2005 IBM Corp.
// Copyright (C) 2006 Intel Corporation.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "parse.h"
#include "elaborate.h"
#include "translate.h"
#include "buildrun.h"
#include "session.h"
#include "hash.h"
#include "cache.h"
#include "util.h"
#include "coveragedb.h"
#include "git_version.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstdlib>
#include <limits.h>

extern "C" {
#include <glob.h>
#include <unistd.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <elfutils/libdwfl.h>
#include <getopt.h>
}

using namespace std;


void
version ()
{
  clog
    << "SystemTap translator/driver "
    << "(version " << VERSION << "/" << dwfl_version (NULL)
    << " " << GIT_MESSAGE << ")" << endl
    << "Copyright (C) 2005-2008 Red Hat, Inc. and others" << endl
    << "This is free software; see the source for copying conditions." << endl;
}

void
usage (systemtap_session& s, int exitcode)
{
  version ();
  clog
    << endl
    << "Usage: stap [options] FILE         Run script in file."
    << endl
    << "   or: stap [options] -            Run script on stdin."
    << endl
    << "   or: stap [options] -e SCRIPT    Run given script."
    << endl
    << "   or: stap [options] -l PROBE     List matching probes."
    << endl
    << endl
    << "Options:" << endl
    << "   --         end of translator options, script options follow" << endl
    << "   -v         increase verbosity [" << s.verbose << "]" << endl
    << "   -h         show help" << endl
    << "   -V         show version" << endl
    << "   -k         keep temporary directory" << endl
    << "   -u         unoptimized translation" << (s.unoptimized ? " [set]" : "") << endl
    << "   -w         suppress warnings" << (s.suppress_warnings ? " [set]" : "") << endl
    << "   -g         guru mode" << (s.guru_mode ? " [set]" : "") << endl
    << "   -P         prologue-searching for function probes" 
    << (s.prologue_searching ? " [set]" : "") << endl
    << "   -b         bulk (percpu file) mode" << (s.bulk_mode ? " [set]" : "") << endl
    << "   -s NUM     buffer size in megabytes, instead of "
    << s.buffer_size << endl
    << "   -p NUM     stop after pass NUM 1-5, instead of "
    << s.last_pass << endl
    << "              (parse, elaborate, translate, compile, run)" << endl
    << "   -I DIR     look in DIR for additional .stp script files";
  if (s.include_path.size() == 0)
    clog << endl;
  else
    clog << ", in addition to" << endl;
  for (unsigned i=0; i<s.include_path.size(); i++)
    clog << "              " << s.include_path[i] << endl;
  clog
    << "   -D NM=VAL  emit macro definition into generated C code" << endl
    << "   -R DIR     look in DIR for runtime, instead of" << endl
    <<      "              " << s.runtime_path << endl
    << "   -r RELEASE cross-compile to kernel RELEASE, instead of "
    << s.kernel_release << endl
    << "   -m MODULE  set probe module name, instead of "
    << s.module_name << endl
    << "   -o FILE    send output to file, instead of stdout" << endl
    << "   -c CMD     start the probes, run CMD, and exit when it finishes"
    << endl
    << "   -x PID     sets target() to PID" << endl
    << "   -d OBJECT  add unwind/symbol data for OBJECT file";
  if (s.unwindsym_modules.size() == 0)
    clog << endl;
  else
    clog << ", in addition to" << endl;
  {
    vector<string> syms (s.unwindsym_modules.begin(), s.unwindsym_modules.end());
    for (unsigned i=0; i<syms.size(); i++)
      clog << "              " << syms[i] << endl;
  }
  clog
    << "   -t         collect probe timing information" << endl
#ifdef HAVE_LIBSQLITE3
    << "   -q         generate information on tapset coverage" << endl
#endif /* HAVE_LIBSQLITE3 */
    << "   --kelf     make do with symbol table from vmlinux" << endl
    << "   --kmap[=FILE]" << endl
    << "              make do with symbol table from nm listing" << endl
    << "   --ignore-vmlinux" << endl
    << "              for testing, pretend vmlinux can't be found" << endl
    << "   --ignore-dwarf" << endl
    << "              for testing, pretend vmlinux and modules lack debug info"
    << endl
    ;
  // -d: dump safety-related external references

  exit (exitcode);
}


static void
printscript(systemtap_session& s, ostream& o)
{
  if (s.listing_mode)
    {
      // We go through some heroic measures to produce clean output.
      set<string> seen;

      for (unsigned i=0; i<s.probes.size(); i++)
        {
          derived_probe* p = s.probes[i];
          // NB: p->basest() is not so interesting;
          // p->almost_basest() doesn't quite work, so ...
          vector<probe*> chain;
          p->collect_derivation_chain (chain);
          probe* second = (chain.size()>1) ? chain[chain.size()-2] : chain[0];

          #if 0
          cerr << "\tchain[" << chain.size() << "]:" << endl;
          for (unsigned i=0; i<chain.size(); i++)
            { cerr << "\t"; chain[i]->printsig(cerr); cerr << endl; }
          #endif

          stringstream tmps;
          const probe_alias *a = second->get_alias();
          if (a)
            {
              assert (a->alias_names.size() >= 1);
              a->alias_names[0]->print(tmps); // XXX: [0] is arbitrary; perhaps print all
            }            
          else
            {
              assert (second->locations.size() >= 1);
              second->locations[0]->print(tmps); // XXX: [0] is less arbitrary here, but still ...
            }
          string pp = tmps.str();

          // Now duplicate-eliminate.  An alias may have expanded to
          // several actual derived probe points, but we only want to
          // print the alias head name once.
          if (seen.find (pp) == seen.end())
            {
              o << pp << endl;
              seen.insert (pp);
            }
        }
    }
  else
    {
      if (s.embeds.size() > 0)
        o << "# global embedded code" << endl;
      for (unsigned i=0; i<s.embeds.size(); i++)
        {
          embeddedcode* ec = s.embeds[i];
          ec->print (o);
          o << endl;
        }
      
      if (s.globals.size() > 0)
        o << "# globals" << endl;
      for (unsigned i=0; i<s.globals.size(); i++)
        {
          vardecl* v = s.globals[i];
          v->printsig (o);
          if (s.verbose && v->init)
            {
              o << " = ";
              v->init->print(o);
            }
          o << endl;
        }
      
      if (s.functions.size() > 0)
        o << "# functions" << endl;
      for (unsigned i=0; i<s.functions.size(); i++)
        {
          functiondecl* f = s.functions[i];
          f->printsig (o);
          o << endl;
          if (f->locals.size() > 0)
            o << "  # locals" << endl;
          for (unsigned j=0; j<f->locals.size(); j++)
            {
              vardecl* v = f->locals[j];
              o << "  ";
              v->printsig (o);
              o << endl;
            }
          if (s.verbose)
            {
              f->body->print (o);
              o << endl;
            }
        }
      
      if (s.probes.size() > 0)
        o << "# probes" << endl;
      for (unsigned i=0; i<s.probes.size(); i++)
        {
          derived_probe* p = s.probes[i];
          p->printsig (o);
          o << endl;
          if (p->locals.size() > 0)
            o << "  # locals" << endl;
          for (unsigned j=0; j<p->locals.size(); j++)
            {
              vardecl* v = p->locals[j];
              o << "  ";
              v->printsig (o);
              o << endl;
            }
          if (s.verbose)
            {
              p->body->print (o);
              o << endl;
            }
        }
    }
}


int pending_interrupts;

extern "C"
void handle_interrupt (int sig)
{
  if (pending_interrupts == 0)
    kill (0, sig); // Forward signals to child processes if any.

  pending_interrupts ++;
  // NB: the "2" below is intended to skip the effect of the self-induced
  // deferred signal coming from the kill() above.

  if (pending_interrupts > 2) // XXX: should be configurable? time-based?
    {
      char msg[] = "Too many interrupts received, exiting.\n";
      int rc = write (2, msg, sizeof(msg)-1);
      if (rc) {/* Do nothing; we don't care if our last gasp went out. */ ;}
      _exit (1);
    }
}


void
setup_signals (sighandler_t handler)
{
  struct sigaction sa;

  sa.sa_handler = handler;
  sigemptyset (&sa.sa_mask);
  if (handler != SIG_IGN)
    {
      sigaddset (&sa.sa_mask, SIGHUP);
      sigaddset (&sa.sa_mask, SIGPIPE);
      sigaddset (&sa.sa_mask, SIGINT);
      sigaddset (&sa.sa_mask, SIGTERM);
    }
  sa.sa_flags = 0;

  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGPIPE, &sa, NULL);
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
}


int
main (int argc, char * const argv [])
{
  string cmdline_script; // -e PROGRAM
  string script_file; // FILE
  bool have_script = false;
  bool release_changed = false;
  bool save_module = false;

  // Initialize defaults
  systemtap_session s;
  struct utsname buf;
  (void) uname (& buf);
  s.kernel_release = string (buf.release);
  s.architecture = string (buf.machine);
  s.verbose = 0;
  s.timing = false;
  s.guru_mode = false;
  s.bulk_mode = false;
  s.unoptimized = false;
  s.suppress_warnings = false;
  s.listing_mode = false;

#ifdef ENABLE_PROLOGUES
  s.prologue_searching = true;
#else
  s.prologue_searching = false;
#endif

  s.buffer_size = 0;
  s.last_pass = 5;
  s.module_name = "stap_" + stringify(getpid());
  s.output_file = ""; // -o FILE
  s.keep_tmpdir = false;
  s.cmd = "";
  s.target_pid = 0;
  s.merge=true;
  s.perfmon=0;
  s.symtab = false;
  s.use_cache = true;
  s.tapset_compile_coverage = false;
  s.need_uprobes = false;
  s.consult_symtab = false;
  s.ignore_vmlinux = false;
  s.ignore_dwarf = false;

  const char* s_p = getenv ("SYSTEMTAP_TAPSET");
  if (s_p != NULL)  
  {
    s.include_path.push_back (s_p);
  }
  else
  {
    s.include_path.push_back (string(PKGDATADIR) + "/tapset");
  }

  const char* s_r = getenv ("SYSTEMTAP_RUNTIME");
  if (s_r != NULL)
    s.runtime_path = s_r;
  else
    s.runtime_path = string(PKGDATADIR) + "/runtime";

  const char* s_d = getenv ("SYSTEMTAP_DIR");
  if (s_d != NULL)
    s.data_path = s_d;
  else
    s.data_path = get_home_directory() + string("/.systemtap");
  if (create_dir(s.data_path.c_str()) == 1)
    {
      const char* e = strerror (errno);
      cerr << "Warning: failed to create systemtap data directory (\""
	   << s.data_path << "\"): " << e << endl;
      cerr << "Disabling cache support." << endl;
      s.use_cache = false;
    }

  if (s.use_cache)
    {
      s.cache_path = s.data_path + "/cache";
      if (create_dir(s.cache_path.c_str()) == 1)
        {
	  const char* e = strerror (errno);
	  cerr << "Warning: failed to create cache directory (\""
	       << s.cache_path << "\"): " << e << endl;
	  cerr << "Disabling cache support." << endl;
	  s.use_cache = false;
	}
    }

  const char* s_tc = getenv ("SYSTEMTAP_COVERAGE");
  if (s_tc != NULL)
    s.tapset_compile_coverage = true;

  while (true)
    {
      int long_opt;
#define LONG_OPT_KELF 1
#define LONG_OPT_KMAP 2
#define LONG_OPT_IGNORE_VMLINUX 3
#define LONG_OPT_IGNORE_DWARF 4
      // NB: also see find_hash(), usage(), switch stmt below, stap.1 man page
      static struct option long_options[] = {
        { "kelf", 0, &long_opt, LONG_OPT_KELF },
        { "kmap", 2, &long_opt, LONG_OPT_KMAP },
        { "ignore-vmlinux", 0, &long_opt, LONG_OPT_IGNORE_VMLINUX },
        { "ignore-dwarf", 0, &long_opt, LONG_OPT_IGNORE_DWARF },
        { NULL, 0, NULL, 0 }
      };
      int grc = getopt_long (argc, argv, "hVMvtp:I:e:o:R:r:m:kgPc:x:D:bs:uqwl:d:",
                                                          long_options, NULL);
      if (grc < 0)
        break;
      switch (grc)
        {
        case 'V':
          version ();
          exit (0);

        case 'M':
          s.merge = false;
          break;

        case 'v':
	  s.verbose ++;
	  break;

        case 't':
	  s.timing = true;
	  break;

        case 'w':
	  s.suppress_warnings = true;
	  break;

        case 'p':
          if (s.listing_mode)
            {
              cerr << "Listing (-l) mode implies pass 2." << endl;
              usage (s, 1);
            }
          s.last_pass = atoi (optarg);
          if (s.last_pass < 1 || s.last_pass > 5)
            {
              cerr << "Invalid pass number (should be 1-5)." << endl;
              usage (s, 1);
            }
          break;

        case 'I':
          s.include_path.push_back (string (optarg));
          break;

        case 'd':
          s.unwindsym_modules.insert (string (optarg));
          break;

        case 'e':
	  if (have_script)
	    {
	      cerr << "Only one script can be given on the command line."
		   << endl;
	      usage (s, 1);
	    }
          cmdline_script = string (optarg);
          have_script = true;
          break;

        case 'o':
          s.output_file = string (optarg);
          break;

        case 'R':
          s.runtime_path = string (optarg);
          break;

        case 'm':
          s.module_name = string (optarg);
	  save_module = true;
	  {
	    string::size_type len = s.module_name.length();

	    // If the module name ends with '.ko', chop it off since
	    // modutils doesn't like modules named 'foo.ko.ko'.
	    if (len > 3 && s.module_name.substr(len - 3, 3) == ".ko")
	      {
		s.module_name.erase(len - 3);
		len -= 3;
		cerr << "Truncating module name to '" << s.module_name
		     << "'" << endl;
	      }

	    // Make sure an empty module name wasn't specified (-m "")
	    if (len == 0)
	    {
		cerr << "Module name cannot be empty." << endl;
		usage (s, 1);
	    }

	    // Make sure the module name is only composed of the
	    // following chars: [_a-zA-Z0-9]
	    const string identchars("_" "abcdefghijklmnopqrstuvwxyz"
				    "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "0123456789");
	    if (s.module_name.find_first_not_of(identchars) != string::npos)
	      {
		cerr << "Invalid module name (must only be composed of"
		    " characters [_a-zA-Z0-9])." << endl;
		usage (s, 1);
	      }

	    // Make sure module name isn't too long.
	    if (s.module_name.size() >= (MODULE_NAME_LEN - 1))
	      {
		s.module_name.resize(MODULE_NAME_LEN - 1);
		cerr << "Truncating module name to '" << s.module_name
		     << "'" << endl;
	      }
	  }

	  s.use_cache = false;
          break;

        case 'r':
          s.kernel_release = string (optarg);
	  release_changed = true;
          break;

        case 'k':
          s.keep_tmpdir = true;
          s.use_cache = false; /* User wants to keep a usable build tree. */
          break;

        case 'g':
          s.guru_mode = true;
          break;

        case 'P':
          s.prologue_searching = true;
          break;

        case 'b':
          s.bulk_mode = true;
          break;

	case 'u':
	  s.unoptimized = true;
	  break;

        case 's':
          s.buffer_size = atoi (optarg);
          if (s.buffer_size < 1 || s.buffer_size > 4095)
            {
              cerr << "Invalid buffer size (should be 1-4095)." << endl;
	      usage (s, 1);
            }
          break;

	case 'c':
	  s.cmd = string (optarg);
	  break;

	case 'x':
	  s.target_pid = atoi(optarg);
	  break;

	case 'D':
	  s.macros.push_back (string (optarg));
	  break;

	case 'q':
	  s.tapset_compile_coverage = true;
	  break;

        case 'h':
          usage (s, 0);
          break;

        case 'l':
	  s.suppress_warnings = true;
          s.listing_mode = true;
          s.last_pass = 2;
          if (have_script)
            {
	      cerr << "Only one script can be given on the command line."
		   << endl;
	      usage (s, 1);
            }
          cmdline_script = string("probe ") + string(optarg) + " {}";
          have_script = true;
          break;

        case 0:
          switch (long_opt)
            {
            case LONG_OPT_KELF:
	      s.consult_symtab = true;
	      break;
            case LONG_OPT_KMAP:
	      // Leave s.consult_symtab unset for now, to ease error checking.
              if (!s.kernel_symtab_path.empty())
		{
		  cerr << "You can't specify multiple --kmap options." << endl;
		  usage(s, 1);
		}
              if (optarg)
                s.kernel_symtab_path = optarg;
              else
#define PATH_TBD string("__TBD__")
                s.kernel_symtab_path = PATH_TBD;
	      break;
	    case LONG_OPT_IGNORE_VMLINUX:
	      s.ignore_vmlinux = true;
	      break;
	    case LONG_OPT_IGNORE_DWARF:
	      s.ignore_dwarf = true;
	      break;
            default:
              cerr << "Internal error parsing command arguments." << endl;
              usage(s, 1);
            }
          break;

        default:
          usage (s, 1);
          break;
        }
    }

  if(!s.bulk_mode && !s.merge)
    {
      cerr << "-M option is valid only for bulk (relayfs) mode." <<endl;
      usage (s, 1);
    }

  if(!s.output_file.empty() && s.bulk_mode && !s.merge)
    {
      cerr << "You can't specify -M, -b and -o options together." <<endl;
      usage (s, 1);
    }

  if((s.cmd != "") && (s.target_pid))
    {
      cerr << "You can't specify -c and -x options together." <<endl;
      usage (s, 1);
    }

  if (!s.kernel_symtab_path.empty())
    {
      if (s.consult_symtab)
      {
        cerr << "You can't specify --kelf and --kmap together." << endl;
	usage (s, 1);
      }
      s.consult_symtab = true;
      if (s.kernel_symtab_path == PATH_TBD)
        s.kernel_symtab_path = string("/boot/System.map-") + s.kernel_release;
    }

  if (s.last_pass > 4 && release_changed)
    {
      if (s.verbose)
        cerr << "Warning: changing last pass to 4 since cross-compiling" << endl;
      s.last_pass = 4;
    }

  for (int i = optind; i < argc; i++)
    {
      if (! have_script)
        {
          script_file = string (argv[i]);
          have_script = true;
        }
      else
        s.args.push_back (string (argv[i]));
    }

  // need a user file
  if (! have_script)
    {
      cerr << "A script must be specified." << endl;
      usage(s, 1);
    }

  // translate path of runtime to absolute path
  if (s.runtime_path[0] != '/')
    {
      char cwd[PATH_MAX];
      if (getcwd(cwd, sizeof(cwd)))
        {
          s.runtime_path = string(cwd) + "/" + s.runtime_path;
        }
    }

  int rc = 0;

  // override PATH and LC_ALL
  const char *path = "/bin:/sbin:/usr/bin:/usr/sbin";
  rc = setenv("PATH", path, 1) || setenv("LC_ALL", "C", 1);
  if (rc)
    {
      const char* e = strerror (errno);
      cerr << "setenv (\"PATH=" << path << "\" + \"LC_ALL=C\"): "
           << e << endl;
    }

  // Get rid of a few standard environment variables (which might
  // cause us to do unintended things).
  rc = unsetenv("IFS") || unsetenv("CDPATH") || unsetenv("ENV")
      || unsetenv("BASH_ENV");
  if (rc)
    {
      const char* e = strerror (errno);
      cerr << "unsetenv failed: " << e << endl;
    }

  s.kernel_base_release.assign(s.kernel_release, 0, s.kernel_release.find('-'));

  // arguments parsed; get down to business
  if (s.verbose > 1)
    {
      version ();
      clog << "Session arch: " << s.architecture
           << " release: " << s.kernel_release
           << endl;
    }

  // Create a temporary directory to build within.
  // Be careful with this, as "s.tmpdir" is "rm -rf"'d at the end.
  {
    const char* tmpdir_env = getenv("TMPDIR");
    if (! tmpdir_env)
      tmpdir_env = "/tmp";
    
    string stapdir = "/stapXXXXXX";
    string tmpdirt = tmpdir_env + stapdir;
    mode_t mask = umask(0);
    const char* tmpdir = mkdtemp((char *)tmpdirt.c_str());
    umask(mask);
    if (! tmpdir)
      {
        const char* e = strerror (errno);
        cerr << "ERROR: cannot create temporary directory (\"" << tmpdirt << "\"): " << e << endl;
        exit (1); // die
      }
    else
      s.tmpdir = tmpdir;

    if (s.verbose>1)
      clog << "Created temporary directory \"" << s.tmpdir << "\"" << endl;
  }

  // Create the name of the C source file within the temporary
  // directory.
  s.translated_source = string(s.tmpdir) + "/" + s.module_name + ".c";

  // Set up our handler to catch routine signals, to allow clean 
  // and reasonably timely exit.
  setup_signals(&handle_interrupt);

  struct tms tms_before;
  times (& tms_before);
  struct timeval tv_before;
  gettimeofday (&tv_before, NULL);

  // PASS 1a: PARSING USER SCRIPT

  struct stat user_file_stat;
  int user_file_stat_rc = -1;

  if (script_file == "-")
    {
      s.user_file = parser::parse (s, cin, s.guru_mode);
      user_file_stat_rc = fstat (STDIN_FILENO, & user_file_stat);
    }
  else if (script_file != "")
    {
      s.user_file = parser::parse (s, script_file, s.guru_mode);
      user_file_stat_rc = stat (script_file.c_str(), & user_file_stat);
    }
  else
    {
      istringstream ii (cmdline_script);
      s.user_file = parser::parse (s, ii, s.guru_mode);
    }
  if (s.user_file == 0)
    // syntax errors already printed
    rc ++;

  // Construct arch / kernel-versioning search path
  vector<string> version_suffixes;
  string kvr = s.kernel_release;
  const string& arch = s.architecture;
  // add full kernel-version-release (2.6.NN-FOOBAR) + arch
  version_suffixes.push_back ("/" + kvr + "/" + arch);
  version_suffixes.push_back ("/" + kvr);
  // add kernel version (2.6.NN) + arch
  if (kvr != s.kernel_base_release) {
    kvr = s.kernel_base_release;
    version_suffixes.push_back ("/" + kvr + "/" + arch);
    version_suffixes.push_back ("/" + kvr);
  }
  // add kernel family (2.6) + arch
  string::size_type dot1_index = kvr.find ('.');
  string::size_type dot2_index = kvr.rfind ('.');
  while (dot2_index > dot1_index && dot2_index != string::npos) {
    kvr.erase(dot2_index);
    version_suffixes.push_back ("/" + kvr + "/" + arch);
    version_suffixes.push_back ("/" + kvr);
    dot2_index = kvr.rfind ('.');
  }
  // add architecture search path
  version_suffixes.push_back("/" + arch);
  // add empty string as last element
  version_suffixes.push_back ("");

  // PASS 1b: PARSING LIBRARY SCRIPTS
  for (unsigned i=0; i<s.include_path.size(); i++)
    {
      // now iterate upon it
      for (unsigned k=0; k<version_suffixes.size(); k++)
        {
          glob_t globbuf;
          string dir = s.include_path[i] + version_suffixes[k] + "/*.stp";
          int r = glob(dir.c_str (), 0, NULL, & globbuf);
          if (r == GLOB_NOSPACE || r == GLOB_ABORTED)
            rc ++;
          // GLOB_NOMATCH is acceptable

          if (s.verbose>1 && globbuf.gl_pathc > 0)
            clog << "Searched '" << dir << "', "
                 << "found " << globbuf.gl_pathc << endl;

          for (unsigned j=0; j<globbuf.gl_pathc; j++)
            {
              if (pending_interrupts)
                break;

              // XXX: privilege only for /usr/share/systemtap?
              stapfile* f = parser::parse (s, globbuf.gl_pathv[j], true);
              if (f == 0)
                rc ++;
              else
                s.library_files.push_back (f);

              struct stat tapset_file_stat;
              int stat_rc = stat (globbuf.gl_pathv[j], & tapset_file_stat);
              if (stat_rc == 0 && user_file_stat_rc == 0 &&
                  user_file_stat.st_dev == tapset_file_stat.st_dev &&
                  user_file_stat.st_ino == tapset_file_stat.st_ino)
                {
                  clog << "usage error: tapset file '" << globbuf.gl_pathv[j]
                       << "' cannot be run directly as a session script." << endl;
                  rc ++;
                }

            }

          globfree (& globbuf);
        }
    }

  if (rc == 0 && s.last_pass == 1)
    {
      cout << "# parse tree dump" << endl;
      s.user_file->print (cout);
      cout << endl;
      if (s.verbose)
        for (unsigned i=0; i<s.library_files.size(); i++)
          {
            s.library_files[i]->print (cout);
            cout << endl;
          }
    }

  struct tms tms_after;
  times (& tms_after);
  unsigned _sc_clk_tck = sysconf (_SC_CLK_TCK);
  struct timeval tv_after;
  gettimeofday (&tv_after, NULL);

#define TIMESPRINT \
           (tms_after.tms_cutime + tms_after.tms_utime \
            - tms_before.tms_cutime - tms_before.tms_utime) * 1000 / (_sc_clk_tck) << "usr/" \
        << (tms_after.tms_cstime + tms_after.tms_stime \
            - tms_before.tms_cstime - tms_before.tms_stime) * 1000 / (_sc_clk_tck) << "sys/" \
        << ((tv_after.tv_sec - tv_before.tv_sec) * 1000 + \
            ((long)tv_after.tv_usec - (long)tv_before.tv_usec) / 1000) << "real ms."

  // syntax errors, if any, are already printed
  if (s.verbose)
    {
      clog << "Pass 1: parsed user script and "
           << s.library_files.size()
           << " library script(s) in "
           << TIMESPRINT
           << endl;
    }

  if (rc)
    cerr << "Pass 1: parse failed.  "
         << "Try again with more '-v' (verbose) options."
         << endl;

  if (rc || s.last_pass == 1 || pending_interrupts) goto cleanup;

  times (& tms_before);
  gettimeofday (&tv_before, NULL);

  // PASS 2: ELABORATION
  rc = semantic_pass (s);

  if (s.listing_mode || (rc == 0 && s.last_pass == 2))
    printscript(s, cout);

  times (& tms_after);
  gettimeofday (&tv_after, NULL);

  if (s.verbose) clog << "Pass 2: analyzed script: "
                      << s.probes.size() << " probe(s), "
                      << s.functions.size() << " function(s), "
                      << s.embeds.size() << " embed(s), "
                      << s.globals.size() << " global(s) in "
                      << TIMESPRINT
                      << endl;

  if (rc)
    cerr << "Pass 2: analysis failed.  "
         << "Try again with more '-v' (verbose) options."
         << endl;
  // Generate hash.  There isn't any point in generating the hash
  // if last_pass is 2, since we'll quit before using it.
  else if (s.last_pass != 2 && s.use_cache)
    {
      ostringstream o;
      unsigned saved_verbose;

      {
        // Make sure we're in verbose mode, so that printscript()
        // will output function/probe bodies.
        saved_verbose = s.verbose;
        s.verbose = 3;
        printscript(s, o);  // Print script to 'o'
        s.verbose = saved_verbose;
      }

      // Generate hash
      find_hash (s, o.str());

      // See if we can use cached source/module.
      if (get_from_cache(s))
        {
	  // If our last pass isn't 5, we're done (since passes 3 and
	  // 4 just generate what we just pulled out of the cache).
	  if (s.last_pass < 5 || pending_interrupts) goto cleanup;

	  // Short-circuit to pass 5.
	  goto pass_5;
	}
    }

  if (rc || s.listing_mode || s.last_pass == 2 || pending_interrupts) goto cleanup;

  // PASS 3: TRANSLATION

  times (& tms_before);
  gettimeofday (&tv_before, NULL);

  rc = translate_pass (s);

  if (rc == 0 && s.last_pass == 3)
    {
      ifstream i (s.translated_source.c_str());
      cout << i.rdbuf();
    }

  times (& tms_after);
  gettimeofday (&tv_after, NULL);

  if (s.verbose) clog << "Pass 3: translated to C into \""
                      << s.translated_source
                      << "\" in "
                      << TIMESPRINT
                      << endl;

  if (rc)
    cerr << "Pass 3: translation failed.  "
         << "Try again with more '-v' (verbose) options."
         << endl;

  if (rc || s.last_pass == 3 || pending_interrupts) goto cleanup;

  // PASS 4: COMPILATION
  times (& tms_before);
  gettimeofday (&tv_before, NULL);
  rc = compile_pass (s);

  if (rc == 0 && s.last_pass == 4)
    {
      cout << ((s.hash_path == "") ? (s.module_name + string(".ko")) : s.hash_path);
      cout << endl;
    }

  times (& tms_after);
  gettimeofday (&tv_after, NULL);

  if (s.verbose) clog << "Pass 4: compiled C into \""
                      << s.module_name << ".ko"
                      << "\" in "
                      << TIMESPRINT
                      << endl;

  if (rc)
    cerr << "Pass 4: compilation failed.  "
         << "Try again with more '-v' (verbose) options."
         << endl;
  else
    {
      // Update cache.
      if (s.use_cache)
	add_to_cache(s);

      // Copy module to the current directory.
      if (save_module && !pending_interrupts)
        {
	  string module_src_path = s.tmpdir + "/" + s.module_name + ".ko";
	  string module_dest_path = s.module_name + ".ko";

	  if (s.verbose > 1)
	    clog << "Copying " << module_src_path << " to "
		 << module_dest_path << endl;
	  if (copy_file(module_src_path.c_str(), module_dest_path.c_str()) != 0)
	    cerr << "Copy failed (\"" << module_src_path << "\" to \""
		 << module_dest_path << "\"): " << strerror(errno) << endl;
	}
    }

  if (rc || s.last_pass == 4 || pending_interrupts) goto cleanup;


  // PASS 5: RUN
pass_5:
  times (& tms_before);
  gettimeofday (&tv_before, NULL);
  // NB: this message is a judgement call.  The other passes don't emit
  // a "hello, I'm starting" message, but then the others aren't interactive
  // and don't take an indefinite amount of time.
  if (s.verbose) clog << "Pass 5: starting run." << endl;
  rc = run_pass (s);
  times (& tms_after);
  gettimeofday (&tv_after, NULL);
  if (s.verbose) clog << "Pass 5: run completed in "
                      << TIMESPRINT
                      << endl;

  if (rc)
    cerr << "Pass 5: run failed.  "
         << "Try again with more '-v' (verbose) options."
         << endl;

  // if (rc) goto cleanup;

 cleanup:

  // update the database information
  if (!rc && s.tapset_compile_coverage && !pending_interrupts) {
#ifdef HAVE_LIBSQLITE3
    update_coverage_db(s);
#else
    cerr << "Coverage database not available without libsqlite3" << endl;
#endif
  }

  // Clean up temporary directory.  Obviously, be careful with this.
  if (s.tmpdir == "")
    ; // do nothing
  else
    {
      if (s.keep_tmpdir)
        clog << "Keeping temporary directory \"" << s.tmpdir << "\"" << endl;
      else
        {
	  // Ignore signals while we're deleting the temporary directory.
	  setup_signals (SIG_IGN);

	  // Remove the temporary directory.
          string cleanupcmd = "rm -rf ";
          cleanupcmd += s.tmpdir;
          if (s.verbose>1) clog << "Running " << cleanupcmd << endl;
	  int status = system (cleanupcmd.c_str());
	  if (status != 0 && s.verbose>1)
	    clog << "Cleanup command failed, status: " << status << endl;
        }
    }

  return (rc||pending_interrupts) ? EXIT_FAILURE : EXIT_SUCCESS;
}
