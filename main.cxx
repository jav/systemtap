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

extern "C" {
#include <glob.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <time.h>
}

using namespace std;


void
usage (systemtap_session& s)
{
  clog 
    << "SystemTap translator/driver "
    << "(version " << VERSION << " built " << DATE << ")" << endl
    << "Copyright (C) 2005 Red Hat, Inc." << endl
    << "This is free software; see the source for copying conditions."
    << endl
    << endl
    << "Usage: stap [options] FILE [ARGS ...]        Run script in file."
    << endl
    << "   or: stap [options] - [ARGS ...]           Run script on stdin."
    << endl
    << "   or: stap [options] -e SCRIPT [ARGS ...]   Run given script." 
    << endl
    << endl
    << "Arguments:" << endl
    << "   --         no more options after this" << endl
    << "   -v         verbose" << (s.verbose ? " [set]" : "")
    << endl
    << "   -t         test mode" << (s.test_mode ? " [set]" : "")
    << endl
    << "   -p NUM     stop after pass NUM 1-5" << endl
    << "              (parse, elaborate, translate, compile, run)" << endl
    << "   -I DIR     look in DIR for additional .stp script files";
  if (s.include_path.size() == 0)
    clog << endl;
  else
    clog << ", instead of" << endl;
  for (unsigned i=0; i<s.include_path.size(); i++)
    clog << "              " << s.include_path[i] << endl;
  clog
    << "   -R DIR     look in DIR for runtime, instead of "
    << s.runtime_path
    << endl
    << "   -r RELEASE use kernel RELEASE, instead of "
    << s.kernel_release
    << endl
    << "   -m MODULE  set probe module name, insetad of "
    << s.module_name
    << endl
    << "   -o FILE    send output to file instead of stdout" << endl
    << "   -k         keep temporary directory" << endl;
  // XXX: other options:
  // -s: safe mode
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
  s.last_pass = 5;
  s.runtime_path = string(PKGDATADIR) + "/runtime";
  s.module_name = "stap_" + stringify(getuid()) + "_" + stringify(time(0));
  s.keep_tmpdir = false;

  while (true)
    {
      int grc = getopt (argc, argv, "vp:I:e:o:tR:r:m:k");
      if (grc < 0)
        break;
      switch (grc)
        {
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

        case '?':
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
    s.user_file = parser::parse (cin);
  else if (script_file != "")
    s.user_file = parser::parse (script_file);
  else
    {
      istringstream ii (cmdline_script);
      s.user_file = parser::parse (ii);
    }
  if (s.user_file == 0)
    // syntax errors already printed
    rc ++;

  // PASS 1b: PARSING LIBRARY SCRIPTS
  for (unsigned i=0; i<s.include_path.size(); i++)
    {
      glob_t globbuf;
      string dir = s.include_path[i] + "/*.stp";
      int r = glob(dir.c_str (), 0, NULL, & globbuf);
      if (r == GLOB_NOSPACE || r == GLOB_ABORTED)
        rc ++;
      // GLOB_NOMATCH is acceptable

      for (unsigned j=0; j<globbuf.gl_pathc; j++)
        {
          stapfile* f = parser::parse (globbuf.gl_pathv[j]);
          if (f == 0)
            rc ++;
          else
            s.library_files.push_back (f);
        }

      globfree (& globbuf);
    }

  if (rc == 0 && s.last_pass == 1)
    {
      cout << "# parse tree dump" << endl;
      s.user_file->print (cout);
      cout << endl;
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

  // semantic errors, if any, are already printed
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

  // translation errors, if any, are already printed
  if (rc || s.last_pass == 3) goto cleanup;
  
  // PASS 4: COMPILATION
  rc = compile_pass (s);

  if (rc)
    cerr << "Pass 4: compilation failed.  "
         << "Try again with '-v' (verbose) option." << endl;

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
          (void) system (cleanupcmd.c_str());
        }
    }

  return rc ? EXIT_FAILURE : EXIT_SUCCESS;
}
