// C++ interface to dwfl
// Copyright (C) 2005-2012 Red Hat Inc.
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
#include "rpm_finder.h"
#include "setupdwfl.h"

#include <cstdlib>
#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
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

// Older glibc elf.h don't know about this new constant.
#ifndef STB_GNU_UNIQUE
#define STB_GNU_UNIQUE  10
#endif


// debug flag to compare to the uncached version from libdw
// #define DEBUG_DWFLPP_GETSCOPES 1


using namespace std;
using namespace __gnu_cxx;


static string TOK_KERNEL("kernel");


dwflpp::dwflpp(systemtap_session & session, const string& name, bool kernel_p):
  sess(session), module(NULL), module_bias(0), mod_info(NULL),
  module_start(0), module_end(0), cu(NULL),
  module_dwarf(NULL), function(NULL), blacklist_func(), blacklist_func_ret(),
  blacklist_file(),  blacklist_enabled(false)
{
  if (kernel_p)
    setup_kernel(name, session);
  else
    {
      vector<string> modules;
      modules.push_back(name);
      setup_user(modules);
    }
}

dwflpp::dwflpp(systemtap_session & session, const vector<string>& names,
	       bool kernel_p):
  sess(session), module(NULL), module_bias(0), mod_info(NULL),
  module_start(0), module_end(0), cu(NULL),
  module_dwarf(NULL), function(NULL), blacklist_enabled(false)
{
  if (kernel_p)
    setup_kernel(names);
  else
    setup_user(names);
}

dwflpp::~dwflpp()
{
  delete_map(module_cu_cache);
  delete_map(cu_function_cache);
  delete_map(mod_function_cache);
  delete_map(cu_inl_function_cache);
  delete_map(global_alias_cache);
  delete_map(cu_die_parent_cache);

  dwfl_ptr.reset();
  // NB: don't "delete mod_info;", as that may be shared
  // between dwflpp instances, and are stored in
  // session.module_cache[] anyway.
}


module_cache::~module_cache ()
{
  delete_map(cache);
}


void
dwflpp::get_module_dwarf(bool required, bool report)
{
  module_dwarf = dwfl_module_getdwarf(module, &module_bias);
  mod_info->dwarf_status = (module_dwarf ? info_present : info_absent);
  if (!module_dwarf && report)
    {
      string msg = _("cannot find ");
      if (module_name == "")
        msg += "kernel";
      else
        msg += string("module ") + module_name;
      msg += " debuginfo";

      int i = dwfl_errno();
      if (i)
        msg += string(": ") + dwfl_errmsg (i);

      /* add module_name to list to find rpm */
      find_debug_rpms(sess, module_name.c_str());

      if (required)
        throw semantic_error (msg);
      else
        sess.print_warning(msg);
    }
}


void
dwflpp::focus_on_module(Dwfl_Module * m, module_info * mi)
{
  module = m;
  mod_info = mi;
  if (m)
    {
      module_name = dwfl_module_info(module, NULL, &module_start, &module_end,
                                     NULL, NULL, NULL, NULL) ?: "module";
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

  // Reset existing pointers and names
  function_name.clear();
  function = NULL;
}


string
dwflpp::cu_name(void)
{
  return dwarf_diename(cu) ?: "<unknown source>";
}


void
dwflpp::focus_on_function(Dwarf_Die * f)
{
  assert(f);
  assert(module);
  assert(cu);

  function = f;
  function_name = dwarf_diename(function) ?: "function";
}


/* Return the Dwarf_Die for the given address in the current module.
 * The address should be in the module address address space (this
 * function will take care of any dw bias).
 */
Dwarf_Die *
dwflpp::query_cu_containing_address(Dwarf_Addr a)
{
  Dwarf_Addr bias;
  assert(dwfl_ptr.get()->dwfl);
  assert(module);
  get_module_dwarf();

  Dwarf_Die* cudie = dwfl_module_addrdie(module, a, &bias);
  assert(bias == module_bias);
  return cudie;
}


bool
dwflpp::module_name_matches(const string& pattern)
{
  bool t = (fnmatch(pattern.c_str(), module_name.c_str(), 0) == 0);
  if (t && sess.verbose>3)
    clog << _F("pattern '%s' matches module '%s'\n",
               pattern.c_str(), module_name.c_str());
  return t;
}


bool
dwflpp::name_has_wildcard (const string& pattern)
{
  return (pattern.find('*') != string::npos ||
          pattern.find('?') != string::npos ||
          pattern.find('[') != string::npos);
}


bool
dwflpp::module_name_final_match(const string& pattern)
{
  // Assume module_name_matches().  Can there be any more matches?
  // Not unless the pattern is a wildcard, since module names are
  // presumed unique.
  return !name_has_wildcard(pattern);
}


bool
dwflpp::function_name_matches_pattern(const string& name, const string& pattern)
{
  bool t = (fnmatch(pattern.c_str(), name.c_str(), 0) == 0);
  if (t && sess.verbose>3)
    clog << _F("pattern '%s' matches function '%s'\n", pattern.c_str(), name.c_str());
  return t;
}


bool
dwflpp::function_name_matches(const string& pattern)
{
  assert(function);
  return function_name_matches_pattern(function_name, pattern);
}


bool
dwflpp::function_scope_matches(const vector<string>& scopes)
{
  // walk up the containing scopes
  Dwarf_Die* die = function;
  for (int i = scopes.size() - 1; i >= 0; --i)
    {
      die = get_parent_scope(die);

      // check if this scope matches, and prepend it if so
      // NB: a NULL die is the global scope, compared as ""
      string name = dwarf_diename(die) ?: "";
      if (name_has_wildcard(scopes[i]) ?
          function_name_matches_pattern(name, scopes[i]) :
          name == scopes[i])
        function_name = name + "::" + function_name;
      else
        return false;

      // make sure there's no more if we're at the global scope
      if (!die && i > 0)
        return false;
    }
  return true;
}


void
dwflpp::setup_kernel(const string& name, systemtap_session & s, bool debuginfo_needed)
{
  if (! sess.module_cache)
    sess.module_cache = new module_cache ();

  unsigned offline_search_matches = 0;
  dwfl_ptr = setup_dwfl_kernel(name, &offline_search_matches, sess);

  if (offline_search_matches < 1)
    {
      if (debuginfo_needed) {
        // Suggest a likely kernel dir to find debuginfo rpm for
        string dir = string(sess.sysroot + "/lib/modules/" + sess.kernel_release );
        find_debug_rpms(sess, dir.c_str());
      }
      throw semantic_error (_F("missing %s kernel/module debuginfo under '%s'",
                                sess.architecture.c_str(), sess.kernel_build_tree.c_str()));
    }
  Dwfl *dwfl = dwfl_ptr.get()->dwfl;
  if (dwfl != NULL)
    {
      ptrdiff_t off = 0;
      do
        {
          assert_no_interrupts();
          off = dwfl_getmodules (dwfl, &add_module_build_id_to_hash, &s, off);
        }
      while (off > 0);
      dwfl_assert("dwfl_getmodules", off == 0);
    }

  build_blacklist();
}

void
dwflpp::setup_kernel(const vector<string> &names, bool debuginfo_needed)
{
  if (! sess.module_cache)
    sess.module_cache = new module_cache ();

  unsigned offline_search_matches = 0;
  set<string> offline_search_names(names.begin(), names.end());
  dwfl_ptr = setup_dwfl_kernel(offline_search_names,
			       &offline_search_matches,
			       sess);

  if (offline_search_matches < offline_search_names.size())
    {
      if (debuginfo_needed) {
        // Suggest a likely kernel dir to find debuginfo rpm for
        string dir = string(sess.sysroot + "/lib/modules/" + sess.kernel_release );
        find_debug_rpms(sess, dir.c_str());
      }
      throw semantic_error (_F("missing %s kernel/module debuginfo under '%s'",
                               sess.architecture.c_str(), sess.kernel_build_tree.c_str()));
    }

  build_blacklist();
}


void
dwflpp::setup_user(const vector<string>& modules, bool debuginfo_needed)
{
  if (! sess.module_cache)
    sess.module_cache = new module_cache ();

  vector<string>::const_iterator it = modules.begin();
  dwfl_ptr = setup_dwfl_user(it, modules.end(), debuginfo_needed, sess);
  if (debuginfo_needed && it != modules.end())
    dwfl_assert (string(_F("missing process %s %s debuginfo",
                           (*it).c_str(), sess.architecture.c_str())),
                           dwfl_ptr.get()->dwfl);
}

void
dwflpp::iterate_over_modules(int (* callback)(Dwfl_Module *, void **,
                                              const char *, Dwarf_Addr,
                                              void *),
                             void *data)
{
  dwfl_getmodules (dwfl_ptr.get()->dwfl, callback, data, 0);

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
                          void * data, bool want_types)
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
          assert_no_interrupts();
          Dwarf_Die die_mem;
          Dwarf_Die *die;
          die = dwarf_offdie (dw, off + cuhl, &die_mem);
          v->push_back (*die); /* copy */
          off = noff;
        }
    }

  if (want_types && module_tus_read.find(dw) == module_tus_read.end())
    {
      // Process type units.
      Dwarf_Off off = 0;
      size_t cuhl;
      Dwarf_Off noff;
      uint64_t type_signature;
      while (dwarf_next_unit (dw, off, &noff, &cuhl, NULL, NULL, NULL, NULL,
			      &type_signature, NULL) == 0)
	{
          assert_no_interrupts();
          Dwarf_Die die_mem;
          Dwarf_Die *die;
          die = dwarf_offdie_types (dw, off + cuhl, &die_mem);
          v->push_back (*die); /* copy */
          off = noff;
	}
      module_tus_read.insert(dw);
    }

  for (vector<Dwarf_Die>::iterator i = v->begin(); i != v->end(); ++i)
    {
      int rc = (*callback)(&*i, data);
      assert_no_interrupts();
      if (rc != DWARF_CB_OK)
        break;
    }
}


bool
dwflpp::func_is_inline()
{
  assert (function);
  return dwarf_func_inline (function) != 0;
}


bool
dwflpp::func_is_exported()
{
  const char *name = dwarf_linkage_name (function) ?: dwarf_diename (function);

  assert (function);

  int syms = dwfl_module_getsymtab (module);
  dwfl_assert (_("Getting symbols"), syms >= 0);

  for (int i = 0; i < syms; i++)
    {
      GElf_Sym sym;
      GElf_Word shndxp;
      const char *symname = dwfl_module_getsym(module, i, &sym, &shndxp);
      if (symname
	  && strcmp (name, symname) == 0)
	{
	  if (GELF_ST_TYPE(sym.st_info) == STT_FUNC
	      && (GELF_ST_BIND(sym.st_info) == STB_GLOBAL
		  || GELF_ST_BIND(sym.st_info) == STB_WEAK
		  || GELF_ST_BIND(sym.st_info) == STB_GNU_UNIQUE))
	    return true;
	  else
	    return false;
	}
    }
  return false;
}

void
dwflpp::cache_inline_instances (Dwarf_Die* die)
{
  // If this is an inline instance, link it back to its origin
  Dwarf_Die origin;
  if (dwarf_tag(die) == DW_TAG_inlined_subroutine &&
      dwarf_attr_die(die, DW_AT_abstract_origin, &origin))
    {
      vector<Dwarf_Die>*& v = cu_inl_function_cache[origin.addr];
      if (!v)
        v = new vector<Dwarf_Die>;
      v->push_back(*die);
    }

  // Recurse through other scopes that may contain inlines
  Dwarf_Die child, import;
  if (dwarf_child(die, &child) == 0)
    do
      {
        switch (dwarf_tag (&child))
          {
          // tags that could contain inlines
          case DW_TAG_compile_unit:
          case DW_TAG_module:
          case DW_TAG_lexical_block:
          case DW_TAG_with_stmt:
          case DW_TAG_catch_block:
          case DW_TAG_try_block:
          case DW_TAG_entry_point:
          case DW_TAG_inlined_subroutine:
          case DW_TAG_subprogram:
            cache_inline_instances(&child);
            break;

          // imported dies should be followed
          case DW_TAG_imported_unit:
            if (dwarf_attr_die(&child, DW_AT_import, &import))
              cache_inline_instances(&import);
            break;

          // nothing to do for other tags
          default:
            break;
          }
      }
    while (dwarf_siblingof(&child, &child) == 0);
}


void
dwflpp::iterate_over_inline_instances (int (* callback)(Dwarf_Die * die, void * arg),
                                       void * data)
{
  assert (function);
  assert (func_is_inline ());

  if (cu_inl_function_cache_done.insert(cu->addr).second)
    cache_inline_instances(cu);

  vector<Dwarf_Die>* v = cu_inl_function_cache[function->addr];
  if (!v)
    return;

  for (vector<Dwarf_Die>::iterator i = v->begin(); i != v->end(); ++i)
    {
      int rc = (*callback)(&*i, data);
      assert_no_interrupts();
      if (rc != DWARF_CB_OK)
        break;
    }
}


