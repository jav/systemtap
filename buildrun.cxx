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
#include <sys/wait.h>
}


using namespace std;


// return as quoted string, with at least '"' backslash-escaped
template <typename IN> inline string
lex_cast_qstring(IN const & in)
{
  stringstream ss;
  string out, out2;
  if (!(ss << in))
    throw runtime_error("bad lexical cast");
  out = ss.str();
  out2 += '"';
  for (unsigned i=0; i<out.length(); i++)
    {
      if (out[i] == '"') // XXX others?
	out2 += '\\';
      out2 += out[i];
    }
  out2 += '"';
  return out2;
}


template <typename T>
static string
stringify(T t)
{
  ostringstream s;
  s << t;
  return s.str ();
}



int
compile_pass (systemtap_session& s)
{
  // fill in a quick Makefile
  string makefile_nm = s.tmpdir + "/Makefile";
  ofstream o (makefile_nm.c_str());
  int rc = 0;

  // Create makefile

  for (unsigned i=0; i<s.macros.size(); i++)
    o << "CFLAGS += -D " << lex_cast_qstring(s.macros[i]) << endl;

  // XXX
  // o << "CFLAGS += -ftime-report" << endl;

  // Assumes linux 2.6 kbuild
  o << "CFLAGS += -Wno-unused -Werror" << endl;
  o << "CFLAGS += -I \"" << s.runtime_path << "\"" << endl;
  o << "CFLAGS += -I \"" << s.runtime_path << "/relayfs\"" << endl;
  o << "obj-m := " << s.module_name << ".o" << endl;

  o.close ();

  // Run make
  string module_dir = string("/lib/modules/")
    + s.kernel_release + "/build";
  string make_cmd = string("make")
    + string (" -C \"") + module_dir + string("\"");
  make_cmd += string(" M=\"") + s.tmpdir + string("\" modules");
  
  if (s.verbose)
    make_cmd += " V=1";
  else
    make_cmd += " -s >/dev/null 2>&1";
  
  if (s.verbose) clog << "Running " << make_cmd << endl;
  rc = system (make_cmd.c_str());
  
  
  if (s.verbose) clog << "Pass 4: compiled into \""
                      << s.module_name << ".ko"
                      << "\"" << endl;
  
  return rc;
}


int
run_pass (systemtap_session& s)
{
  int rc = 0;

  // for now, just spawn stpd
  string stpd_cmd = string("sudo ") 
    + string(PKGLIBDIR) + "/stpd "
    + (s.bulk_mode ? "" : "-r ")
    + (s.verbose ? "" : "-q ")
    + (s.output_file.empty() ? "" : "-o " + s.output_file + " ");
  
  stpd_cmd += "-d " + stringify(getpid()) + " ";
  
  if (s.cmd != "")
    stpd_cmd += "-c \"" + s.cmd + "\" ";
  
  if (s.target_pid)
    stpd_cmd += "-t " + stringify(s.target_pid) + " ";
  
  if (s.buffer_size)
    stpd_cmd += "-b " + stringify(s.buffer_size) + " ";
  
  stpd_cmd += s.tmpdir + "/" + s.module_name + ".ko";
  
  if (s.verbose) clog << "Running " << stpd_cmd << endl;
  
  signal (SIGHUP, SIG_IGN);
  signal (SIGINT, SIG_IGN);
  rc = system (stpd_cmd.c_str ());

  return rc;
}
