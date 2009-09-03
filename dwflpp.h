// C++ interface to dwfl
// Copyright (C) 2005-2009 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008 James.Bottomley@HansenPartnership.com
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef DWFLPP_H
#define DWFLPP_H

#include "config.h"
#include "dwarf_wrappers.h"
#include "elaborate.h"
#include "session.h"
#include "unordered.h"

#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include <elfutils/libdwfl.h>
#include <regex.h>
}

#if !_ELFUTILS_PREREQ(0,142)
// Always use newer name, old name is deprecated in 0.142.
#define elf_getshdrstrndx elf_getshstrndx
#endif


struct func_info;
struct inline_instance_info;
struct symbol_table;
struct base_query;
struct dwarf_query;

enum line_t { ABSOLUTE, RELATIVE, RANGE, WILDCARD };
enum info_status { info_unknown, info_present, info_absent };

// module -> cu die[]
typedef unordered_map<Dwarf*, std::vector<Dwarf_Die>*> module_cu_cache_t;

// typename -> die
typedef unordered_map<std::string, Dwarf_Die> cu_type_cache_t;

// cu die -> (typename -> die)
typedef unordered_map<void*, cu_type_cache_t*> mod_cu_type_cache_t;

// function -> die
typedef unordered_multimap<std::string, Dwarf_Die> cu_function_cache_t;
typedef std::pair<cu_function_cache_t::iterator,
                  cu_function_cache_t::iterator>
        cu_function_cache_range_t;

// cu die -> (function -> die)
typedef unordered_map<void*, cu_function_cache_t*> mod_cu_function_cache_t;

// inline function die -> instance die[]
typedef unordered_map<void*, std::vector<Dwarf_Die>*> cu_inl_function_cache_t;

typedef std::vector<func_info> func_info_map_t;
typedef std::vector<inline_instance_info> inline_instance_map_t;


/* XXX FIXME functions that dwflpp needs from tapsets.cxx */
func_info_map_t *get_filtered_functions(dwarf_query *q);
inline_instance_map_t *get_filtered_inlines(dwarf_query *q);


struct
module_info
{
  Dwfl_Module* mod;
  const char* name;
  std::string elf_path;
  Dwarf_Addr addr;
  Dwarf_Addr bias;
  symbol_table *sym_table;
  info_status dwarf_status;     // module has dwarf info?
  info_status symtab_status;    // symbol table cached?

  void get_symtab(dwarf_query *q);
  void update_symtab(cu_function_cache_t *funcs);

  module_info(const char *name) :
    mod(NULL),
    name(name),
    addr(0),
    bias(0),
    sym_table(NULL),
    dwarf_status(info_unknown),
    symtab_status(info_unknown)
  {}

  ~module_info();
};


struct
module_cache
{
  std::map<std::string, module_info*> cache;
  bool paths_collected;
  bool dwarf_collected;

  module_cache() : paths_collected(false), dwarf_collected(false) {}
};


struct func_info
{
  func_info()
    : decl_file(NULL), decl_line(-1), addr(0), prologue_end(0), weak(false)
  {
    std::memset(&die, 0, sizeof(die));
  }
  std::string name;
  char const * decl_file;
  int decl_line;
  Dwarf_Die die;
  Dwarf_Addr addr;
  Dwarf_Addr entrypc;
  Dwarf_Addr prologue_end;
  bool weak;
};


struct inline_instance_info
{
  inline_instance_info()
    : decl_file(NULL), decl_line(-1)
  {
    std::memset(&die, 0, sizeof(die));
  }
  bool operator<(const inline_instance_info& other) const;
  std::string name;
  char const * decl_file;
  int decl_line;
  Dwarf_Addr entrypc;
  Dwarf_Die die;
};


struct dwflpp
{
  systemtap_session & sess;

  // These are "current" values we focus on.
  Dwfl_Module * module;
  Dwarf_Addr module_bias;
  module_info * mod_info;

  // These describe the current module's PC address range
  Dwarf_Addr module_start;
  Dwarf_Addr module_end;

