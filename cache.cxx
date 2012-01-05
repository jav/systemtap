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
#include "stap-probe.h"
#include <cerrno>
#include <string>
#include <fstream>
#include <cstring>
#include <cassert>
#include <sstream>
#include <vector>

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>
#include <regex.h>
#include <utime.h>
#include <sys/time.h>
#include <unistd.h>
}

using namespace std;


#define SYSTEMTAP_CACHE_MAX_FILENAME "cache_mb_limit"
#define SYSTEMTAP_CACHE_DEFAULT_MB 256
#define SYSTEMTAP_CACHE_CLEAN_INTERVAL_FILENAME "cache_clean_interval_s"
#define SYSTEMTAP_CACHE_CLEAN_DEFAULT_INTERVAL_S 30

struct cache_ent_info {
  vector<string> paths;
  off_t size; // sum across all paths
  time_t mtime; // newest of all paths

  cache_ent_info(const vector<string>& paths);
  bool operator<(const struct cache_ent_info& other) const;
  void unlink() const;
};


void
add_stapconf_to_cache(systemtap_session& s)
{
  bool verbose = s.verbose > 1;

  string stapconf_src_path = s.tmpdir + "/" + s.stapconf_name;
  if (!copy_file(stapconf_src_path, s.stapconf_path, verbose))
    {
      // NB: this is not so severe as to prevent reuse of the .ko
      // already copied.
      //
      // s.use_script_cache = false;
      // return;
    }
}


void
add_script_to_cache(systemtap_session& s)
{
  bool verbose = s.verbose > 1;

  // PR10543: clean the cache *before* we try putting something new into it.
  // We don't want to risk having the brand new contents being erased again.
  clean_cache(s);

  string module_src_path = s.tmpdir + "/" + s.module_name + ".ko";
  PROBE2(stap, cache__add__module, module_src_path.c_str(), s.hash_path.c_str());
  if (!copy_file(module_src_path, s.hash_path, verbose))
    {
      s.use_script_cache = false;
      return;
    }
  // Copy the signature file, if any. It is not an error if this fails.
  if (file_exists (module_src_path + ".sgn"))
    copy_file(module_src_path + ".sgn", s.hash_path + ".sgn", verbose);

  string c_dest_path = s.hash_path;
  if (endswith(c_dest_path, ".ko"))
    c_dest_path.resize(c_dest_path.size() - 3);
  c_dest_path += ".c";

  PROBE2(stap, cache__add__source, s.translated_source.c_str(), c_dest_path.c_str());
  if (!copy_file(s.translated_source, c_dest_path, verbose))
    {
      // NB: this is not so severe as to prevent reuse of the .ko
      // already copied.
      //
      // s.use_script_cache = false;
    }
}


bool
get_stapconf_from_cache(systemtap_session& s)
{
  if (s.poison_cache)
    return false;

  string stapconf_dest_path = s.tmpdir + "/" + s.stapconf_name;
  int fd_stapconf;

  // See if stapconf exists
  fd_stapconf = open(s.stapconf_path.c_str(), O_RDONLY);
  if (fd_stapconf == -1)
    {
      // It isn't in cache.
      return false;
    }

  // Copy the stapconf header file to the destination
  if (!get_file_size(fd_stapconf) ||
      !copy_file(s.stapconf_path, stapconf_dest_path))
    {
      close(fd_stapconf);
      return false;
    }

  // We're done with this file handle.
  close(fd_stapconf);

  if (s.verbose > 1)
    clog << _("Pass 4: using cached ") << s.stapconf_path << endl;

  return true;
}


