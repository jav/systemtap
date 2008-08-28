// Copyright (C) Andrew Tridgell 2002 (original file)
// Copyright (C) 2006 Red Hat Inc. (systemtap changes)
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
#include <stdexcept>
#include <cerrno>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
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


// Copy a file.  The copy is done via a temporary file and atomic
// rename.
int
copy_file(const char *src, const char *dest)
{
  int fd1, fd2;
  char buf[10240];
  int n;
  string tmp;
  char *tmp_name;
  mode_t mask;

  // Open the src file.
  fd1 = open(src, O_RDONLY);
  if (fd1 == -1)
    return -1;

  // Open the temporary output file.
  tmp = dest + string(".XXXXXX");
  tmp_name = (char *)tmp.c_str();
  fd2 = mkstemp(tmp_name);
  if (fd2 == -1)
    {
      close(fd1);
      return -1;
    }

  // Copy the src file to the temporary output file.
  while ((n = read(fd1, buf, sizeof(buf))) > 0)
    {
      if (write(fd2, buf, n) != n)
        {
	  close(fd2);
	  close(fd1);
	  unlink(tmp_name);
	  return -1;
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
      return -1;
    }

  // Rename the temporary output file to the destination file.
  unlink(dest);
  if (rename(tmp_name, dest) == -1)
    {
      unlink(tmp_name);
      return -1;
    }

  return 0;
}


// Make sure a directory exists.
int
create_dir(const char *dir)
{
  struct stat st;
  if (stat(dir, &st) == 0)
    {
      if (S_ISDIR(st.st_mode))
	return 0;
      errno = ENOTDIR;
      return 1;
    }

  if (mkdir(dir, 0777) != 0 && errno != EEXIST)
    return 1;

  return 0;
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

string find_executable(const string& name)
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
      char *path = getenv("PATH");
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

