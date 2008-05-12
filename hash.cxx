// Copyright (C) Andrew Tridgell 2002 (original file)
// Copyright (C) 2006-2007 Red Hat Inc. (systemtap changes)
// 
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "config.h"
#include "session.h"
#include "hash.h"
#include "util.h"

#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <cerrno>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
}

using namespace std;

void
hash::start()
{
  mdfour_begin(&md4);
}


void
hash::add(const unsigned char *buffer, size_t size)
{
  mdfour_update(&md4, buffer, size);
}


void
hash::result(string& r)
{
  ostringstream rstream;
  unsigned char sum[16];

  mdfour_update(&md4, NULL, 0);
  mdfour_result(&md4, sum);

  for (int i=0; i<16; i++)
    {
      rstream << hex << setfill('0') << setw(2) << (unsigned)sum[i];
    }
  rstream << "_" << setw(0) << dec << (unsigned)md4.totalN;
  r = rstream.str();
}


void
find_hash (systemtap_session& s, const string& script)
{
  hash h;
  int nlevels = 1;
  struct stat st;

  // We use a N level subdir for the cache path.  Let N be adjustable.
  const char *s_n;
  if ((s_n = getenv("SYSTEMTAP_NLEVELS")))
    {
      nlevels = atoi(s_n);
      if (nlevels < 1) nlevels = 1;
      if (nlevels > 8) nlevels = 8;
    }

  // Hash getuid.  This really shouldn't be necessary (since who you
  // are doesn't change the generated output), but the hash gets used
  // as the module name.  If two different users try to run the same
  // script at the same time, we need something to differentiate the
  // module name.
  h.add(getuid());

  // Hash kernel release and arch.
  h.add(s.kernel_release);
  h.add(s.architecture);

  // Hash user-specified arguments (that change the generated module).
  h.add(s.bulk_mode);			// '-b'
  h.add(s.merge);			// '-M'
  h.add(s.timing);			// '-t'
  h.add(s.prologue_searching);		// '-P'
  h.add(s.ignore_vmlinux);		// --ignore-vmlinux
  h.add(s.ignore_dwarf);		// --ignore-dwarf
  h.add(s.consult_symtab);		// --kelf, --kmap
  if (!s.kernel_symtab_path.empty())	// --kmap
    {
      h.add(s.kernel_symtab_path);
      if (stat(s.kernel_symtab_path.c_str(), &st) == 0)
        {
	  // NB: stat of /proc/kallsyms always returns size=0, mtime=now...
	  // which is a good reason to use the default /boot/System.map-2.6.xx
	  // instead.
          h.add(st.st_size);
	  h.add(st.st_mtime);
        }
    }
  for (unsigned i = 0; i < s.macros.size(); i++)
    h.add(s.macros[i]);

  // Hash runtime path (that gets added in as "-I path").
  h.add(s.runtime_path);

  // Hash compiler path, size, and mtime.  We're just going to assume
  // we'll be using gcc, which should be correct most of the time.
  string gcc_path;
  if (find_executable("gcc", gcc_path))
    {
      if (stat(gcc_path.c_str(), &st) == 0)
        {
	  h.add(gcc_path);
	  h.add(st.st_size);
	  h.add(st.st_mtime);
	}
    }

  // Hash the systemtap size and mtime.  We could use VERSION/DATE,
  // but when developing systemtap that doesn't work well (since you
  // can compile systemtap multiple times in 1 day).  Since we don't
  // know exactly where we're getting run from, we'll use
  // /proc/self/exe.
  if (stat("/proc/self/exe", &st) == 0)
  {
      h.add(st.st_size);
      h.add(st.st_mtime);
  }

  // Add in pass 2 script output.
  h.add(script);

  // Use a N level subdir for the cache path to reduce the impact on
  // filesystems which are slow for large directories.
  string hashdir = s.cache_path;
  string result;
  h.result(result);

  for (int i = 0; i < nlevels; i++)
    {
      hashdir += string("/") + result[i*2] + result[i*2 + 1];
      if (create_dir(hashdir.c_str()) != 0)
        {
	  cerr << "Warning: failed to create cache directory (\""
	       << hashdir + "\"): " << strerror(errno) << endl;
	  cerr << "Disabling cache support." << endl;
	  s.use_cache = false;
	  return;
	}
    }

  // Update module name to be 'stap_{hash start}'.  '{hash start}'
  // must not be too long.  This shouldn't happen, since the maximum
  // size of a hash is 32 fixed chars + 1 (for the '_') + a max of 11.
  s.module_name = "stap_" + result;
  if (s.module_name.size() >= (MODULE_NAME_LEN - 1))
    s.module_name.resize(MODULE_NAME_LEN - 1);

  // 'ccache' would use a hash path of something like:
  //    s.hash_path = hashdir + "/" + result.substr(nlevels);
  // which would look like:
  //    ~/.stap_cache/A/B/CDEFGHIJKLMNOPQRSTUVWXYZABCDEF_XXX
  //
  // We're using the following so that the module can be used straight
  // from the cache if desired.  This ends up looking like this:
  //    ~/.stap_cache/A/B/stap_ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEF_XXX.ko
  s.hash_path = hashdir + "/" + s.module_name + ".ko";

  // Update C source name with new module_name.
  s.translated_source = string(s.tmpdir) + "/" + s.module_name + ".c";
}
