// Copyright (C) Andrew Tridgell 2002 (original file)
// Copyright (C) 2006-2010 Red Hat Inc. (systemtap changes)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "util.h"
#include "stap-probe.h"
#include <stdexcept>
#include <cerrno>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <cassert>

extern "C" {
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <regex.h>
}

using namespace std;


// Return current users home directory or die.
const char *
get_home_directory(void)
{
  const char *p = getenv("HOME");
  if (p)
    return p;

  struct passwd *pwd = getpwuid(getuid());
  if (pwd)
    return pwd->pw_dir;

  throw runtime_error("Unable to determine home directory");
  return NULL;
}


// Get the size of a file in bytes
size_t
get_file_size(const string &path)
{
  struct stat file_info;

  if (stat(path.c_str(), &file_info) == 0)
    return file_info.st_size;
  else
    return 0;
}

// Get the size of a file in bytes
size_t
get_file_size(int fd)
{
  struct stat file_info;

  if (fstat(fd, &file_info) == 0)
    return file_info.st_size;
  else
    return 0;
}

// Check that a file is present
bool
file_exists (const string &path)
{
  struct stat file_info;

  if (stat(path.c_str(), &file_info) == 0)
    return true;

  return false;
}

// Copy a file.  The copy is done via a temporary file and atomic
// rename.
bool
copy_file(const string& src, const string& dest, bool verbose)
{
  int fd1, fd2;
  char buf[10240];
  int n;
  string tmp;
  char *tmp_name;
  mode_t mask;

  if (verbose)
    clog << "Copying " << src << " to " << dest << endl;

  // Open the src file.
  fd1 = open(src.c_str(), O_RDONLY);
  if (fd1 == -1)
    goto error;

  // Open the temporary output file.
  tmp = dest + string(".XXXXXX");
  tmp_name = (char *)tmp.c_str();
  fd2 = mkstemp(tmp_name);
  if (fd2 == -1)
    {
      close(fd1);
      goto error;
    }

  // Copy the src file to the temporary output file.
  while ((n = read(fd1, buf, sizeof(buf))) > 0)
    {
      if (write(fd2, buf, n) != n)
        {
	  close(fd2);
	  close(fd1);
	  unlink(tmp_name);
          goto error;
	}
    }
  close(fd1);

  // Set the permissions on the temporary output file.
  mask = umask(0);
  fchmod(fd2, 0666 & ~mask);
  umask(mask);

  // Close the temporary output file.  The close can fail on NFS if
  // out of space.
  if (close(fd2) == -1)
    {
      unlink(tmp_name);
      goto error;
    }

  // Rename the temporary output file to the destination file.
  unlink(dest.c_str());
  if (rename(tmp_name, dest.c_str()) == -1)
    {
      unlink(tmp_name);
      goto error;
    }

  return true;

error:
  cerr << "Copy failed (\"" << src << "\" to \"" << dest << "\"): "
       << strerror(errno) << endl;
  return false;
}


// Make sure a directory exists.
int
create_dir(const char *dir, int mode)
{
  struct stat st;
  if (stat(dir, &st) == 0)
    {
      if (S_ISDIR(st.st_mode))
	return 0;
      errno = ENOTDIR;
      return 1;
    }

  // Create the directory. We must create each component
  // of the path ourselves.
  vector<string> components;
  tokenize (dir, components, "/");
  string path;
  if (*dir == '/')
    {
      // Absolute path
      path = "/";
    }
  unsigned limit = components.size ();
  assert (limit != 0);
  for (unsigned ix = 0; ix < limit; ++ix)
    {
      path += components[ix] + '/';
      if (mkdir(path.c_str (), mode) != 0 && errno != EEXIST)
	return 1;
    }

  return 0;
}

// Remove a file or directory
int
remove_file_or_dir (const char *name)
{
  int rc;
  struct stat st;

  if ((rc = stat(name, &st)) != 0)
    {
      if (errno == ENOENT)
	return 0;
      return 1;
    }

  if (remove (name) != 0)
    return 1;
  cerr << "remove returned 0" << endl;
  return 0;
}