void
dwflpp::cache_die_parents(cu_die_parent_cache_t* parents, Dwarf_Die* die)
{
  // Record and recurse through DIEs we care about
  Dwarf_Die child, import;
  if (dwarf_child(die, &child) == 0)
    do
      {
        switch (dwarf_tag (&child))
          {
          // normal tags to recurse
          case DW_TAG_compile_unit:
          case DW_TAG_module:
          case DW_TAG_lexical_block:
          case DW_TAG_with_stmt:
          case DW_TAG_catch_block:
          case DW_TAG_try_block:
          case DW_TAG_entry_point:
          case DW_TAG_inlined_subroutine:
          case DW_TAG_subprogram:
          case DW_TAG_namespace:
          case DW_TAG_class_type:
          case DW_TAG_structure_type:
            parents->insert(make_pair(child.addr, *die));
            cache_die_parents(parents, &child);
            break;

          // record only, nothing to recurse
          case DW_TAG_label:
            parents->insert(make_pair(child.addr, *die));
            break;

          // imported dies should be followed
          case DW_TAG_imported_unit:
            if (dwarf_attr_die(&child, DW_AT_import, &import))
              {
                parents->insert(make_pair(import.addr, *die));
                cache_die_parents(parents, &import);
              }
            break;

          // nothing to do for other tags
          default:
            break;
          }
      }
    while (dwarf_siblingof(&child, &child) == 0);
}


cu_die_parent_cache_t*
dwflpp::get_die_parents()
{
  assert (cu);

  cu_die_parent_cache_t *& parents = cu_die_parent_cache[cu->addr];
  if (!parents)
    {
      parents = new cu_die_parent_cache_t;
      cache_die_parents(parents, cu);
      if (sess.verbose > 4)
        clog << _F("die parent cache %s:%s size %zu", module_name.c_str(),
                   cu_name().c_str(), parents->size()) << endl;
    }
  return parents;
}


vector<Dwarf_Die>
dwflpp::getscopes_die(Dwarf_Die* die)
{
  cu_die_parent_cache_t *parents = get_die_parents();

  vector<Dwarf_Die> scopes;
  Dwarf_Die *scope = die;
  cu_die_parent_cache_t::iterator it;
  do
    {
      scopes.push_back(*scope);
      it = parents->find(scope->addr);
      scope = &it->second;
    }
  while (it != parents->end());

#ifdef DEBUG_DWFLPP_GETSCOPES
  Dwarf_Die *dscopes = NULL;
  int nscopes = dwarf_getscopes_die(die, &dscopes);

  assert(nscopes == (int)scopes.size());
  for (unsigned i = 0; i < scopes.size(); ++i)
    assert(scopes[i].addr == dscopes[i].addr);
  free(dscopes);
#endif

  return scopes;
}


std::vector<Dwarf_Die>
dwflpp::getscopes(Dwarf_Die* die)
{
  cu_die_parent_cache_t *parents = get_die_parents();

  vector<Dwarf_Die> scopes;

  Dwarf_Die origin;
  Dwarf_Die *scope = die;
  cu_die_parent_cache_t::iterator it;
  do
    {
      scopes.push_back(*scope);
      if (dwarf_tag(scope) == DW_TAG_inlined_subroutine &&
          dwarf_attr_die(scope, DW_AT_abstract_origin, &origin))
        scope = &origin;

      it = parents->find(scope->addr);
      scope = &it->second;
    }
  while (it != parents->end());

#ifdef DEBUG_DWFLPP_GETSCOPES
  // there isn't an exact libdw equivalent, but if dwarf_getscopes on the
  // entrypc returns the same first die, then all the scopes should match
  Dwarf_Addr pc;
  if (die_entrypc(die, &pc))
    {
      Dwarf_Die *dscopes = NULL;
      int nscopes = dwarf_getscopes(cu, pc, &dscopes);
      if (nscopes > 0 && dscopes[0].addr == die->addr)
        {
          assert(nscopes == (int)scopes.size());
          for (unsigned i = 0; i < scopes.size(); ++i)
            assert(scopes[i].addr == dscopes[i].addr);
        }
      free(dscopes);
    }
#endif

  return scopes;
}


std::vector<Dwarf_Die>
dwflpp::getscopes(Dwarf_Addr pc)
{
  // The die_parent_cache doesn't help us without knowing where the pc is
  // contained, so we have to do this one the old fashioned way.

  assert (cu);

  vector<Dwarf_Die> scopes;

  Dwarf_Die* dwarf_scopes;
  int nscopes = dwarf_getscopes(cu, pc, &dwarf_scopes);
  if (nscopes > 0)
    {
      scopes.assign(dwarf_scopes, dwarf_scopes + nscopes);
      free(dwarf_scopes);
    }

#ifdef DEBUG_DWFLPP_GETSCOPES
  // check that getscopes on the starting die gets the same result
  if (!scopes.empty())
    {
      vector<Dwarf_Die> other = getscopes(&scopes[0]);
      assert(scopes.size() == other.size());
      for (unsigned i = 0; i < scopes.size(); ++i)
        assert(scopes[i].addr == other[i].addr);
    }
#endif

  return scopes;
}


Dwarf_Die*
dwflpp::get_parent_scope(Dwarf_Die* die)
{
  Dwarf_Die specification;
  if (dwarf_attr_die(die, DW_AT_specification, &specification))
    die = &specification;

  cu_die_parent_cache_t *parents = get_die_parents();
  cu_die_parent_cache_t::iterator it = parents->find(die->addr);
  while (it != parents->end())
    {
      Dwarf_Die* scope = &it->second;
      switch (dwarf_tag (scope))
        {
        case DW_TAG_namespace:
        case DW_TAG_class_type:
        case DW_TAG_structure_type:
          return scope;

        default:
          break;
        }
      it = parents->find(scope->addr);
    }
  return NULL;
}

static const char*
cache_type_prefix(Dwarf_Die* type)
{
  switch (dwarf_tag(type))
    {
    case DW_TAG_enumeration_type:
      return "enum ";
    case DW_TAG_structure_type:
    case DW_TAG_class_type:
      // treating struct/class as equals
      return "struct ";
    case DW_TAG_union_type:
      return "union ";
    }
  return "";
}

int
dwflpp::global_alias_caching_callback(Dwarf_Die *die, bool has_inner_types,
                                      const string& prefix, void *arg)
{
  cu_type_cache_t *cache = static_cast<cu_type_cache_t*>(arg);
  const char *name = dwarf_diename(die);

  if (!name || dwarf_hasattr(die, DW_AT_declaration))
    return DWARF_CB_OK;

  int tag = dwarf_tag(die);
  if (has_inner_types && (tag == DW_TAG_namespace
                          || tag == DW_TAG_structure_type
                          || tag == DW_TAG_class_type))
    iterate_over_types(die, has_inner_types, prefix + name + "::",
                       global_alias_caching_callback, arg);

  if (tag != DW_TAG_namespace)
    {
      string type_name = prefix + cache_type_prefix(die) + name;
      if (cache->find(type_name) == cache->end())
        (*cache)[type_name] = *die;
    }

  return DWARF_CB_OK;
}

int
dwflpp::global_alias_caching_callback_cus(Dwarf_Die *die, void *arg)
{
  mod_cu_type_cache_t *global_alias_cache;
  global_alias_cache = &static_cast<dwflpp *>(arg)->global_alias_cache;

  cu_type_cache_t *v = (*global_alias_cache)[die->addr];
  if (v != 0)
    return DWARF_CB_OK;

  v = new cu_type_cache_t;
  (*global_alias_cache)[die->addr] = v;
  iterate_over_globals(die, global_alias_caching_callback, v);

  return DWARF_CB_OK;
}

Dwarf_Die *
dwflpp::declaration_resolve_other_cus(const string& name)
{
  iterate_over_cus(global_alias_caching_callback_cus, this, true);
  for (mod_cu_type_cache_t::iterator i = global_alias_cache.begin();
         i != global_alias_cache.end(); ++i)
    {
      cu_type_cache_t *v = (*i).second;
      if (v->find(name) != v->end())
        return & ((*v)[name]);
    }

  return NULL;
}

Dwarf_Die *
dwflpp::declaration_resolve(const string& name)
{
  cu_type_cache_t *v = global_alias_cache[cu->addr];
  if (v == 0) // need to build the cache, just once per encountered module/cu
    {
      v = new cu_type_cache_t;
      global_alias_cache[cu->addr] = v;
      iterate_over_globals(cu, global_alias_caching_callback, v);
      if (sess.verbose > 4)
        clog << _F("global alias cache %s:%s size %zu", module_name.c_str(),
                   cu_name().c_str(), v->size()) << endl;
    }

  // XXX: it may be desirable to search other modules' declarations
  // too, in case a module/shared-library processes a
  // forward-declared pointer type only, where the actual definition
  // may only be in vmlinux or the application.

  if (v->find(name) == v->end())
    return declaration_resolve_other_cus(name);

  return & ((*v)[name]);
}

Dwarf_Die *
dwflpp::declaration_resolve(Dwarf_Die *type)
{
  const char* name = dwarf_diename(type);
  if (!name)
    return NULL;

  string type_name = cache_type_prefix(type) + string(name);
  return declaration_resolve(type_name);
}


int
dwflpp::cu_function_caching_callback (Dwarf_Die* func, void *arg)
{
  cu_function_cache_t* v = static_cast<cu_function_cache_t*>(arg);
  const char *name = dwarf_diename(func);
  if (!name)
    return DWARF_CB_OK;

  v->insert(make_pair(string(name), *func));
  return DWARF_CB_OK;
}


int
dwflpp::mod_function_caching_callback (Dwarf_Die* cu, void *arg)
{
  dwarf_getfuncs (cu, cu_function_caching_callback, arg, 0);
  return DWARF_CB_OK;
}


int
dwflpp::iterate_over_functions (int (* callback)(Dwarf_Die * func, base_query * q),
                                base_query * q, const string& function)
{
  int rc = DWARF_CB_OK;
  assert (module);
  assert (cu);

  cu_function_cache_t *v = cu_function_cache[cu->addr];
  if (v == 0)
    {
      v = new cu_function_cache_t;
      cu_function_cache[cu->addr] = v;
      dwarf_getfuncs (cu, cu_function_caching_callback, v, 0);
      if (sess.verbose > 4)
        clog << _F("function cache %s:%s size %zu", module_name.c_str(),
                   cu_name().c_str(), v->size()) << endl;
      mod_info->update_symtab(v);
    }

  cu_function_cache_t::iterator it;
  cu_function_cache_range_t range = v->equal_range(function);
  if (range.first != range.second)
    {
      for (it = range.first; it != range.second; ++it)
        {
          Dwarf_Die& die = it->second;
          if (sess.verbose > 4)
            clog << _F("function cache %s:%s hit %s", module_name.c_str(),
                       cu_name().c_str(), function.c_str()) << endl;  
          rc = (*callback)(& die, q);
          if (rc != DWARF_CB_OK) break;
        }
    }
  else if (startswith(function, "_Z"))
    {
      // C++ names are mangled starting with a "_Z" prefix.  Most of the time
      // we can discover the mangled name from a die's MIPS_linkage_name
      // attribute, so we read that to match against the user's function
      // pattern.  Note that this isn't perfect, as not all will have that
      // attribute (notably ctors and dtors), but we do what we can...
      for (it = v->begin(); it != v->end(); ++it)
        {
          if (pending_interrupts) return DWARF_CB_ABORT;
          Dwarf_Die& die = it->second;
          const char* linkage_name = NULL;
          if ((linkage_name = dwarf_linkage_name (&die))
              && function_name_matches_pattern (linkage_name, function))
            {
              if (sess.verbose > 4)
                clog << _F("function cache %s:%s match %s vs %s", module_name.c_str(),
                           cu_name().c_str(), linkage_name, function.c_str()) << endl;

              rc = (*callback)(& die, q);
              if (rc != DWARF_CB_OK) break;
            }
        }
    }
  else if (name_has_wildcard (function))
    {
      for (it = v->begin(); it != v->end(); ++it)
        {
          if (pending_interrupts) return DWARF_CB_ABORT;
          const string& func_name = it->first;
          Dwarf_Die& die = it->second;
          if (function_name_matches_pattern (func_name, function))
            {
              if (sess.verbose > 4)
                clog << _F("function cache %s:%s match %s vs %s", module_name.c_str(),
                           cu_name().c_str(), func_name.c_str(), function.c_str()) << endl;

              rc = (*callback)(& die, q);
              if (rc != DWARF_CB_OK) break;
            }
        }
    }
  else // not a linkage name or wildcard and no match in this CU
    {
      // do nothing
    }
  return rc;
}


