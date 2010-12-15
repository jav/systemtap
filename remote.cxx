// systemtap remote execution
// Copyright (C) 2010 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

extern "C" {
#include <sys/utsname.h>
}

#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include "buildrun.h"
#include "remote.h"
#include "util.h"

using namespace std;

// loopback target for running locally
class direct : public remote {
  private:
    pid_t child;
    direct(systemtap_session& s): remote(s), child(0) {}

  public:
    friend class remote;

    virtual ~direct() {}

    int start()
      {
        string module = s->tmpdir + "/" + s->module_name + ".ko";
        pid_t pid = stap_spawn (s->verbose, make_run_command (*s, module));
	if (pid <= 0)
	  return 1;
	child = pid;
	return 0;
      }

    int finish()
      {
	if (child <= 0)
	  return 1;

	int ret = stap_waitpid(s->verbose, child);
	child = 0;
	return ret;
      }
};

class ssh_remote : public remote {
    // NB: ssh commands use a tty (-t) so signals are passed along to the remote
  private:
    string uri, tmpdir;
    pid_t child;

    ssh_remote(systemtap_session& s, const string& uri)
      : remote(s), uri(uri), child(0)
      {
        ostringstream out;
        vector<string> uname;
        string uname_cmd = "ssh -t -q " + lex_cast_qstring(uri) + " uname -rm";
        int rc = stap_system_read(s.verbose, uname_cmd, out);
        if (rc == 0)
          tokenize(out.str(), uname, " \t\r\n");
        if (uname.size() != 2)
          throw runtime_error("failed to get uname from " + uri
                              + " : rc=" + lex_cast(rc));
        string release = uname[0];
        string arch = uname[1];
	// XXX need to deal with command-line vs. implied arch/release
	this->s = s.clone(arch, release);
      }

  public:
    friend class remote;

    virtual ~ssh_remote() {}

    int start()
      {
        int rc;
        string localmodule = s->tmpdir + "/" + s->module_name + ".ko";
        string tmpmodule;
        string quri = lex_cast_qstring(uri);

        // Make a remote tempdir.
        {
          ostringstream out;
          vector<string> vout;
          string cmd = "ssh -t -q " + quri + " mktemp -d -t stapXXXXXX";
          rc = stap_system_read(s->verbose, cmd, out);
          if (rc == 0)
            tokenize(out.str(), vout, "\r\n");
          if (vout.size() != 1)
            {
              cerr << "failed to make a tempdir on " << uri
                   << " : rc=" << rc << endl;
              return -1;
            }
          tmpdir = vout[0];
          tmpmodule = tmpdir + "/" + s->module_name + ".ko";
        }

        // Transfer the module.  XXX and uprobes.ko, sigs, etc.
        if (rc == 0) {
          string cmd = "scp -q " + localmodule + " " + quri + ":" + tmpmodule;
          rc = stap_system(s->verbose, cmd);
          if (rc != 0)
            cerr << "failed to copy the module to " << uri
                 << " : rc=" << rc << endl;
        }

        // Run the module on the remote.
        if (rc == 0) {
          string cmd = "ssh -t -q " + quri + " "
            + lex_cast_qstring(make_run_command(*s, tmpmodule));
          pid_t pid = stap_spawn(s->verbose, cmd);
          if (pid > 0)
	    child = pid;
	  else
	    {
	      cerr << "failed to run the module on " << uri
		   << " : ret=" << pid << endl;
	      rc = -1;
	    }
        }

	return rc;
      }

    int finish()
      {
	int rc = 0;

	if (child > 0)
	  {
	    rc = stap_waitpid(s->verbose, child);
	    child = 0;
	  }

        if (!tmpdir.empty())
	  {
	    // Remove the tempdir.
	    // XXX need to make sure this runs even with e.g. CTRL-C exits
	    string quri = lex_cast_qstring(uri);
	    string cmd = "ssh -t -q " + quri + " rm -r " + tmpdir;
	    tmpdir.clear();
	    int rc2 = stap_system(s->verbose, cmd);
	    if (rc2 != 0)
	      cerr << "failed to delete the tempdir on " << uri
		   << " : rc=" << rc2 << endl;
	    if (rc == 0)
	      rc = rc2;
	  }

	return rc;
      }
};


remote*
remote::create(systemtap_session& s, const string& uri)
{
  try
    {
      if (uri == "direct")
        return new direct(s);
      else
        // XXX assuming everything else is ssh for now...
        return new ssh_remote(s, uri);
    }
  catch (std::runtime_error& e)
    {
      cerr << e.what() << endl;
      return NULL;
    }
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
