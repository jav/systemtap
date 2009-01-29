// systemtap cache manager
// Copyright (C) 2006-2008 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.


#include "session.h"
#include "cache.h"
#include "util.h"
#include <cerrno>
#include <string>
#include <fstream>
#include <cstring>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>
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
      s.use_cache = false;
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
      if (s.verbose > 1)
        cerr << "Copy failed (\"" << s.translated_source << "\" to \""
             << c_dest_path << "\"): " << strerror(errno) << endl;
      // NB: this is not so severe as to prevent reuse of the .ko
      // already copied.
      //
      // s.use_cache = false;
    }

  clean_cache(s);
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

  return true;
}


void
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
            clog << "Cache limit file " << s.cache_path << "/" << SYSTEMTAP_CACHE_MAX_FILENAME << " missing, creating default." << endl;
        }

      //glob for all kernel modules in the cache dir
      glob_t cache_glob;
      string glob_str = s.cache_path + "/*/*.ko";
      glob(glob_str.c_str(), 0, NULL, &cache_glob);


      set<struct cache_ent_info, struct weight_sorter> cache_contents;
      unsigned long cache_size_b = 0;

      //grab info for each cache entry (.ko and .c)
      for (unsigned int i = 0; i < cache_glob.gl_pathc; i++)
        {
          struct cache_ent_info cur_info;
          string cache_ent_path = cache_glob.gl_pathv[i];
          long cur_size = 0;

          cache_ent_path = cache_ent_path.substr(0, cache_ent_path.length() - 3);
          cur_info.path = cache_ent_path;
          cur_info.weight = get_cache_file_weight(cache_ent_path);

          cur_size = get_cache_file_size(cache_ent_path);
          cur_info.size = cur_size;
          cache_size_b += cur_size;

          if (cur_info.size != 0 && cur_info.weight != 0)
            {
              cache_contents.insert(cur_info);
            }
        }

      globfree(&cache_glob);

      set<struct cache_ent_info, struct weight_sorter>::iterator i;
      unsigned long r_cache_size = cache_size_b;
      string removed_dirs = "";

      //unlink .ko and .c until the cache size is under the limit
      for (i = cache_contents.begin(); i != cache_contents.end(); ++i)
        {
          if ( (r_cache_size / 1024 / 1024) < cache_mb_max)    //convert r_cache_size to MiB
            break;

          //remove this (*i) cache_entry, add to removed list
          r_cache_size -= i->size;
          unlink_cache_entry(i->path);
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

//Get the size, in bytes, of the module (.ko) and the
// corresponding source (.c)
long
get_cache_file_size(const string &cache_ent_path)
{
  size_t cache_ent_size = 0;
  string mod_path    = cache_ent_path + ".ko",
         source_path = cache_ent_path + ".c";

  struct stat file_info;

  if (stat(mod_path.c_str(), &file_info) == 0)
    cache_ent_size += file_info.st_size;
  else
    return 0;

  //Don't care if the .c isn't there, it's much smaller
  // than the .ko anyway
  if (stat(source_path.c_str(), &file_info) == 0)
    cache_ent_size += file_info.st_size;


  return cache_ent_size; // / 1024 / 1024;	//convert to MiB
}

//Assign a weight to this cache entry. A lower weight
// will be removed before a higher weight.
//TODO: for now use system mtime... later base a
// weighting on size, ctime, atime etc..
long
get_cache_file_weight(const string &cache_ent_path)
{
  time_t dir_mtime = 0;
  struct stat dir_stat_info;
  string module_path = cache_ent_path + ".ko";

  if (stat(module_path.c_str(), &dir_stat_info) == 0)
    //GNU struct stat defines st_atime as st_atim.tv_sec
    // but it doesnt seem to work properly in practice
    // so use st_atim.tv_sec -- bad for portability?
    dir_mtime = dir_stat_info.st_mtim.tv_sec;

  return dir_mtime;
}


//deletes the module and source file contain
void
unlink_cache_entry(const string &cache_ent_path)
{
  //remove both .ko and .c files
  string mod_path    = cache_ent_path + ".ko";
  string source_path = cache_ent_path + ".c";

  unlink(mod_path.c_str());     //it must exist, globbed for it earlier
  unlink(source_path.c_str());  //if its not there, no matter
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
