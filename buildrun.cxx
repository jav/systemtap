// build/run probes
// Copyright (C) 2005, 2006 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "buildrun.h"
#include "session.h"

#include <fstream>
#include <sstream>

extern "C" {
#include "signal.h"
#include <sys/wait.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
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

  if (s.verbose > 2)
    o << "CFLAGS += -ftime-report -Q" << endl;

  o << "CFLAGS += -freorder-blocks" << endl; // improve on -Os

  // o << "CFLAGS += -fno-unit-at-a-time" << endl;
    
  // Assumes linux 2.6 kbuild
  o << "CFLAGS += -Wno-unused -Werror" << endl;
  o << "CFLAGS += -I\"" << s.runtime_path << "\"" << endl;
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
  
  if (s.verbose > 1)
    make_cmd += " V=1";
  else
    make_cmd += " -s >/dev/null 2>&1";
  
  if (s.verbose > 1) clog << "Running " << make_cmd << endl;
  rc = system (make_cmd.c_str());
  
  return rc;
}


int
run_pass (systemtap_session& s)
{
  int rc = 0;

  struct passwd *pw = getpwuid(getuid());
  string username = string(pw->pw_name);

  // for now, just spawn stpd
  string stpd_cmd = string("sudo ") 
    + string(PKGLIBDIR) + "/stpd "
    + (s.verbose>1 ? "" : "-q ")
    + (s.merge ? "" : "-m ")
    + "-u " + username + " "
    + (s.output_file.empty() ? "" : "-o " + s.output_file + " ");
  
  stpd_cmd += "-d " + stringify(getpid()) + " ";
  
  if (s.cmd != "")
    stpd_cmd += "-c \"" + s.cmd + "\" ";
  
  if (s.target_pid)
    stpd_cmd += "-t " + stringify(s.target_pid) + " ";
  
  if (s.buffer_size)
    stpd_cmd += "-b " + stringify(s.buffer_size) + " ";
  
  stpd_cmd += s.tmpdir + "/" + s.module_name + ".ko";
  
  if (s.verbose>1) clog << "Running " << stpd_cmd << endl;
  
  signal (SIGHUP, SIG_IGN);
  signal (SIGINT, SIG_IGN);
  rc = system (stpd_cmd.c_str ());

  return rc;
}
