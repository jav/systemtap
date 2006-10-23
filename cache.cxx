#include "session.h"
#include "cache.h"
#include "util.h"
#include <cerrno>
#include <string>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
}

using namespace std;


void
add_to_cache(systemtap_session& s)
{
  string module_src_path = s.tmpdir + "/" + s.module_name + ".ko";
  if (s.verbose > 1)
    clog << "Copying " << module_src_path << " to " << s.hash_path << endl;
  if (copy_file(module_src_path.c_str(), s.hash_path.c_str()) != 0)
    {
      cerr << "Copy failed (\"" << module_src_path << "\" to \""
	   << s.hash_path << "\"): " << strerror(errno) << endl;
      return;
    }

  string c_dest_path = s.hash_path;
  if (c_dest_path.rfind(".ko") == (c_dest_path.size() - 3))
    c_dest_path.resize(c_dest_path.size() - 3);
  c_dest_path += ".c";

  if (s.verbose > 1)
    clog << "Copying " << s.translated_source << " to " << c_dest_path
	 << endl;
  if (copy_file(s.translated_source.c_str(), c_dest_path.c_str()) != 0)
    {
      cerr << "Copy failed (\"" << s.translated_source << "\" to \""
	   << c_dest_path << "\"): " << strerror(errno) << endl;
    }
}


bool
get_from_cache(systemtap_session& s)
{
  string module_dest_path = s.tmpdir + "/" + s.module_name + ".ko";
  string c_src_path = s.hash_path;
  int fd_module, fd_c;

  if (c_src_path.rfind(".ko") == (c_src_path.size() - 3))
    c_src_path.resize(c_src_path.size() - 3);
  c_src_path += ".c";

  // See if module exists
  fd_module = open(s.hash_path.c_str(), O_RDONLY);
  if (fd_module == -1)
    {
      // It isn't in cache.
      return false;
    }

  // See if C file exists.
  fd_c = open(c_src_path.c_str(), O_RDONLY);
  if (fd_c == -1)
    {
      // The module is there, but the C file isn't.  Cleanup and
      // return.
      close(fd_module);
      unlink(s.hash_path.c_str());
      return false;
    }

  // Copy the cached C file to the destination
  if (copy_file(c_src_path.c_str(), s.translated_source.c_str()) != 0)
    {
      cerr << "Copy failed (\"" << c_src_path << "\" to \""
	   << s.translated_source << "\"): " << strerror(errno) << endl;
      close(fd_module);
      close(fd_c);
      return false;
    }

  // Copy the cached module to the destination (if needed)
  if (s.last_pass != 3)
    {
      if (copy_file(s.hash_path.c_str(), module_dest_path.c_str()) != 0)
        {
	  cerr << "Copy failed (\"" << s.hash_path << "\" to \""
	       << module_dest_path << "\"): " << strerror(errno) << endl;
	  unlink(c_src_path.c_str());
	  close(fd_module);
	  close(fd_c);
	  return false;
	}
    }

  // If everything worked, tell the user.  We need to do this here,
  // since if copying the cached C file works, but copying the cached
  // module fails, we remove the cached C file and let the C file get
  // regenerated.
  if (s.verbose)
    {
      clog << "Pass 3: using cached " << c_src_path << endl;
      if (s.last_pass != 3)
	clog << "Pass 4: using cached " << s.hash_path << endl;
    }

  close(fd_module);
  close(fd_c);
  return true;
}
