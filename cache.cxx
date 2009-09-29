// systemtap cache manager
// Copyright (C) 2006-2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "session.h"
#include "cache.h"
#include "util.h"
#include "sys/sdt.h"
#include <cerrno>
#include <string>
#include <fstream>
#include <cstring>
#include <cassert>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>
}

using namespace std;


#define SYSTEMTAP_CACHE_MAX_FILENAME "cache_mb_limit"
#define SYSTEMTAP_CACHE_DEFAULT_MB 64

struct cache_ent_info {
  string path;
  bool is_module;
  size_t size;
  long weight;  //lower == removed earlier

  cache_ent_info(const string& path, bool is_module);
  bool operator<(const struct cache_ent_info& other) const
  { return weight < other.weight; }
  void unlink() const;
};

static void clean_cache(systemtap_session& s);


void
add_to_cache(systemtap_session& s)
{
  bool verbose = s.verbose > 1;

  // PR10543: clean the cache *before* we try putting something new into it.
  // We don't want to risk having the brand new contents being erased again.
  clean_cache(s);

  string stapconf_src_path = s.tmpdir + "/" + s.stapconf_name;
  if (!copy_file(stapconf_src_path, s.stapconf_path, verbose))
    {
      s.use_cache = false;
      return;
    }

  string module_src_path = s.tmpdir + "/" + s.module_name + ".ko";
  STAP_PROBE2(stap, cache__add__module, module_src_path.c_str(), s.hash_path.c_str());
  if (!copy_file(module_src_path, s.hash_path, verbose))
    {
      s.use_cache = false;
      return;
    }

  string c_dest_path = s.hash_path;
  if (c_dest_path.rfind(".ko") == (c_dest_path.size() - 3))
    c_dest_path.resize(c_dest_path.size() - 3);
  c_dest_path += ".c";

  STAP_PROBE2(stap, cache__add__source, s.translated_source.c_str(), c_dest_path.c_str());
  if (!copy_file(s.translated_source, c_dest_path, verbose))
    {
      // NB: this is not so severe as to prevent reuse of the .ko
      // already copied.
      //
      // s.use_cache = false;
    }
}


bool
get_from_cache(systemtap_session& s)
{
  string stapconf_dest_path = s.tmpdir + "/" + s.stapconf_name;
  string module_dest_path = s.tmpdir + "/" + s.module_name + ".ko";
  string c_src_path = s.hash_path;
  int fd_stapconf, fd_module, fd_c;

  if (c_src_path.rfind(".ko") == (c_src_path.size() - 3))
    c_src_path.resize(c_src_path.size() - 3);
  c_src_path += ".c";

  // See if stapconf exists
  fd_stapconf = open(s.stapconf_path.c_str(), O_RDONLY);
  if (fd_stapconf == -1)
    {
      // It isn't in cache.
      return false;
    }

  // Copy the stapconf header file to the destination
  if (!copy_file(s.stapconf_path, stapconf_dest_path))
    {
      close(fd_stapconf);
      return false;
    }

  // We're done with this file handle.
  close(fd_stapconf);

  if (s.verbose > 1)
    clog << "Pass 3: using cached " << s.stapconf_path << endl;

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
  if (!copy_file(c_src_path, s.translated_source))
    {
      close(fd_module);
      close(fd_c);
      return false;
    }

  // Copy the cached module to the destination (if needed)
  if (s.last_pass != 3)
    {
      if (!copy_file(s.hash_path, module_dest_path))
        {
	  unlink(c_src_path.c_str());
	  close(fd_module);
	  close(fd_c);
	  return false;
	}
    }

  // We're done with these file handles.
  close(fd_module);
  close(fd_c);

  // To preserve semantics (since this will happen if we're not
  // caching), display the C source if the last pass is 3.
  if (s.last_pass == 3)
    {
      ifstream i (s.translated_source.c_str());
      cout << i.rdbuf();
    }
  // And similarly, display probe module name for -p4.
  if (s.last_pass == 4)
    cout << s.hash_path << endl;

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

  STAP_PROBE2(stap, cache__get, c_src_path.c_str(), s.hash_path.c_str());

  return true;
}