int
dwflpp::iterate_single_function (int (* callback)(Dwarf_Die * func, base_query * q),
                                 base_query * q, const string& function)
{
  int rc = DWARF_CB_OK;
  assert (module);

  get_module_dwarf(false);
  if (!module_dwarf)
    return rc;

  cu_function_cache_t *v = mod_function_cache[module_dwarf];
  if (v == 0)
    {
      v = new cu_function_cache_t;
      mod_function_cache[module_dwarf] = v;
      iterate_over_cus (mod_function_caching_callback, v, false);
      if (sess.verbose > 4)
        clog << _F("module function cache %s size %zu", module_name.c_str(),
                   v->size()) << endl;
      mod_info->update_symtab(v);
    }

  cu_function_cache_t::iterator it;
  cu_function_cache_range_t range = v->equal_range(function);
  if (range.first != range.second)
    {
      for (it = range.first; it != range.second; ++it)
        {
          Dwarf_Die cu_mem;
          Dwarf_Die& die = it->second;
          if (sess.verbose > 4)
            clog << _F("module function cache %s hit %s", module_name.c_str(),
                       function.c_str()) << endl;

          // since we're iterating out of cu-context, we need each focus
          focus_on_cu(dwarf_diecu(&die, &cu_mem, NULL, NULL));

          rc = (*callback)(& die, q);
          if (rc != DWARF_CB_OK) break;
        }
    }

  // undo the focus_on_cu
  this->cu = NULL;
  this->function_name.clear();
  this->function = NULL;

  return rc;
}


/* This basically only goes one level down from the compile unit so it
 * only picks up top level stuff (i.e. nothing in a lower scope) */
int
dwflpp::iterate_over_globals (Dwarf_Die *cu_die,
                              int (* callback)(Dwarf_Die *, bool,
                                               const string&, void *),
                              void * data)
{
  assert (cu_die);
  assert (dwarf_tag(cu_die) == DW_TAG_compile_unit
	  || dwarf_tag(cu_die) == DW_TAG_type_unit);

  // If this is C++, recurse for any inner types
  bool has_inner_types = dwarf_srclang(cu_die) == DW_LANG_C_plus_plus;

  return iterate_over_types(cu_die, has_inner_types, "", callback, data);
}


int
dwflpp::iterate_over_types (Dwarf_Die *top_die,
                            bool has_inner_types,
                            const string& prefix,
                            int (* callback)(Dwarf_Die *, bool,
                                             const string&, void *),
                            void * data)
{
  int rc = DWARF_CB_OK;
  Dwarf_Die die;

  assert (top_die);

  if (dwarf_child(top_die, &die) != 0)
    return rc;

  do
    /* We're only currently looking for named types,
     * although other types of declarations exist */
    switch (dwarf_tag(&die))
      {
      case DW_TAG_base_type:
      case DW_TAG_enumeration_type:
      case DW_TAG_structure_type:
      case DW_TAG_class_type:
      case DW_TAG_typedef:
      case DW_TAG_union_type:
      case DW_TAG_namespace:
        rc = (*callback)(&die, has_inner_types, prefix, data);
        break;
      }
  while (rc == DWARF_CB_OK && dwarf_siblingof(&die, &die) == 0);

  return rc;
}


/* For each notes section in the current module call 'callback', use
 * 'data' for the notes buffer and pass 'object' back in case
 * 'callback' is a method */

int
dwflpp::iterate_over_notes (void *object, void (*callback)(void *object, int type, const char *data, size_t len))
{
  Dwarf_Addr bias;
  // Note we really want the actual elf file, not the dwarf .debug file.
  // Older binutils had a bug where they mangled the SHT_NOTE type during
  // --keep-debug.
  Elf* elf = dwfl_module_getelf (module, &bias);
  size_t shstrndx;
  if (elf_getshdrstrndx (elf, &shstrndx))
    return elf_errno();

  Elf_Scn *scn = NULL;

  vector<Dwarf_Die> notes;

  while ((scn = elf_nextscn (elf, scn)) != NULL)
    {
      GElf_Shdr shdr;
      if (gelf_getshdr (scn, &shdr) == NULL)
	  continue;
      switch (shdr.sh_type)
	{
	case SHT_NOTE:
	  if (!(shdr.sh_flags & SHF_ALLOC))
	    {
	      Elf_Data *data = elf_getdata (scn, NULL);
	      size_t next;
	      GElf_Nhdr nhdr;
	      size_t name_off;
	      size_t desc_off;
	      for (size_t offset = 0;
		   (next = gelf_getnote (data, offset, &nhdr, &name_off, &desc_off)) > 0;
		   offset = next)
		(*callback) (object, nhdr.n_type, (const char*)((long)(data->d_buf) + (long)desc_off), nhdr.n_descsz);
	    }
	  break;
	}
    }
  return 0;
}


/* For each entry in the .dynamic section in the current module call 'callback'
 * returning 'object' in case 'callback' is a method */

void
dwflpp::iterate_over_libraries (void (*callback)(void *object, const char *arg), void *q)
{
  std::set<std::string> added;
  string interpreter;

  assert (this->module_name.length() != 0);

  Dwarf_Addr bias;
//  We cannot use this: dwarf_getelf (dwfl_module_getdwarf (module, &bias))
  Elf *elf = dwfl_module_getelf (module, &bias);
//  elf_getphdrnum (elf, &phnum) is not available in all versions of elfutils
//  needs libelf from elfutils 0.144+
  for (int i = 0; ; i++)
    {
      GElf_Phdr mem;
      GElf_Phdr *phdr;
      phdr = gelf_getphdr (elf, i, &mem);
      if (phdr == NULL)
        break;
      if (phdr->p_type == PT_INTERP)
        {
          size_t maxsize;
          char *filedata = elf_rawfile (elf, &maxsize);

          if (filedata != NULL && phdr->p_offset < maxsize)
            interpreter = (char*) (filedata + phdr->p_offset);
          break;
        }
    }

  if (interpreter.length() == 0)
    return;
  // If it gets cumbersome to maintain this whitelist, we could just check for
  // startswith("/lib/ld") || startswith("/lib64/ld"), and trust that no admin
  // would install untrustworthy loaders in those paths.
  // See also http://sourceware.org/git/?p=glibc.git;a=blob;f=shlib-versions;hb=HEAD
  if (interpreter != "/lib/ld.so.1"                     // s390, ppc
      && interpreter != "/lib/ld64.so.1"                // s390x, ppc64
      && interpreter != "/lib64/ld64.so.1"
      && interpreter != "/lib/ld-linux-ia64.so.2"       // ia64
      && interpreter != "/emul/ia32-linux/lib/ld-linux.so.2"
      && interpreter != "/lib64/ld-linux-x86-64.so.2"   // x8664
      && interpreter != "/lib/ld-linux.so.2"            // x86
      && interpreter != "/lib/ld-linux.so.3"            // arm
      && interpreter != "/lib/ld-linux-armhf.so.3"      // arm
      )
    {
      sess.print_warning (_F("module %s --ldd skipped: unsupported interpreter: %s",
                               module_name.c_str(), interpreter.c_str()));
      return;
    }

  vector<string> ldd_command;
  ldd_command.push_back("/usr/bin/env");
  ldd_command.push_back("LD_TRACE_LOADED_OBJECTS=1");
  ldd_command.push_back("LD_WARN=yes");
  ldd_command.push_back("LD_BIND_NOW=yes");
  ldd_command.push_back(interpreter);
  ldd_command.push_back(module_name);

  FILE *fp;
  int child_fd;
  pid_t child = stap_spawn_piped(sess.verbose, ldd_command, NULL, &child_fd);
  if (child <= 0 || !(fp = fdopen(child_fd, "r")))
    clog << _F("library iteration on %s failed: %s",
               module_name.c_str(), strerror(errno)) << endl;
  else
    {
      while (1) // this parsing loop borrowed from add_unwindsym_ldd
        {
          char linebuf[256];
          char *soname = 0;
          char *shlib = 0;
          unsigned long int addr = 0;

          char *line = fgets (linebuf, 256, fp);
          if (line == 0) break; // EOF or error

          // Try soname => shlib (0xaddr)
          int nf = sscanf (line, "%as => %as (0x%lx)",
              &soname, &shlib, &addr);
          if (nf != 3 || shlib[0] != '/')
            {
              // Try shlib (0xaddr)
              nf = sscanf (line, " %as (0x%lx)", &shlib, &addr);
              if (nf != 2 || shlib[0] != '/')
                continue; // fewer than expected fields, or bad shlib.
            }

          if (added.find (shlib) == added.end())
            {
              if (sess.verbose > 2)
                {
                  clog << _F("Added -d '%s", shlib);
                  if (nf == 3)
                    clog << _F("' due to '%s'", soname);
                  else
                    clog << "'";
                  clog << endl;
                }
              added.insert (shlib);
            }

          free (soname);
          free (shlib);
        }
      if ((fclose(fp) || stap_waitpid(sess.verbose, child)))
         sess.print_warning("failed to read libraries from " + module_name + ": " + strerror(errno));
    }

  for (std::set<std::string>::iterator it = added.begin();
      it != added.end();
      it++)
    {
      string modname = *it;
      (callback) (q, modname.c_str());
    }
}


/* For each plt section in the current module call 'callback', pass the plt entry
 * 'address' and 'name' back, and pass 'object' back in case 'callback' is a method */

int
dwflpp::iterate_over_plt (void *object, void (*callback)(void *object, const char *name, size_t addr))
{
  Dwarf_Addr load_addr;
  // Note we really want the actual elf file, not the dwarf .debug file.
  Elf* elf = dwfl_module_getelf (module, &load_addr);
  size_t shstrndx;
  assert (elf_getshdrstrndx (elf, &shstrndx) >= 0);

  // Get the load address
  for (int i = 0; ; i++)
    {
      GElf_Phdr mem;
      GElf_Phdr *phdr;
      phdr = gelf_getphdr (elf, i, &mem);
      if (phdr == NULL)
	break;
      if (phdr->p_type == PT_LOAD)
	{
	  load_addr = phdr->p_vaddr;
	  break;
	}
    }

  // Get the plt section header
  Elf_Scn *scn = NULL;
  GElf_Shdr *plt_shdr = NULL;
  GElf_Shdr plt_shdr_mem;
  while ((scn = elf_nextscn (elf, scn)))
    {
      plt_shdr = gelf_getshdr (scn, &plt_shdr_mem);
      assert (plt_shdr != NULL);
      if (strcmp (elf_strptr (elf, shstrndx, plt_shdr->sh_name), ".plt") == 0)
	break;
    }
	
  // Layout of the plt section
  int plt0_entry_size;
  int plt_entry_size;
  GElf_Ehdr ehdr_mem;
  GElf_Ehdr* em = gelf_getehdr (elf, &ehdr_mem);
  switch (em->e_machine)
  {
  case EM_386:    plt0_entry_size = 16; plt_entry_size = 16; break;
  case EM_X86_64: plt0_entry_size = 16; plt_entry_size = 16; break;
  case EM_PPC64:
  case EM_S390:
  case EM_PPC:
  default:
    throw semantic_error(".plt is not supported on this architecture");
  }

  scn = NULL;
  while ((scn = elf_nextscn (elf, scn)))
    {
      GElf_Shdr shdr_mem;
      GElf_Shdr *shdr = gelf_getshdr (scn, &shdr_mem);
      bool have_rela = false;
      bool have_rel = false;

      if (shdr == NULL)
        continue;
      assert (shdr != NULL);

      if ((have_rela = (strcmp (elf_strptr (elf, shstrndx, shdr->sh_name), ".rela.plt") == 0))
	  || (have_rel = (strcmp (elf_strptr (elf, shstrndx, shdr->sh_name), ".rel.plt") == 0)))
	{
	  /* Get the data of the section.  */
	  Elf_Data *data = elf_getdata (scn, NULL);
	  assert (data != NULL);
	  /* Get the symbol table information.  */
	  Elf_Scn *symscn = elf_getscn (elf, shdr->sh_link);
	  GElf_Shdr symshdr_mem;
	  GElf_Shdr *symshdr = gelf_getshdr (symscn, &symshdr_mem);
	  assert (symshdr != NULL);
	  Elf_Data *symdata = elf_getdata (symscn, NULL);
	  assert (symdata != NULL);

	  unsigned int nsyms = shdr->sh_size / shdr->sh_entsize;
	  
	  for (unsigned int cnt = 0; cnt < nsyms; ++cnt)
	    {
	      GElf_Ehdr ehdr_mem;
	      GElf_Ehdr* em = gelf_getehdr (elf, &ehdr_mem);
	      if (em == 0) { dwfl_assert ("dwfl_getehdr", dwfl_errno()); }

	      GElf_Rela relamem;
	      GElf_Rela *rela = NULL;
	      GElf_Rel relmem;
	      GElf_Rel *rel = NULL;
	      if (have_rela)
		{
		  rela = gelf_getrela (data, cnt, &relamem);
		  assert (rela != NULL);
		}
	      else if (have_rel)
		{
		  rel = gelf_getrel (data, cnt, &relmem);
		  assert (rel != NULL);
		}
	      GElf_Sym symmem;
	      Elf32_Word xndx;
	      Elf_Data *xndxdata = NULL;
	      GElf_Sym *sym =
		gelf_getsymshndx (symdata, xndxdata,
				  GELF_R_SYM (have_rela ? rela->r_info : rel->r_info),
				  &symmem, &xndx);
	      assert (sym != NULL);
	      Dwarf_Addr addr = plt_shdr->sh_offset + plt0_entry_size + cnt * plt_entry_size;

	      if (elf_strptr (elf, symshdr->sh_link, sym->st_name))
	        (*callback) (object, elf_strptr (elf, symshdr->sh_link, sym->st_name), addr + load_addr);
	    }
	  break; // while scn
	}
    }
  return 0;
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
	clog << _F("alternative line %d rejected: nsrcs=%zu", lineno, nsrcs) << endl;
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
	    clog << _F("alternative line %d accepted: fn=%s", lineno, i->name.c_str()) << endl;
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
	    clog << _F("alternative line %d accepted: ifn=%s", lineno, i->name.c_str()) << endl;
	  return true;
	}
    }

  if (sess.verbose>4)
    //TRANSLATORS:  given line number leaves (is beyond) given function.
    clog << _F("alternative line %d rejected: leaves selected fns", lineno) << endl;
  return false;
}


