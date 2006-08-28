// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SESSION_H
#define SESSION_H

#include "elaborate.h"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <map>


// forward decls for all referenced systemtap types
struct match_node;
struct stapfile;
struct vardecl;
struct functiondecl;
struct derived_probe_group_container;
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

  // command line args
  std::vector<std::string> include_path;
  std::vector<std::string> macros;
  std::vector<std::string> args;
  std::string kernel_release;
  std::string architecture;
  std::string runtime_path;
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
  derived_probe_group_container probes;
  std::vector<embeddedcode*> embeds;
  std::map<std::string, statistic_decl> stat_decls;
  // XXX: vector<*> instead please?

  // module-referencing file handles
  std::map<std::string,int> module_fds;

  // unparser data
  translator_output* op;
  unparser* up;

  unsigned num_errors;
  // void print_error (const parse_error& e);
  void print_error (const semantic_error& e);
};


#endif // SESSION_H