static void
clean_cache(systemtap_session& s)
{
  if (s.cache_path != "")
    {
      /* Get cache size limit from file in the stap cache dir */
      string cache_max_filename = s.cache_path + "/";
      cache_max_filename += SYSTEMTAP_CACHE_MAX_FILENAME;
      ifstream cache_max_file(cache_max_filename.c_str(), ios::in);
      unsigned long cache_mb_max;

      if (cache_max_file.is_open())
        {
          cache_max_file >> cache_mb_max;
          cache_max_file.close();
        }
      else
        {
          //file doesnt exist, create a default size
          ofstream default_cache_max(cache_max_filename.c_str(), ios::out);
          default_cache_max << SYSTEMTAP_CACHE_DEFAULT_MB << endl;
          cache_mb_max = SYSTEMTAP_CACHE_DEFAULT_MB;

          if (s.verbose > 1)
            clog << "Cache limit file " << s.cache_path << "/"
              << SYSTEMTAP_CACHE_MAX_FILENAME
              << " missing, creating default." << endl;
        }

      //glob for all kernel modules in the cache dir
      glob_t cache_glob;
      string glob_str = s.cache_path + "/*/*.ko";
      glob(glob_str.c_str(), 0, NULL, &cache_glob);


      set<struct cache_ent_info> cache_contents;
      unsigned long cache_size_b = 0;

      //grab info for each cache entry (.ko and .c)
      for (unsigned int i = 0; i < cache_glob.gl_pathc; i++)
        {
          string cache_ent_path = cache_glob.gl_pathv[i];
          cache_ent_path.resize(cache_ent_path.length() - 3);

          struct cache_ent_info cur_info(cache_ent_path, true);
          if (cur_info.size != 0 && cur_info.weight != 0)
            {
              cache_size_b += cur_info.size;
              cache_contents.insert(cur_info);
            }
        }

      globfree(&cache_glob);

      //grab info for each typequery user module (.so)
      glob_str = s.cache_path + "/*/*.so";
      glob(glob_str.c_str(), 0, NULL, &cache_glob);
      for (unsigned int i = 0; i < cache_glob.gl_pathc; i++)
        {
          string cache_ent_path = cache_glob.gl_pathv[i];
          struct cache_ent_info cur_info(cache_ent_path, false);
          if (cur_info.size != 0 && cur_info.weight != 0)
            {
              cache_size_b += cur_info.size;
              cache_contents.insert(cur_info);
            }
        }

      globfree(&cache_glob);

      //grab info for each stapconf cache entry (.h)
      glob_str = s.cache_path + "/*/*.h";
      glob(glob_str.c_str(), 0, NULL, &cache_glob);
      for (unsigned int i = 0; i < cache_glob.gl_pathc; i++)
        {
          string cache_ent_path = cache_glob.gl_pathv[i];
          struct cache_ent_info cur_info(cache_ent_path, false);
          if (cur_info.size != 0 && cur_info.weight != 0)
            {
              cache_size_b += cur_info.size;
              cache_contents.insert(cur_info);
            }
        }

      globfree(&cache_glob);

      set<struct cache_ent_info>::iterator i;
      unsigned long r_cache_size = cache_size_b;
      string removed_dirs = "";

      //unlink .ko and .c until the cache size is under the limit
      for (i = cache_contents.begin(); i != cache_contents.end(); ++i)
        {
          if ( (r_cache_size / 1024 / 1024) < cache_mb_max)    //convert r_cache_size to MiB
            break;

          STAP_PROBE1(stap, cache__clean, (i->path).c_str());
          //remove this (*i) cache_entry, add to removed list
          i->unlink();
          r_cache_size -= i->size;
          removed_dirs += i->path + ", ";
        }

      cache_contents.clear();

      if (s.verbose > 1 && removed_dirs != "")
        {
          //remove trailing ", "
          removed_dirs = removed_dirs.substr(0, removed_dirs.length() - 2);
          clog << "Cache cleaning successful, removed entries: " 
               << removed_dirs << endl;
        }
    }
  else
    {
      if (s.verbose > 1)
        clog << "Cache cleaning skipped, no cache path." << endl;
    }
}

//Assign a weight for a particular file. A lower weight
// will be removed before a higher weight.
//TODO: for now use system mtime... later base a
// weighting on size, ctime, atime etc..
static long
get_file_weight(const string &path)
{
  time_t dir_mtime = 0;
  struct stat dir_stat_info;

  if (stat(path.c_str(), &dir_stat_info) == 0)
    //GNU struct stat defines st_atime as st_atim.tv_sec
    // but it doesnt seem to work properly in practice
    // so use st_atim.tv_sec -- bad for portability?
    dir_mtime = dir_stat_info.st_mtim.tv_sec;

  return dir_mtime;
}


cache_ent_info::cache_ent_info(const string& path, bool is_module):
  path(path), is_module(is_module)
{
  if (is_module)
    {
      string mod_path    = path + ".ko";
      string modsgn_path = path + ".ko.sgn";
      string source_path = path + ".c";
      size = get_file_size(mod_path)
        + get_file_size(modsgn_path);
        + get_file_size(source_path);
      weight = get_file_weight(mod_path);
    }
  else
    {
      size = get_file_size(path);
      weight = get_file_weight(path);
    }
}


void
cache_ent_info::unlink() const
{
  if (is_module)
    {
      string mod_path    = path + ".ko";
      string modsgn_path = path + ".ko.sgn";
      string source_path = path + ".c";
      ::unlink(mod_path.c_str());
      ::unlink(modsgn_path.c_str());
      ::unlink(source_path.c_str());
    }
  else
    ::unlink(path.c_str());
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
