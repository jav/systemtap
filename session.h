// -*- C++ -*-
// Copyright (C) 2005-2009 Red Hat Inc.
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
#include <set>

extern "C" {
#include <elfutils/libdw.h>
}


// forward decls for all referenced systemtap types
struct match_node;
struct stapfile;
struct vardecl;
struct token;
struct functiondecl;
struct derived_probe;
struct be_derived_probe_group;
struct dwarf_derived_probe_group;
struct uprobe_derived_probe_group;
struct utrace_derived_probe_group;
struct itrace_derived_probe_group;
struct task_finder_derived_probe_group;
struct timer_derived_probe_group;
struct profile_derived_probe_group;
struct mark_derived_probe_group;
struct tracepoint_derived_probe_group;
struct hrtimer_derived_probe_group;
struct perfmon_derived_probe_group;
struct procfs_derived_probe_group;
struct embeddedcode;
struct translator_output;
struct unparser;
struct semantic_error;
struct module_cache;
struct update_visitor;


// XXX: a generalized form of this descriptor could be associated with
// a vardecl instead of out here at the systemtap_session level.
struct statistic_decl
{
  statistic_decl()
    : type(none),
      linear_low(0), linear_high(0), linear_step(0)
  {}
  enum { none, linear, logarithmic } type;
  int64_t linear_low;
  int64_t linear_high;
  int64_t linear_step;
  bool operator==(statistic_decl const & other)
  {
    return type == other.type
      && linear_low == other.linear_low
      && linear_high == other.linear_high
      && linear_step == other.linear_step;
  }
};


struct systemtap_session
{
  systemtap_session ();
  // NB: new POD members likely need to be explicitly cleared in the ctor.
  // See elaborate.cxx.

  // command line args
  std::vector<std::string> include_path;
  std::vector<std::string> macros;
  std::vector<std::string> args;
  std::string kernel_release;
  std::string kernel_base_release;
  std::string kernel_build_tree;
  std::string architecture;
  std::string runtime_path;
  std::string data_path;
  std::string module_name;
  std::string stapconf_name;
  std::string output_file;
  std::string size_option;
  std::string cmd;
  int target_pid;
  int last_pass;
  unsigned perpass_verbose[5];
  unsigned verbose;
  bool timing;
  bool keep_tmpdir;
  bool guru_mode;
  bool listing_mode;
  bool bulk_mode;
  bool unoptimized;
  bool merge;
  bool suppress_warnings;
  int buffer_size;
  unsigned perfmon;
  bool symtab; /* true: emit symbol table at translation time; false: let staprun do it. */
  bool prologue_searching;
  bool tapset_compile_coverage;
  bool need_uprobes;
  bool load_only; // flight recorder mode

  // Cache data
  bool use_cache;
  std::string cache_path;
  std::string hash_path;
  std::string stapconf_path;

  // dwarfless operation
  bool consult_symtab;
  std::string kernel_symtab_path;
  bool ignore_vmlinux;
  bool ignore_dwarf;

  // Skip bad $ vars
  bool skip_badvars;

  // temporary directory for module builds etc.
  // hazardous - it is "rm -rf"'d at exit
  std::string tmpdir;
  std::string translated_source; // C source code

  match_node* pattern_root;
  void register_library_aliases();

  // parse trees for the various script files
  stapfile* user_file;
  std::vector<stapfile*> library_files;

  // filters to run over all code before symbol resolution
  //   e.g. @cast expansion
  std::vector<update_visitor*> code_filters;

  // resolved globals/functions/probes for the run as a whole
  std::vector<stapfile*> files;
  std::vector<vardecl*> globals;
  std::map<std::string,functiondecl*> functions;
  std::vector<derived_probe*> probes; // see also *_probes groups below
  std::vector<embeddedcode*> embeds;
  std::map<std::string, statistic_decl> stat_decls;
  // track things that are removed
  std::vector<vardecl*> unused_globals;
  std::vector<derived_probe*> unused_probes; // see also *_probes groups below
  std::vector<functiondecl*> unused_functions;
  // XXX: vector<*> instead please?

  // Every probe in these groups must also appear in the
  // session.probes vector.
  be_derived_probe_group* be_derived_probes;
  dwarf_derived_probe_group* dwarf_derived_probes;
  uprobe_derived_probe_group* uprobe_derived_probes;
  utrace_derived_probe_group* utrace_derived_probes;
  itrace_derived_probe_group* itrace_derived_probes;
  task_finder_derived_probe_group* task_finder_derived_probes;
  timer_derived_probe_group* timer_derived_probes;
  profile_derived_probe_group* profile_derived_probes;
  mark_derived_probe_group* mark_derived_probes;
  tracepoint_derived_probe_group* tracepoint_derived_probes;
  hrtimer_derived_probe_group* hrtimer_derived_probes;
  perfmon_derived_probe_group* perfmon_derived_probes;
  procfs_derived_probe_group* procfs_derived_probes;
  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (elaborate.cxx).

  // unparser data
  translator_output* op;
  unparser* up;

  // some symbol addresses
  // XXX: these belong elsewhere; perhaps the dwflpp instance
  Dwarf_Addr sym_kprobes_text_start;
  Dwarf_Addr sym_kprobes_text_end;
  Dwarf_Addr sym_stext;

  // List of libdwfl module names to extract symbol/unwind data for.
  std::set<std::string> unwindsym_modules;
  struct module_cache* module_cache;

  std::set<std::string> seen_errors;
  std::set<std::string> seen_warnings;
  unsigned num_errors () { return seen_errors.size(); }

  // void print_error (const parse_error& e);
  const token* last_token;
  void print_token (std::ostream& o, const token* tok);
  void print_error (const semantic_error& e);
  void print_error_source (std::ostream&, std::string&, const token* tok);
  void print_warning (const std::string& w, const token* tok = 0);

  // reNB: new POD members likely need to be explicitly cleared in the ctor.
};


// global counter of SIGINT/SIGTERM's received
extern int pending_interrupts;

#endif // SESSION_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
