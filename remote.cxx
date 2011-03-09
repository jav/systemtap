// systemtap remote execution
// Copyright (C) 2010 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

extern "C" {
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
}

#include <cstdio>
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
        throw runtime_error("string doesn't appear to be a URI: " + uri);

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


class stapsh : public remote {
  private:
    int interrupts_sent;
    int fdin, fdout;
    FILE *IN, *OUT;

    virtual void prepare_poll(vector<pollfd>& fds)
      {
        if (fdout >= 0 && OUT)
          {
            pollfd p = { fdout, POLLIN };
            fds.push_back(p);
          }

        // need to send a signal?
        if (fdin >= 0 && IN && interrupts_sent < pending_interrupts)
          {
            pollfd p = { fdin, POLLOUT };
            fds.push_back(p);
          }
      }

    virtual void handle_poll(vector<pollfd>& fds)
      {
        for (unsigned i=0; i < fds.size(); ++i)
          if (fds[i].revents)
            {
              if (fdout >= 0 && OUT && fds[i].fd == fdout)
                {
                  if (fds[i].revents & POLLIN)
                    {
                      // XXX should we do line-buffering?
                      char buf[4096];
                      size_t rc = fread(buf, 1, sizeof(buf), OUT);
                      if (rc > 0)
                        {
                          cout.write(buf, rc);
                          continue;
                        }
                    }
                  close();
                }

              // need to send a signal?
              if (fdin >= 0 && IN && fds[i].fd == fdin &&
                  interrupts_sent < pending_interrupts)
                {
                  if (fds[i].revents & POLLOUT)
                    {
                      ostringstream cmd;
                      cmd << "signal " << SIGINT << "\n";
                      if (send_command(cmd.str()) == 0)
                        {
                          ++interrupts_sent;
                          continue;
                        }
                    }
                  close();
                }
            }
      }

    int send_command(const string& cmd)
      {
        if (!IN)
          return 2;
        if (fputs(cmd.c_str(), IN) < 0 ||
            fflush(IN) != 0)
          return 1;
        return 0;
      }

    int send_file(const string& filename, const string& dest)
      {
        int rc = 0;
        FILE* f = fopen(filename.c_str(), "r");
        if (!f)
          return 1;

        struct stat fs;
        rc = fstat(fileno(f), &fs);
        if (!rc)
          {
            ostringstream cmd;
            cmd << "file " << fs.st_size << " " << dest << "\n";
            rc = send_command(cmd.str());
          }

        off_t i = 0;
        while (!rc && i < fs.st_size)
          {
            char buf[4096];
            size_t r = sizeof(buf);
            if (fs.st_size - i < (off_t)r)
              r = fs.st_size - i;
            r = fread(buf, 1, r, f);
            if (r == 0)
              rc = 1;
            else
              {
                size_t w = fwrite(buf, 1, r, IN);
                if (w != r)
                  rc = 1;
                else
                  i += w;
              }
          }
        if (!rc)
          rc = fflush(IN);

        fclose(f);
        return rc;
      }

    int send_arg(const string& arg)
      {
        ostringstream cmd;
        cmd << "arg " << arg.size() << "\n";
        int rc = send_command(cmd.str());
        if (!rc)
          {
            size_t w = fwrite(arg.c_str(), 1, arg.size(), IN);
            if (w != arg.size())
              rc = 1;
          }
        if (!rc)
          rc = fflush(IN);
        return rc;
      }

  protected:
    stapsh(systemtap_session& s)
      : remote(s), interrupts_sent(0),
        fdin(-1), fdout(-1), IN(0), OUT(0)
      {}

