// systemtap translator driver
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

#include <iostream>
#include <fstream>
#include <sstream>

extern "C" {
#include <glob.h>
#include <unistd.h>
}

using namespace std;


void usage ()
{
  cerr << "SystemTap translator "
       << "(version " << VERSION << " built " << DATE << ")" << endl;
  cerr << "Copyright (C) 2005 Red Hat, Inc." << endl;
  cerr << "This is free software; see the source for copying conditions." << endl;
  cerr << endl;
  cerr << "Usage: stap [options] FILE [ARGS ...]        Run script in file." << endl;
  cerr << "   or: stap [options] - [ARGS ...]           Run script on stdin." << endl;
  cerr << "   or: stap [options] -e SCRIPT [ARGS ...]   Run given script." << endl;
  cerr << endl;
  cerr << "Arguments:" << endl;
  cerr << "   --\tNo more options after this" << endl;
  cerr << "   -p NUM\tStop after pass NUM 1-3" << endl;
  cerr << "         \t(parse, elaborate, translate)" << endl;
  cerr << "   -I DIR\tLook in DIR for additional .stp script files." << endl;
  cerr << "   -o FILE\tSend translator output to file instead of stdout." << endl;
  // XXX: other options:
  // -s: safe mode
  // -d: dump safety-related external references 

  exit (0);
}


int
main (int argc, char * const argv [])
{
  int last_pass = 3; // -p NUM
  string cmdline_script; // -e PROGRAM
  string script_file; // FILE
  bool have_script = false;
  string output_file; // -o FILE
  vector<string> include_path; // -I DIR
  vector<string> args; // ARGS
  while (true)
    {
      int grc = getopt (argc, argv, "p:I:e:o:");
      if (grc < 0)
        break;
      switch (grc)
        {
        case 'p':
          last_pass = atoi (optarg);
          if (last_pass < 1 || last_pass > 3)
            {
              cerr << "Invalid pass number." << endl;
              usage ();
            }
          break;

        case 'I':
          include_path.push_back (string (optarg));
          break;

        case 'e':
	  if (have_script)
	    usage ();
          cmdline_script = string (optarg);
          have_script = true;
          break;

        case 'o':
	  if (output_file != "")
	    usage ();
          output_file = string (optarg);
          break;

        case '?':
        case 'h':
        default:
          usage ();
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
        args.push_back (string (argv[i]));
    }

  // need a user file
  if (! have_script)
    usage();

  // arguments parsed; get down to business

  int rc = 0;
  systemtap_session s;
  if (output_file != "")
    s.op = new translator_output (output_file);
  else
    s.op = new translator_output (cout);


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
  for (unsigned i=0; i<include_path.size(); i++)
    {
      glob_t globbuf;
      string dir = include_path[i] + "/*.stp";
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

  if (last_pass == 1 && rc == 0)
    {
      s.op->line() << "# parse tree dump";
      s.user_file->print (s.op->newline());
      for (unsigned i=0; i<s.library_files.size(); i++)
	s.library_files[i]->print (s.op->newline());
    }

  // PASS 2: ELABORATION
  if (rc == 0 && last_pass > 1)
    rc = semantic_pass (s);
  if (last_pass == 2 && rc == 0)
    {
      s.op->line() << "# globals";
      for (unsigned i=0; i<s.globals.size(); i++)
	{
	  vardecl* v = s.globals[i];
	  v->printsig (s.op->newline());
	}

      s.op->newline() << "# functions";
      for (unsigned i=0; i<s.functions.size(); i++)
	{
	  functiondecl* f = s.functions[i];
	  f->printsig (s.op->newline());
	}

      s.op->newline() << "# probes";
      for (unsigned i=0; i<s.probes.size(); i++)
	{
	  derived_probe* p = s.probes[i];
	  p->printsig (s.op->newline());
	}
    }

  // PASS 3: TRANSLATION
  if (rc == 0 && last_pass > 2)
    rc = translate_pass (s);

  delete s.op;

  return rc;
}