  Dwarf_Die * cu;

  std::string module_name;
  std::string function_name;

  dwflpp(systemtap_session & session, const std::string& user_module, bool kernel_p);
  dwflpp(systemtap_session & session, const std::vector<std::string>& user_modules);
  ~dwflpp();

  void get_module_dwarf(bool required = false, bool report = true);

  void focus_on_module(Dwfl_Module * m, module_info * mi);
  void focus_on_cu(Dwarf_Die * c);
  void focus_on_function(Dwarf_Die * f);

  std::string cu_name(void);

  Dwarf_Die *query_cu_containing_address(Dwarf_Addr a);

  bool module_name_matches(const std::string& pattern);
  static bool name_has_wildcard(const std::string& pattern);
  bool module_name_final_match(const std::string& pattern);

  bool function_name_matches_pattern(const std::string& name, const std::string& pattern);
  bool function_name_matches(const std::string& pattern);
  bool function_name_final_match(const std::string& pattern);

  void iterate_over_modules(int (* callback)(Dwfl_Module *, void **,
                                             const char *, Dwarf_Addr,
                                             void *),
                            base_query *data);

  void iterate_over_cus (int (*callback)(Dwarf_Die * die, void * arg),
                         void * data);

  bool func_is_inline();

  void iterate_over_inline_instances (int (* callback)(Dwarf_Die * die, void * arg),
                                      void * data);

  Dwarf_Die *declaration_resolve(const char *name);

  mod_cu_function_cache_t cu_function_cache;

  int iterate_over_functions (int (* callback)(Dwarf_Die * func, base_query * q),
                              base_query * q, const std::string& function,
                              bool has_statement_num=false);

  void iterate_over_srcfile_lines (char const * srcfile,
                                   int lines[2],
                                   bool need_single_match,
                                   enum line_t line_type,
                                   void (* callback) (const dwarf_line_t& line,
                                                      void * arg),
                                   const std::string& func_pattern,
                                   void *data);

  void iterate_over_labels (Dwarf_Die *begin_die,
                            const std::string& sym,
                            const std::string& symfunction,
                            dwarf_query *q,
                            void (* callback)(const std::string &,
                                              const char *,
                                              const char *,
                                              int,
                                              Dwarf_Die *,
                                              Dwarf_Addr,
                                              dwarf_query *),
                            const std::string& current_function);

  void collect_srcfiles_matching (std::string const & pattern,
                                  std::set<std::string> & filtered_srcfiles);

  void resolve_prologue_endings (func_info_map_t & funcs);

  bool function_entrypc (Dwarf_Addr * addr);
  bool die_entrypc (Dwarf_Die * die, Dwarf_Addr * addr);

  void function_die (Dwarf_Die *d);
  void function_file (char const ** c);
  void function_line (int *linep);

  bool die_has_pc (Dwarf_Die & die, Dwarf_Addr pc);

  std::string literal_stmt_for_local (Dwarf_Die *scope_die,
                                      Dwarf_Addr pc,
                                      std::string const & local,
                                      const target_symbol *e,
                                      bool lvalue,
                                      exp_type & ty);


  std::string literal_stmt_for_return (Dwarf_Die *scope_die,
                                       Dwarf_Addr pc,
                                       const target_symbol *e,
                                       bool lvalue,
                                       exp_type & ty);

  std::string literal_stmt_for_pointer (Dwarf_Die *type_die,
                                        const target_symbol *e,
                                        bool lvalue,
                                        exp_type & ty);

  bool blacklisted_p(const std::string& funcname,
                     const std::string& filename,
                     int line,
                     const std::string& module,
                     Dwarf_Addr addr,
                     bool has_return);

  Dwarf_Addr relocate_address(Dwarf_Addr addr, std::string& reloc_section);

  Dwarf_Addr literal_addr_to_sym_addr(Dwarf_Addr lit_addr);


private:
  Dwfl * dwfl;

  // These are "current" values we focus on.
  Dwarf * module_dwarf;
  Dwarf_Die * function;