void
dwflpp::iterate_over_srcfile_lines (char const * srcfile,
                                    int lines[2],
                                    bool need_single_match,
                                    enum line_t line_type,
                                    void (* callback) (const dwarf_line_t& line,
                                                       void * arg),
                                    const std::string& func_pattern,
                                    void *data)
{
  Dwarf_Line **srcsp = NULL;
  size_t nsrcs = 0;
  dwarf_query * q = static_cast<dwarf_query *>(data);
  int lineno = lines[0];
  auto_free_ref<Dwarf_Line**> free_srcsp(srcsp);

  get_module_dwarf();
  if (!this->function)
    return;

  if (line_type == RELATIVE)
    {
      Dwarf_Addr addr;
      Dwarf_Line *line;
      int line_number;

      die_entrypc(this->function, &addr);

      if (addr != 0)
        {
          line = dwarf_getsrc_die (this->cu, addr);
          dwarf_assert ("dwarf_getsrc_die", line == NULL);
          dwarf_assert ("dwarf_lineno", dwarf_lineno (line, &line_number));
        }
      else if (dwarf_decl_line (this->function, &line_number) != 0)
        {
          // use DW_AT_decl_line as a fallback method
          Dwarf_Attribute type_attr;
          Dwarf_Word constant;
          if (dwarf_attr_integrate (this->function, DW_AT_decl_line, &type_attr))
            {
              dwarf_formudata (&type_attr, &constant);
              line_number = constant;
            }
          else
            return;
        }
      lineno += line_number;
    }
  else if (line_type == WILDCARD)
    function_line (&lineno);
  else if (line_type == RANGE) { /* correct lineno */
      int start_lineno;

      if (name_has_wildcard(func_pattern)) /* PR10294: wider range like statement("*@foo.c") */
         start_lineno = lineno;
      else
         function_line (&start_lineno);
      lineno = lineno < start_lineno ? start_lineno : lineno;
      if (lineno > lines[1]) { /* invalid line range */
        stringstream advice;
        advice << _("Invalid line range (") << lines[0] << "-" << lines[1] << ")";
        if (start_lineno > lines[1])
          advice << _(", the end line number ") << lines[1] << " < " << start_lineno;
        throw semantic_error (advice.str());
       }
  }


  for (int l = lineno; ; l = l + 1)
    {
      set<int> lines_probed;
      pair<set<int>::iterator,bool> line_probed;
      int ret = 0;

      assert_no_interrupts();

      nsrcs = 0;
      ret = dwarf_getsrc_file (module_dwarf, srcfile, l, 0,
					 &srcsp, &nsrcs);
      if (ret != 0) /* tolerate invalid line number */
        break;

      if (line_type == WILDCARD || line_type == RANGE)
        {
          Dwarf_Addr line_addr;

          dwarf_lineno (srcsp [0], &lineno);
	  /* Maybe lineno will exceed the input end */
	  if (line_type == RANGE && lineno > lines[1])
 	     break;
          line_probed = lines_probed.insert(lineno);
          if (lineno != l || line_probed.second == false || nsrcs > 1)
            continue;
          dwarf_lineaddr (srcsp [0], &line_addr);
          if (!function_name_matches(func_pattern) && dwarf_haspc (function, line_addr) != 1)
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
          advice << _F("multiple addresses for %s:%d", srcfile, lineno);
          if (lo_try > 0 || hi_try > 0)
            {
              //TRANSLATORS: Here we are trying to advise what source file 
              //TRANSLATORS: to attempt.
              advice << _(" (try ");
              if (lo_try > 0)
                advice << srcfile << ":" << lo_try;
              if (lo_try > 0 && hi_try > 0)
                advice << _(" or ");
              if (hi_try > 0)
                advice << srcfile << ":" << hi_try;
              advice << ")";
            }
          throw semantic_error (advice.str());
        }

      for (size_t i = 0; i < nsrcs; ++i)
        {
          assert_no_interrupts();
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
                             const string& sym,
                             const string& function,
                             dwarf_query *q,
                             void (* callback)(const string &,
                                               const char *,
                                               const char *,
                                               int,
                                               Dwarf_Die *,
                                               Dwarf_Addr,
                                               dwarf_query *))
{
  get_module_dwarf();

  Dwarf_Die die;
  const char *name;
  int res = dwarf_child (begin_die, &die);
  if (res != 0)
    return;  // die without children, bail out.

  do
    {
      switch (dwarf_tag(&die))
        {
        case DW_TAG_label:
          name = dwarf_diename (&die);
          if (name &&
              (name == sym
               || (name_has_wildcard(sym)
                   && function_name_matches_pattern (name, sym))))
            {
              // Don't try to be smart. Just drop no addr labels.
              Dwarf_Addr stmt_addr;
              if (dwarf_lowpc (&die, &stmt_addr) == 0)
                {
                  // Get the file/line number for this label
                  int dline;
                  const char *file = dwarf_decl_file (&die);
                  dwarf_decl_line (&die, &dline);

                  vector<Dwarf_Die> scopes = getscopes_die(&die);
                  if (scopes.size() > 1)
                    {
                      Dwarf_Die scope;
                      if (!inner_die_containing_pc(scopes[1], stmt_addr, scope))
                        {
                          sess.print_warning(_F("label '%s' at address %s (dieoffset: %s) is not "
                                                "contained by its scope '%s' (dieoffset: %s) -- bad"
                                                " debuginfo?", name, lex_cast_hex(stmt_addr).c_str(),
                                                lex_cast_hex(dwarf_dieoffset(&die)).c_str(),
                                                (dwarf_diename(&scope) ?: "<unknown>"),
                                                lex_cast_hex(dwarf_dieoffset(&scope)).c_str()));
                        }
                      callback(function, name, file, dline,
                               &scope, stmt_addr, q);
                    }
                }
            }
          break;

        case DW_TAG_subprogram:
        case DW_TAG_inlined_subroutine:
          // Stay within our filtered function
          break;

        default:
          if (dwarf_haschildren (&die))
            iterate_over_labels (&die, sym, function, q, callback);
          break;
        }
    }
  while (dwarf_siblingof (&die, &die) == 0);
}


void
dwflpp::collect_srcfiles_matching (string const & pattern,
                                   set<string> & filtered_srcfiles)
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
            clog << _F("selected source file '%s'\n", fname);
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
            clog << _F("missing entrypc dwarf line record for function '%s'\n",
                       it->name.c_str());
          // This is probably an inlined function.  We'll end up using
          // its lowpc as a probe address.
          continue;
        }

      if (entrypc == 0)
        { 
          if (sess.verbose > 2)
            clog << _F("null entrypc dwarf line record for function '%s'\n",
                       it->name.c_str());
          // This is probably an inlined function.  We'll skip this instance;
          // it is messed up. 
          continue;
        }

      if (sess.verbose>2)
        clog << _F("searching for prologue of function '%s' %#" PRIx64 "-%#" PRIx64 
                   "@%s:%d\n", it->name.c_str(), entrypc, highpc, it->decl_file,
                   it->decl_line);

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
            clog << _F("checking line record %#" PRIx64 "@%s:%d\n", postprologue_addr,
                       postprologue_file, postprologue_lineno);

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
                  clog << _F("prologue found function '%s'", it->name.c_str());
                  // Add a little classification datum
                  //TRANSLATORS: Here we're adding some classification datum (ie Prologue Free)
                  if (postprologue_srcline_idx == entrypc_srcline_idx) clog << _(" (naked)");
                  //TRANSLATORS: Here we're adding some classification datum (ie Prologue Free)
                  if (ranoff_end) clog << _(" (tail-call?)");
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
            lookup_method += _F(", ignored %s more", lex_cast(extra).c_str());
        }
    }

  // PR10574: reject subprograms where the entrypc address turns out
  // to be 0, since they tend to correspond to duplicate-eliminated
  // COMDAT copies of C++ functions.
  if (rc == 0 && *addr == 0)
    {
      lookup_method += _(" (skip comdat)");
      rc = 1;
    }

  if (sess.verbose > 2)
    clog << _F("entry-pc lookup (%s dieoffset: %s) = %#" PRIx64 " (rc %d)", lookup_method.c_str(), 
               lex_cast_hex(dwarf_dieoffset(die)).c_str(), *addr, rc) << endl;

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


bool
dwflpp::inner_die_containing_pc(Dwarf_Die& scope, Dwarf_Addr addr,
                                Dwarf_Die& result)
{
  result = scope;

  // Sometimes we're in a bad scope to begin with -- just let it be.  This can
  // happen for example if the compiler outputs a label PC that's just outside
  // the lexical scope.  We can't really do anything about that, but variables
  // will probably not be accessible in this case.
  if (!die_has_pc(scope, addr))
    return false;

  Dwarf_Die child;
  int rc = dwarf_child(&result, &child);
  while (rc == 0)
    {
      switch (dwarf_tag (&child))
        {
        // lexical tags to recurse within the same starting scope
        // NB: this intentionally doesn't cross into inlines!
        case DW_TAG_lexical_block:
        case DW_TAG_with_stmt:
        case DW_TAG_catch_block:
        case DW_TAG_try_block:
        case DW_TAG_entry_point:
          if (die_has_pc(child, addr))
            {
              result = child;
              rc = dwarf_child(&result, &child);
              continue;
            }
        }
      rc = dwarf_siblingof(&child, &child);
    }
  return true;
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
  int n = dwfl_module_relocations (module);
  dwfl_assert ("dwfl_module_relocations", n >= 0);
  Dwarf_Addr reloc_address = address;
  const char *secname = "";
  if (n > 1)
    {
      int i = dwfl_module_relocate_address (module, &reloc_address);
      dwfl_assert ("dwfl_module_relocate_address", i >= 0);
      secname = dwfl_module_relocation_info (module, i, NULL);
    }

  if (sess.verbose > 2)
    {
      clog << _F("emit dwarf addr %#" PRIx64 " => module %s section %s relocaddr %#" PRIx64,
                 address, module_name.c_str (), (secname ?: "null"),
                 reloc_address) << endl;
    }

  if (n > 0 && !(n == 1 && secname == NULL))
   {
      dwfl_assert ("dwfl_module_relocation_info", secname);
      if (n > 1 || secname[0] != '\0')
        {
          // This gives us the module name, and section name within the
          // module, for a kernel module (or other ET_REL module object).
          obstack_printf (pool, "({ unsigned long addr = 0; ");
          obstack_printf (pool, "addr = _stp_kmodule_relocate (\"%s\",\"%s\",%#" PRIx64 "); ",
                          module_name.c_str(), secname, reloc_address);
          obstack_printf (pool, "addr; })");
        }
      else if (n == 1 && module_name == TOK_KERNEL && secname[0] == '\0')
        {
          // elfutils' way of telling us that this is a relocatable kernel address, which we
          // need to treat the same way here as dwarf_query::add_probe_point does: _stext.
          address -= sess.sym_stext;
          secname = "_stext";
          // Note we "cache" the result here through a static because the
          // kernel will never move after being loaded (unlike modules and
          // user-space dynamic share libraries).
          obstack_printf (pool, "({ static unsigned long addr = 0; ");
          obstack_printf (pool, "if (addr==0) addr = _stp_kmodule_relocate (\"%s\",\"%s\",%#" PRIx64 "); ",
                          module_name.c_str(), secname, address); // PR10000 NB: not reloc_address
          obstack_printf (pool, "addr; })");
        }
      else
        {
          enable_task_finder (sess);
          obstack_printf (pool, "({ unsigned long addr = 0; ");
          obstack_printf (pool, "addr = _stp_umodule_relocate (\"%s\",%#" PRIx64 ", current); ",
                          canonicalize_file_name(module_name.c_str()), address);
          obstack_printf (pool, "addr; })");
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
dwflpp::print_locals(vector<Dwarf_Die>& scopes, ostream &o)
{
  // XXX Shouldn't this be walking up to outer scopes too?

  // Try to get the first child of die.
  Dwarf_Die child;
  if (dwarf_child (&scopes[0], &child) == 0)
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
                o << " $" << name;
              break;
            default:
              break;
            }
        }
      while (dwarf_siblingof (&child, &child) == 0);
    }
}


