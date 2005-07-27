// build/run probes
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "buildrun.h"

#include <fstream>
#include <sstream>

extern "C" {
#include "signal.h"
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
  if (s.test_mode)
    {
      string module_dir = string("/lib/modules/")
        + s.kernel_release + "/build";
      o << "CFLAGS += -I \"" << module_dir << "/include\"" << endl;
      o << "CFLAGS += -I \"" << s.runtime_path << "/user\"" << endl;
      o << "CFLAGS += -I \"" << s.runtime_path << "\"" << endl;
      o << "CFLAGS += -I \"" << module_dir << "/include/asm/mach-default\"" << endl;
      o << s.module_name << ": " << s.translated_source << endl;
      o << "\t$(CC) $(CFLAGS) -o " << s.module_name
        << " " << s.translated_source << endl;
      o.close ();
    }
  else
    {
      // Assumes linux 2.6 kbuild
      o << "CFLAGS += -Wno-unused -Werror" << endl;
      o << "CFLAGS += -I \"" << s.runtime_path << "\"" << endl;
      o << "CFLAGS += -I \"" << s.runtime_path << "/relayfs\"" << endl;
      o << "obj-m := " << s.module_name << ".o" << endl;
      o.close ();
    }

  // Run make
  if (s.test_mode)
    {
      string make_cmd = string("/usr/bin/make -C \"") + s.tmpdir + "\"";

      if (! s.verbose)
        make_cmd += " -s >/dev/null 2>&1";
      
      if (s.verbose) clog << "Running " << make_cmd << endl;
      rc = system (make_cmd.c_str());
      
      if (s.verbose) clog << "Pass 4: compiled into \""
                          << s.module_name
                          << "\"" << endl;
    }
  else
    {
      string module_dir = string("/lib/modules/")
        + s.kernel_release + "/build";
      string make_cmd = string("/usr/bin/make")
        + string (" -C \"") + module_dir + string("\"");
      make_cmd += string(" M=\"") + s.tmpdir + string("\" modules");

      if (! s.verbose)
        make_cmd += " -s >/dev/null 2>&1";
      
      if (s.verbose) clog << "Running " << make_cmd << endl;
      rc = system (make_cmd.c_str());
      
      
      if (s.verbose) clog << "Pass 4: compiled into \""
                          << s.module_name << ".ko"
                          << "\"" << endl;
    }

  return rc;
}



int
run_pass (systemtap_session& s)
{
  int rc = 0;

  if (s.test_mode)
    {
      string run_cmd = s.tmpdir + "/" + s.module_name;

      if (s.verbose) clog << "Running " << run_cmd << endl;
      rc = system (run_cmd.c_str ());
    }
  else // real run
    {
      // leave parent process alone
      sighandler_t oldsig = signal (SIGINT, SIG_IGN);

      // for now, just spawn stpd
      string stpd_cmd = string("/usr/bin/sudo ") 
        + string(PKGLIBDIR) + "/stpd "
        + (s.verbose ? "" : "-q ")
        + s.tmpdir + "/" + s.module_name + ".ko";

      if (s.verbose) clog << "Running " << stpd_cmd << endl;
      rc = system (stpd_cmd.c_str ());
      
      signal (SIGINT, oldsig);
    }      

  return rc;
}
