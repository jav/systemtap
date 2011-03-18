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

// Decode URIs as per RFC 3986, though not bothering to be strict
class uri_decoder {
  public:
    const string uri;
    string scheme, authority, path, query, fragment;
    bool has_authority, has_query, has_fragment;

    uri_decoder(const string& uri):
      uri(uri), has_authority(false), has_query(false), has_fragment(false)
    {
      const string re =
        "^([^:]+):(//[^/?#]*)?([^?#]*)(\\?[^#]*)?(#.*)?$";

      vector<string> matches;
      if (regexp_match(uri, re, matches) != 0)
        throw runtime_error(_F("string doesn't appear to be a URI: %s", uri.c_str()));

      scheme = matches[1];

      if (!matches[2].empty())
        {
          has_authority = true;
          authority = matches[2].substr(2);
        }

      path = matches[3];

      if (!matches[4].empty())
        {
          has_query = true;
          query = matches[4].substr(1);
        }

      if (!matches[5].empty())
        {
          has_fragment = true;
          fragment = matches[5].substr(1);
        }
    }
};


// loopback target for running locally
class direct : public remote {
  private:
    pid_t child;
    direct(systemtap_session& s): remote(s), child(0) {}

    int start()
      {
        pid_t pid = stap_spawn (s->verbose, make_run_command (*s));
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

  public:
    friend class remote;

    virtual ~direct() {}
};

class ssh_remote : public remote {
    // NB: ssh commands use a tty (-t) so signals are passed along to the remote
  private:
    vector<string> ssh_args, scp_args;
    string ssh_control;
    string host, tmpdir;
    pid_t child;

    ssh_remote(systemtap_session& s, const string& host)
      : remote(s), host(host), child(0)
      {
        init();
      }

    ssh_remote(systemtap_session& s, const uri_decoder& ud)
      : remote(s), child(0)
      {
        if (!ud.has_authority || ud.authority.empty())
          throw runtime_error(_("ssh target requires a hostname"));
        if (!ud.path.empty() && ud.path != "/")
          throw runtime_error(_("ssh target URI doesn't support a /path"));
        if (ud.has_query)
          throw runtime_error(_("ssh target URI doesn't support a ?query"));
        if (ud.has_fragment)
          throw runtime_error(_("ssh target URI doesn't support a #fragment"));

        host = ud.authority;
        init();
      }

    void init()
      {
        open_control_master();
        try
          {
            get_uname();
          }
        catch (runtime_error&)
          {
            close_control_master();
            throw;
          }
      }

    void open_control_master()
      {
        static unsigned index = 0;

        if (s->tmpdir.empty()) // sanity check, shouldn't happen
          throw runtime_error(_("No tmpdir available for ssh control master"));

        ssh_control = s->tmpdir + "/ssh_remote_control_" + lex_cast(++index);

        scp_args.clear();
        scp_args.push_back("scp");
        scp_args.push_back("-q");
        scp_args.push_back("-o");
        scp_args.push_back("ControlPath=" + ssh_control);

        ssh_args = scp_args;
        ssh_args[0] = "ssh";
        ssh_args.push_back(host);

        // NB: ssh -f will stay in the foreground until authentication is
        // complete and the control socket is created, so we know it's ready to
        // go when stap_system returns.
        vector<string> cmd = ssh_args;
        cmd.push_back("-f");
        cmd.push_back("-N");
        cmd.push_back("-M");
        int rc = stap_system(s->verbose, cmd);
        if (rc != 0)
            throw runtime_error(_F("failed to create an ssh control master for %s : rc= %d",
                                   host.c_str(), rc));

        if (s->verbose>1)
          clog << _F("Created ssh control master at %s",
                     lex_cast_qstring(ssh_control).c_str()) << endl;
      }

    void close_control_master()
      {
        if (ssh_control.empty())
          return;

        vector<string> cmd = ssh_args;
        cmd.push_back("-O");
        cmd.push_back("exit");
        int rc = stap_system(s->verbose, cmd, true, true);
        if (rc != 0)
          cerr << _F("failed to stop the ssh control master for %s : rc=%d",
                     host.c_str(), rc) << endl;

        ssh_control.clear();
        scp_args.clear();
        ssh_args.clear();
      }