// Determine whether the current user is in the given group
// by gid.
bool
in_group_id (gid_t target_gid)
{
  // According to the getgroups() man page, getgroups() may not
  // return the effective gid, so try to match it first. */
  if (target_gid == getegid())
    return true;

  // Get the list of the user's groups.
  int ngids = getgroups(0, 0); // Returns the number to allocate.
  if (ngids > 0) {
    gid_t gidlist[ngids];
    ngids = getgroups(ngids, gidlist);
    for (int i = 0; i < ngids; i++) {
      // If the user is a member of the target group, then we're done.
      if (gidlist[i] == target_gid)
	return true;
    }
  }
  if (ngids < 0) {
    cerr << "Unable to retrieve group list" << endl;
    return false;
  }

  // The user is not a member of the target group
  return false;
}

/*
 * Returns a string describing memory resource usage.
 * Since it seems getrusage() doesn't maintain the mem related fields,
 * this routine parses /proc/self/statm to get the statistics.
 */
string
getmemusage ()
{
  static long sz = sysconf(_SC_PAGESIZE);

  long pages, kb;
  ostringstream oss;
  ifstream statm("/proc/self/statm");
  statm >> pages;
  kb = pages * sz / 1024;
  oss << "using " << kb << "virt/";
  statm >> pages;
  kb = pages * sz / 1024;
  oss << kb << "res/";
  statm >> pages;
  kb = pages * sz / 1024;
  oss << kb << "shr kb, ";
  return oss.str();
}

void
tokenize(const string& str, vector<string>& tokens,
	 const string& delimiters = " ")
{
  // Skip delimiters at beginning.
  string::size_type lastPos = str.find_first_not_of(delimiters, 0);
  // Find first "non-delimiter".
  string::size_type pos     = str.find_first_of(delimiters, lastPos);

  while (pos != string::npos || lastPos != string::npos)
    {
      // Found a token, add it to the vector.
      tokens.push_back(str.substr(lastPos, pos - lastPos));
      // Skip delimiters.  Note the "not_of"
      lastPos = str.find_first_not_of(delimiters, pos);
      // Find next "non-delimiter"
      pos = str.find_first_of(delimiters, lastPos);
    }
}


// Resolve an executable name to a canonical full path name, with the
// same policy as execvp().  A program name not containing a slash
// will be searched along the $PATH.

string find_executable(const string& name, const string& env_path)
{
  string retpath;

  if (name.size() == 0)
    return name;

  struct stat st;

  if (name.find('/') != string::npos) // slash in the path already?
    {
      retpath = name;
    }
  else // Nope, search $PATH.
    {
      char *path = getenv(env_path.c_str());
      if (path)
        {
          // Split PATH up.
          vector<string> dirs;
          tokenize(string(path), dirs, string(":"));

          // Search the path looking for the first executable of the right name.
          for (vector<string>::iterator i = dirs.begin(); i != dirs.end(); i++)
            {
              string fname = *i + "/" + name;
              const char *f = fname.c_str();

              // Look for a normal executable file.
              if (access(f, X_OK) == 0
                  && stat(f, &st) == 0
                  && S_ISREG(st.st_mode))
                {
                  retpath = fname;
                  break;
                }
            }
        }
    }


  // Could not find the program on the $PATH.  We'll just fall back to
  // the unqualified name, which our caller will probably fail with.
  if (retpath == "")
    retpath = name;

  // Canonicalize the path name.
  char *cf = canonicalize_file_name (retpath.c_str());
  if (cf)
    {
      retpath = string(cf);
      free (cf);
    }

  return retpath;
}



