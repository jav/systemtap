// -*- C++ -*-
// Copyright (C) 2005-2012 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SESSION_H
#define SESSION_H

#include "config.h"
#include <libintl.h>
#include <locale.h>

#include <list>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <stdexcept>

extern "C" {
#include <signal.h>
#include <elfutils/libdw.h>
}

#include "privilege.h"

#if ENABLE_NLS
#define _(string) gettext(string)
#define _N(string, string_plural, count) \
        ngettext((string), (string_plural), (count))
#else
#define _(string) (string)
#define _N(string, string_plural, count) \
        ( (count) == 1 ? (string) : (string_plural) )
#endif
#define _F(format, ...) autosprintf(_(format), __VA_ARGS__)
#define _NF(format, format_plural, count, ...) \
        autosprintf(_N((format), (format_plural), (count)), __VA_ARGS__)

// forward decls for all referenced systemtap types
class hash;
class match_node;
struct stapfile;
struct vardecl;
struct token;
struct functiondecl;
struct derived_probe;
struct be_derived_probe_group;
struct dwarf_derived_probe_group;
struct kprobe_derived_probe_group;
struct hwbkpt_derived_probe_group;
struct perf_derived_probe_group;
struct uprobe_derived_probe_group;
struct utrace_derived_probe_group;
struct itrace_derived_probe_group;
struct task_finder_derived_probe_group;
struct timer_derived_probe_group;
struct netfilter_derived_probe_group;
struct profile_derived_probe_group;
struct mark_derived_probe_group;
struct tracepoint_derived_probe_group;
struct hrtimer_derived_probe_group;
struct procfs_derived_probe_group;
struct embeddedcode;
class translator_output;
struct unparser;
struct semantic_error;
struct module_cache;
struct update_visitor;
struct compile_server_cache;

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
private:
  // disable implicit constructors by not implementing these
  systemtap_session (const systemtap_session& other);
  systemtap_session& operator= (const systemtap_session& other);

  // copy constructor used by ::clone()
  systemtap_session (const systemtap_session& other,
                     const std::string& arch,
                     const std::string& kern);

public:
  systemtap_session ();
  ~systemtap_session ();

  // To reset the tmp_dir
  void create_tmp_dir();
  void remove_tmp_dir();
  void reset_tmp_dir();

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).
  void setup_kernel_release (const char* kstr);
  void insert_loaded_modules ();

  // command line parsing
  int  parse_cmdline (int argc, char * const argv []);
  void version ();
  void usage (int exitcode);
  void check_options (int argc, char * const argv []);
  static const char* morehelp;

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

  // command line args
  std::string script_file; // FILE
  std::string cmdline_script; // -e PROGRAM
  bool have_script;
  std::vector<std::string> include_path;
  int include_arg_start;
  std::vector<std::string> macros;
  std::vector<std::string> args;
  std::vector<std::string> kbuildflags; // -B var=val
  std::vector<std::string> globalopts; // -G var=val
  std::vector<std::string> modinfos; // --modinfo tag=value
  std::string release;
  std::string kernel_release;
  std::string kernel_base_release;
  std::string kernel_build_tree;
  std::string kernel_source_tree;
  std::string sysroot;
  std::map<std::string,std::string> sysenv;
  bool update_release_sysroot;
  std::map<std::string,std::string> kernel_config;
  std::set<std::string> kernel_exports;
  std::string machine;
  std::string architecture;
  bool native_build;
  std::string runtime_path;
  bool runtime_specified;
  std::string data_path;
  std::string module_name;
  std::string stapconf_name;
  std::string output_file;
  std::string size_option;
  std::string cmd;
  std::string compatible; // use (strverscmp(s.compatible.c_str(), "N.M") >= 0)
  int target_pid;
  int last_pass;
  unsigned perpass_verbose[5];
  unsigned verbose;
  bool timing;
  bool save_module;
  bool modname_given;
  bool keep_tmpdir;
  bool guru_mode;
  bool listing_mode;
  bool listing_mode_vars;
  bool bulk_mode;
  bool unoptimized;
  bool suppress_warnings;
  bool panic_warnings;
  int buffer_size;
  bool prologue_searching;
  bool tapset_compile_coverage;
  bool need_uprobes;
  bool need_unwind;
  bool need_symbols;
  std::string uprobes_path;
  std::string uprobes_hash;
  bool load_only; // flight recorder mode
  bool omit_werror;
  privilege_t privilege;
  bool privilege_set;
  bool systemtap_v_check;
  bool tmpdir_opt_set;
  bool dump_probe_types;
  int download_dbinfo;
  bool suppress_handler_errors;

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

  // Client/server
