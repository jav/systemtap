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
  if (1)
    {
      // Assumes linux 2.6 kbuild
      string makefile_nm = s.tmpdir + "/Makefile";
      ofstream o (makefile_nm.c_str());
      o << "CFLAGS += -Werror" << endl;
      if (s.test_mode)
        o << "CFLAGS += -I \"" << s.runtime_path << "/user\"" << endl;
      o << "CFLAGS += -I \"" << s.runtime_path << "\"" << endl;
      o << "CFLAGS += -I \"" << s.runtime_path << "/relayfs\"" << endl;
      o << "obj-m := " << s.module_name << ".o" << endl;
    }

  // run module make
  string module_dir = string("/lib/modules/") + s.kernel_release + "/build";
  string make_cmd = string("make")
    + string (" -C \"") + module_dir + string("\"");
  make_cmd += string(" M=\"") + s.tmpdir + string("\" modules");
  if (! s.verbose)
    make_cmd += " -s >/dev/null 2>&1";

  if (s.verbose) clog << "Running " << make_cmd << endl;
  int rc = system (make_cmd.c_str());


  if (s.verbose) clog << "Pass 4: compiled into \""
                      << s.module_name << ".ko"
                      << "\"" << endl;

  return rc;
}



int
run_pass (systemtap_session& s)
{
  if (s.test_mode)
    return 1; // XXX: don't know how to do this yet
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
      int rc = system (stpd_cmd.c_str ());
      
      signal (SIGINT, oldsig);
      return rc;
    }      
}