    virtual int start()
      {
        int rc = 0;

        string localmodule = s->tmpdir + "/" + s->module_name + ".ko";
        string remotemodule = s->module_name + ".ko";
        if ((rc = send_file(localmodule, remotemodule)))
          return rc;

        // XXX upload module.sig [and uprobes, uprobes.sig]

        // Send the staprun args
        // NB: The remote is left to decide its own staprun path
        vector<string> cmd = make_run_command(*s, remotemodule);
        for (unsigned i = 1; i < cmd.size(); ++i)
          if ((rc = send_arg(cmd[i])))
            return rc;

        rc = send_command("run\n");

        if (!rc)
          {
            long flags = fcntl(fdout, F_GETFL) | O_NONBLOCK;
            fcntl(fdout, F_SETFL, flags);
          }

        return rc;
      }

    void close()
      {
        if (IN) fclose(IN);
        if (OUT) fclose(OUT);
        IN = OUT = NULL;
        fdin = fdout = -1;
      }

    virtual int finish()
      {
        close();
        return 0;
      }

    void set_child_fds(int in, int out)
      {
        if (fdin >= 0 || fdout >= 0 || IN || OUT)
          throw runtime_error("stapsh file descriptors already set!");

        fdin = in;
        fdout = out;
        IN = fdopen(fdin, "w");
        OUT = fdopen(fdout, "r");
        if (!IN || !OUT)
          throw runtime_error("invalid file descriptors for stapsh!");

        if (send_command("stap " VERSION "\n"))
          throw runtime_error("error sending hello to stapsh!");

        char reply[1024];
        if (!fgets(reply, sizeof(reply), OUT))
          throw runtime_error("error receiving hello from stapsh!");

        // stapsh VERSION MACHINE RELEASE
        vector<string> uname;
        tokenize(reply, uname, " \t\r\n");
        if (uname.size() != 4 || uname[0] != "stapsh")
          throw runtime_error("failed to get uname from stapsh");
        // XXX check VERSION compatibility

        this->s = s->clone(uname[2], uname[3]);
      }

  public:
    virtual ~stapsh() { close(); }
};


class direct_stapsh : public stapsh {
  private:
    pid_t child;

    direct_stapsh(systemtap_session& s)
      : stapsh(s), child(0)
      {
        int in, out;
        vector<string> cmd;
        cmd.push_back(BINDIR "/stapsh");
        child = stap_spawn_piped(s.verbose, cmd, &in, &out);
        if (child <= 0)
          throw runtime_error("error launching stapsh!");

        try
          {
            set_child_fds(in, out);
          }
        catch (runtime_error&)
          {
            finish();
            throw;
          }
      }

    virtual int finish()
      {
        int rc = stapsh::finish();
        if (child <= 0)
          return rc;

        int rc2 = stap_waitpid(s->verbose, child);
        child = 0;
        return rc ?: rc2;
      }

  public:
    friend class remote;

    virtual ~direct_stapsh() {}
};


#if 1 // stapsh-based ssh_remote
class ssh_remote : public stapsh {
  private:
    pid_t child;

    ssh_remote(systemtap_session& s, const string& host)
      : stapsh(s), child(0)
      {
        init(host);
      }

    ssh_remote(systemtap_session& s, const uri_decoder& ud)
      : stapsh(s), child(0)
      {
        if (!ud.has_authority || ud.authority.empty())
          throw runtime_error("ssh target requires a hostname");
        if (!ud.path.empty() && ud.path != "/")
          throw runtime_error("ssh target URI doesn't support a /path");
        if (ud.has_query)
          throw runtime_error("ssh target URI doesn't support a ?query");
        if (ud.has_fragment)
          throw runtime_error("ssh target URI doesn't support a #fragment");

        init(ud.authority);
      }

    void init(const string& host)
      {
        // mask signals while we spawn, so we can manually send even tty
        // signals *through* ssh rather than to ssh itself
        sigset_t mask, oldmask;
        sigemptyset (&mask);
        sigaddset (&mask, SIGHUP);
        sigaddset (&mask, SIGPIPE);
        sigaddset (&mask, SIGINT);
        sigaddset (&mask, SIGTERM);
        sigprocmask (SIG_BLOCK, &mask, &oldmask);

        int in, out;
        vector<string> cmd;
        cmd.push_back("ssh");
        cmd.push_back("-q");
        cmd.push_back(host);
        cmd.push_back("stapsh"); // NB: relies on remote $PATH
        child = stap_spawn_piped(s->verbose, cmd, &in, &out);
        sigprocmask (SIG_SETMASK, &oldmask, NULL); // back to normal signals
        if (child <= 0)
          throw runtime_error("error launching stapsh!");

        try
          {
            set_child_fds(in, out);
          }
        catch (runtime_error&)
          {
            finish();
            throw;
          }
      }