const string cmdstr_quoted(const string& cmd)
{
	// original cmd : substr1
	//           or : substr1'substr2
	//           or : substr1'substr2'substr3......
	// after quoted :
	// every substr(even it's empty) is quoted by ''
	// every single-quote(') is quoted by ""
	// examples: substr1 --> 'substr1'
	//           substr1'substr2 --> 'substr1'"'"'substr2'

	string quoted_cmd;
	string quote("'");
	string replace("'\"'\"'");
	string::size_type pos = 0;

	quoted_cmd += quote;
	for (string::size_type quote_pos = cmd.find(quote, pos);
			quote_pos != string::npos;
			quote_pos = cmd.find(quote, pos)) {
		quoted_cmd += cmd.substr(pos, quote_pos - pos);
		quoted_cmd += replace;
		pos = quote_pos + 1;
	}
	quoted_cmd += cmd.substr(pos, cmd.length() - pos);
	quoted_cmd += quote;

	return quoted_cmd;
}


string
git_revision(const string& path)
{
  string revision = "(not-a-git-repository)";
  string git_dir = path + "/.git/";

  struct stat st;
  if (stat(git_dir.c_str(), &st) == 0)
    {
      string command = "git --git-dir=\"" + git_dir
        + "\" rev-parse HEAD 2>/dev/null";

      char buf[50];
      FILE *fp = popen(command.c_str(), "r");
      if (fp != NULL)
        {
          char *bufp = fgets(buf, sizeof(buf), fp);
          int rc = pclose(fp);
          if (bufp != NULL && rc == 0)
            revision = buf;
        }
    }

  return revision;
}


// XXX written only from the main thread, but can be read in a
//     signal handler.  synchronization needed?
static set<pid_t> spawned_pids;


int
stap_waitpid(int verbose, pid_t pid)
{
  int ret, status;
  if (verbose > 1 && spawned_pids.count(pid) == 0)
    clog << "Spawn waitpid call on unmanaged pid " << pid << endl;
  ret = waitpid(pid, &status, 0);
  if (ret == pid)
    {
      spawned_pids.erase(pid);
      ret = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      if (verbose > 2)
        clog << "Spawn waitpid result (0x" << lex_cast_hex(status) << "): " << ret << endl;
    }
  else
    {
      if (verbose > 1)
        clog << "Spawn waitpid error (" << ret << "): " << strerror(errno) << endl;
      ret = -1;
    }
  return ret;
}

// Runs a command with a saved PID, so we can kill it from the signal handler
pid_t
stap_spawn(int verbose, const std::string& command)
{
  const char *cmd = command.c_str();
  char const * const argv[] = { "sh", "-c", cmd, NULL };
  int ret;

  pid_t pid = 0;

  if (verbose > 1)
    clog << "Running " << command << endl;

  ret = posix_spawn(&pid, "/bin/sh", NULL, NULL,
                    const_cast<char * const *>(argv), environ);
  PROBE2(stap, stap_system__spawn, ret, pid);
  if (ret != 0)
    {
      if (verbose > 1)
        clog << "Spawn error (" << ret << "): " << strerror(ret) << endl;
      pid = -1;
    }
  else
    spawned_pids.insert(pid);
  return pid;
}

// Runs a command with a saved PID, so we can kill it from the signal handler,
// and wait for it to finish.
int
stap_system(int verbose, const std::string& command)
{
  const char *cmd;
  cmd = command.c_str();
  PROBE1(stap, stap_system__start, cmd);

  int ret = -1;
  pid_t pid = stap_spawn(verbose, command);
  if (pid > 0)
    ret = stap_waitpid(verbose, pid);
  PROBE1(stap, stap_system__complete, ret);
  return ret;
}

// Like stap_system, but capture stdout
int
stap_system_read(int verbose, const string& command, ostream& out)
{
  int ret, pfd[2];

  ret = pipe(pfd);
  if (ret != 0)
    {
      return -1;
    }

  pid_t child = fork();
  if (child < 0)
    {
      return -1;
    }
  else if (child == 0)
    {
      // remap the write fd to stdout
      dup2(pfd[1], 1);
      close(pfd[0]);
      close(pfd[1]);

      // exec the desired command
      char const * const argv[] = { "sh", "-c", command.c_str(), NULL };
      ret = execv("/bin/sh", const_cast<char * const *>(argv));
      exit(ret); // only reached on exec error
    }
  else
    spawned_pids.insert(child);

  // read everything from the child
  string readpath = "/proc/self/fd/" + lex_cast(pfd[0]);
  ifstream in(readpath.c_str());
  close(pfd[0]);
  close(pfd[1]);
  out << in.rdbuf();

  return stap_waitpid(verbose, child);
}


