// systemtap translator/driver
// Copyright (C) 2005 Red Hat Inc.
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

#include <iostream>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstdlib>

extern "C" {
#include <glob.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <time.h>
}

using namespace std;


void
version ()
{
  clog
    << "SystemTap translator/driver "
    << "(version " << VERSION << " built " << DATE << ")" << endl
    << "Copyright (C) 2005 Red Hat, Inc." << endl
    << "This is free software; see the source for copying conditions."
    << endl;
}

void
usage (systemtap_session& s)
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
    << endl
    << "Options:" << endl
    << "   --         no more options after this" << endl
    << "   -v         verbose" << (s.verbose ? " [set]" : "") << endl
    << "   -h         show help" << endl
    << "   -V         show version" << endl
    << "   -k         keep temporary directory" << endl
    // << "   -t         test mode" << (s.test_mode ? " [set]" : "") << endl
    << "   -g         guru mode" << (s.guru_mode ? " [set]" : "") << endl
    << "   -p NUM     stop after pass NUM 1-5" << endl
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
    // << "   -r RELEASE use kernel RELEASE, instead of" << endl
    // <<      "              " << s.kernel_release << endl
    << "   -m MODULE  set probe module name, instead of" << endl
    <<      "              " << s.module_name << endl
    << "   -o FILE    send output to file instead of stdout" << endl
    << "   -c CMD     start the probes, run CMD, and exit when it finishes" << endl
    << "   -x PID     sets target() to PID" << endl
    ;
  // -d: dump safety-related external references

  exit (0);
}


// little utility function

template <typename T>
static string
stringify(T t)
{
  ostringstream s;
  s << t;
  return s.str ();
}