  void setup_kernel(const std::string& module_name, bool debuginfo_needed = true);
  void setup_user(const std::vector<std::string>& modules, bool debuginfo_needed = true);

  module_cu_cache_t module_cu_cache;

  std::set<void*> cu_inl_function_cache_done; // CUs that are already cached
  cu_inl_function_cache_t cu_inl_function_cache;
  void cache_inline_instances (Dwarf_Die* die);

  /* The global alias cache is used to resolve any DIE found in a
   * module that is stubbed out with DW_AT_declaration with a defining
   * DIE found in a different module.  The current assumption is that
   * this only applies to structures and unions, which have a global
   * namespace (it deliberately only traverses program scope), so this
   * cache is indexed by name.  If other declaration lookups were
   * added to it, it would have to be indexed by name and tag
   */
  mod_cu_type_cache_t global_alias_cache;
  static int global_alias_caching_callback(Dwarf_Die *die, void *arg);
  int iterate_over_globals (int (* callback)(Dwarf_Die *, void *),
                            void * data);

  static int cu_function_caching_callback (Dwarf_Die* func, void *arg);

  bool has_single_line_record (dwarf_query * q, char const * srcfile, int lineno);

  static void loc2c_error (void *, const char *fmt, ...);

  // This function generates code used for addressing computations of
  // target variables.
  void emit_address (struct obstack *pool, Dwarf_Addr address);
  static void loc2c_emit_address (void *arg, struct obstack *pool,
                                  Dwarf_Addr address);

  void print_locals(Dwarf_Die *die, std::ostream &o);
  void print_members(Dwarf_Die *vardie, std::ostream &o);

  Dwarf_Attribute *find_variable_and_frame_base (Dwarf_Die *scope_die,
                                                 Dwarf_Addr pc,
                                                 std::string const & local,
                                                 const target_symbol *e,
                                                 Dwarf_Die *vardie,
                                                 Dwarf_Attribute *fb_attr_mem);

  struct location *translate_location(struct obstack *pool,
                                      Dwarf_Attribute *attr,
                                      Dwarf_Addr pc,
                                      Dwarf_Attribute *fb_attr,
                                      struct location **tail,
                                      const target_symbol *e);

  bool find_struct_member(const target_symbol::component& c,
                          Dwarf_Die *parentdie,
                          Dwarf_Die *memberdie,
                          std::vector<Dwarf_Attribute>& locs);

  Dwarf_Die *translate_components(struct obstack *pool,
                                  struct location **tail,
                                  Dwarf_Addr pc,
                                  const target_symbol *e,
                                  Dwarf_Die *vardie,
                                  Dwarf_Die *die_mem,
                                  Dwarf_Attribute *attr_mem);

  Dwarf_Die *resolve_unqualified_inner_typedie (Dwarf_Die *typedie_mem,
                                                Dwarf_Attribute *attr_mem,
                                                const target_symbol *e);

  void translate_final_fetch_or_store (struct obstack *pool,
                                       struct location **tail,
                                       Dwarf_Addr module_bias,
                                       Dwarf_Die *die,
                                       Dwarf_Attribute *attr_mem,
                                       bool lvalue,
                                       const target_symbol *e,
                                       std::string &,
                                       std::string &,
                                       exp_type & ty);

  std::string express_as_string (std::string prelude,
                                 std::string postlude,
                                 struct location *head);

  regex_t blacklist_func; // function/statement probes
  regex_t blacklist_func_ret; // only for .return probes
  regex_t blacklist_file; // file name
  regex_t blacklist_section; // init/exit sections
  bool blacklist_enabled;
  void build_blacklist();
  std::string get_blacklist_section(Dwarf_Addr addr);

  Dwarf_Addr pc_cached_scopes;
  int num_cached_scopes;
  Dwarf_Die *cached_scopes;
  int dwarf_getscopes_cached (Dwarf_Addr pc, Dwarf_Die **scopes);

  // Returns the call frame address operations for the given program counter.
  Dwarf_Op *get_cfa_ops (Dwarf_Addr pc);

};

#endif // DWFLPP_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