Dwarf_Attribute *
dwflpp::find_variable_and_frame_base (vector<Dwarf_Die>& scopes,
                                      Dwarf_Addr pc,
                                      string const & local,
                                      const target_symbol *e,
                                      Dwarf_Die *vardie,
                                      Dwarf_Attribute *fb_attr_mem)
{
  Dwarf_Die *scope_die = &scopes[0];
  Dwarf_Attribute *fb_attr = NULL;

  assert (cu);

  int declaring_scope = dwarf_getscopevar (&scopes[0], scopes.size(),
                                           local.c_str(),
                                           0, NULL, 0, 0,
                                           vardie);
  if (declaring_scope < 0)
    {
      stringstream alternatives;
      print_locals (scopes, alternatives);
      if (e->cu_name == "")
        throw semantic_error (_F("unable to find local '%s' near pc %s %s %s %s (%s)",
                                 local.c_str(),
                                 lex_cast_hex(pc).c_str(),
                                 (scope_die == NULL) ? "" : _("in"),
                                 (dwarf_diename(scope_die) ?: "<unknown>"),
                                 (dwarf_diename(cu) ?: "<unknown>"),
                                 (alternatives.str() == ""
                                  ? (_("<no alternatives>"))
				  : (_(" (alternatives:")
                                       + alternatives.str())).c_str()),
                              e->tok);
      else
        throw semantic_error (_F("unable to find global '%s' %s %s %s (%s)",
                                 local.c_str(),
                                 (scope_die == NULL) ? "" : _("in"),
                                 (dwarf_diename(scope_die) ?: "<unknown>"),
                                 e->cu_name.c_str(),
                                 (alternatives.str() == ""
                                  ? (_("<no alternatives>"))
				  : (_(" (alternatives:")
                                       + alternatives.str())).c_str()),
                              e->tok);
    }

  /* Some GCC versions would output duplicate external variables, one
     without a location attribute. If so, try to find the other if it
     exists in the same scope. See GCC PR51410.  */
  Dwarf_Attribute attr_mem;
  if (dwarf_attr_integrate (vardie, DW_AT_const_value, &attr_mem) == NULL
      && dwarf_attr_integrate (vardie, DW_AT_location, &attr_mem) == NULL
      && dwarf_attr_integrate (vardie, DW_AT_external, &attr_mem) != NULL
      && dwarf_tag(&scopes[declaring_scope]) == DW_TAG_compile_unit)
    {
      Dwarf_Die orig_vardie = *vardie;
      bool alt_found = false;
      if (dwarf_child(&scopes[declaring_scope], vardie) == 0)
	do
	  {
	    if (dwarf_tag (vardie) == DW_TAG_variable
		&& strcmp (dwarf_diename (vardie), local.c_str ()) == 0
		&& (dwarf_attr_integrate (vardie, DW_AT_external, &attr_mem)
		    != NULL)
		&& ((dwarf_attr_integrate (vardie, DW_AT_const_value, &attr_mem)
		     != NULL)
		    || (dwarf_attr_integrate (vardie, DW_AT_location, &attr_mem)
			!= NULL)))
	      alt_found = true;
	  }
	while (!alt_found && dwarf_siblingof(vardie, vardie) == 0);

      if (! alt_found)
	*vardie = orig_vardie;
    }

  // Global vars don't need (cannot use) frame base in location descriptor.
  if (e->cu_name != "")
    return NULL;

  /* We start out walking the "lexical scopes" as returned by
   * as returned by dwarf_getscopes for the address, starting with the
   * declaring_scope that the variable was found in.
   */
  vector<Dwarf_Die> physcopes, *fbscopes = &scopes;
  for (size_t inner = declaring_scope;
       inner < fbscopes->size() && fb_attr == NULL;
       ++inner)
    {
      Dwarf_Die& scope = (*fbscopes)[inner];
      switch (dwarf_tag (&scope))
        {
        default:
          continue;
        case DW_TAG_subprogram:
        case DW_TAG_entry_point:
          fb_attr = dwarf_attr_integrate (&scope,
                                          DW_AT_frame_base,
                                          fb_attr_mem);
          break;
        case DW_TAG_inlined_subroutine:
          /* Unless we already are going through the "pyshical die tree",
           * we now need to start walking the die tree where this
           * subroutine is inlined to find the appropriate frame base. */
           if (declaring_scope != -1)
             {
               physcopes = getscopes_die(&scope);
               if (physcopes.empty())
                 throw semantic_error (_F("unable to get die scopes for '%s' in an inlined subroutine",
                                          local.c_str()), e->tok);
               fbscopes = &physcopes;
               inner = 0; // zero is current scope, for look will increase.
               declaring_scope = -1;
             }
          break;
        }
    }

  return fb_attr;
}


struct location *
dwflpp::translate_location(struct obstack *pool,
                           Dwarf_Attribute *attr, Dwarf_Die *die,
			   Dwarf_Addr pc,
                           Dwarf_Attribute *fb_attr,
                           struct location **tail,
                           const target_symbol *e)
{

  /* DW_AT_data_member_location, can be either constant offsets
     (struct member fields), or full blown location expressions.  */

  /* There is no location expression, but a constant value instead.  */
  if (dwarf_whatattr (attr) == DW_AT_const_value)
    {
      *tail = c_translate_constant (pool, &loc2c_error, this,
				    &loc2c_emit_address, 0, pc, attr);
      return *tail;
    }

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
      throw semantic_error (_F("not accessible at this address (%s, dieoffset: %s)",
                               lex_cast_hex(pc).c_str(), lex_cast_hex(dwarf_dieoffset(die)).c_str()),
                               e->tok);

    default:			/* Shouldn't happen.  */
    case -1:
      throw semantic_error (_F("dwarf_getlocation_addr failed, %s", dwarf_errmsg(-1)), e->tok);
    }

  Dwarf_Op *cfa_ops;
  // pc is in the dw address space of the current module, which is what
  // c_translate_location expects. get_cfa_ops wants the global dwfl address.
  // cfa_ops only make sense for locals.
  if (e->cu_name == "")
    {
      Dwarf_Addr addr = pc + module_bias;
      cfa_ops = get_cfa_ops (addr);
    }
  else
    cfa_ops = NULL;

  return c_translate_location (pool, &loc2c_error, this,
                               &loc2c_emit_address,
                               1, 0 /* PR9768 */,
                               pc, attr, expr, len, tail, fb_attr, cfa_ops);
}


void
dwflpp::print_members(Dwarf_Die *vardie, ostream &o, set<string> &dupes)
{
  const int typetag = dwarf_tag (vardie);

  if (typetag != DW_TAG_structure_type &&
      typetag != DW_TAG_class_type &&
      typetag != DW_TAG_union_type)
    {
      o << _F(" Error: %s isn't a struct/class/union", dwarf_type_name(vardie).c_str());
      return;
    }

  // Try to get the first child of vardie.
  Dwarf_Die die_mem;
  Dwarf_Die *die = &die_mem;
  switch (dwarf_child (vardie, die))
    {
    case 1:				// No children.
      o << _F("%s is empty", dwarf_type_name(vardie).c_str());
      break;

    case -1:				// Error.
    default:				// Shouldn't happen.
      o << dwarf_type_name(vardie)
        << ": " << dwarf_errmsg (-1);
      break;

    case 0:				// Success.
      break;
    }

  // Output each sibling's name to 'o'.
  do
    {
      int tag = dwarf_tag(die);
      if (tag != DW_TAG_member && tag != DW_TAG_inheritance)
        continue;

      const char *member = dwarf_diename (die) ;

      if ( tag == DW_TAG_member && member != NULL )
        {
          // Only output if this is new, to avoid inheritance dupes.
          if (dupes.insert(member).second)
            o << " " << member;
        }
      else
        {
          Dwarf_Die temp_die;
          if (!dwarf_attr_die (die, DW_AT_type, &temp_die))
            {
              string source = dwarf_decl_file(die) ?: "<unknown source>";
              int line = -1;
              dwarf_decl_line(die, &line);
              clog << _F("\n Error in obtaining type attribute for anonymous "
                         "member at %s:%d", source.c_str(), line);
              return;
            }

          print_members(&temp_die, o, dupes);
        }

    }
  while (dwarf_siblingof (die, die) == 0);
}


bool
dwflpp::find_struct_member(const target_symbol::component& c,
                           Dwarf_Die *parentdie,
                           Dwarf_Die *memberdie,
                           vector<Dwarf_Die>& dies,
                           vector<Dwarf_Attribute>& locs)
{
  Dwarf_Attribute attr;
  Dwarf_Die die;

  /* With inheritance, a subclass may mask member names of parent classes, so
   * our search among the inheritance tree must be breadth-first rather than
   * depth-first (recursive).  The parentdie is still our starting point. */
  deque<Dwarf_Die> inheritees(1, *parentdie);
  for (; !inheritees.empty(); inheritees.pop_front())
    {
      switch (dwarf_child (&inheritees.front(), &die))
        {
        case 0:		/* First child found.  */
          break;
        case 1:		/* No children.  */
          continue;
        case -1:	/* Error.  */
        default:	/* Shouldn't happen */
          throw semantic_error (dwarf_type_name(&inheritees.front()) + ": "
                                + string (dwarf_errmsg (-1)),
                                c.tok);
        }

      do
        {
          int tag = dwarf_tag(&die);
          if (tag != DW_TAG_member && tag != DW_TAG_inheritance)
            continue;

          const char *name = dwarf_diename(&die);
          if (tag == DW_TAG_inheritance)
            {
              /* Remember inheritee for breadth-first search. */
              Dwarf_Die inheritee;
              if (dwarf_attr_die (&die, DW_AT_type, &inheritee))
                inheritees.push_back(inheritee);
            }
          else if (name == NULL)
            {
              /* Need to recurse for anonymous structs/unions. */
              Dwarf_Die subdie;
              if (dwarf_attr_die (&die, DW_AT_type, &subdie) &&
                  find_struct_member(c, &subdie, memberdie, dies, locs))
                goto success;
            }
          else if (name == c.member)
            {
              *memberdie = die;
              goto success;
            }
        }
      while (dwarf_siblingof (&die, &die) == 0);
    }

  return false;

success:
  /* As we unwind the recursion, we need to build the chain of
   * locations that got to the final answer. */
  if (dwarf_attr_integrate (&die, DW_AT_data_member_location, &attr))
    {
      dies.insert(dies.begin(), die);
      locs.insert(locs.begin(), attr);
    }

  /* Union members don't usually have a location,
   * but just use the containing union's location.  */
  else if (dwarf_tag(parentdie) != DW_TAG_union_type)
    throw semantic_error (_F("no location for field '%s':%s",
                             c.member.c_str(), dwarf_errmsg(-1)), c.tok);

  return true;
}


static inline void
dwarf_die_type (Dwarf_Die *die, Dwarf_Die *typedie_mem, const token *tok=NULL)
{
  if (!dwarf_attr_die (die, DW_AT_type, typedie_mem))
    throw semantic_error (_F("cannot get type of field: %s", dwarf_errmsg(-1)), tok);
}


