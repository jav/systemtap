// C++ interface to dwfl
// Copyright (C) 2005-2009 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008 James.Bottomley@HansenPartnership.com
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "dwflpp.h"
#include "config.h"
#include "staptree.h"
#include "elaborate.h"
#include "tapsets.h"
#include "task_finder.h"
#include "translate.h"
#include "session.h"
#include "util.h"
#include "buildrun.h"
#include "dwarf_wrappers.h"
#include "auto_free.h"
#include "hash.h"

#include <cstdlib>
#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#ifdef HAVE_TR1_UNORDERED_MAP
#include <tr1/unordered_map>
#else
#include <ext/hash_map>
#endif
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstdarg>
#include <cassert>
#include <iomanip>
#include <cerrno>

extern "C" {
#include <fcntl.h>
#include <elfutils/libdwfl.h>
#include <elfutils/libdw.h>
#include <dwarf.h>
#include <elf.h>
#include <obstack.h>
#include <regex.h>
#include <glob.h>
#include <fnmatch.h>
#include <stdio.h>
#include <sys/types.h>

#include "loc2c.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}


using namespace std;
using namespace __gnu_cxx;


static string TOK_KERNEL("kernel");


dwflpp::dwflpp(systemtap_session & session, const string& user_module):
  sess(session), dwfl(NULL), module(NULL), module_dwarf(NULL),
  module_bias(0), mod_info(NULL), module_start(0), module_end(0),
  cu(NULL), function(NULL)
{
  if (user_module.empty())
    setup_kernel();
  else
    setup_user(user_module);
}


dwflpp::~dwflpp()
{
  if (dwfl)
    dwfl_end(dwfl);
}


string const
dwflpp::default_name(char const * in, char const *)
{
  if (in)
    return in;
  return string("");
}


void
dwflpp::get_module_dwarf(bool required, bool report)
{
  module_dwarf = dwfl_module_getdwarf(module, &module_bias);
  mod_info->dwarf_status = (module_dwarf ? info_present : info_absent);
  if (!module_dwarf && report)
    {
      string msg = "cannot find ";
      if (module_name == "")
        msg += "kernel";
      else
        msg += string("module ") + module_name;
      msg += " debuginfo";

      int i = dwfl_errno();
      if (i)
        msg += string(": ") + dwfl_errmsg (i);

      if (required)
        throw semantic_error (msg);
      else
        cerr << "WARNING: " << msg << "\n";
    }
}


void
dwflpp::focus_on_module(Dwfl_Module * m, module_info * mi)
{
  module = m;
  mod_info = mi;
  if (m)
    {
      module_name = default_name(dwfl_module_info(module, NULL,
                                                  &module_start, &module_end,
                                                  NULL, NULL, NULL, NULL),
                                 "module");
    }
  else
    {
      assert(mi && mi->name && mi->name == TOK_KERNEL);
      module_name = mi->name;
      module_start = 0;
      module_end = 0;
      module_bias = mi->bias;
    }

  // Reset existing pointers and names

  module_dwarf = NULL;

  cu_name.clear();
  cu = NULL;

  function_name.clear();
  function = NULL;
}


void
dwflpp::focus_on_cu(Dwarf_Die * c)
{
  assert(c);
  assert(module);

  cu = c;
  cu_name = default_name(dwarf_diename(c), "CU");

  // Reset existing pointers and names
  function_name.clear();
  function = NULL;
}


void
dwflpp::focus_on_function(Dwarf_Die * f)
{
  assert(f);
  assert(module);
  assert(cu);

  function = f;
  function_name = default_name(dwarf_diename(function), "function");
}


void
dwflpp::query_cu_containing_address(Dwarf_Addr a, void *arg)
{
  Dwarf_Addr bias;
  assert(dwfl);
  assert(module);
  get_module_dwarf();

  // globalize the module-relative address
  if (module_name != TOK_KERNEL && dwfl_module_relocations (module) > 0)
    a += module_start;

  Dwarf_Die* cudie = dwfl_module_addrdie(module, a, &bias);
  if (cudie) // address could be wildly out of range
    query_cu (cudie, arg);
  assert(bias == module_bias);
}


bool
dwflpp::module_name_matches(string pattern)
{
  bool t = (fnmatch(pattern.c_str(), module_name.c_str(), 0) == 0);
  if (t && sess.verbose>3)
    clog << "pattern '" << pattern << "' "
      << "matches "
      << "module '" << module_name << "'" << "\n";
  return t;
}


bool
dwflpp::name_has_wildcard(string pattern)
{
  return (pattern.find('*') != string::npos ||
          pattern.find('?') != string::npos ||
          pattern.find('[') != string::npos);
}


bool
dwflpp::module_name_final_match(string pattern)
{
  // Assume module_name_matches().  Can there be any more matches?
  // Not unless the pattern is a wildcard, since module names are
  // presumed unique.
  return !name_has_wildcard(pattern);
}


bool
dwflpp::function_name_matches_pattern(string name, string pattern)
{
  bool t = (fnmatch(pattern.c_str(), name.c_str(), 0) == 0);
  if (t && sess.verbose>3)
    clog << "pattern '" << pattern << "' "
         << "matches "
         << "function '" << name << "'" << "\n";
  return t;
}


bool
dwflpp::function_name_matches(string pattern)
{
  assert(function);
  return function_name_matches_pattern(function_name, pattern);
}


bool
dwflpp::function_name_final_match(string pattern)
{
  return module_name_final_match (pattern);
}


void
dwflpp::setup_kernel(bool debuginfo_needed)
{
  // XXX: See also translate.cxx:emit_symbol_data

  if (! sess.module_cache)
    sess.module_cache = new module_cache ();

  static const char *debuginfo_path_arr = "+:.debug:/usr/lib/debug:build";
  static const char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
  static const char *debuginfo_path = (debuginfo_env_arr ?: debuginfo_path_arr );

  static const Dwfl_Callbacks kernel_callbacks =
    {
      dwfl_linux_kernel_find_elf,
      dwfl_standard_find_debuginfo,
      dwfl_offline_section_address,
      (char **) & debuginfo_path
    };

  dwfl = dwfl_begin (&kernel_callbacks);
  if (!dwfl)
    throw semantic_error ("cannot open dwfl");
  dwfl_report_begin (dwfl);

  // We have a problem with -r REVISION vs -r BUILDDIR here.  If
  // we're running against a fedora/rhel style kernel-debuginfo
  // tree, s.kernel_build_tree is not the place where the unstripped
  // vmlinux will be installed.  Rather, it's over yonder at
  // /usr/lib/debug/lib/modules/$REVISION/.  It seems that there is
  // no way to set the dwfl_callback.debuginfo_path and always
  // passs the plain kernel_release here.  So instead we have to
  // hard-code this magic here.
  string elfutils_kernel_path;
  if (sess.kernel_build_tree == string("/lib/modules/" + sess.kernel_release + "/build"))
    elfutils_kernel_path = sess.kernel_release;
  else
    elfutils_kernel_path = sess.kernel_build_tree;      

  int rc = dwfl_linux_kernel_report_offline (dwfl,
                                             elfutils_kernel_path.c_str(),
                                             &dwfl_report_offline_predicate);

  if (debuginfo_needed)
    dwfl_assert (string("missing ") + sess.architecture +
                 string(" kernel/module debuginfo under '") +
                 sess.kernel_build_tree + string("'"),
                 rc);

  // XXX: it would be nice if we could do a single
  // ..._report_offline call for an entire systemtap script, so
  // that a selection predicate would filter out modules outside
  // the union of all the requested wildcards.  But we build
  // derived_probes one-by-one and we don't have lookahead.
  // PR 3498.

  // XXX: a special case: if we have only kernel.* probe points,
  // we shouldn't waste time looking for module debug-info (and
  // vice versa).

  // NB: the result of an _offline call is the assignment of
  // virtualized addresses to relocatable objects such as
  // modules.  These have to be converted to real addresses at
  // run time.  See the dwarf_derived_probe ctor and its caller.

  dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
}


void
dwflpp::setup_user(const string& module_name, bool debuginfo_needed)
{
  if (! sess.module_cache)
    sess.module_cache = new module_cache ();

  static const char *debuginfo_path_arr = "+:.debug:/usr/lib/debug:build";
  static const char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
  // NB: kernel_build_tree doesn't enter into this, as it's for
  // kernel-side modules only.
  static const char *debuginfo_path = (debuginfo_env_arr ?: debuginfo_path_arr);

  static const Dwfl_Callbacks user_callbacks =
    {
      NULL, /* dwfl_linux_kernel_find_elf, */
      dwfl_standard_find_debuginfo,
      dwfl_offline_section_address,
      (char **) & debuginfo_path
    };

  dwfl = dwfl_begin (&user_callbacks);
  if (!dwfl)
    throw semantic_error ("cannot open dwfl");
  dwfl_report_begin (dwfl);

  // XXX: should support buildid-based naming

  Dwfl_Module *mod = dwfl_report_offline (dwfl,
                                module_name.c_str(),
                                module_name.c_str(),
                                -1);
  // XXX: save mod!

  if (debuginfo_needed)
    dwfl_assert (string("missing process ") +
                 module_name +
                 string(" ") +
                 sess.architecture +
                 string(" debuginfo"),
                 mod);

  if (!module)
    module = mod;

  // NB: the result of an _offline call is the assignment of
  // virtualized addresses to relocatable objects such as
  // modules.  These have to be converted to real addresses at
  // run time.  See the dwarf_derived_probe ctor and its caller.

  dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
}


void
dwflpp::iterate_over_modules(int (* callback)(Dwfl_Module *, void **,
                                              const char *, Dwarf_Addr,
                                              void *),
                             base_query *data)
{
  ptrdiff_t off = 0;
  do
    {
      if (pending_interrupts) return;
      off = dwfl_getmodules (dwfl, callback, data, off);
    }
  while (off > 0);
  // Don't complain if we exited dwfl_getmodules early.
  // This could be a $target variable error that will be
  // reported soon anyway.
  // dwfl_assert("dwfl_getmodules", off == 0);

  // PR6864 XXX: For dwarfless case (if .../vmlinux is missing), then the
  // "kernel" module is not reported in the loop above.  However, we
  // may be able to make do with symbol table data.
}


void
dwflpp::iterate_over_cus (int (*callback)(Dwarf_Die * die, void * arg),
                          void * data)
{
  get_module_dwarf(false);
  Dwarf *dw = module_dwarf;
  if (!dw) return;

  vector<Dwarf_Die>* v = module_cu_cache[dw];
  if (v == 0)
    {
      v = new vector<Dwarf_Die>;
      module_cu_cache[dw] = v;

      Dwarf_Off off = 0;
      size_t cuhl;
      Dwarf_Off noff;
      while (dwarf_nextcu (dw, off, &noff, &cuhl, NULL, NULL, NULL) == 0)
        {
          if (pending_interrupts) return;
          Dwarf_Die die_mem;
          Dwarf_Die *die;
          die = dwarf_offdie (dw, off + cuhl, &die_mem);
          v->push_back (*die); /* copy */
          off = noff;
        }
    }

  for (unsigned i = 0; i < v->size(); i++)
    {
      if (pending_interrupts) return;
      Dwarf_Die die = v->at(i);
      int rc = (*callback)(& die, data);
      if (rc != DWARF_CB_OK) break;
    }
}


bool
dwflpp::func_is_inline()
{
  assert (function);
  return dwarf_func_inline (function) != 0;
}


int
dwflpp::cu_inl_function_caching_callback (Dwarf_Die* func, void *arg)
{
  vector<Dwarf_Die>* v = static_cast<vector<Dwarf_Die>*>(arg);
  v->push_back (* func);
  return DWARF_CB_OK;
}


void
dwflpp::iterate_over_inline_instances (int (* callback)(Dwarf_Die * die, void * arg),
                                       void * data)
{
  assert (function);
  assert (func_is_inline ());

  string key = module_name + ":" + cu_name + ":" + function_name;
  vector<Dwarf_Die>* v = cu_inl_function_cache[key];
  if (v == 0)
    {
      v = new vector<Dwarf_Die>;
      cu_inl_function_cache[key] = v;
      dwarf_func_inline_instances (function, cu_inl_function_caching_callback, v);
    }

  for (unsigned i=0; i<v->size(); i++)
    {
      if (pending_interrupts) return;
      Dwarf_Die die = v->at(i);
      int rc = (*callback)(& die, data);
      if (rc != DWARF_CB_OK) break;
    }
}


int
dwflpp::global_alias_caching_callback(Dwarf_Die *die, void *arg)
{
  cu_function_cache_t *cache = static_cast<cu_function_cache_t*>(arg);
  const char *name = dwarf_diename(die);

  if (!name)
    return DWARF_CB_OK;

  string structure_name = name;

  if (!dwarf_hasattr(die, DW_AT_declaration) &&
      cache->find(structure_name) == cache->end())
    (*cache)[structure_name] = *die;

  return DWARF_CB_OK;
}


Dwarf_Die *
dwflpp::declaration_resolve(const char *name)
{
  if (!name)
    return NULL;

  string key = module_name + ":" + cu_name;
  cu_function_cache_t *v = global_alias_cache[key];
  if (v == 0) // need to build the cache, just once per encountered module/cu
    {
      v = new cu_function_cache_t;
      global_alias_cache[key] = v;
      iterate_over_globals(global_alias_caching_callback, v);
      if (sess.verbose > 4)
        clog << "global alias cache " << key << " size " << v->size() << endl;
    }

  // XXX: it may be desirable to search other modules' declarations
  // too, in case a module/shared-library processes a
  // forward-declared pointer type only, where the actual definition
  // may only be in vmlinux or the application.

  // XXX: it is probably desirable to search other CU's declarations
  // in the same module.

  if (v->find(name) == v->end())
    return NULL;

  return & ((*v)[name]);
}


int
dwflpp::cu_function_caching_callback (Dwarf_Die* func, void *arg)
{
  cu_function_cache_t* v = static_cast<cu_function_cache_t*>(arg);
  const char *name = dwarf_diename(func);
  if (!name)
    return DWARF_CB_OK;

  string function_name = name;
  (*v)[function_name] = * func;
  return DWARF_CB_OK;
}


int
dwflpp::iterate_over_functions (int (* callback)(Dwarf_Die * func, base_query * q),
                                base_query * q, const string& function,
                                bool has_statement_num)
{
  int rc = DWARF_CB_OK;
  assert (module);
  assert (cu);

  string key = module_name + ":" + cu_name;
  cu_function_cache_t *v = cu_function_cache[key];
  if (v == 0)
    {
      v = new cu_function_cache_t;
      cu_function_cache[key] = v;
      dwarf_getfuncs (cu, cu_function_caching_callback, v, 0);
      if (sess.verbose > 4)
        clog << "function cache " << key << " size " << v->size() << endl;
    }

  string subkey = function;
  if (v->find(subkey) != v->end())
    {
      Dwarf_Die die = v->find(subkey)->second;
      if (sess.verbose > 4)
        clog << "function cache " << key << " hit " << subkey << endl;
      return (*callback)(& die, q);
    }
  else if (name_has_wildcard (subkey))
    {
      for (cu_function_cache_t::iterator it = v->begin(); it != v->end(); it++)
        {
        if (pending_interrupts) return DWARF_CB_ABORT;
          string func_name = it->first;
          Dwarf_Die die = it->second;
          if (function_name_matches_pattern (func_name, subkey))
            {
              if (sess.verbose > 4)
                clog << "function cache " << key << " match " << func_name << " vs "
                     << subkey << endl;

              rc = (*callback)(& die, q);
              if (rc != DWARF_CB_OK) break;
            }
        }
    }
  else if (has_statement_num) // searching all for kernel.statement
    {
      for (cu_function_cache_t::iterator it = v->begin(); it != v->end(); it++)
        {
          Dwarf_Die die = it->second;
          rc = (*callback)(& die, q);
          if (rc != DWARF_CB_OK) break;
        }
    }
  else // not a wildcard and no match in this CU
    {
      // do nothing
    }
  return rc;
}


/* This basically only goes one level down from the compile unit so it
 * only picks up top level stuff (i.e. nothing in a lower scope) */
int
dwflpp::iterate_over_globals (int (* callback)(Dwarf_Die *, void *),
                              void * data)
{
  int rc = DWARF_CB_OK;
  Dwarf_Die die;

  assert (module);
  assert (cu);
  assert (dwarf_tag(cu) == DW_TAG_compile_unit);

  if (dwarf_child(cu, &die) != 0)
    return rc;

  do
    /* We're only currently looking for named types,
     * although other types of declarations exist */
    switch (dwarf_tag(&die))
      {
      case DW_TAG_base_type:
      case DW_TAG_enumeration_type:
      case DW_TAG_structure_type:
      case DW_TAG_typedef:
      case DW_TAG_union_type:
        rc = (*callback)(&die, data);
        break;
      }
  while (rc == DWARF_CB_OK && dwarf_siblingof(&die, &die) == 0);

  return rc;
}


// This little test routine represents an unfortunate breakdown in
// abstraction between dwflpp (putatively, a layer right on top of
// elfutils), and dwarf_query (interpreting a systemtap probe point).
// It arises because we sometimes try to fix up slightly-off
// .statement() probes (something we find out in fairly low-level).
//
// An alternative would be to put some more intelligence into query_cu(),
// and have it print additional suggestions after finding that
// q->dw.iterate_over_srcfile_lines resulted in no new finished_results.

bool
dwflpp::has_single_line_record (dwarf_query * q, char const * srcfile, int lineno)
{
  if (lineno < 0)
    return false;

    Dwarf_Line **srcsp = NULL;
    size_t nsrcs = 0;

    dwarf_assert ("dwarf_getsrc_file",
                  dwarf_getsrc_file (module_dwarf,
                                    srcfile, lineno, 0,
                                     &srcsp, &nsrcs));

    if (nsrcs != 1)
      {
        if (sess.verbose>4)
          clog << "alternative line " << lineno << " rejected: nsrcs=" << nsrcs << endl;
        return false;
      }

    // We also try to filter out lines that leave the selected
    // functions (if any).

    dwarf_line_t line(srcsp[0]);
    Dwarf_Addr addr = line.addr();

    func_info_map_t *filtered_functions = get_filtered_functions(q);
    for (func_info_map_t::iterator i = filtered_functions->begin();
         i != filtered_functions->end(); ++i)
      {
        if (die_has_pc (i->die, addr))
          {
            if (sess.verbose>4)
              clog << "alternative line " << lineno << " accepted: fn=" << i->name << endl;
            return true;
          }
      }

    inline_instance_map_t *filtered_inlines = get_filtered_inlines(q);
    for (inline_instance_map_t::iterator i = filtered_inlines->begin();
         i != filtered_inlines->end(); ++i)
      {
        if (die_has_pc (i->die, addr))
          {
            if (sess.verbose>4)
              clog << "alternative line " << lineno << " accepted: ifn=" << i->name << endl;
            return true;
          }
      }

    if (sess.verbose>4)
      clog << "alternative line " << lineno << " rejected: leaves selected fns" << endl;
    return false;
}


void
dwflpp::iterate_over_srcfile_lines (char const * srcfile,
                                    int lines[2],
                                    bool need_single_match,
                                    enum line_t line_type,
                                    void (* callback) (const dwarf_line_t& line,
                                                       void * arg),
                                    void *data)
{
  Dwarf_Line **srcsp = NULL;
  size_t nsrcs = 0;
  dwarf_query * q = static_cast<dwarf_query *>(data);
  int lineno = lines[0];
  auto_free_ref<Dwarf_Line**> free_srcsp(srcsp);

  get_module_dwarf();

  if (line_type == RELATIVE)
    {
      Dwarf_Addr addr;
      Dwarf_Line *line;
      int line_number;

      dwarf_assert ("dwarf_entrypc", dwarf_entrypc (this->function, &addr));
      line = dwarf_getsrc_die (this->cu, addr);
      dwarf_assert ("dwarf_getsrc_die", line == NULL);
      dwarf_assert ("dwarf_lineno", dwarf_lineno (line, &line_number));
      lineno += line_number;
    }
  else if (line_type == WILDCARD)
    function_line (&lineno);

  for (int l = lineno; ; l = l + 1)
    {
      set<int> lines_probed;
      pair<set<int>::iterator,bool> line_probed;
      dwarf_assert ("dwarf_getsrc_file",
                    dwarf_getsrc_file (module_dwarf,
                                       srcfile, l, 0,
                                       &srcsp, &nsrcs));
      if (line_type == WILDCARD || line_type == RANGE)
        {
          Dwarf_Addr line_addr;
          dwarf_lineno (srcsp [0], &lineno);
          line_probed = lines_probed.insert(lineno);
          if (lineno != l || line_probed.second == false || nsrcs > 1)
            continue;
          dwarf_lineaddr (srcsp [0], &line_addr);
          if (dwarf_haspc (function, line_addr) != 1)
            break;
        }

      // NB: Formerly, we used to filter, because:

      // dwarf_getsrc_file gets one *near hits* for line numbers, not
      // exact matches.  For example, an existing file but a nonexistent
      // line number will be rounded up to the next definition in that
      // file.  This may be similar to the GDB breakpoint algorithm, but
      // we don't want to be so fuzzy in systemtap land.  So we filter.

      // But we now see the error of our ways, and skip this filtering.

      // XXX: the code also fails to match e.g.  inline function
      // definitions when the srcfile is a header file rather than the
      // CU name.

      size_t remaining_nsrcs = nsrcs;

      if (need_single_match && remaining_nsrcs > 1)
        {
          // We wanted a single line record (a unique address for the
          // line) and we got a bunch of line records. We're going to
          // skip this probe (throw an exception) but before we throw
          // we're going to look around a bit to see if there's a low or
          // high line number nearby which *doesn't* have this problem,
          // so we can give the user some advice.

          int lo_try = -1;
          int hi_try = -1;
          for (size_t i = 1; i < 6; ++i)
            {
              if (lo_try == -1 && has_single_line_record(q, srcfile, lineno - i))
                lo_try = lineno - i;

              if (hi_try == -1 && has_single_line_record(q, srcfile, lineno + i))
                hi_try = lineno + i;
            }

          stringstream advice;
          advice << "multiple addresses for " << srcfile << ":" << lineno;
          if (lo_try > 0 || hi_try > 0)
            {
              advice << " (try ";
              if (lo_try > 0)
                advice << srcfile << ":" << lo_try;
              if (lo_try > 0 && hi_try > 0)
                advice << " or ";
              if (hi_try > 0)
                advice << srcfile << ":" << hi_try;
              advice << ")";
            }
          throw semantic_error (advice.str());
        }

      for (size_t i = 0; i < nsrcs; ++i)
        {
          if (pending_interrupts) return;
          if (srcsp [i]) // skip over mismatched lines
            callback (dwarf_line_t(srcsp[i]), data);
        }

      if (line_type == ABSOLUTE || line_type == RELATIVE)
        break;
      else if (line_type == RANGE && l == lines[1])
        break;
    }
}


void
dwflpp::iterate_over_labels (Dwarf_Die *begin_die,
                             const char *sym,
                             const char *symfunction,
                             void *data,
                             void (* callback)(const string &,
                                               const char *,
                                               int,
                                               Dwarf_Die *,
                                               Dwarf_Addr,
                                               dwarf_query *))
{
  dwarf_query * q __attribute__ ((unused)) = static_cast<dwarf_query *>(data) ;

  get_module_dwarf();

  Dwarf_Die die;
  int res = dwarf_child (begin_die, &die);
  if (res != 0)
    return;  // die without children, bail out.

  static string function_name = dwarf_diename (begin_die);
  do
    {
      Dwarf_Attribute attr_mem;
      Dwarf_Attribute *attr = dwarf_attr (&die, DW_AT_name, &attr_mem);
      int tag = dwarf_tag(&die);
      const char *name = dwarf_formstring (attr);
      if (name == 0)
        continue;
      switch (tag)
        {
        case DW_TAG_label:
          break;
        case DW_TAG_subprogram:
          if (!dwarf_hasattr(&die, DW_AT_declaration))
            function_name = name;
          else
            continue;
        default:
          if (dwarf_haschildren (&die))
            iterate_over_labels (&die, sym, symfunction, q, callback);
          continue;
        }

      if (strcmp(function_name.c_str(), symfunction) == 0
          || (name_has_wildcard(symfunction)
              && function_name_matches (symfunction)))
        {
        }
      else
        continue;
      if (strcmp(name, sym) == 0
          || (name_has_wildcard(sym)
              && function_name_matches_pattern (name, sym)))
        {
          const char *file = dwarf_decl_file (&die);
          // Get the line number for this label
          Dwarf_Attribute attr;
          dwarf_attr (&die,DW_AT_decl_line, &attr);
          Dwarf_Sword dline;
          dwarf_formsdata (&attr, &dline);
          Dwarf_Addr stmt_addr;
          if (dwarf_lowpc (&die, &stmt_addr) != 0)
            {
              // There is no lowpc so figure out the address
              // Get the real die for this cu
              Dwarf_Die cudie;
              dwarf_diecu (cu, &cudie, NULL, NULL);
              size_t nlines = 0;
              // Get the line for this label
              Dwarf_Line **aline;
              dwarf_getsrc_file (module_dwarf, file, (int)dline, 0, &aline, &nlines);
              // Get the address
              for (size_t i = 0; i < nlines; i++)
                {
                  dwarf_lineaddr (*aline, &stmt_addr);
                  if ((dwarf_haspc (&die, stmt_addr)))
                    break;
                }
            }

          Dwarf_Die *scopes;
          int nscopes = 0;
          nscopes = dwarf_getscopes_die (&die, &scopes);
          if (nscopes > 1)
            {
              callback(function_name.c_str(), file,
                       (int)dline, &scopes[1], stmt_addr, q);
              add_label_name(q, name);
            }
        }
    }
  while (dwarf_siblingof (&die, &die) == 0);
}


void
dwflpp::collect_srcfiles_matching (string const & pattern,
                                   set<char const *> & filtered_srcfiles)
{
  assert (module);
  assert (cu);

  size_t nfiles;
  Dwarf_Files *srcfiles;

  // PR 5049: implicit * in front of given path pattern.
  // NB: fnmatch() is used without FNM_PATHNAME.
  string prefixed_pattern = string("*/") + pattern;

  dwarf_assert ("dwarf_getsrcfiles",
                dwarf_getsrcfiles (cu, &srcfiles, &nfiles));
  {
  for (size_t i = 0; i < nfiles; ++i)
    {
      char const * fname = dwarf_filesrc (srcfiles, i, NULL, NULL);
      if (fnmatch (pattern.c_str(), fname, 0) == 0 ||
          fnmatch (prefixed_pattern.c_str(), fname, 0) == 0)
        {
          filtered_srcfiles.insert (fname);
          if (sess.verbose>2)
            clog << "selected source file '" << fname << "'\n";
        }
    }
  }
}


void
dwflpp::resolve_prologue_endings (func_info_map_t & funcs)
{
  // This heuristic attempts to pick the first address that has a
  // source line distinct from the function declaration's.  In a
  // perfect world, this would be the first statement *past* the
  // prologue.

  assert(module);
  assert(cu);

  size_t nlines = 0;
  Dwarf_Lines *lines = NULL;

  /* trouble cases:
     malloc do_symlink  in init/initramfs.c    tail-recursive/tiny then no-prologue
     sys_get?id         in kernel/timer.c      no-prologue
     sys_exit_group                            tail-recursive
     {do_,}sys_open                            extra-long-prologue (gcc 3.4)
     cpu_to_logical_apicid                     NULL-decl_file
   */

  // Fetch all srcline records, sorted by address.
  dwarf_assert ("dwarf_getsrclines",
                dwarf_getsrclines(cu, &lines, &nlines));
  // XXX: free lines[] later, but how?

  for(func_info_map_t::iterator it = funcs.begin(); it != funcs.end(); it++)
    {
#if 0 /* someday */
      Dwarf_Addr* bkpts = 0;
      int n = dwarf_entry_breakpoints (& it->die, & bkpts);
      // ...
      free (bkpts);
#endif

      Dwarf_Addr entrypc = it->entrypc;
      Dwarf_Addr highpc; // NB: highpc is exclusive: [entrypc,highpc)
      dwfl_assert ("dwarf_highpc", dwarf_highpc (& it->die,
                                                 & highpc));

      if (it->decl_file == 0) it->decl_file = "";

      unsigned entrypc_srcline_idx = 0;
      dwarf_line_t entrypc_srcline;
      // open-code binary search for exact match
      {
        unsigned l = 0, h = nlines;
        while (l < h)
          {
            entrypc_srcline_idx = (l + h) / 2;
            const dwarf_line_t lr(dwarf_onesrcline(lines,
                                                   entrypc_srcline_idx));
            Dwarf_Addr addr = lr.addr();
            if (addr == entrypc) { entrypc_srcline = lr; break; }
            else if (l + 1 == h) { break; } // ran off bottom of tree
            else if (addr < entrypc) { l = entrypc_srcline_idx; }
            else { h = entrypc_srcline_idx; }
          }
      }
      if (!entrypc_srcline)
        {
          if (sess.verbose > 2)
            clog << "missing entrypc dwarf line record for function '"
                 << it->name << "'\n";
          // This is probably an inlined function.  We'll end up using
          // its lowpc as a probe address.
          continue;
        }

      if (sess.verbose>2)
        clog << "prologue searching function '" << it->name << "'"
             << " 0x" << hex << entrypc << "-0x" << highpc << dec
             << "@" << it->decl_file << ":" << it->decl_line
             << "\n";

      // Now we go searching for the first line record that has a
      // file/line different from the one in the declaration.
      // Normally, this will be the next one.  BUT:
      //
      // We may have to skip a few because some old compilers plop
      // in dummy line records for longer prologues.  If we go too
      // far (addr >= highpc), we take the previous one.  Or, it may
      // be the first one, if the function had no prologue, and thus
      // the entrypc maps to a statement in the body rather than the
      // declaration.

      unsigned postprologue_srcline_idx = entrypc_srcline_idx;
      bool ranoff_end = false;
      while (postprologue_srcline_idx < nlines)
        {
          dwarf_line_t lr(dwarf_onesrcline(lines, postprologue_srcline_idx));
          Dwarf_Addr postprologue_addr = lr.addr();
          const char* postprologue_file = lr.linesrc();
          int postprologue_lineno = lr.lineno();

          if (sess.verbose>2)
            clog << "checking line record 0x" << hex << postprologue_addr << dec
                 << "@" << postprologue_file << ":" << postprologue_lineno << "\n";

          if (postprologue_addr >= highpc)
            {
              ranoff_end = true;
              postprologue_srcline_idx --;
              continue;
            }
          if (ranoff_end ||
              (strcmp (postprologue_file, it->decl_file) || // We have a winner!
               (postprologue_lineno != it->decl_line)))
            {
              it->prologue_end = postprologue_addr;

              if (sess.verbose>2)
                {
                  clog << "prologue found function '" << it->name << "'";
                  // Add a little classification datum
                  if (postprologue_srcline_idx == entrypc_srcline_idx) clog << " (naked)";
                  if (ranoff_end) clog << " (tail-call?)";
                  clog << " = 0x" << hex << postprologue_addr << dec << "\n";
                }

              break;
            }

          // Let's try the next srcline.
          postprologue_srcline_idx ++;
        } // loop over srclines

      // if (strlen(it->decl_file) == 0) it->decl_file = NULL;

    } // loop over functions

  // XXX: how to free lines?
}


bool
dwflpp::function_entrypc (Dwarf_Addr * addr)
{
  assert (function);
  return (dwarf_entrypc (function, addr) == 0);
  // XXX: see also _lowpc ?
}


bool
dwflpp::die_entrypc (Dwarf_Die * die, Dwarf_Addr * addr)
{
  int rc = 0;
  string lookup_method;

  * addr = 0;

  lookup_method = "dwarf_entrypc";
  rc = dwarf_entrypc (die, addr);

  if (rc)
    {
      lookup_method = "dwarf_lowpc";
      rc = dwarf_lowpc (die, addr);
    }

  if (rc)
    {
      lookup_method = "dwarf_ranges";

      Dwarf_Addr base;
      Dwarf_Addr begin;
      Dwarf_Addr end;
      ptrdiff_t offset = dwarf_ranges (die, 0, &base, &begin, &end);
      if (offset < 0) rc = -1;
      else if (offset > 0)
        {
          * addr = begin;
          rc = 0;

          // Now we need to check that there are no more ranges
          // associated with this function, which could conceivably
          // happen if a function is inlined, then pieces of it are
          // split amongst different conditional branches.  It's not
          // obvious which of them to favour.  As a heuristic, we
          // pick the beginning of the first range, and ignore the
          // others (but with a warning).

          unsigned extra = 0;
          while ((offset = dwarf_ranges (die, offset, &base, &begin, &end)) > 0)
            extra ++;
          if (extra)
            lookup_method += ", ignored " + lex_cast<string>(extra) + " more";
        }
    }

  if (sess.verbose > 2)
    clog << "entry-pc lookup (" << lookup_method << ") = 0x" << hex << *addr << dec
         << " (rc " << rc << ")"
         << endl;
  return (rc == 0);
}


void
dwflpp::function_die (Dwarf_Die *d)
{
  assert (function);
  *d = *function;
}


void
dwflpp::function_file (char const ** c)
{
  assert (function);
  assert (c);
  *c = dwarf_decl_file (function);
}


void
dwflpp::function_line (int *linep)
{
  assert (function);
  dwarf_decl_line (function, linep);
}


bool
dwflpp::die_has_pc (Dwarf_Die & die, Dwarf_Addr pc)
{
  int res = dwarf_haspc (&die, pc);
  // dwarf_ranges will return -1 if a function die has no DW_AT_ranges
  // if (res == -1)
  //    dwarf_assert ("dwarf_haspc", res);
  return res == 1;
}


void
dwflpp::loc2c_error (void *, const char *fmt, ...)
{
  const char *msg = "?";
  char *tmp = NULL;
  int rc;
  va_list ap;
  va_start (ap, fmt);
  rc = vasprintf (& tmp, fmt, ap);
  if (rc < 0)
    msg = "?";
  else
    msg = tmp;
  va_end (ap);
  throw semantic_error (msg);
}


// This function generates code used for addressing computations of
// target variables.
void
dwflpp::emit_address (struct obstack *pool, Dwarf_Addr address)
{
  #if 0
  // The easy but incorrect way is to just print a hard-wired
  // constant.
  obstack_printf (pool, "%#" PRIx64 "UL", address);
  #endif

  // Turn this address into a section-relative offset if it should be one.
  // We emit a comment approximating the variable+offset expression that
  // relocatable module probing code will need to have.
  Dwfl_Module *mod = dwfl_addrmodule (dwfl, address);
  dwfl_assert ("dwfl_addrmodule", mod);
  const char *modname = dwfl_module_info (mod, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL);
  int n = dwfl_module_relocations (mod);
  dwfl_assert ("dwfl_module_relocations", n >= 0);
  Dwarf_Addr reloc_address = address;
  int i = dwfl_module_relocate_address (mod, &reloc_address);
  dwfl_assert ("dwfl_module_relocate_address", i >= 0);
  dwfl_assert ("dwfl_module_info", modname);
  const char *secname = dwfl_module_relocation_info (mod, i, NULL);

  if (sess.verbose > 2)
    {
      clog << "emit dwarf addr 0x" << hex << address << dec
           << " => module " << modname
           << " section " << (secname ?: "null")
           << " relocaddr 0x" << hex << reloc_address << dec
           << endl;
    }

  if (n > 0 && !(n == 1 && secname == NULL))
   {
      dwfl_assert ("dwfl_module_relocation_info", secname);
      if (n > 1 || secname[0] != '\0')
        {
          // This gives us the module name, and section name within the
          // module, for a kernel module (or other ET_REL module object).
          obstack_printf (pool, "({ static unsigned long addr = 0; ");
          obstack_printf (pool, "if (addr==0) addr = _stp_module_relocate (\"%s\",\"%s\",%#" PRIx64 "); ",
                          modname, secname, reloc_address);
          obstack_printf (pool, "addr; })");
        }
      else if (n == 1 && module_name == TOK_KERNEL && secname[0] == '\0')
        {
          // elfutils' way of telling us that this is a relocatable kernel address, which we
          // need to treat the same way here as dwarf_query::add_probe_point does: _stext.
          address -= sess.sym_stext;
          secname = "_stext";
          obstack_printf (pool, "({ static unsigned long addr = 0; ");
          obstack_printf (pool, "if (addr==0) addr = _stp_module_relocate (\"%s\",\"%s\",%#" PRIx64 "); ",
                          modname, secname, address); // PR10000 NB: not reloc_address
          obstack_printf (pool, "addr; })");
        }
      else
        {
          throw semantic_error ("cannot relocate user-space dso (?) address");
#if 0
          // This would happen for a Dwfl_Module that's a user-level DSO.
          obstack_printf (pool, " /* %s+%#" PRIx64 " */",
                          modname, address);
#endif
        }
    }
  else
    obstack_printf (pool, "%#" PRIx64 "UL", address); // assume as constant
}


void
dwflpp::loc2c_emit_address (void *arg, struct obstack *pool,
                            Dwarf_Addr address)
{
  static_cast<dwflpp *>(arg)->emit_address (pool, address);
}


void
dwflpp::print_locals(Dwarf_Die *die, ostream &o)
{
  // Try to get the first child of die.
  Dwarf_Die child;
  if (dwarf_child (die, &child) == 0)
    {
      do
        {
          const char *name;
          // Output each sibling's name (that is a variable or
          // parameter) to 'o'.
          switch (dwarf_tag (&child))
            {
            case DW_TAG_variable:
            case DW_TAG_formal_parameter:
              name = dwarf_diename (&child);
              if (name)
                o << " " << name;
              break;
            default:
              break;
            }
        }
      while (dwarf_siblingof (&child, &child) == 0);
    }
}


Dwarf_Attribute *
dwflpp::find_variable_and_frame_base (Dwarf_Die *scope_die,
                                      Dwarf_Addr pc,
                                      string const & local,
                                      const target_symbol *e,
                                      Dwarf_Die *vardie,
                                      Dwarf_Attribute *fb_attr_mem)
{
  Dwarf_Die *scopes;
  int nscopes = 0;
  Dwarf_Attribute *fb_attr = NULL;

  assert (cu);

  nscopes = dwarf_getscopes (cu, pc, &scopes);
  int sidx;
  // if pc and scope_die are disjoint then we need dwarf_getscopes_die
  for (sidx = 0; sidx < nscopes; sidx++)
    if (scopes[sidx].addr == scope_die->addr)
      break;
  if (sidx == nscopes)
    nscopes = dwarf_getscopes_die (scope_die, &scopes);

  if (nscopes <= 0)
    {
      throw semantic_error ("unable to find any scopes containing "
                            + lex_cast_hex<string>(pc)
                            + ((scope_die == NULL) ? ""
                               : (string (" in ")
                                  + (dwarf_diename(scope_die) ?: "<unknown>")
                                  + "(" + (dwarf_diename(cu) ?: "<unknown>")
                                  + ")"))
                            + " while searching for local '" + local + "'",
                            e->tok);
    }

  int declaring_scope = dwarf_getscopevar (scopes, nscopes,
                                           local.c_str(),
                                           0, NULL, 0, 0,
                                           vardie);
  if (declaring_scope < 0)
    {
      stringstream alternatives;
      print_locals (scopes, alternatives);
      throw semantic_error ("unable to find local '" + local + "'"
                            + " near pc " + lex_cast_hex<string>(pc)
                            + ((scope_die == NULL) ? ""
                               : (string (" in ")
                                  + (dwarf_diename(scope_die) ?: "<unknown>")
                                  + "(" + (dwarf_diename(cu) ?: "<unknown>")
                                  + ")"))
                            + (alternatives.str() == "" ? "" : (" (alternatives:" + alternatives.str () + ")")),
                            e->tok);
    }

  for (int inner = 0; inner < nscopes; ++inner)
    {
      switch (dwarf_tag (&scopes[inner]))
        {
        default:
          continue;
        case DW_TAG_subprogram:
        case DW_TAG_entry_point:
        case DW_TAG_inlined_subroutine:  /* XXX */
          if (inner >= declaring_scope)
            fb_attr = dwarf_attr_integrate (&scopes[inner],
                                            DW_AT_frame_base,
                                            fb_attr_mem);
          break;
        }
    }
  return fb_attr;
}


struct location *
dwflpp::translate_location(struct obstack *pool,
                           Dwarf_Attribute *attr, Dwarf_Addr pc,
                           Dwarf_Attribute *fb_attr,
                           struct location **tail,
                           const target_symbol *e)
{
  Dwarf_Op *expr;
  size_t len;

  /* PR9768: formerly, we added pc+module_bias here.  However, that bias value
     is not present in the pc value by the time we get it, so adding it would
     result in false negatives of variable reachibility.  In other instances
     further below, the c_translate_FOO functions, the module_bias value used
     to be passed in, but instead should now be zero for the same reason. */

  switch (dwarf_getlocation_addr (attr, pc /*+ module_bias*/, &expr, &len, 1))
    {
    case 1:			/* Should always happen.  */
      if (len > 0)
        break;
      /* Fall through.  */

    case 0:			/* Shouldn't happen.  */
      throw semantic_error ("not accessible at this address", e->tok);

    default:			/* Shouldn't happen.  */
    case -1:
      throw semantic_error (string ("dwarf_getlocation_addr failed") +
                            string (dwarf_errmsg (-1)),
                            e->tok);
    }

  return c_translate_location (pool, &loc2c_error, this,
                               &loc2c_emit_address,
                               1, 0 /* PR9768 */,
                               pc, expr, len, tail, fb_attr);
}


void
dwflpp::print_members(Dwarf_Die *vardie, ostream &o)
{
  const int typetag = dwarf_tag (vardie);

  if (typetag != DW_TAG_structure_type && typetag != DW_TAG_union_type)
    {
      o << " Error: "
        << (dwarf_diename_integrate (vardie) ?: "<anonymous>")
        << " isn't a struct/union";
      return;
    }

  // Try to get the first child of vardie.
  Dwarf_Die die_mem;
  Dwarf_Die *die = &die_mem;
  switch (dwarf_child (vardie, die))
    {
    case 1:				// No children.
      o << ((typetag == DW_TAG_union_type) ? " union " : " struct ")
        << (dwarf_diename_integrate (die) ?: "<anonymous>")
        << " is empty";
      break;

    case -1:				// Error.
    default:				// Shouldn't happen.
      o << ((typetag == DW_TAG_union_type) ? " union " : " struct ")
        << (dwarf_diename_integrate (die) ?: "<anonymous>")
        << ": " << dwarf_errmsg (-1);
      break;

    case 0:				// Success.
      break;
    }

  // Output each sibling's name to 'o'.
  while (dwarf_tag (die) == DW_TAG_member)
    {
      const char *member = dwarf_diename_integrate (die) ;

      if ( member != NULL )
        o << " " << member;
      else
        {
          Dwarf_Die temp_die = *die;
          Dwarf_Attribute temp_attr ;

          if (!dwarf_attr_integrate (&temp_die, DW_AT_type, &temp_attr))
            {
              clog << "\n Error in obtaining type attribute for "
                   << (dwarf_diename(&temp_die)?:"<anonymous>");
              return;
            }

          if (!dwarf_formref_die (&temp_attr, &temp_die))
            {
              clog << "\n Error in decoding type attribute for "
                   << (dwarf_diename(&temp_die)?:"<anonymous>");
              return;
            }

          print_members(&temp_die,o);
        }

      if (dwarf_siblingof (die, &die_mem) != 0)
        break;
    }
}


bool
dwflpp::find_struct_member(const string& member,
                           Dwarf_Die *parentdie,
                           const target_symbol *e,
                           Dwarf_Die *memberdie,
                           vector<Dwarf_Attribute>& locs)
{
  Dwarf_Attribute attr;
  Dwarf_Die die = *parentdie;

  switch (dwarf_child (&die, &die))
    {
    case 0:		/* First child found.  */
      break;
    case 1:		/* No children.  */
      return false;
    case -1:		/* Error.  */
    default:		/* Shouldn't happen */
      throw semantic_error (string (dwarf_tag(&die) == DW_TAG_union_type ? "union" : "struct")
                            + string (dwarf_diename_integrate (&die) ?: "<anonymous>")
                            + string (dwarf_errmsg (-1)),
                            e->tok);
    }

  do
    {
      if (dwarf_tag(&die) != DW_TAG_member)
        continue;

      const char *name = dwarf_diename_integrate(&die);
      if (name == NULL)
        {
          // need to recurse for anonymous structs/unions
          Dwarf_Die subdie;

          if (!dwarf_attr_integrate (&die, DW_AT_type, &attr) ||
              !dwarf_formref_die (&attr, &subdie))
            continue;

          if (find_struct_member(member, &subdie, e, memberdie, locs))
            goto success;
        }
      else if (name == member)
        {
          *memberdie = die;
          goto success;
        }
    }
  while (dwarf_siblingof (&die, &die) == 0);

  return false;

success:
  /* As we unwind the recursion, we need to build the chain of
   * locations that got to the final answer. */
  if (dwarf_attr_integrate (&die, DW_AT_data_member_location, &attr))
    locs.insert(locs.begin(), attr);

  /* Union members don't usually have a location,
   * but just use the containing union's location.  */
  else if (dwarf_tag(parentdie) != DW_TAG_union_type)
    throw semantic_error ("no location for field '" + member
                          + "': " + string(dwarf_errmsg (-1)),
                          e->tok);

  return true;
}


Dwarf_Die *
dwflpp::translate_components(struct obstack *pool,
                             struct location **tail,
                             Dwarf_Addr pc,
                             const target_symbol *e,
                             Dwarf_Die *vardie,
                             Dwarf_Die *die_mem,
                             Dwarf_Attribute *attr_mem)
{
  Dwarf_Die *die = NULL;

  unsigned i = 0;

  if (vardie)
    *die_mem = *vardie;

  if (e->components.empty())
    return die_mem;

  while (i < e->components.size())
    {
      /* XXX: This would be desirable, but we don't get the target_symbol token,
         and printing that gives us the file:line number too early anyway. */
#if 0
      // Emit a marker to note which field is being access-attempted, to give
      // better error messages if deref() fails.
      string piece = string(...target_symbol token...) + string ("#") + stringify(components[i].second);
      obstack_printf (pool, "c->last_stmt = %s;", lex_cast_qstring(piece).c_str());
#endif

      die = die ? dwarf_formref_die (attr_mem, die_mem) : die_mem;
      const int typetag = dwarf_tag (die);
      switch (typetag)
        {
        case DW_TAG_typedef:
        case DW_TAG_const_type:
        case DW_TAG_volatile_type:
          /* Just iterate on the referent type.  */
          break;

        case DW_TAG_pointer_type:
          if (e->components[i].first == target_symbol::comp_literal_array_index)
            throw semantic_error ("cannot index pointer", e->tok);
          // XXX: of course, we should support this the same way C does,
          // by explicit pointer arithmetic etc.  PR4166.

          c_translate_pointer (pool, 1, 0 /* PR9768*/, die, tail);
          break;

        case DW_TAG_array_type:
          if (e->components[i].first == target_symbol::comp_literal_array_index)
            {
              c_translate_array (pool, 1, 0 /* PR9768 */, die, tail,
                                 NULL, lex_cast<Dwarf_Word>(e->components[i].second));
              ++i;
            }
          else
            throw semantic_error("bad field '"
                                 + e->components[i].second
                                 + "' for array type",
                                 e->tok);
          break;

        case DW_TAG_structure_type:
        case DW_TAG_union_type:
          if (dwarf_hasattr(die, DW_AT_declaration))
            {
              Dwarf_Die *tmpdie = dwflpp::declaration_resolve(dwarf_diename(die));
              if (tmpdie == NULL)
                throw semantic_error ("unresolved struct "
                                      + string (dwarf_diename_integrate (die) ?: "<anonymous>"),
                                      e->tok);
              *die_mem = *tmpdie;
            }

            {
              vector<Dwarf_Attribute> locs;
              if (!find_struct_member(e->components[i].second, die, e, die, locs))
                {
                  string alternatives;
                  stringstream members;
                  print_members(die, members);
                  if (members.str().size() != 0)
                    alternatives = " (alternatives:" + members.str();
                  throw semantic_error("unable to find member '" +
                                       e->components[i].second + "' for struct "
                                       + string(dwarf_diename_integrate(die) ?: "<unknown>")
                                       + alternatives,
                                       e->tok);
                }

              for (unsigned j = 0; j < locs.size(); ++j)
                translate_location (pool, &locs[j], pc, NULL, tail, e);
            }

          ++i;
          break;

        case DW_TAG_enumeration_type:
          throw semantic_error ("field '"
                                + e->components[i].second
                                + "' vs. enum type "
                                + string(dwarf_diename_integrate (die) ?: "<anonymous type>"),
                                e->tok);
          break;
        case DW_TAG_base_type:
          throw semantic_error ("field '"
                                + e->components[i].second
                                + "' vs. base type "
                                + string(dwarf_diename_integrate (die) ?: "<anonymous type>"),
                                e->tok);
          break;
        case -1:
          throw semantic_error ("cannot find type: " + string(dwarf_errmsg (-1)),
                                e->tok);
          break;

        default:
          throw semantic_error (string(dwarf_diename_integrate (die) ?: "<anonymous type>")
                                + ": unexpected type tag "
                                + lex_cast<string>(dwarf_tag (die)),
                                e->tok);
          break;
        }

      /* Now iterate on the type in DIE's attribute.  */
      if (dwarf_attr_integrate (die, DW_AT_type, attr_mem) == NULL)
        throw semantic_error ("cannot get type of field: " + string(dwarf_errmsg (-1)), e->tok);
    }
  return die;
}


Dwarf_Die *
dwflpp::resolve_unqualified_inner_typedie (Dwarf_Die *typedie_mem,
                                           Dwarf_Attribute *attr_mem,
                                           const target_symbol *e)
{
  Dwarf_Die *typedie;
  int typetag = 0;
  while (1)
    {
      typedie = dwarf_formref_die (attr_mem, typedie_mem);
      if (typedie == NULL)
        throw semantic_error ("cannot get type: " + string(dwarf_errmsg (-1)), e->tok);
      typetag = dwarf_tag (typedie);
      if (typetag != DW_TAG_typedef &&
          typetag != DW_TAG_const_type &&
          typetag != DW_TAG_volatile_type)
        break;
      if (dwarf_attr_integrate (typedie, DW_AT_type, attr_mem) == NULL)
        throw semantic_error ("cannot get type of pointee: " + string(dwarf_errmsg (-1)), e->tok);
    }
  return typedie;
}


void
dwflpp::translate_final_fetch_or_store (struct obstack *pool,
                                        struct location **tail,
                                        Dwarf_Addr module_bias,
                                        Dwarf_Die *die,
                                        Dwarf_Attribute *attr_mem,
                                        bool lvalue,
                                        const target_symbol *e,
                                        string &,
                                        string &,
                                        exp_type & ty)
{
  /* First boil away any qualifiers associated with the type DIE of
     the final location to be accessed.  */

  Dwarf_Die typedie_mem;
  Dwarf_Die *typedie;
  int typetag;
  char const *dname;
  string diestr;

  typedie = resolve_unqualified_inner_typedie (&typedie_mem, attr_mem, e);
  typetag = dwarf_tag (typedie);

  /* Then switch behavior depending on the type of fetch/store we
     want, and the type and pointer-ness of the final location. */

  switch (typetag)
    {
    default:
      dname = dwarf_diename(die);
      diestr = (dname != NULL) ? dname : "<unknown>";
      throw semantic_error ("unsupported type tag "
                            + lex_cast<string>(typetag)
                            + " for " + diestr, e->tok);
      break;

    case DW_TAG_structure_type:
    case DW_TAG_union_type:
      dname = dwarf_diename(die);
      diestr = (dname != NULL) ? dname : "<unknown>";
      throw semantic_error ("struct/union '" + diestr
                            + "' is being accessed instead of a member of the struct/union", e->tok);
      break;

    case DW_TAG_enumeration_type:
    case DW_TAG_base_type:

      // Reject types we can't handle in systemtap
      {
        dname = dwarf_diename(die);
        diestr = (dname != NULL) ? dname : "<unknown>";

        Dwarf_Attribute encoding_attr;
        Dwarf_Word encoding = (Dwarf_Word) -1;
        dwarf_formudata (dwarf_attr_integrate (typedie, DW_AT_encoding, &encoding_attr),
                         & encoding);
        if (encoding < 0)
          {
            // clog << "bad type1 " << encoding << " diestr" << endl;
            throw semantic_error ("unsupported type (mystery encoding " + lex_cast<string>(encoding) + ")" +
                                  " for " + diestr, e->tok);
          }

        if (encoding == DW_ATE_float
            || encoding == DW_ATE_complex_float
            /* XXX || many others? */)
          {
            // clog << "bad type " << encoding << " diestr" << endl;
            throw semantic_error ("unsupported type (encoding " + lex_cast<string>(encoding) + ")" +
                                  " for " + diestr, e->tok);
          }
      }

      ty = pe_long;
      if (lvalue)
        c_translate_store (pool, 1, 0 /* PR9768 */, die, typedie, tail,
                           "THIS->value");
      else
        c_translate_fetch (pool, 1, 0 /* PR9768 */, die, typedie, tail,
                           "THIS->__retvalue");
      break;

    case DW_TAG_array_type:
    case DW_TAG_pointer_type:

        {
        Dwarf_Die pointee_typedie_mem;
        Dwarf_Die *pointee_typedie;
        Dwarf_Word pointee_encoding;
        Dwarf_Word pointee_byte_size = 0;

        pointee_typedie = resolve_unqualified_inner_typedie (&pointee_typedie_mem, attr_mem, e);

        if (dwarf_attr_integrate (pointee_typedie, DW_AT_byte_size, attr_mem))
          dwarf_formudata (attr_mem, &pointee_byte_size);

        dwarf_formudata (dwarf_attr_integrate (pointee_typedie, DW_AT_encoding, attr_mem),
                         &pointee_encoding);

        if (lvalue)
          {
            ty = pe_long;
            if (typetag == DW_TAG_array_type)
              throw semantic_error ("cannot write to array address", e->tok);
            assert (typetag == DW_TAG_pointer_type);
            c_translate_pointer_store (pool, 1, 0 /* PR9768 */, typedie, tail,
                                       "THIS->value");
          }
        else
          {
            // We have the pointer: cast it to an integral type via &(*(...))

            // NB: per bug #1187, at one point char*-like types were
            // automagically converted here to systemtap string values.
            // For several reasons, this was taken back out, leaving
            // pointer-to-string "conversion" (copying) to tapset functions.

            ty = pe_long;
            if (typetag == DW_TAG_array_type)
              c_translate_array (pool, 1, 0 /* PR9768 */, typedie, tail, NULL, 0);
            else
              c_translate_pointer (pool, 1, 0 /* PR9768 */, typedie, tail);
            c_translate_addressof (pool, 1, 0 /* PR9768 */, NULL, pointee_typedie, tail,
                                   "THIS->__retvalue");
          }
        }
      break;
    }
}


string
dwflpp::express_as_string (string prelude,
                           string postlude,
                           struct location *head)
{
  size_t bufsz = 1024;
  char *buf = static_cast<char*>(malloc(bufsz));
  assert(buf);

  FILE *memstream = open_memstream (&buf, &bufsz);
  assert(memstream);

  fprintf(memstream, "{\n");
  fprintf(memstream, "%s", prelude.c_str());
  bool deref = c_emit_location (memstream, head, 1);
  fprintf(memstream, "%s", postlude.c_str());
  fprintf(memstream, "  goto out;\n");

  // dummy use of deref_fault label, to disable warning if deref() not used
  fprintf(memstream, "if (0) goto deref_fault;\n");

  // XXX: deref flag not reliable; emit fault label unconditionally
  (void) deref;
  fprintf(memstream,
          "deref_fault:\n"
          "  goto out;\n");
  fprintf(memstream, "}\n");

  fclose (memstream);
  string result(buf);
  free (buf);
  return result;
}


string
dwflpp::literal_stmt_for_local (Dwarf_Die *scope_die,
                                Dwarf_Addr pc,
                                string const & local,
                                const target_symbol *e,
                                bool lvalue,
                                exp_type & ty)
{
  Dwarf_Die vardie;
  Dwarf_Attribute fb_attr_mem, *fb_attr = NULL;

  fb_attr = find_variable_and_frame_base (scope_die, pc, local, e,
                                          &vardie, &fb_attr_mem);

  if (sess.verbose>2)
    clog << "finding location for local '" << local
         << "' near address 0x" << hex << pc
         << ", module bias 0x" << module_bias << dec
         << "\n";

  Dwarf_Attribute attr_mem;
  if (dwarf_attr_integrate (&vardie, DW_AT_location, &attr_mem) == NULL)
    {
      throw semantic_error("failed to retrieve location "
                           "attribute for local '" + local
                           + "' (dieoffset: "
                           + lex_cast_hex<string>(dwarf_dieoffset (&vardie))
                           + ")",
                           e->tok);
    }

#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

  struct obstack pool;
  obstack_init (&pool);
  struct location *tail = NULL;

  /* Given $foo->bar->baz[NN], translate the location of foo. */

  struct location *head = translate_location (&pool,
                                              &attr_mem, pc, fb_attr, &tail,
                                              e);

  if (dwarf_attr_integrate (&vardie, DW_AT_type, &attr_mem) == NULL)
    throw semantic_error("failed to retrieve type "
                         "attribute for local '" + local + "'",
                         e->tok);

  /* Translate the ->bar->baz[NN] parts. */

  Dwarf_Die die_mem, *die = dwarf_formref_die (&attr_mem, &die_mem);
  die = translate_components (&pool, &tail, pc, e,
                              die, &die_mem, &attr_mem);

  /* Translate the assignment part, either
     x = $foo->bar->baz[NN]
     or
     $foo->bar->baz[NN] = x
  */

  string prelude, postlude;
  translate_final_fetch_or_store (&pool, &tail, module_bias,
                                  die, &attr_mem, lvalue, e,
                                  prelude, postlude, ty);

  /* Write the translation to a string. */
  return express_as_string(prelude, postlude, head);
}


string
dwflpp::literal_stmt_for_return (Dwarf_Die *scope_die,
                                 Dwarf_Addr pc,
                                 const target_symbol *e,
                                 bool lvalue,
                                 exp_type & ty)
{
  if (sess.verbose>2)
      clog << "literal_stmt_for_return: finding return value for "
           << (dwarf_diename(scope_die) ?: "<unknown>")
           << "("
           << (dwarf_diename(cu) ?: "<unknown>")
           << ")\n";

  struct obstack pool;
  obstack_init (&pool);
  struct location *tail = NULL;

  /* Given $return->bar->baz[NN], translate the location of return. */
  const Dwarf_Op *locops;
  int nlocops = dwfl_module_return_value_location (module, scope_die,
                                                   &locops);
  if (nlocops < 0)
    {
      throw semantic_error("failed to retrieve return value location"
                           " for "
                           + string(dwarf_diename(scope_die) ?: "<unknown>")
                           + "(" + string(dwarf_diename(cu) ?: "<unknown>")
                           + ")",
                           e->tok);
    }
  // the function has no return value (e.g. "void" in C)
  else if (nlocops == 0)
    {
      throw semantic_error("function "
                           + string(dwarf_diename(scope_die) ?: "<unknown>")
                           + "(" + string(dwarf_diename(cu) ?: "<unknown>")
                           + ") has no return value",
                           e->tok);
    }

  struct location  *head = c_translate_location (&pool, &loc2c_error, this,
                                                 &loc2c_emit_address,
                                                 1, 0 /* PR9768 */,
                                                 pc, locops, nlocops,
                                                 &tail, NULL);

  /* Translate the ->bar->baz[NN] parts. */

  Dwarf_Attribute attr_mem;
  if (dwarf_attr_integrate (scope_die, DW_AT_type, &attr_mem) == NULL)
    throw semantic_error("failed to retrieve return value type attribute for "
                         + string(dwarf_diename(scope_die) ?: "<unknown>")
                         + "(" + string(dwarf_diename(cu) ?: "<unknown>")
                         + ")",
                         e->tok);

  Dwarf_Die die_mem, *die = dwarf_formref_die (&attr_mem, &die_mem);
  die = translate_components (&pool, &tail, pc, e,
                              die, &die_mem, &attr_mem);

  /* Translate the assignment part, either
     x = $return->bar->baz[NN]
     or
     $return->bar->baz[NN] = x
  */

  string prelude, postlude;
  translate_final_fetch_or_store (&pool, &tail, module_bias,
                                  die, &attr_mem, lvalue, e,
                                  prelude, postlude, ty);

  /* Write the translation to a string. */
  return express_as_string(prelude, postlude, head);
}


string
dwflpp::literal_stmt_for_pointer (Dwarf_Die *type_die,
                                  const target_symbol *e,
                                  bool lvalue,
                                  exp_type & ty)
{
  if (sess.verbose>2)
      clog << "literal_stmt_for_pointer: finding value for "
           << (dwarf_diename(type_die) ?: "<unknown>")
           << "("
           << (dwarf_diename(cu) ?: "<unknown>")
           << ")\n";

  struct obstack pool;
  obstack_init (&pool);
  struct location *head = c_translate_argument (&pool, &loc2c_error, this,
                                                &loc2c_emit_address,
                                                1, "THIS->pointer");
  struct location *tail = head;

  /* Translate the ->bar->baz[NN] parts. */

  Dwarf_Attribute attr_mem;
  Dwarf_Die die_mem, *die = NULL;
  die = translate_components (&pool, &tail, 0, e,
                              type_die, &die_mem, &attr_mem);

  /* Translate the assignment part, either
     x = (THIS->pointer)->bar->baz[NN]
     or
     (THIS->pointer)->bar->baz[NN] = x
  */

  string prelude, postlude;
  translate_final_fetch_or_store (&pool, &tail, module_bias,
                                  die, &attr_mem, lvalue, e,
                                  prelude, postlude, ty);

  /* Write the translation to a string. */
  return express_as_string(prelude, postlude, head);
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
