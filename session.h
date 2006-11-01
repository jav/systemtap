// -*- C++ -*-
// Copyright (C) 2005, 2006 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SESSION_H
#define SESSION_H

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <map>

extern "C" {
#include <elfutils/libdw.h>
}


// forward decls for all referenced systemtap types
struct match_node;
struct stapfile;
struct vardecl;
struct functiondecl;
struct derived_probe;
struct be_derived_probe_group;
struct dwarf_derived_probe_group;
struct timer_derived_probe_group;
struct profile_derived_probe_group;
struct mark_derived_probe_group;
struct hrtimer_derived_probe_group;
struct perfmon_derived_probe_group;
struct embeddedcode;
struct translator_output;
struct unparser;
struct semantic_error;


// XXX: a generalized form of this descriptor could be associated with
// a vardecl instead of out here at the systemtap_session level.
struct statistic_decl
{
  statistic_decl()
    : type(none), 
      logarithmic_buckets(0),
      linear_low(0), linear_high(0), linear_step(0)
  {}    
  enum { none, linear, logarithmic } type;
  int64_t logarithmic_buckets;
  int64_t linear_low;
  int64_t linear_high;
  int64_t linear_step;
  bool operator==(statistic_decl const & other)
  {
    return type == other.type 
      && logarithmic_buckets == other.logarithmic_buckets
      && linear_low == other.linear_low
      && linear_high == other.linear_high
      && linear_step == other.linear_step;
  }
};


struct systemtap_session
{
  systemtap_session ();
  // NB: new POD members likely need to be explicitly cleared in the ctor.

  // command line args
  std::vector<std::string> include_path;
  std::vector<std::string> macros;
  std::vector<std::string> args;
  std::string kernel_release;
  std::string kernel_base_release;
  std::string architecture;
  std::string runtime_path;
  std::string data_path;
  std::string module_name;
  std::string output_file;
  std::string cmd;
  int target_pid;
  int last_pass;
  unsigned verbose;
  unsigned timing;
  bool keep_tmpdir;
  bool guru_mode;
  bool bulk_mode;
  bool unoptimized;
  bool merge;
  int buffer_size;
  unsigned perfmon;

  // Cache data
  bool use_cache;
  std::string cache_path;
  std::string hash_path;

  // temporary directory for module builds etc.
  // hazardous - it is "rm -rf"'d at exit
  std::string tmpdir;
  std::string translated_source; // C source code

  match_node* pattern_root;
  void register_library_aliases();

  // parse trees for the various script files
  stapfile* user_file;
  std::vector<stapfile*> library_files;

  // resolved globals/functions/probes for the run as a whole
  std::vector<stapfile*> files;
  std::vector<vardecl*> globals;
  std::vector<functiondecl*> functions;
  std::vector<derived_probe*> probes; // see also *_probes groups below
  std::vector<embeddedcode*> embeds;
  std::map<std::string, statistic_decl> stat_decls;
  // XXX: vector<*> instead please?

  // Every probe in these groups must also appear in the
  // session.probes vector.
  be_derived_probe_group* be_derived_probes;
  dwarf_derived_probe_group* dwarf_derived_probes;
  timer_derived_probe_group* timer_derived_probes;
  profile_derived_probe_group* profile_derived_probes;
  mark_derived_probe_group* mark_derived_probes;
  hrtimer_derived_probe_group* hrtimer_derived_probes;
  perfmon_derived_probe_group* perfmon_derived_probes;

  // unparser data
  translator_output* op;
  unparser* up;

  // kprobes_text data
  bool kprobes_text_initialized;
  Dwarf_Addr kprobes_text_start;
  Dwarf_Addr kprobes_text_end;

  unsigned num_errors;
  // void print_error (const parse_error& e);
  void print_error (const semantic_error& e);

  // reNB: new POD members likely need to be explicitly cleared in the ctor.
};


#endif // SESSION_H