    void get_uname()
      {
        ostringstream out;
        vector<string> uname;
        vector<string> cmd = ssh_args;
        cmd.push_back("-t");
        cmd.push_back("uname -rm");
        int rc = stap_system_read(s->verbose, cmd, out);
        if (rc == 0)
          tokenize(out.str(), uname, " \t\r\n");
        if (uname.size() != 2)
          throw runtime_error(_F("failed to get uname from %s : rc= %d", host.c_str(), rc));
        const string& release = uname[0];
        const string& arch = uname[1];
        // XXX need to deal with command-line vs. implied arch/release
        this->s = s->clone(arch, release);
      }

    int start()
      {
        int rc;
        string localmodule = s->tmpdir + "/" + s->module_name + ".ko";
        string tmpmodule;

        // Make a remote tempdir.
        {
          ostringstream out;
          vector<string> vout;
          vector<string> cmd = ssh_args;
          cmd.push_back("-t");
          cmd.push_back("mktemp -d -t stapXXXXXX");
          rc = stap_system_read(s->verbose, cmd, out);
          if (rc == 0)
            tokenize(out.str(), vout, "\r\n");
          if (vout.size() != 1)
            {
              cerr << _F("failed to make a tempdir on %s : rc=%d",
                         host.c_str(), rc) << endl;
              return -1;
            }
          tmpdir = vout[0];
          tmpmodule = tmpdir + "/" + s->module_name + ".ko";
        }

        // Transfer the module.  XXX and uprobes.ko, sigs, etc.
        if (rc == 0) {
          vector<string> cmd = scp_args;
          cmd.push_back(localmodule);
          cmd.push_back(host + ":" + tmpmodule);
          rc = stap_system(s->verbose, cmd);
          if (rc != 0)
            cerr << _F("failed to copy the module to %s : rc=%d",
                       host.c_str(), rc) << endl;
        }

        // Run the module on the remote.
        if (rc == 0) {
          vector<string> cmd = ssh_args;
          cmd.push_back("-t");
          cmd.push_back(cmdstr_join(make_run_command(*s, tmpmodule)));
          pid_t pid = stap_spawn(s->verbose, cmd);
          if (pid > 0)
            child = pid;
          else
            {
              cerr << _F("failed to run the module on %s : ret=%d",
                         host.c_str(), pid) << endl;
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
            vector<string> cmd = ssh_args;
            cmd.push_back("-t");
            cmd.push_back("rm -r " + cmdstr_quoted(tmpdir));
            int rc2 = stap_system(s->verbose, cmd);
            if (rc2 != 0)
              cerr << _F("failed to delete the tempdir on %s : rc=%d",
                         host.c_str(), rc2) << endl;
            if (rc == 0)
              rc = rc2;
            tmpdir.clear();
          }

        close_control_master();

        return rc;
      }

  public:
    friend class remote;

    virtual ~ssh_remote()
      {
        close_control_master();
      }
};


remote*
remote::create(systemtap_session& s, const string& uri)
{
  try
    {
      if (uri == "direct")
        return new direct(s);
      else if (uri.find(':') != string::npos)
        {
          const uri_decoder ud(uri);
          if (ud.scheme == "ssh")
            return new ssh_remote(s, ud);
          else
              throw runtime_error(_F("unrecognized URI scheme '%s' in remote: %s",
                                     ud.scheme.c_str(), uri.c_str()));
        }
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

int
remote::run(const vector<remote*>& remotes)
{
  // NB: the first failure "wins"
  int ret = 0, rc = 0;

  for (unsigned i = 0; i < remotes.size() && !pending_interrupts; ++i)
    {
      rc = remotes[i]->start();
      if (!ret)
        ret = rc;
    }

  for (unsigned i = 0; i < remotes.size(); ++i)
    {
      rc = remotes[i]->finish();
      if (!ret)
        ret = rc;
    }

  return ret;
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