void
dwflpp::translate_components(struct obstack *pool,
                             struct location **tail,
                             Dwarf_Addr pc,
                             const target_symbol *e,
                             Dwarf_Die *vardie,
                             Dwarf_Die *typedie,
                             unsigned first)
{
  unsigned i = first;
  while (i < e->components.size())
    {
      const target_symbol::component& c = e->components[i];

      /* XXX: This would be desirable, but we don't get the target_symbol token,
         and printing that gives us the file:line number too early anyway. */
#if 0
      // Emit a marker to note which field is being access-attempted, to give
      // better error messages if deref() fails.
      string piece = string(...target_symbol token...) + string ("#") + lex_cast(components[i].second);
      obstack_printf (pool, "c->last_stmt = %s;", lex_cast_qstring(piece).c_str());
#endif

      switch (dwarf_tag (typedie))
        {
        case DW_TAG_typedef:
        case DW_TAG_const_type:
        case DW_TAG_volatile_type:
          /* Just iterate on the referent type.  */
          dwarf_die_type (typedie, typedie, c.tok);
          break;

        case DW_TAG_reference_type:
        case DW_TAG_rvalue_reference_type:
          if (pool)
            c_translate_pointer (pool, 1, 0 /* PR9768*/, typedie, tail);
          dwarf_die_type (typedie, typedie, c.tok);
          break;

        case DW_TAG_pointer_type:
          /* A pointer with no type is a void* -- can't dereference it. */
          if (!dwarf_hasattr_integrate (typedie, DW_AT_type))
            throw semantic_error (_F("invalid access '%s' vs '%s'", lex_cast(c).c_str(),
                                     dwarf_type_name(typedie).c_str()), c.tok);

          if (pool)
            c_translate_pointer (pool, 1, 0 /* PR9768*/, typedie, tail);
          if (c.type != target_symbol::comp_literal_array_index &&
              c.type != target_symbol::comp_expression_array_index)
            {
              dwarf_die_type (typedie, typedie, c.tok);
              break;
            }
          /* else fall through as an array access */

        case DW_TAG_array_type:
          if (c.type == target_symbol::comp_literal_array_index)
            {
              if (pool)
                c_translate_array (pool, 1, 0 /* PR9768 */, typedie, tail,
                                   NULL, c.num_index);
            }
          else if (c.type == target_symbol::comp_expression_array_index)
            {
              string index = "THIS->index" + lex_cast(i);
              if (pool)
                c_translate_array (pool, 1, 0 /* PR9768 */, typedie, tail,
                                   index.c_str(), 0);
            }
          else
            throw semantic_error (_F("invalid access '%s' for array type",
                                     lex_cast(c).c_str()), c.tok);

          dwarf_die_type (typedie, typedie, c.tok);
          *vardie = *typedie;
          ++i;
          break;

        case DW_TAG_structure_type:
        case DW_TAG_union_type:
        case DW_TAG_class_type:
          if (c.type != target_symbol::comp_struct_member)
            throw semantic_error (_F("invalid access '%s' for %s",
                                     lex_cast(c).c_str(), dwarf_type_name(typedie).c_str()));

          if (dwarf_hasattr(typedie, DW_AT_declaration))
            {
              Dwarf_Die *tmpdie = declaration_resolve(typedie);
              if (tmpdie == NULL)
                throw semantic_error (_F("unresolved %s", dwarf_type_name(typedie).c_str()), c.tok);
              *typedie = *tmpdie;
            }

            {
              vector<Dwarf_Die> dies;
              vector<Dwarf_Attribute> locs;
              if (!find_struct_member(c, typedie, vardie, dies, locs))
                {
                  /* Add a file:line hint for anonymous types */
                  string source;
                  if (!dwarf_hasattr_integrate(typedie, DW_AT_name))
                    {
                      int line;
                      const char *file = dwarf_decl_file(typedie);
                      if (file && dwarf_decl_line(typedie, &line) == 0)
                        source = " (" + string(file) + ":"
                                 + lex_cast(line) + ")";
                    }

                  string alternatives;
                  stringstream members;
                  set<string> member_dupes;
                  print_members(typedie, members, member_dupes);
                  if (members.str().size() != 0)
                    alternatives = " (alternatives:" + members.str() + ")";
                  throw semantic_error(_F("unable to find member '%s' for %s%s%s", c.member.c_str(),
                                          dwarf_type_name(typedie).c_str(), source.c_str(),
                                          alternatives.c_str()), c.tok);
                }

              for (unsigned j = 0; j < locs.size(); ++j)
                if (pool)
                  translate_location (pool, &locs[j], &dies[j],
                                      pc, NULL, tail, e);
            }

          dwarf_die_type (vardie, typedie, c.tok);
          ++i;
          break;

        case DW_TAG_enumeration_type:
        case DW_TAG_base_type:
          throw semantic_error (_F("invalid access '%s' vs. %s", lex_cast(c).c_str(),
                                   dwarf_type_name(typedie).c_str()), c.tok);
          break;

        case -1:
          throw semantic_error (_F("cannot find type: %s", dwarf_errmsg(-1)), c.tok);
          break;

        default:
          throw semantic_error (_F("%s: unexpected type tag %s", dwarf_type_name(typedie).c_str(),
                                   lex_cast(dwarf_tag(typedie)).c_str()), c.tok);
          break;
        }
    }
}


void
dwflpp::resolve_unqualified_inner_typedie (Dwarf_Die *typedie,
                                           Dwarf_Die *innerdie,
                                           const target_symbol *e)
{
  int typetag = dwarf_tag (typedie);
  *innerdie = *typedie;
  while (typetag == DW_TAG_typedef ||
         typetag == DW_TAG_const_type ||
         typetag == DW_TAG_volatile_type)
    {
      if (!dwarf_attr_die (innerdie, DW_AT_type, innerdie))
        throw semantic_error (_F("cannot get type of pointee: %s", dwarf_errmsg(-1)), e->tok);
      typetag = dwarf_tag (innerdie);
    }
}


void
dwflpp::translate_final_fetch_or_store (struct obstack *pool,
                                        struct location **tail,
                                        Dwarf_Addr /*module_bias*/,
                                        Dwarf_Die *vardie,
                                        Dwarf_Die *start_typedie,
                                        bool lvalue,
                                        const target_symbol *e,
                                        string &,
                                        string &,
                                        exp_type & ty)
{
  /* First boil away any qualifiers associated with the type DIE of
     the final location to be accessed.  */

  Dwarf_Die typedie_mem, *typedie = &typedie_mem;
  resolve_unqualified_inner_typedie (start_typedie, typedie, e);

  /* If we're looking for an address, then we can just provide what
     we computed to this point, without using a fetch/store. */
  if (e->addressof)
    {
      if (lvalue)
        throw semantic_error (_("cannot write to member address"), e->tok);

      if (dwarf_hasattr_integrate (vardie, DW_AT_bit_offset))
        throw semantic_error (_("cannot take address of bit-field"), e->tok);

      c_translate_addressof (pool, 1, 0, vardie, typedie, tail, "THIS->__retvalue");
      ty = pe_long;
      return;
    }

  /* Then switch behavior depending on the type of fetch/store we
     want, and the type and pointer-ness of the final location. */

  int typetag = dwarf_tag (typedie);
  switch (typetag)
    {
    default:
      throw semantic_error (_F("unsupported type tag %s for %s", lex_cast(typetag).c_str(),
                               dwarf_type_name(typedie).c_str()), e->tok);
      break;

    case DW_TAG_structure_type:
    case DW_TAG_class_type:
    case DW_TAG_union_type:
      throw semantic_error (_F("'%s' is being accessed instead of a member",
                               dwarf_type_name(typedie).c_str()), e->tok);
      break;

    case DW_TAG_base_type:

      // Reject types we can't handle in systemtap
      {
        Dwarf_Attribute encoding_attr;
        Dwarf_Word encoding = (Dwarf_Word) -1;
        dwarf_formudata (dwarf_attr_integrate (typedie, DW_AT_encoding, &encoding_attr),
                         & encoding);
        if (encoding == (Dwarf_Word) -1)
          {
            // clog << "bad type1 " << encoding << " diestr" << endl;
            throw semantic_error (_F("unsupported type (mystery encoding %s for %s", lex_cast(encoding).c_str(),
                                     dwarf_type_name(typedie).c_str()), e->tok);
          }

        if (encoding == DW_ATE_float
            || encoding == DW_ATE_complex_float
            /* XXX || many others? */)
          {
            // clog << "bad type " << encoding << " diestr" << endl;
            throw semantic_error (_F("unsupported type (encoding %s) for %s", lex_cast(encoding).c_str(),
                                     dwarf_type_name(typedie).c_str()), e->tok);
          }
      }
      // Fallthrough. enumeration_types are always scalar.
    case DW_TAG_enumeration_type:

      ty = pe_long;
      if (lvalue)
        c_translate_store (pool, 1, 0 /* PR9768 */, vardie, typedie, tail,
                           "THIS->value");
      else
        c_translate_fetch (pool, 1, 0 /* PR9768 */, vardie, typedie, tail,
                           "THIS->__retvalue");
      break;

    case DW_TAG_array_type:
    case DW_TAG_pointer_type:
    case DW_TAG_reference_type:
    case DW_TAG_rvalue_reference_type:

        if (lvalue)
          {
            ty = pe_long;
            if (typetag == DW_TAG_array_type)
              throw semantic_error (_("cannot write to array address"), e->tok);
            if (typetag == DW_TAG_reference_type ||
                typetag == DW_TAG_rvalue_reference_type)
              throw semantic_error (_("cannot write to reference"), e->tok);
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
            c_translate_addressof (pool, 1, 0 /* PR9768 */, NULL, NULL, tail,
                                   "THIS->__retvalue");
          }
      break;
    }
}


string
dwflpp::express_as_string (string prelude,
                           string postlude,
                           struct location *head)
{
  size_t bufsz = 0;
  char *buf = 0; // NB: it would leak to pre-allocate a buffer here
  FILE *memstream = open_memstream (&buf, &bufsz);
  assert(memstream);

  fprintf(memstream, "{\n");
  fprintf(memstream, "%s", prelude.c_str());

  unsigned int stack_depth;
  bool deref = c_emit_location (memstream, head, 1, &stack_depth);