    int finish()
      {
        int rc = stapsh::finish();
        if (child <= 0)
          return rc;

        int rc2 = stap_waitpid(s->verbose, child);
        child = 0;
        return rc ?: rc2;
      }

  public:
    friend class remote;

    virtual ~ssh_remote() { }
};
#else // !stapsh-based ssh_remote
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
          throw runtime_error("ssh target requires a hostname");
        if (!ud.path.empty() && ud.path != "/")
          throw runtime_error("ssh target URI doesn't support a /path");
        if (ud.has_query)
          throw runtime_error("ssh target URI doesn't support a ?query");
        if (ud.has_fragment)
          throw runtime_error("ssh target URI doesn't support a #fragment");

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
          throw runtime_error("No tmpdir available for ssh control master");

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
          {
            ostringstream err;
            err << "failed to create an ssh control master for " << host
                << " : rc=" << rc;
            throw runtime_error(err.str());
          }

        if (s->verbose>1)
          clog << "Created ssh control master at "
               << lex_cast_qstring(ssh_control) << endl;
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
          cerr << "failed to stop the ssh control master for " << host
               << " : rc=" << rc << endl;

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
          throw runtime_error("failed to get uname from " + host
                              + " : rc=" + lex_cast(rc));
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
              cerr << "failed to make a tempdir on " << host
                   << " : rc=" << rc << endl;
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
            cerr << "failed to copy the module to " << host
                 << " : rc=" << rc << endl;
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
              cerr << "failed to run the module on " << host
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
            vector<string> cmd = ssh_args;
            cmd.push_back("-t");
            cmd.push_back("rm -r " + cmdstr_quoted(tmpdir));
            int rc2 = stap_system(s->verbose, cmd);
            if (rc2 != 0)
              cerr << "failed to delete the tempdir on " << host
                   << " : rc=" << rc2 << endl;
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
#endif


remote*
remote::create(systemtap_session& s, const string& uri)
{
  try
    {
      if (uri == "direct")
        return new direct(s);
      else if (uri == "stapsh")
        return new direct_stapsh(s);
      else if (uri.find(':') != string::npos)
        {
          const uri_decoder ud(uri);
          if (ud.scheme == "ssh")
            return new ssh_remote(s, ud);
          else
            {
              ostringstream msg;
              msg << "unrecognized URI scheme '" << ud.scheme
                  << "' in remote: " << uri;
              throw runtime_error(msg.str());
            }
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

  // mask signals while we're preparing to poll
  sigset_t mask, oldmask;
  sigemptyset (&mask);
  sigaddset (&mask, SIGHUP);
  sigaddset (&mask, SIGPIPE);
  sigaddset (&mask, SIGINT);
  sigaddset (&mask, SIGTERM);
  sigprocmask (SIG_BLOCK, &mask, &oldmask);

  for (;;) // polling loop for remotes that have fds to watch
    {
      vector<pollfd> fds;
      for (unsigned i = 0; i < remotes.size(); ++i)
	remotes[i]->prepare_poll (fds);
      if (fds.empty())
	break;

      rc = ppoll (&fds[0], fds.size(), NULL, &oldmask);
      if (rc < 0 && errno != EINTR)
	break;

      for (unsigned i = 0; i < remotes.size(); ++i)
	remotes[i]->handle_poll (fds);
    }

  sigprocmask (SIG_SETMASK, &oldmask, NULL);

  for (unsigned i = 0; i < remotes.size(); ++i)
    {
      rc = remotes[i]->finish();
      if (!ret)
        ret = rc;
    }

  return ret;
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