bool
get_script_from_cache(systemtap_session& s)
{
  if (s.poison_cache)
    return false;

  string module_dest_path = s.tmpdir + "/" + s.module_name + ".ko";
  string c_src_path = s.hash_path;
  int fd_module, fd_c;

  if (endswith(c_src_path, ".ko"))
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

  // Check that the files aren't empty, and then
  // copy the cached C file to the destination
  if (!get_file_size(fd_module) || !get_file_size(fd_c) ||
      !copy_file(c_src_path, s.translated_source))
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
      // Copy the module signature file, if any.
      // It is not an error if this fails.
      if (file_exists (s.hash_path + ".sgn"))
	copy_file(s.hash_path + ".sgn", module_dest_path + ".sgn");
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
  // NB: don't use s.verbose here, since we're still in pass-2,
  // i.e., s.verbose = s.perpass_verbose[1].
  if (s.perpass_verbose[2])
    clog << _("Pass 3: using cached ") << c_src_path << endl;
  if (s.perpass_verbose[3] && s.last_pass != 3)
    clog << _("Pass 4: using cached ") << s.hash_path << endl;

  PROBE2(stap, cache__get, c_src_path.c_str(), s.hash_path.c_str());

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
            clog << _F("Cache limit file %s/%s missing, creating default.",
                       s.cache_path.c_str(), SYSTEMTAP_CACHE_MAX_FILENAME) << endl;
        }

      /* Get cache clean interval from file in the stap cache dir */
      string cache_clean_interval_filename = s.cache_path + "/";
      cache_clean_interval_filename += SYSTEMTAP_CACHE_CLEAN_INTERVAL_FILENAME;
      ifstream cache_clean_interval_file(cache_clean_interval_filename.c_str(), ios::in);
      unsigned long cache_clean_interval;

      if (cache_clean_interval_file.is_open())
        {
          cache_clean_interval_file >> cache_clean_interval;
          cache_clean_interval_file.close();
        }
      else
        {
          //file doesnt exist, create a default interval
          ofstream default_cache_clean_interval(cache_clean_interval_filename.c_str(), ios::out);
          default_cache_clean_interval << SYSTEMTAP_CACHE_CLEAN_DEFAULT_INTERVAL_S << endl;
          cache_clean_interval = SYSTEMTAP_CACHE_CLEAN_DEFAULT_INTERVAL_S;

          if (s.verbose > 1)
            clog << _F("Cache clean interval file %s missing, creating default.",
                       cache_clean_interval_filename.c_str())<< endl;
        }

      /* Check the cache cleaning interval */
      struct stat sb;
      if(stat(cache_clean_interval_filename.c_str(), &sb) < 0)
        {
          const char* e = strerror (errno);
          cerr << _F("clean_cache stat error: %s", e) << endl;
          return;
        }

      struct timeval current_time;
      gettimeofday(&current_time, NULL);
      if(difftime(current_time.tv_sec, sb.st_mtime) < cache_clean_interval)
        {
          //interval not passed, don't continue
          if (s.verbose > 1)
            clog << _F("Cache cleaning skipped, interval not reached %lu s / %lu s.",
                       (current_time.tv_sec-sb.st_mtime), cache_clean_interval)  << endl;
          return;
        }
      else
        {
          //interval reached, continue
          if (s.verbose > 1)
            clog << _F("Cleaning cache, interval reached %lu s > %lu s.",
                       (current_time.tv_sec-sb.st_mtime), cache_clean_interval)  << endl;
        }

      // glob for all files that look like hashes
      glob_t cache_glob;
      ostringstream glob_pattern;
      glob_pattern << s.cache_path << "/*/*";
      for (unsigned int i = 0; i < 32; i++)
        glob_pattern << "[[:xdigit:]]";
      glob_pattern << "*";
      int rc = glob(glob_pattern.str().c_str(), 0, NULL, &cache_glob);
      if (rc) {
        cerr << _F("clean_cache glob error rc=%d", rc) << endl;
        return;
      }

      regex_t hash_len_re;
      rc = regcomp (&hash_len_re, "([[:xdigit:]]{32}_[[:digit:]]+)", REG_EXTENDED);
      if (rc) {
        cerr << _F("clean_cache regcomp error rc=%d", rc) << endl;
        globfree(&cache_glob);
        return;
      }

      // group all files with the same HASH_LEN
      map<string, vector<string> > cache_groups;
      for (size_t i = 0; i < cache_glob.gl_pathc; i++)
        {
          const char* path = cache_glob.gl_pathv[i];
          regmatch_t hash_len;
          rc = regexec(&hash_len_re, path, 1, &hash_len, 0);
          if (rc || hash_len.rm_so == -1 || hash_len.rm_eo == -1)
            cache_groups[path].push_back(path); // ungrouped
          else
            cache_groups[string(path + hash_len.rm_so,
                                hash_len.rm_eo - hash_len.rm_so)]
              .push_back(path);
        }
      regfree(&hash_len_re);
      globfree(&cache_glob);


      // create each cache entry and accumulate the sum
      off_t cache_size_b = 0;
      set<cache_ent_info> cache_contents;
      for (map<string, vector<string> >::const_iterator it = cache_groups.begin();
           it != cache_groups.end(); ++it)
        {
          cache_ent_info cur_info(it->second);
          if (cache_contents.insert(cur_info).second)
            cache_size_b += cur_info.size;
        }

      unsigned long r_cache_size = cache_size_b;
      vector<const cache_ent_info*> removed;

      //unlink .ko and .c until the cache size is under the limit
      for (set<cache_ent_info>::iterator i = cache_contents.begin();
           i != cache_contents.end(); ++i)
        {
          if (r_cache_size < cache_mb_max * 1024 * 1024) //convert cache_mb_max to bytes
            break;

          //remove this (*i) cache_entry, add to removed list
          for (size_t j = 0; j < i->paths.size(); ++j)
            PROBE1(stap, cache__clean, i->paths[j].c_str());
          i->unlink();
          r_cache_size -= i->size;
          removed.push_back(&*i);
        }

      if (s.verbose > 1 && !removed.empty())
        {
          clog << _("Cache cleaning successful, removed entries: ") << endl;
          for (size_t i = 0; i < removed.size(); ++i)
            for (size_t j = 0; j < removed[i]->paths.size(); ++j)
              clog << "  " << removed[i]->paths[j] << endl;
        }

      if(utime(cache_clean_interval_filename.c_str(), NULL)<0)
        {
          const char* e = strerror (errno);
          cerr << _F("clean_cache utime error: %s", e) << endl;
          return;
        }
    }
  else
    {
      if (s.verbose > 1)
        clog << _("Cache cleaning skipped, no cache path.") << endl;
    }
}


cache_ent_info::cache_ent_info(const vector<string>& paths):
  paths(paths), size(0), mtime(0)
{
  struct stat file_info;
  for (size_t i = 0; i < paths.size(); ++i)
    if (stat(paths[i].c_str(), &file_info) == 0)
      {
        size += file_info.st_size;
        if (file_info.st_mtime > mtime)
          mtime = file_info.st_mtime;
      }
}


// The ordering here determines the order that
// files will be removed from the cache.
bool
cache_ent_info::operator<(const struct cache_ent_info& other) const
{
  if (mtime != other.mtime)
    return mtime < other.mtime;
  if (size != other.size)
    return size < other.size;
  if (paths.size() != other.paths.size())
    return paths.size() < other.paths.size();
  for (size_t i = 0; i < paths.size(); ++i)
    if (paths[i] != other.paths[i])
      return paths[i] < other.paths[i];
  return false;
}


void
cache_ent_info::unlink() const
{
  for (size_t i = 0; i < paths.size(); ++i)
    ::unlink(paths[i].c_str());
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