#if HAVE_NSS
  static bool NSPR_Initialized; // only once for all sessions
  void NSPR_init ();
#endif
  bool client_options;
  std::string client_options_disallowed_for_unprivileged;
  std::vector<std::string> server_status_strings;
  std::vector<std::string> specified_servers;
  bool automatic_server_mode;
  std::string server_trust_spec;
  std::vector<std::string> server_args;
  std::string winning_server;
  compile_server_cache* server_cache;

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

  // Mechanism for retrying compilation with a compile server should it fail due
  // to lack of resources on the local host.
  // Once it has been decided not to try the server (e.g. syntax error),
  // that decision cannot be changed.
  int try_server_status;
  bool use_server_on_error;

  enum { try_server_unset, dont_try_server, do_try_server };
  void init_try_server ();
  void set_try_server (int t = do_try_server);
  bool try_server () const { return try_server_status == do_try_server; }

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

  // Remote execution
  std::vector<std::string> remote_uris;
  bool use_remote_prefix;
  typedef std::map<std::pair<std::string, std::string>, systemtap_session*> session_map_t;
  session_map_t subsessions;
  systemtap_session* clone(const std::string& arch, const std::string& release);

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

  // Cache data
  bool use_cache;               // control all caching
  bool use_script_cache;        // control caching of pass-3/4 output
  bool poison_cache;            // consider the cache to be write-only
  std::string cache_path;       // usually ~/.systemtap/cache
  std::string hash_path;        // path to the cached script module
  std::string stapconf_path;    // path to the cached stapconf
  hash *base_hash;              // hash common to all caching

  // dwarfless operation
  bool consult_symtab;
  std::string kernel_symtab_path;
  bool ignore_vmlinux;
  bool ignore_dwarf;

  // Skip bad $ vars
  bool skip_badvars;

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

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
  kprobe_derived_probe_group* kprobe_derived_probes;
  hwbkpt_derived_probe_group* hwbkpt_derived_probes;
  perf_derived_probe_group* perf_derived_probes;
  uprobe_derived_probe_group* uprobe_derived_probes;
  utrace_derived_probe_group* utrace_derived_probes;
  itrace_derived_probe_group* itrace_derived_probes;
  task_finder_derived_probe_group* task_finder_derived_probes;
  timer_derived_probe_group* timer_derived_probes;
  netfilter_derived_probe_group* netfilter_derived_probes;
  profile_derived_probe_group* profile_derived_probes;
  mark_derived_probe_group* mark_derived_probes;
  tracepoint_derived_probe_group* tracepoint_derived_probes;
  hrtimer_derived_probe_group* hrtimer_derived_probes;
  procfs_derived_probe_group* procfs_derived_probes;

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

  // unparser data
  translator_output* op;
  std::vector<translator_output*> auxiliary_outputs;
  unparser* up;

  // some symbol addresses
  // XXX: these belong elsewhere; perhaps the dwflpp instance
  Dwarf_Addr sym_kprobes_text_start;
  Dwarf_Addr sym_kprobes_text_end;
  Dwarf_Addr sym_stext;

  // List of libdwfl module names to extract symbol/unwind data for.
  std::set<std::string> unwindsym_modules;
  bool unwindsym_ldd;
  struct module_cache* module_cache;
  std::vector<std::string> build_ids;

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).

  std::set<std::string> seen_errors;
  std::set<std::string> seen_warnings;
  unsigned num_errors () { return seen_errors.size() + (panic_warnings ? seen_warnings.size() : 0); }

  std::set<std::string> rpms_to_install;

  translator_output* op_create_auxiliary();

  // void print_error (const parse_error& e);
  const token* last_token;
  void print_token (std::ostream& o, const token* tok);
  void print_error (const semantic_error& e);
  void print_error_source (std::ostream&, std::string&, const token* tok);
  void print_warning (const std::string& w, const token* tok = 0);
  void printscript(std::ostream& o);

  // NB: It is very important for all of the above (and below) fields
  // to be cleared in the systemtap_session ctor (session.cxx).
};


// global counter of SIGINT/SIGTERM's received
extern int pending_interrupts;

// Interrupt exception subclass for catching
// interrupts (i.e. ctrl-c).
struct interrupt_exception: public std::runtime_error
{
  interrupt_exception ():
    runtime_error (_("interrupt received")){}
};

void assert_no_interrupts();

#endif // SESSION_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