  // Ensure that DWARF keeps loc2c to a "reasonable" stack size
  // 32 intptr_t leads to max 256 bytes on the stack
  if (stack_depth > 32)
    throw semantic_error("oversized DWARF stack");

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

Dwarf_Addr
dwflpp::vardie_from_symtable (Dwarf_Die *vardie, Dwarf_Addr *addr)
{
  const char *name = dwarf_linkage_name (vardie) ?: dwarf_diename (vardie);

  if (sess.verbose > 2)
    clog << _F("finding symtable address for %s\n", name);

  *addr = 0;
  int syms = dwfl_module_getsymtab (module);
  dwfl_assert (_("Getting symbols"), syms >= 0);

  for (int i = 0; *addr == 0 && i < syms; i++)
    {
      GElf_Sym sym;
      GElf_Word shndxp;
      const char *symname = dwfl_module_getsym(module, i, &sym, &shndxp);
      if (symname
	  && ! strcmp (name, symname)
	  && sym.st_shndx != SHN_UNDEF
	  && (GELF_ST_TYPE (sym.st_info) == STT_NOTYPE // PR13284
	      || GELF_ST_TYPE (sym.st_info) == STT_OBJECT))
	*addr = sym.st_value;
    }

  // Don't relocate for the kernel, or kernel modules we handle those
  // specially in emit_address.
  if (dwfl_module_relocations (module) == 1 && module_name != TOK_KERNEL)
    dwfl_module_relocate_address (module, addr);

  if (sess.verbose > 2)
    clog << _F("found %s @%#" PRIx64 "\n", name, *addr);

  return *addr;
}

string
dwflpp::literal_stmt_for_local (vector<Dwarf_Die>& scopes,
                                Dwarf_Addr pc,
                                string const & local,
                                const target_symbol *e,
                                bool lvalue,
                                exp_type & ty)
{
  Dwarf_Die vardie;
  Dwarf_Attribute fb_attr_mem, *fb_attr = NULL;

  fb_attr = find_variable_and_frame_base (scopes, pc, local, e,
                                          &vardie, &fb_attr_mem);

  if (sess.verbose>2)
    {
      if (e->cu_name == "")
        clog << _F("finding location for local '%s' near address %#" PRIx64
                   ", module bias %#" PRIx64 "\n", local.c_str(), pc,
	           module_bias);
      else
        clog << _F("finding location for global '%s' in CU '%s'\n",
		   local.c_str(), e->cu_name.c_str());
    }


#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

  struct obstack pool;
  obstack_init (&pool);
  struct location *tail = NULL;

  /* Given $foo->bar->baz[NN], translate the location of foo. */

  struct location *head;

  Dwarf_Attribute attr_mem;
  if (dwarf_attr_integrate (&vardie, DW_AT_const_value, &attr_mem) == NULL
      && dwarf_attr_integrate (&vardie, DW_AT_location, &attr_mem) == NULL)
    {
      Dwarf_Op addr_loc;
      memset(&addr_loc, 0, sizeof(Dwarf_Op));
      addr_loc.atom = DW_OP_addr;
      // If it is an external variable try the symbol table. PR10622.
      if (dwarf_attr_integrate (&vardie, DW_AT_external, &attr_mem) != NULL
	  && vardie_from_symtable (&vardie, &addr_loc.number) != 0)
	{
	  head = c_translate_location (&pool, &loc2c_error, this,
				       &loc2c_emit_address,
				       1, 0, pc,
				       NULL, &addr_loc, 1, &tail, NULL, NULL);
	}
      else
        throw semantic_error (_F("failed to retrieve location attribute for '%s' (dieoffset: %s)",
                                 local.c_str(), lex_cast_hex(dwarf_dieoffset(&vardie)).c_str()), e->tok);
    }
  else
    head = translate_location (&pool, &attr_mem, &vardie, pc, fb_attr, &tail, e);

  /* Translate the ->bar->baz[NN] parts. */

  Dwarf_Die typedie;
  if (dwarf_attr_die (&vardie, DW_AT_type, &typedie) == NULL)
    throw semantic_error(_F("failed to retrieve type attribute for '%s' (dieoffset: %s)", local.c_str(), lex_cast_hex(dwarf_dieoffset(&vardie)).c_str()), e->tok);

  translate_components (&pool, &tail, pc, e, &vardie, &typedie);

  /* Translate the assignment part, either
     x = $foo->bar->baz[NN]
     or
     $foo->bar->baz[NN] = x
  */

  string prelude, postlude;
  translate_final_fetch_or_store (&pool, &tail, module_bias,
                                  &vardie, &typedie, lvalue, e,
                                  prelude, postlude, ty);

  /* Write the translation to a string. */
  string result = express_as_string(prelude, postlude, head);
  obstack_free (&pool, 0);
  return result;
}

Dwarf_Die*
dwflpp::type_die_for_local (vector<Dwarf_Die>& scopes,
                            Dwarf_Addr pc,
                            string const & local,
                            const target_symbol *e,
                            Dwarf_Die *typedie)
{
  Dwarf_Die vardie;
  Dwarf_Attribute attr_mem;

  find_variable_and_frame_base (scopes, pc, local, e, &vardie, &attr_mem);

  if (dwarf_attr_die (&vardie, DW_AT_type, typedie) == NULL)
    throw semantic_error(_F("failed to retrieve type attribute for '%s'", local.c_str()), e->tok);

  translate_components (NULL, NULL, pc, e, &vardie, typedie);
  return typedie;
}


string
dwflpp::literal_stmt_for_return (Dwarf_Die *scope_die,
                                 Dwarf_Addr pc,
                                 const target_symbol *e,
                                 bool lvalue,
                                 exp_type & ty)
{
  if (sess.verbose>2)
      clog << _F("literal_stmt_for_return: finding return value for %s (%s)\n",
                (dwarf_diename(scope_die) ?: "<unknown>"), (dwarf_diename(cu) ?: "<unknown>"));

  struct obstack pool;
  obstack_init (&pool);
  struct location *tail = NULL;

  /* Given $return->bar->baz[NN], translate the location of return. */
  const Dwarf_Op *locops;
  int nlocops = dwfl_module_return_value_location (module, scope_die,
                                                   &locops);
  if (nlocops < 0)
    {
      throw semantic_error(_F("failed to retrieve return value location for %s (%s)",
                          (dwarf_diename(scope_die) ?: "<unknown>"),
                          (dwarf_diename(cu) ?: "<unknown>")), e->tok);
    }
  // the function has no return value (e.g. "void" in C)
  else if (nlocops == 0)
    {
      throw semantic_error(_F("function %s (%s) has no return value",
                             (dwarf_diename(scope_die) ?: "<unknown>"),
                             (dwarf_diename(cu) ?: "<unknown>")), e->tok);
    }

  struct location  *head = c_translate_location (&pool, &loc2c_error, this,
                                                 &loc2c_emit_address,
                                                 1, 0 /* PR9768 */,
                                                 pc, NULL, locops, nlocops,
                                                 &tail, NULL, NULL);

  /* Translate the ->bar->baz[NN] parts. */

  Dwarf_Die vardie = *scope_die, typedie;
  if (dwarf_attr_die (&vardie, DW_AT_type, &typedie) == NULL)
    throw semantic_error(_F("failed to retrieve return value type attribute for %s (%s)",
                           (dwarf_diename(&vardie) ?: "<unknown>"),
                           (dwarf_diename(cu) ?: "<unknown>")), e->tok);

  translate_components (&pool, &tail, pc, e, &vardie, &typedie);

  /* Translate the assignment part, either
     x = $return->bar->baz[NN]
     or
     $return->bar->baz[NN] = x
  */

  string prelude, postlude;
  translate_final_fetch_or_store (&pool, &tail, module_bias,
                                  &vardie, &typedie, lvalue, e,
                                  prelude, postlude, ty);

  /* Write the translation to a string. */
  string result = express_as_string(prelude, postlude, head);
  obstack_free (&pool, 0);
  return result;
}

Dwarf_Die*
dwflpp::type_die_for_return (Dwarf_Die *scope_die,
                             Dwarf_Addr pc,
                             const target_symbol *e,
                             Dwarf_Die *typedie)
{
  Dwarf_Die vardie = *scope_die;
  if (dwarf_attr_die (&vardie, DW_AT_type, typedie) == NULL)
    throw semantic_error(_F("failed to retrieve return value type attribute for %s (%s)",
                           (dwarf_diename(&vardie) ?: "<unknown>"),
                           (dwarf_diename(cu) ?: "<unknown>")), e->tok);

  translate_components (NULL, NULL, pc, e, &vardie, typedie);
  return typedie;
}


string
dwflpp::literal_stmt_for_pointer (Dwarf_Die *start_typedie,
                                  const target_symbol *e,
                                  bool lvalue,
                                  exp_type & ty)
{
  if (sess.verbose>2)
      clog << _F("literal_stmt_for_pointer: finding value for %s (%s)\n",
                  dwarf_type_name(start_typedie).c_str(), (dwarf_diename(cu) ?: "<unknown>"));

  struct obstack pool;
  obstack_init (&pool);
  struct location *head = c_translate_argument (&pool, &loc2c_error, this,
                                                &loc2c_emit_address,
                                                1, "THIS->pointer");
  struct location *tail = head;

  /* Translate the ->bar->baz[NN] parts. */

  unsigned first = 0;
  Dwarf_Die typedie = *start_typedie, vardie = typedie;

  /* As a special case when typedie is not an array or pointer, we can allow
   * array indexing on THIS->pointer instead (since we do know the pointee type
   * and can determine its size).  PR11556. */
  const target_symbol::component* c =
    e->components.empty() ? NULL : &e->components[0];
  if (c && (c->type == target_symbol::comp_literal_array_index ||
            c->type == target_symbol::comp_expression_array_index))
    {
      resolve_unqualified_inner_typedie (&typedie, &typedie, e);
      int typetag = dwarf_tag (&typedie);
      if (typetag != DW_TAG_pointer_type &&
          typetag != DW_TAG_array_type)
        {
          if (c->type == target_symbol::comp_literal_array_index)
            c_translate_array_pointer (&pool, 1, &typedie, &tail, NULL, c->num_index);
          else
            c_translate_array_pointer (&pool, 1, &typedie, &tail, "THIS->index0", 0);
          ++first;
        }
    }

  /* Now translate the rest normally. */

  translate_components (&pool, &tail, 0, e, &vardie, &typedie, first);

  /* Translate the assignment part, either
     x = (THIS->pointer)->bar->baz[NN]
     or
     (THIS->pointer)->bar->baz[NN] = x
  */

  string prelude, postlude;
  translate_final_fetch_or_store (&pool, &tail, module_bias,
                                  &vardie, &typedie, lvalue, e,
                                  prelude, postlude, ty);

  /* Write the translation to a string. */
  string result = express_as_string(prelude, postlude, head);
  obstack_free (&pool, 0);
  return result;
}

Dwarf_Die*
dwflpp::type_die_for_pointer (Dwarf_Die *start_typedie,
                              const target_symbol *e,
                              Dwarf_Die *typedie)
{
  unsigned first = 0;
  *typedie = *start_typedie;
  Dwarf_Die vardie = *typedie;

  /* Handle the same PR11556 case as above. */
  const target_symbol::component* c =
    e->components.empty() ? NULL : &e->components[0];
  if (c && (c->type == target_symbol::comp_literal_array_index ||
            c->type == target_symbol::comp_expression_array_index))
    {
      resolve_unqualified_inner_typedie (typedie, typedie, e);
      int typetag = dwarf_tag (typedie);
      if (typetag != DW_TAG_pointer_type &&
          typetag != DW_TAG_array_type)
        ++first;
    }

  translate_components (NULL, NULL, 0, e, &vardie, typedie, first);
  return typedie;
}


static bool
in_kprobes_function(systemtap_session& sess, Dwarf_Addr addr)
{
  if (sess.sym_kprobes_text_start != 0 && sess.sym_kprobes_text_end != 0)
    {
      // If the probe point address is anywhere in the __kprobes
      // address range, we can't use this probe point.
      if (addr >= sess.sym_kprobes_text_start && addr < sess.sym_kprobes_text_end)
        return true;
    }
  return false;
}


bool
dwflpp::blacklisted_p(const string& funcname,
                      const string& filename,
                      int,
                      const string& module,
                      Dwarf_Addr addr,
                      bool has_return)
{
  if (!blacklist_enabled)
    return false; // no blacklist for userspace

  bool blacklisted = false;

  // check against section blacklist
  string section = get_blacklist_section(addr);
  // PR6503: modules don't need special init/exit treatment
  if (module == TOK_KERNEL && !regexec (&blacklist_section, section.c_str(), 0, NULL, 0))
    {
      blacklisted = true;
      if (sess.verbose>1)
        clog << _(" init/exit");
    }

  // Check for function marked '__kprobes'.
  if (module == TOK_KERNEL && in_kprobes_function(sess, addr))
    {
      blacklisted = true;
      if (sess.verbose>1)
        clog << _(" __kprobes");
    }

  // Check probe point against file/function blacklists.
  int goodfn = regexec (&blacklist_func, funcname.c_str(), 0, NULL, 0);
  if (has_return)
    goodfn = goodfn && regexec (&blacklist_func_ret, funcname.c_str(), 0, NULL, 0);
  int goodfile = regexec (&blacklist_file, filename.c_str(), 0, NULL, 0);

  if (! (goodfn && goodfile))
    {
      blacklisted = true;
      if (sess.verbose>1)
        clog << _(" file/function blacklist");
    }

  if (sess.guru_mode && blacklisted)
    {
      blacklisted = false;
      if (sess.verbose>1)
        clog << _(" - not skipped (guru mode enabled)");
    }

  if (blacklisted && sess.verbose>1)
    clog << _(" - skipped");

  // This probe point is not blacklisted.
  return blacklisted;
}


void
dwflpp::build_blacklist()
{
  // We build up the regexps in these strings

  // Add ^ anchors at the front; $ will be added just before regcomp.

  string blfn = "^(";
  string blfn_ret = "^(";
  string blfile = "^(";
  string blsection = "^(";

  blsection += "\\.init\\."; // first alternative, no "|"
  blsection += "|\\.exit\\.";
  blsection += "|\\.devinit\\.";
  blsection += "|\\.devexit\\.";
  blsection += "|\\.cpuinit\\.";
  blsection += "|\\.cpuexit\\.";
  blsection += "|\\.meminit\\.";
  blsection += "|\\.memexit\\.";

  /* NOTE all include/asm .h blfile patterns might need "full path"
     so prefix those with '.*' - see PR13108 and PR13112. */
  blfile += "kernel/kprobes\\.c"; // first alternative, no "|"
  blfile += "|arch/.*/kernel/kprobes\\.c";
  blfile += "|.*/include/asm/io\\.h";
  blfile += "|.*/include/asm/io_64\\.h";
  blfile += "|.*/include/asm/bitops\\.h";
  blfile += "|drivers/ide/ide-iops\\.c";
  // paravirt ops
  blfile += "|arch/.*/kernel/paravirt\\.c";
  blfile += "|.*/include/asm/paravirt\\.h";

  // XXX: it would be nice if these blacklisted functions were pulled
  // in dynamically, instead of being statically defined here.
  // Perhaps it could be populated from script files.  A "noprobe
  // kernel.function("...")"  construct might do the trick.

  // Most of these are marked __kprobes in newer kernels.  We list
  // them here (anyway) so the translator can block them on older
  // kernels that don't have the __kprobes function decorator.  This
  // also allows detection of problems at translate- rather than
  // run-time.

  blfn += "atomic_notifier_call_chain"; // first blfn; no "|"
  blfn += "|default_do_nmi";
  blfn += "|__die";
  blfn += "|die_nmi";
  blfn += "|do_debug";
  blfn += "|do_general_protection";
  blfn += "|do_int3";
  blfn += "|do_IRQ";
  blfn += "|do_page_fault";
  blfn += "|do_sparc64_fault";
  blfn += "|do_trap";
  blfn += "|dummy_nmi_callback";
  blfn += "|flush_icache_range";
  blfn += "|ia64_bad_break";
  blfn += "|ia64_do_page_fault";
  blfn += "|ia64_fault";
  blfn += "|io_check_error";
  blfn += "|mem_parity_error";
  blfn += "|nmi_watchdog_tick";
  blfn += "|notifier_call_chain";
  blfn += "|oops_begin";
  blfn += "|oops_end";
  blfn += "|program_check_exception";
  blfn += "|single_step_exception";
  blfn += "|sync_regs";
  blfn += "|unhandled_fault";
  blfn += "|unknown_nmi_error";
  blfn += "|xen_[gs]et_debugreg";
  blfn += "|xen_irq_.*";
  blfn += "|xen_.*_fl_direct.*";
  blfn += "|check_events";
  blfn += "|xen_adjust_exception_frame";
  blfn += "|xen_iret.*";
  blfn += "|xen_sysret64.*";
  blfn += "|test_ti_thread_flag.*";
  blfn += "|inat_get_opcode_attribute";
  blfn += "|system_call_after_swapgs";

  // Lots of locks
  blfn += "|.*raw_.*_lock.*";
  blfn += "|.*raw_.*_unlock.*";
  blfn += "|.*raw_.*_trylock.*";
  blfn += "|.*read_lock.*";
  blfn += "|.*read_unlock.*";
  blfn += "|.*read_trylock.*";
  blfn += "|.*write_lock.*";
  blfn += "|.*write_unlock.*";
  blfn += "|.*write_trylock.*";
  blfn += "|.*write_seqlock.*";
  blfn += "|.*write_sequnlock.*";
  blfn += "|.*spin_lock.*";
  blfn += "|.*spin_unlock.*";
  blfn += "|.*spin_trylock.*";
  blfn += "|.*spin_is_locked.*";
  blfn += "|rwsem_.*lock.*";
  blfn += "|.*mutex_.*lock.*";
  blfn += "|raw_.*";

  // atomic functions
  blfn += "|atomic_.*";
  blfn += "|atomic64_.*";

  // few other problematic cases
  blfn += "|get_bh";
  blfn += "|put_bh";

  // Experimental
  blfn += "|.*apic.*|.*APIC.*";
  blfn += "|.*softirq.*";
  blfn += "|.*IRQ.*";
  blfn += "|.*_intr.*";
  blfn += "|__delay";
  blfn += "|.*kernel_text.*";
  blfn += "|get_current";
  blfn += "|current_.*";
  blfn += "|.*exception_tables.*";
  blfn += "|.*setup_rt_frame.*";

  // PR 5759, CONFIG_PREEMPT kernels
  blfn += "|.*preempt_count.*";
  blfn += "|preempt_schedule";

  // These functions don't return, so return probes would never be recovered
  blfn_ret += "do_exit"; // no "|"
  blfn_ret += "|sys_exit";
  blfn_ret += "|sys_exit_group";

  // __switch_to changes "current" on x86_64 and i686, so return probes
  // would cause kernel panic, and it is marked as "__kprobes" on x86_64
  if (sess.architecture == "x86_64")
    blfn += "|__switch_to";
  if (sess.architecture == "i686")
    blfn_ret += "|__switch_to";

  // RHEL6 pre-beta 2.6.32-19.el6
  blfn += "|special_mapping_.*";
  blfn += "|.*_pte_.*"; // or "|smaps_pte_range";
  blfile += "|fs/seq_file\\.c";

  blfn += ")$";
  blfn_ret += ")$";
  blfile += ")$";
  blsection += ")"; // NB: no $, sections match just the beginning

  if (sess.verbose > 2)
    {
      clog << _("blacklist regexps:") << endl;
      clog << "blfn: " << blfn << endl;
      clog << "blfn_ret: " << blfn_ret << endl;
      clog << "blfile: " << blfile << endl;
      clog << "blsection: " << blsection << endl;
    }

  int rc = regcomp (& blacklist_func, blfn.c_str(), REG_NOSUB|REG_EXTENDED);
  if (rc) throw semantic_error (_("blacklist_func regcomp failed"));
  rc = regcomp (& blacklist_func_ret, blfn_ret.c_str(), REG_NOSUB|REG_EXTENDED);
  if (rc) throw semantic_error (_("blacklist_func_ret regcomp failed"));
  rc = regcomp (& blacklist_file, blfile.c_str(), REG_NOSUB|REG_EXTENDED);
  if (rc) throw semantic_error (_("blacklist_file regcomp failed"));
  rc = regcomp (& blacklist_section, blsection.c_str(), REG_NOSUB|REG_EXTENDED);
  if (rc) throw semantic_error (_("blacklist_section regcomp failed"));

  blacklist_enabled = true;
}


string
dwflpp::get_blacklist_section(Dwarf_Addr addr)
{
  string blacklist_section;
  Dwarf_Addr bias;
  // We prefer dwfl_module_getdwarf to dwfl_module_getelf here,
  // because dwfl_module_getelf can force costly section relocations
  // we don't really need, while either will do for this purpose.
  Elf* elf = (dwarf_getelf (dwfl_module_getdwarf (module, &bias))
              ?: dwfl_module_getelf (module, &bias));

  Dwarf_Addr offset = addr - bias;
  if (elf)
    {
      Elf_Scn* scn = 0;
      size_t shstrndx;
      dwfl_assert ("getshdrstrndx", elf_getshdrstrndx (elf, &shstrndx));
      while ((scn = elf_nextscn (elf, scn)) != NULL)
        {
          GElf_Shdr shdr_mem;
          GElf_Shdr *shdr = gelf_getshdr (scn, &shdr_mem);
          if (! shdr)
            continue; // XXX error?

          if (!(shdr->sh_flags & SHF_ALLOC))
            continue;

          GElf_Addr start = shdr->sh_addr;
          GElf_Addr end = start + shdr->sh_size;
          if (! (offset >= start && offset < end))
            continue;

          blacklist_section =  elf_strptr (elf, shstrndx, shdr->sh_name);
          break;
        }
    }
  return blacklist_section;
}


/* Find the section named 'section_name'  in the current module
 * returning the section header using 'shdr_mem' */

GElf_Shdr *
dwflpp::get_section(string section_name, GElf_Shdr *shdr_mem, Elf **elf_ret)
{
  GElf_Shdr *shdr = NULL;
  Elf* elf;
  Dwarf_Addr bias;
  size_t shstrndx;

  // Explicitly look in the main elf file first.
  elf = dwfl_module_getelf (module, &bias);
  Elf_Scn *probe_scn = NULL;

  dwfl_assert ("getshdrstrndx", elf_getshdrstrndx (elf, &shstrndx));

  bool have_section = false;

  while ((probe_scn = elf_nextscn (elf, probe_scn)))
    {
      shdr = gelf_getshdr (probe_scn, shdr_mem);
      assert (shdr != NULL);

      if (elf_strptr (elf, shstrndx, shdr->sh_name) == section_name)
	{
	  have_section = true;
	  break;
	}
    }

  // Older versions may put the section in the debuginfo dwarf file,
  // so check if it actually exists, if not take a look in the debuginfo file
  if (! have_section || (have_section && shdr->sh_type == SHT_NOBITS))
    {
      elf = dwarf_getelf (dwfl_module_getdwarf (module, &bias));
      if (! elf)
	return NULL;
      dwfl_assert ("getshdrstrndx", elf_getshdrstrndx (elf, &shstrndx));
      probe_scn = NULL;
      while ((probe_scn = elf_nextscn (elf, probe_scn)))
	{
	  shdr = gelf_getshdr (probe_scn, shdr_mem);
	  if (elf_strptr (elf, shstrndx, shdr->sh_name) == section_name)
	    {
	      have_section = true;
	      break;
	    }
	}
    }

  if (!have_section)
    return NULL;

  if (elf_ret)
    *elf_ret = elf;
  return shdr;
}


Dwarf_Addr
dwflpp::relocate_address(Dwarf_Addr dw_addr, string& reloc_section)
{
  // PR10273
  // libdw address, so adjust for bias gotten from dwfl_module_getdwarf
  Dwarf_Addr reloc_addr = dw_addr + module_bias;
  if (!module)
    {
      assert(module_name == TOK_KERNEL);
      reloc_section = "";
    }
  else if (dwfl_module_relocations (module) > 0)
    {
      // This is a relocatable module; libdwfl already knows its
      // sections, so we can relativize addr.
      int idx = dwfl_module_relocate_address (module, &reloc_addr);
      const char* r_s = dwfl_module_relocation_info (module, idx, NULL);
      if (r_s)
        reloc_section = r_s;

      if (reloc_section == "" && dwfl_module_relocations (module) == 1)
          reloc_section = ".dynamic";
    }
  else
    reloc_section = ".absolute";
  return reloc_addr;
}

/* Returns the call frame address operations for the given program counter
 * in the libdw address space.
 */
Dwarf_Op *
dwflpp::get_cfa_ops (Dwarf_Addr pc)
{
  Dwarf_Op *cfa_ops = NULL;

  if (sess.verbose > 2)
    clog << "get_cfa_ops @0x" << hex << pc << dec
	 << ", module_start @0x" << hex << module_start << dec << endl;

  // Try debug_frame first, then fall back on eh_frame.
  size_t cfa_nops = 0;
  Dwarf_Addr bias = 0;
  Dwarf_Frame *frame = NULL;
  Dwarf_CFI *cfi = dwfl_module_dwarf_cfi (module, &bias);
  if (cfi != NULL)
    {
      if (sess.verbose > 3)
	clog << "got dwarf cfi bias: 0x" << hex << bias << dec << endl;
      if (dwarf_cfi_addrframe (cfi, pc - bias, &frame) == 0)
	dwarf_frame_cfa (frame, &cfa_ops, &cfa_nops);
      else if (sess.verbose > 3)
	clog << "dwarf_cfi_addrframe failed: " << dwarf_errmsg(-1) << endl;
    }
  else if (sess.verbose > 3)
    clog << "dwfl_module_dwarf_cfi failed: " << dwfl_errmsg(-1) << endl;

  if (cfa_ops == NULL)
    {
      cfi = dwfl_module_eh_cfi (module, &bias);
      if (cfi != NULL)
	{
	  if (sess.verbose > 3)
	    clog << "got eh cfi bias: 0x" << hex << bias << dec << endl;
	  Dwarf_Frame *frame = NULL;
	  if (dwarf_cfi_addrframe (cfi, pc - bias, &frame) == 0)
	    dwarf_frame_cfa (frame, &cfa_ops, &cfa_nops);
	  else if (sess.verbose > 3)
	    clog << "dwarf_cfi_addrframe failed: " << dwarf_errmsg(-1) << endl;
	}
      else if (sess.verbose > 3)
	clog << "dwfl_module_eh_cfi failed: " << dwfl_errmsg(-1) << endl;

    }

  if (sess.verbose > 2)
    {
      if (cfa_ops == NULL)
	clog << _("not found cfa") << endl;
      else
	{
	  Dwarf_Addr frame_start, frame_end;
	  bool frame_signalp;
	  int info = dwarf_frame_info (frame, &frame_start, &frame_end,
				       &frame_signalp);
          clog << _F("found cfa, info: %d [start: %#" PRIx64 ", end: %#" PRIx64 
                     ", nops: %zu", info, frame_start, frame_end, cfa_nops) << endl;
	}
    }

  return cfa_ops;
}

int
dwflpp::add_module_build_id_to_hash (Dwfl_Module *m,
                 void **userdata __attribute__ ((unused)),
                 const char *name,
                 Dwarf_Addr base,
                 void *arg)
{
   string modname = name;
   systemtap_session * s = (systemtap_session *)arg;
  if (pending_interrupts)
    return DWARF_CB_ABORT;

  // Extract the build ID
  const unsigned char *bits;
  GElf_Addr vaddr;
  int bits_length = dwfl_module_build_id(m, &bits, &vaddr);
  if(bits_length > 0)
    {
      // Convert the binary bits to a hex string
      string hex = hex_dump(bits, bits_length);

      // Store the build ID in the session
      s->build_ids.push_back(hex);
    }

  return DWARF_CB_OK;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