// Send a signal to our spawned commands
int
kill_stap_spawn(int sig)
{
  int ret = 0;
  for (set<pid_t>::iterator it = spawned_pids.begin();
       it != spawned_pids.end(); ++it)
    if (*it > 0)
      {
        int pidret = kill(*it, sig);
        if (!ret)
          ret = pidret;
      }
  return ret;
}



void assert_regexp_match (const string& name, const string& value, const string& re)
{
  typedef map<string,regex_t*> cache;
  static cache compiled;
  cache::iterator it = compiled.find (re);
  regex_t* r = 0;
  if (it == compiled.end())
    {
      r = new regex_t;
      int rc = regcomp (r, re.c_str(), REG_ICASE|REG_NOSUB|REG_EXTENDED);
      if (rc) {
        cerr << "regcomp " << re << " (" << name << ") error rc=" << rc << endl;
        exit(1);
      }
      compiled[re] = r;
    }
  else
    r = it->second;

  // run regexec
  int rc = regexec (r, value.c_str(), 0, 0, 0);
  if (rc)
    {
      cerr << "ERROR: Safety pattern mismatch for " << name
           << " ('" << value << "' vs. '" << re << "') rc=" << rc << endl;
      exit(1);
    }
}


int regexp_match (const string& value, const string& re, vector<string>& matches)
{
  typedef map<string,regex_t*> cache;  // separate cache because we use different regcomp options
  static cache compiled;
  cache::iterator it = compiled.find (re);
  regex_t* r = 0;
  if (it == compiled.end())
    {
      r = new regex_t;
      int rc = regcomp (r, re.c_str(), REG_EXTENDED); /* REG_ICASE? */
      if (rc) {
        cerr << "regcomp " << re << " error rc=" << rc << endl;
        exit(1);
      }
      compiled[re] = r;
    }
  else
    r = it->second;


  // run regexec
#define maxmatches 10
  regmatch_t rm[maxmatches];

  int rc = regexec (r, value.c_str(), maxmatches, rm, 0);
  if (rc) return rc;

  matches.erase(matches.begin(), matches.end());
  for (unsigned i=0; i<maxmatches; i++) // XXX: ideally, the number of actual subexpressions in re
    {
      if (rm[i].rm_so >= 0)
        matches.push_back(value.substr (rm[i].rm_so, rm[i].rm_eo-rm[i].rm_so));
      else
        matches.push_back("");
    }

  return 0;
}


bool contains_glob_chars (const string& str)
{
  return (str.find("*") != str.npos ||
          str.find("?") != str.npos ||
          str.find("[") != str.npos);
}

string
normalize_machine(const string& machine)
{
  // PR4186: Copy logic from coreutils uname (uname -i) to squash
  // i?86->i386.  Actually, copy logic from linux top-level Makefile
  // to squash uname -m -> $(SUBARCH).
  //
  // This logic needs to match the logic in the stap_get_arch shell
  // function in stap-env.

  if (machine == "i486") return "i386";
  else if (machine == "i586") return "i386";
  else if (machine == "i686") return "i386";
  else if (machine == "sun4u") return "sparc64";
  else if (machine.substr(0,3) == "arm") return "arm";
  else if (machine == "sa110") return "arm";
  else if (machine == "s390x") return "s390";
  else if (machine.substr(0,3) == "ppc") return "powerpc";
  else if (machine.substr(0,4) == "mips") return "mips";
  else if (machine.substr(0,3) == "sh2") return "sh";
  else if (machine.substr(0,3) == "sh3") return "sh";
  else if (machine.substr(0,3) == "sh4") return "sh";
  return machine;
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