int
main (int argc, char * const argv [])
{
  string cmdline_script; // -e PROGRAM
  string script_file; // FILE
  bool have_script = false;
  string output_file = "-"; // -o FILE

  // Initialize defaults
  systemtap_session s;
  struct utsname buf;
  (void) uname (& buf);
  s.kernel_release = string (buf.release);
  s.verbose = false;
  s.test_mode = false;
  s.guru_mode = false;
  s.last_pass = 5;
  s.module_name = "stap_" + stringify(getpid());
  s.keep_tmpdir = false;
  s.cmd = "";
  s.target_pid = 0;

  const char* s_p = getenv ("SYSTEMTAP_TAPSET");
  if (s_p != NULL)
    s.include_path.push_back (s_p);
  else
    s.include_path.push_back (string(PKGDATADIR) + "/tapset");

  const char* s_r = getenv ("SYSTEMTAP_RUNTIME");
  if (s_r != NULL)
    s.runtime_path = s_r;
  else
    s.runtime_path = string(PKGDATADIR) + "/runtime";

  while (true)
    {
      int grc = getopt (argc, argv, "hVvp:I:e:o:tR:r:m:kgc:x:D:");
      if (grc < 0)
        break;
      switch (grc)
        {
        case 'V':
          version ();
          exit (0);

        case 'v':
	  s.verbose = true;
	  break;

        case 'p':
          s.last_pass = atoi (optarg);
          if (s.last_pass < 1 || s.last_pass > 5)
            {
              cerr << "Invalid pass number." << endl;
              usage (s);
            }
          break;

        case 'I':
          s.include_path.push_back (string (optarg));
          break;

        case 'e':
	  if (have_script)
	    usage (s);
          cmdline_script = string (optarg);
          have_script = true;
          break;

        case 'o':
          output_file = string (optarg);
          break;

        case 't':
          s.test_mode = true;
          break;

        case 'R':
          s.runtime_path = string (optarg);
          break;

        case 'm':
          s.module_name = string (optarg);
          break;

        case 'r':
          s.kernel_release = string (optarg);
          break;

        case 'k':
          s.keep_tmpdir = true;
          break;

        case 'g':
          s.guru_mode = true;
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

        case 'h':
        default:
          usage (s);
        }
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
    usage(s);

  int rc = 0;

  // arguments parsed; get down to business

  // Create a temporary directory to build within.
  // Be careful with this, as "s.tmpdir" is "rm -rf"'d at the end.
  {
    char tmpdirt[] = "/tmp/stapXXXXXX";
    const char* tmpdir = mkdtemp (tmpdirt);
    if (! tmpdir)
      {
        const char* e = strerror (errno);
        cerr << "mkdtemp (\"" << tmpdir << "\"): " << e << endl;
        s.tmpdir = "";
        rc = 1;
      }
    else
      s.tmpdir = tmpdir;

    if (s.verbose)
      clog << "Created temporary directory \"" << s.tmpdir << "\"" << endl;
  }


  // PASS 1a: PARSING USER SCRIPT
  // XXX: pass args vector, so parser (or lexer?) can substitute
  // $1..$NN with actual arguments
  if (script_file == "-")
    s.user_file = parser::parse (cin, s.guru_mode);
  else if (script_file != "")
    s.user_file = parser::parse (script_file, s.guru_mode);
  else
    {
      istringstream ii (cmdline_script);
      s.user_file = parser::parse (ii, s.guru_mode);
    }
  if (s.user_file == 0)
    // syntax errors already printed
    rc ++;

  // Construct kernel-versioning search path
  vector<string> version_suffixes;
  const string& kvr = s.kernel_release;
  // add full kernel-version-release (2.6.NN-FOOBAR)
  version_suffixes.push_back ("/" + kvr);
  // add kernel version (2.6.NN)
  string::size_type dash_rindex = kvr.rfind ('-');
  if (dash_rindex > 0 && dash_rindex != string::npos)
    version_suffixes.push_back ("/" + kvr.substr (0, dash_rindex));
  // add kernel family (2.6)
  string::size_type dot_index = kvr.find ('.');
  string::size_type dot2_index = kvr.find ('.', dot_index+1);
  if (dot2_index > 0 && dot2_index != string::npos)
    version_suffixes.push_back ("/" + kvr.substr (0, dot2_index));
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

          if (s.verbose)
            clog << "Searched '" << dir << "', "
                 << "match count " << globbuf.gl_pathc << endl;

          for (unsigned j=0; j<globbuf.gl_pathc; j++)
            {
              // privilege only for /usr/share/systemtap?
              stapfile* f = parser::parse (globbuf.gl_pathv[j], true);
              if (f == 0)
                rc ++;
              else
                s.library_files.push_back (f);
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

  // syntax errors, if any, are already printed
  if (s.verbose) clog << "Pass 1: parsed user script and "
                      << s.library_files.size()
                      << " library script(s)." << endl;

  if (rc || s.last_pass == 1) goto cleanup;

  // PASS 2: ELABORATION
  rc = semantic_pass (s);

  if (rc == 0 && s.last_pass == 2)
    {
      if (s.globals.size() > 0)
        cout << "# globals" << endl;
      for (unsigned i=0; i<s.globals.size(); i++)
	{
	  vardecl* v = s.globals[i];
	  v->printsig (cout);
          cout << endl;
	}

      if (s.functions.size() > 0)
        cout << "# functions" << endl;
      for (unsigned i=0; i<s.functions.size(); i++)
	{
	  functiondecl* f = s.functions[i];
	  f->printsig (cout);
          cout << endl;
          if (f->locals.size() > 0)
            cout << "  # locals" << endl;
          for (unsigned j=0; j<f->locals.size(); j++)
            {
              vardecl* v = f->locals[j];
              cout << "  ";
              v->printsig (cout);
              cout << endl;
            }
	}

      if (s.probes.size() > 0)
        cout << "# probes" << endl;
      for (unsigned i=0; i<s.probes.size(); i++)
	{
	  derived_probe* p = s.probes[i];
	  p->printsig (cout);
          cout << endl;
          if (p->locals.size() > 0)
            cout << "  # locals" << endl;
          for (unsigned j=0; j<p->locals.size(); j++)
            {
              vardecl* v = p->locals[j];
              cout << "  ";
              v->printsig (cout);
              cout << endl;
            }
	}
    }

  if (s.verbose) clog << "Pass 2: analyzed user script.  "
                      << s.probes.size() << " probe(s), "
                      << s.functions.size() << " function(s), "
                      << s.globals.size() << " global(s)." << endl;

  if (rc)
    cerr << "Pass 2: analysis failed.  "
         << "Try again with '-v' (verbose) option." << endl;

  if (rc || s.last_pass == 2) goto cleanup;

  // PASS 3: TRANSLATION
  s.translated_source = string(s.tmpdir) + "/" + s.module_name + ".c";
  rc = translate_pass (s);

  if (rc == 0 && s.last_pass == 3)
    {
      ifstream i (s.translated_source.c_str());
      cout << i.rdbuf();
    }

  if (s.verbose) clog << "Pass 3: translated to C into \""
                      << s.translated_source
                      << "\"" << endl;

  if (rc)
    cerr << "Pass 2: translation failed.  "
         << "Try again with '-v' (verbose) option." << endl;

  if (rc || s.last_pass == 3) goto cleanup;

  // PASS 4: COMPILATION
  rc = compile_pass (s);

  if (rc)
    cerr << "Pass 4: compilation failed.  "
         << "Try again with '-v' (verbose) option." << endl;

  // XXX: what to do if rc==0 && last_pass == 4?  dump .ko file to stdout? 
  if (rc || s.last_pass == 4) goto cleanup;

  // PASS 5: RUN
  rc = run_pass (s);

  if (rc)
    cerr << "Pass 5: run failed.  "
         << "Try again with '-v' (verbose) option." << endl;

  // if (rc) goto cleanup;

 cleanup:
  // Clean up temporary directory.  Obviously, be careful with this.
  if (s.tmpdir == "")
    ; // do nothing
  else
    {
      if (s.keep_tmpdir)
        clog << "Keeping temporary directory \"" << s.tmpdir << "\"" << endl;
      else
        {
          string cleanupcmd = "/bin/rm -rf ";
          cleanupcmd += s.tmpdir;
          if (s.verbose) clog << "Running " << cleanupcmd << endl;
	  int status = system (cleanupcmd.c_str());
	  if (status != 0 && s.verbose)
	    clog << "Cleanup command failed, status: " << status << endl;
        }
    }

  return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
