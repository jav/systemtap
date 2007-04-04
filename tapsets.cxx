// tapset resolution
// Copyright (C) 2005-2007 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "elaborate.h"
#include "tapsets.h"
#include "translate.h"
#include "session.h"
#include "util.h"

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

extern "C" {
#include <fcntl.h>
#include <elfutils/libdwfl.h>
#include <elfutils/libdw.h>
#include <elfutils/libebl.h>
#include <dwarf.h>
#include <elf.h>
#include <obstack.h>
#include <regex.h>
#include <glob.h>
#include <fnmatch.h>

#include "loc2c.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}


#ifdef PERFMON
#include <perfmon/pfmlib.h>
#include <perfmon/perfmon.h>
#endif

using namespace std;


// ------------------------------------------------------------------------
// Generic derived_probe_group: contains an ordinary vector of the
// given type.  It provides only the enrollment function.

template <class DP> struct generic_dpg: public derived_probe_group
{
protected:
  vector <DP*> probes;
public:
  generic_dpg () {}
  void enroll (DP* probe) { probes.push_back (probe); }
};



// ------------------------------------------------------------------------
// begin/end probes are run right during registration / deregistration
// ------------------------------------------------------------------------

struct be_derived_probe: public derived_probe
{
  bool begin;
  int64_t priority;

  be_derived_probe (probe* p, bool b, int64_t pr):
    derived_probe (p), begin (b), priority (pr) {}
  be_derived_probe (probe* p, probe_point* l, bool b, int64_t pr):
    derived_probe (p, l), begin (b), priority (pr) {}

  void join_group (systemtap_session& s);

  static inline bool comp(be_derived_probe const *a,
			  be_derived_probe const *b)
  { return a->priority < b->priority; }

  bool needs_global_locks () { return false; }
  // begin/end probes don't need locks around global variables, since
  // they aren't run concurrently with any other probes
};


struct be_derived_probe_group: public generic_dpg<be_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct be_builder: public derived_probe_builder
{
  bool begin;
  be_builder(bool b) : begin(b) {}
  virtual void build(systemtap_session &,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    int64_t priority;
    if ((begin && !get_param(parameters, "begin", priority))
	|| (!begin && !get_param(parameters, "end", priority)))
      priority = 0;
    finished_results.push_back(
	new be_derived_probe(base, location, begin, priority));
  }
};


void
be_derived_probe::join_group (systemtap_session& s)
{
  if (! s.be_derived_probes)
    s.be_derived_probes = new be_derived_probe_group ();
  s.be_derived_probes->enroll (this);
}


// ------------------------------------------------------------------------
void
common_probe_entryfn_prologue (translator_output* o, string statestr,
			       bool overload_processing = true,
                               bool interruptible = false)
{
  o->newline() << "struct context* __restrict__ c;";
  if (! interruptible)
    o->newline() << "unsigned long flags;";

  if (overload_processing)
    o->newline() << "#if defined(STP_TIMING) || defined(STP_OVERLOAD)";
  else
    o->newline() << "#ifdef STP_TIMING";
  o->newline() << "cycles_t cycles_atstart = get_cycles ();";
  o->newline() << "#endif";

#if 0 /* XXX: PERFMON */
  o->newline() << "static struct pfarg_ctx _pfm_context;";
  o->newline() << "static void *_pfm_desc;";
  o->newline() << "static struct pfarg_pmc *_pfm_pmc_x;";
  o->newline() << "static int _pfm_num_pmc_x;";
  o->newline() << "static struct pfarg_pmd *_pfm_pmd_x;";
  o->newline() << "static int _pfm_num_pmd_x;";
#endif

  if (! interruptible)
    o->newline() << "local_irq_save (flags);";
  else
    o->newline() << "preempt_disable ();";

  // Check for enough free enough stack space
  o->newline() << "if (unlikely ((((unsigned long) (& c)) & (THREAD_SIZE-1))"; // free space
  o->newline(1) << "< (MINSTACKSPACE + sizeof (struct thread_info)))) {"; // needed space
  // XXX: may need porting to platforms where task_struct is not at bottom of kernel stack
  // NB: see also CONFIG_DEBUG_STACKOVERFLOW
  o->newline() << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
  o->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  o->newline() << "_stp_exit ();";
  o->newline(-1) << "}";
  o->newline() << "goto probe_epilogue;";
  o->newline(-1) << "}";

  o->newline() << "if (atomic_read (&session_state) != " << statestr << ")";
  o->newline(1) << "goto probe_epilogue;";
  o->indent(-1);

  o->newline() << "c = per_cpu_ptr (contexts, smp_processor_id());";
  o->newline() << "if (unlikely (atomic_inc_return (&c->busy) != 1)) {";
  o->newline(1) << "if (atomic_inc_return (& skipped_count) > MAXSKIPPED) {";
  o->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  // NB: We don't assume that we can safely call stp_error etc. in such
  // a reentrant context.  But this is OK:
  o->newline() << "_stp_exit ();";
  o->newline(-1) << "}";
  o->newline() << "atomic_dec (& c->busy);";
  o->newline() << "goto probe_epilogue;";
  o->newline(-1) << "}";
  o->newline();
  o->newline() << "c->last_error = 0;";
  o->newline() << "c->nesting = 0;";
  o->newline() << "c->regs = 0;";
  o->newline() << "c->pi = 0;";
  o->newline() << "c->probe_point = 0;";
  if (! interruptible)
    o->newline() << "c->actionremaining = MAXACTION;";
  else
    o->newline() << "c->actionremaining = MAXACTION_INTERRUPTIBLE;";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "c->statp = 0;";
  o->newline() << "#endif";
}


void
common_probe_entryfn_epilogue (translator_output* o,
			       bool overload_processing = true,
                               bool interruptible = false)
{
  if (overload_processing)
    o->newline() << "#if defined(STP_TIMING) || defined(STP_OVERLOAD)";
  else
    o->newline() << "#ifdef STP_TIMING";
  o->newline() << "{";
  o->newline(1) << "cycles_t cycles_atend = get_cycles ();";
  // NB: we truncate cycles counts to 32 bits.  Perhaps it should be
  // fewer, if the hardware counter rolls over really quickly.  We
  // handle 32-bit wraparound here.
  o->newline() << "int32_t cycles_elapsed = ((int32_t)cycles_atend > (int32_t)cycles_atstart)";
  o->newline(1) << "? ((int32_t)cycles_atend - (int32_t)cycles_atstart)";
  o->newline() << ": (~(int32_t)0) - (int32_t)cycles_atstart + (int32_t)cycles_atend + 1;";
  o->indent(-1);

  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "if (likely (c->statp)) _stp_stat_add(*c->statp, cycles_elapsed);";
  o->newline() << "#endif";

  if (overload_processing)
    {
      o->newline() << "#ifdef STP_OVERLOAD";
      o->newline() << "{";
      // If the cycle count has wrapped (cycles_atend > cycles_base),
      // let's go ahead and pretend the interval has been reached.
      // This should reset cycles_base and cycles_sum.
      o->newline(1) << "cycles_t interval = (cycles_atend > c->cycles_base)";
      o->newline(1) << "? (cycles_atend - c->cycles_base)";
      o->newline() << ": (STP_OVERLOAD_INTERVAL + 1);";
      o->newline(-1) << "c->cycles_sum += cycles_elapsed;";

      // If we've spent more than STP_OVERLOAD_THRESHOLD cycles in a
      // probe during the last STP_OVERLOAD_INTERVAL cycles, the probe
      // has overloaded the system and we need to quit.
      o->newline() << "if (interval > STP_OVERLOAD_INTERVAL) {";
      o->newline(1) << "if (c->cycles_sum > STP_OVERLOAD_THRESHOLD) {";
      o->newline(1) << "_stp_error (\"probe overhead exceeded threshold\");";
      o->newline() << "atomic_set (&session_state, STAP_SESSION_ERROR);";
      o->newline(-1) << "}";

      o->newline() << "c->cycles_base = cycles_atend;";
      o->newline() << "c->cycles_sum = 0;";
      o->newline(-1) << "}";
      o->newline(-1) << "}";
      o->newline() << "#endif";
    }

  o->newline(-1) << "}";
  o->newline() << "#endif";

  o->newline() << "if (unlikely (c->last_error && c->last_error[0])) {";
  o->newline(1) << "if (c->last_stmt != NULL)";
  o->newline(1) << "_stp_softerror (\"%s near %s\", c->last_error, c->last_stmt);";
  o->newline(-1) << "else";
  o->newline(1) << "_stp_softerror (\"%s\", c->last_error);";
  o->indent(-1);
  o->newline() << "atomic_inc (& error_count);";
  o->newline() << "if (atomic_read (& error_count) > MAXERRORS) {";
  o->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  o->newline() << "_stp_exit ();";
  o->newline(-1) << "}";
  o->newline(-1) << "}";
  o->newline() << "atomic_dec (&c->busy);";

  o->newline(-1) << "probe_epilogue:"; // context is free
  o->indent(1);

  if (! interruptible)
    o->newline() << "local_irq_restore (flags);";
  else
    o->newline() << "preempt_enable_no_resched ();";
}


// ------------------------------------------------------------------------

void
be_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- begin/end probes ---- */";
  s.op->newline() << "void enter_begin_probe (void (*fn)(struct context*)) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_STARTING", false, true);
  s.op->newline() << "c->probe_point = \"begin\";";
  s.op->newline() << "(*fn) (c);";
  common_probe_entryfn_epilogue (s.op, false, true);
  s.op->newline(-1) << "}";
  s.op->newline() << "void enter_end_probe (void (*fn)(struct context*)) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_STOPPING", false, true);
  s.op->newline() << "c->probe_point = \"end\";";
  s.op->newline() << "(*fn) (c);";
  common_probe_entryfn_epilogue (s.op, false, true);
  s.op->newline(-1) << "}";
}

void
be_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  bool have_begin_probes = false;
  sort(probes.begin(), probes.end(), be_derived_probe::comp);
  for (unsigned i=0; i < probes.size (); i++)
    if (probes[i]->begin)
      {
	have_begin_probes = true;
	s.op->newline() << "enter_begin_probe (& " << probes[i]->name << ");";
      }

  // If any of the begin probes signaled an error, indicate
  // failure to the rest of systemtap_module_init.
  if (have_begin_probes)
    s.op->newline() << "rc = (atomic_read (&session_state) == STAP_SESSION_ERROR);";
}

void
be_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  sort(probes.begin(), probes.end(), be_derived_probe::comp);
  for (unsigned i=0; i < probes.size (); i++)
    if (! probes[i]->begin) // note polarity
      s.op->newline() << "enter_end_probe (& " << probes[i]->name << ");";
}



// ------------------------------------------------------------------------
// never probes are never run
// ------------------------------------------------------------------------

struct never_derived_probe: public derived_probe
{
  never_derived_probe (probe* p): derived_probe (p) {}
  never_derived_probe (probe* p, probe_point* l): derived_probe (p, l) {}
  void join_group (systemtap_session&) { /* thus no probe_group */ }
};


struct never_builder: public derived_probe_builder
{
  never_builder() {}
  virtual void build(systemtap_session &,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const &,
		     vector<derived_probe *> & finished_results)
  {
    finished_results.push_back(new never_derived_probe(base, location));
  }
};



// ------------------------------------------------------------------------
//  Dwarf derived probes.  "We apologize for the inconvience."
// ------------------------------------------------------------------------

static string TOK_KERNEL("kernel");
static string TOK_MODULE("module");
static string TOK_FUNCTION("function");
static string TOK_INLINE("inline");
static string TOK_CALL("call");
static string TOK_RETURN("return");
static string TOK_MAXACTIVE("maxactive");
static string TOK_STATEMENT("statement");
static string TOK_ABSOLUTE("absolute");



struct
func_info
{
  func_info()
    : decl_file(NULL), decl_line(-1), prologue_end(0)
  {
    memset(&die, 0, sizeof(die));
  }
  string name;
  char const * decl_file;
  int decl_line;
  Dwarf_Die die;
  Dwarf_Addr prologue_end;
};

struct
inline_instance_info
{
  inline_instance_info()
    : decl_file(NULL), decl_line(-1)
  {
    memset(&die, 0, sizeof(die));
  }
  string name;
  char const * decl_file;
  int decl_line;
  Dwarf_Die die;
};


static int
query_cu (Dwarf_Die * cudie, void * arg);


// Helper for dealing with selected portions of libdwfl in a more readable
// fashion, and with specific cleanup / checking / logging options.

static const char *
dwarf_diename_integrate (Dwarf_Die *die)
{
  Dwarf_Attribute attr_mem;
  return dwarf_formstring (dwarf_attr_integrate (die, DW_AT_name, &attr_mem));
}

struct dwflpp
{
  systemtap_session & sess;
  Dwfl * dwfl;

  // These are "current" values we focus on.
  Dwfl_Module * module;
  Dwarf * module_dwarf;
  Dwarf_Addr module_bias;

  // These describe the current module's PC address range
  Dwarf_Addr module_start;
  Dwarf_Addr module_end;

  Dwarf_Die * cu;
  Dwarf_Die * function;

  string module_name;
  string cu_name;
  string function_name;

  string const default_name(char const * in,
			    char const *)
  {
    if (in)
      return in;
    return string("");
  }


  void get_module_dwarf(bool required = false)
  {
    if (!module_dwarf)
      module_dwarf = dwfl_module_getdwarf(module, &module_bias);

    if (!module_dwarf)
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

  void focus_on_module(Dwfl_Module * m)
  {
    assert(m);
    module = m;
    module_name = default_name(dwfl_module_info(module, NULL,
						&module_start, &module_end,
						NULL, NULL,
						NULL, NULL),
			       "module");

    // Reset existing pointers and names

    module_dwarf = NULL;

    cu_name.clear();
    cu = NULL;

    function_name.clear();
    function = NULL;
  }


  void focus_on_cu(Dwarf_Die * c)
  {
    assert(c);
    assert(module);

    cu = c;
    cu_name = default_name(dwarf_diename(c), "CU");

    // Reset existing pointers and names
    function_name.clear();
    function = NULL;
  }


  void focus_on_function(Dwarf_Die * f)
  {
    assert(f);
    assert(module);
    assert(cu);

    function = f;
    function_name = default_name(dwarf_diename(function),
				 "function");
  }


  void focus_on_module_containing_global_address(Dwarf_Addr a)
  {
    assert(dwfl);
    cu = NULL;
    Dwfl_Module* mod = dwfl_addrmodule(dwfl, a);
    if (mod) // address could be wildly out of range
      focus_on_module(mod);
  }


  void query_cu_containing_global_address(Dwarf_Addr a, void *arg)
  {
    Dwarf_Addr bias;
    assert(dwfl);
    get_module_dwarf();
    Dwarf_Die* cudie = dwfl_module_addrdie(module, a, &bias);
    if (cudie) // address could be wildly out of range
      query_cu (cudie, arg);
    assert(bias == module_bias);
  }


  void query_cu_containing_module_address(Dwarf_Addr a, void *arg)
  {
    query_cu_containing_global_address(module_address_to_global(a), arg);
  }


  Dwarf_Addr module_address_to_global(Dwarf_Addr a)
  {
    assert(dwfl);
    assert(module);
    get_module_dwarf();
    if (module_name == TOK_KERNEL)
      return a;
    return a + module_start;
  }


  Dwarf_Addr global_address_to_module(Dwarf_Addr a)
  {
    assert(module);
    get_module_dwarf();
    return a - module_bias;
  }


  bool module_name_matches(string pattern)
  {
    assert(module);
    bool t = (fnmatch(pattern.c_str(), module_name.c_str(), 0) == 0);
    if (t && sess.verbose>3)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "module '" << module_name << "'" << "\n";
    return t;
  }
  bool module_name_final_match(string pattern)
  {
    // Assume module_name_matches().  Can there be any more matches?
    // Not unless the pattern is a wildcard, since module names are
    // presumed unique.
    return (pattern.find('*') == string::npos &&
            pattern.find('?') == string::npos &&
            pattern.find('[') == string::npos);
  }


  bool function_name_matches(string pattern)
  {
    assert(function);
    bool t = (fnmatch(pattern.c_str(), function_name.c_str(), 0) == 0);
    if (t && sess.verbose>3)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "function '" << function_name << "'" << "\n";
    return t;
  }


  bool cu_name_matches(string pattern)
  {
    assert(cu);
    bool t = (fnmatch(pattern.c_str(), cu_name.c_str(), 0) == 0);
    if (t && sess.verbose>3)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "CU '" << cu_name << "'" << "\n";
    return t;
  }


  // NB: "rc == 0" means OK in this case
  static void dwfl_assert(string desc, int rc, string extra_msg = "")
  {
    string msg = "libdwfl failure (" + desc + "): ";
    if (rc < 0) msg += dwfl_errmsg (rc);
    else if (rc > 0) msg += strerror (rc);
    if (rc != 0)
      {
	if (extra_msg.length() > 0)
	  msg += "\n" + extra_msg;
	throw semantic_error (msg);
      }
  }

  void dwarf_assert(string desc, int rc) // NB: "rc == 0" means OK in this case
  {
    string msg = "libdw failure (" + desc + "): ";
    if (rc < 0) msg += dwarf_errmsg (rc);
    else if (rc > 0) msg += strerror (rc);
    if (rc != 0)
      throw semantic_error (msg);
  }


  dwflpp(systemtap_session & sess)
    :
    sess(sess),
    dwfl(NULL),
    module(NULL),
    module_dwarf(NULL),
    module_bias(0),
    module_start(0),
    module_end(0),
    cu(NULL),
    function(NULL)
  {}


  void setup(bool kernel)
  {
    // XXX: this is where the session -R parameter could come in
    static char debuginfo_path_arr[] = "-:.debug:/usr/lib/debug";
    static char *debuginfo_path = debuginfo_path_arr;

    static const Dwfl_Callbacks proc_callbacks =
      {
	dwfl_linux_proc_find_elf,
	dwfl_standard_find_debuginfo,
	NULL,
        & debuginfo_path
      };

    static const Dwfl_Callbacks kernel_callbacks =
      {
	dwfl_linux_kernel_find_elf,
	dwfl_standard_find_debuginfo,
	dwfl_offline_section_address,
        & debuginfo_path
      };

    if (kernel)
      {
	dwfl = dwfl_begin (&kernel_callbacks);
	if (!dwfl)
	  throw semantic_error ("cannot open dwfl");
	dwfl_report_begin (dwfl);

        dwfl_assert ("missing kernel debuginfo",
                     dwfl_linux_kernel_report_offline 
                     (dwfl,
                      sess.kernel_release.c_str(),
                      NULL /* selection predicate */));

        // XXX: it would be nice if we could do a single
        // ..._report_offline call for an entire systemtap script, so
        // that a selection predicate would filter out modules outside
        // the union of all the requested wildcards.  But we build
        // derived_probes one-by-one and we don't have lookahead.

        // XXX: a special case: if we have only kernel.* probe points,
        // we shouldn't waste time looking for module debug-info (and
        // vice versa).

        // NB: the result of an _offline call is the assignment of
        // virtualized addresses to relocatable objects such as
        // modules.  These have to be converted to real addresses at
        // run time.  See the dwarf_derived_probe ctor and its caller.
      }
    else
      {
	dwfl = dwfl_begin (&proc_callbacks);
	dwfl_report_begin (dwfl);
	if (!dwfl)
	  throw semantic_error ("cannot open dwfl");

        throw semantic_error ("user-space probes not yet implemented");
	// XXX: Find pids or processes, do userspace stuff.
      }

    dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
  }



  // -----------------------------------------------------------------

  struct module_cache_entry {
    Dwfl_Module* mod;
    const char* name;
    Dwarf_Addr addr; 
  };
  typedef vector<module_cache_entry> module_cache_t;
  module_cache_t module_cache;

  static int module_caching_callback(Dwfl_Module * mod, 
                                     void **,
                                     const char *name,
                                     Dwarf_Addr addr,
                                     void *param)
  {
    module_cache_t* cache = static_cast<module_cache_t*>(param);
    module_cache_entry it;
    it.mod = mod;
    it.name = name;
    it.addr = addr;
    cache->push_back (it);
    return DWARF_CB_OK;
  }


  void iterate_over_modules(int (* callback)(Dwfl_Module *, void **,
					     const char *, Dwarf_Addr,
					     void *),
			    void * data)
  {
    if (module_cache.empty())
      {
        ptrdiff_t off = 0;
        do
          {
            off = dwfl_getmodules (dwfl, module_caching_callback,
                                   & module_cache, off);
          }
        while (off > 0);
        dwfl_assert("dwfl_getmodules", off);
      }

    // Traverse the cache.
    for (unsigned i = 0; i < module_cache.size(); i++)
      {
        module_cache_entry& it = module_cache[i];
        int rc = callback (it.mod, 0, it.name, it.addr, data);
        if (rc != DWARF_CB_OK) break;
      }
  }



  // -----------------------------------------------------------------

  typedef map<Dwarf*, vector<Dwarf_Die>*> module_cu_cache_t;
  module_cu_cache_t module_cu_cache;

  void iterate_over_cus (int (*callback)(Dwarf_Die * die, void * arg),
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
            Dwarf_Die die_mem;
            Dwarf_Die *die;
            die = dwarf_offdie (dw, off + cuhl, &die_mem);
            v->push_back (*die); /* copy */
            off = noff;
          }
      }

    for (unsigned i = 0; i < v->size(); i++)
      {
        Dwarf_Die die = v->at(i);
        int rc = (*callback)(& die, data);
        if (rc != DWARF_CB_OK) break;
      }
  }


  // -----------------------------------------------------------------

  bool func_is_inline()
  {
    assert (function);
    return dwarf_func_inline (function) != 0;
  }


  typedef map<string, vector<Dwarf_Die>*> cu_inl_function_cache_t;
  cu_inl_function_cache_t cu_inl_function_cache;

  static int cu_inl_function_caching_callback (Dwarf_Die* func, void *arg)
  {
    vector<Dwarf_Die>* v = static_cast<vector<Dwarf_Die>*>(arg);
    v->push_back (* func);
    return DWARF_CB_OK;
  }

  void iterate_over_inline_instances (int (* callback)(Dwarf_Die * die, void * arg),
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
        Dwarf_Die die = v->at(i);
        int rc = (*callback)(& die, data);
        if (rc != DWARF_CB_OK) break;
      }
  }


  // -----------------------------------------------------------------

  typedef map<string, vector<Dwarf_Die>*> cu_function_cache_t;
  cu_function_cache_t cu_function_cache;

  static int cu_function_caching_callback (Dwarf_Die* func, void *arg)
  {
    vector<Dwarf_Die>* v = static_cast<vector<Dwarf_Die>*>(arg);
    v->push_back (* func);
    return DWARF_CB_OK;
  }

  void iterate_over_functions (int (* callback)(Dwarf_Die * func, void * arg),
			       void * data)
  {
    assert (module);
    assert (cu);

    string key = module_name + ":" + cu_name;
    vector<Dwarf_Die>* v = cu_function_cache[key];
    if (v == 0)
      {
        v = new vector<Dwarf_Die>;
        cu_function_cache[key] = v;
        dwarf_getfuncs (cu, cu_function_caching_callback, v, 0);
      }

    for (unsigned i=0; i<v->size(); i++)
      {
        Dwarf_Die die = v->at(i);
        int rc = (*callback)(& die, data);
        if (rc != DWARF_CB_OK) break;
      }
  }


  bool has_single_line_record (char const * srcfile, int lineno)
  {
    if (lineno < 0)
      return false;

    Dwarf_Line **srcsp = NULL;
    size_t nsrcs = 0;

    dwarf_assert ("dwarf_getsrc_file",
		  dwarf_getsrc_file (module_dwarf,
				     srcfile, lineno, 0,
				     &srcsp, &nsrcs));

    return nsrcs == 1;
  }

  void iterate_over_srcfile_lines (char const * srcfile,
				   int lineno,
				   bool need_single_match,
				   void (* callback) (Dwarf_Line * line, void * arg),
				   void *data)
  {
    Dwarf_Line **srcsp = NULL;
    size_t nsrcs = 0;

    get_module_dwarf();

    dwarf_assert ("dwarf_getsrc_file",
		  dwarf_getsrc_file (module_dwarf,
				     srcfile, lineno, 0,
				     &srcsp, &nsrcs));

    if (need_single_match && nsrcs > 1)
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
	    if (lo_try == -1 && has_single_line_record(srcfile, lineno - i))
	      lo_try = lineno - i;

	    if (hi_try == -1 && has_single_line_record(srcfile, lineno + i))
	      hi_try = lineno + i;
	  }

	string advice = "";
	if (lo_try > 0 || hi_try > 0)
	  advice = " (try "
	    + (lo_try > 0
	       ? (string(srcfile) + ":" + lex_cast<string>(lo_try))
	       : string(""))
	    + (lo_try > 0 && hi_try > 0 ? " or " : "")
	    + (hi_try > 0
	       ? (string(srcfile) + ":"+ lex_cast<string>(hi_try))
	       : string(""))
	    + ")";

	throw semantic_error("multiple addresses for "
			     + string(srcfile)
			     + ":"
			     + lex_cast<string>(lineno)
			     + advice);
      }

    try
      {
	for (size_t i = 0; i < nsrcs; ++i)
	  {
	    callback (srcsp[i], data);
	  }
      }
    catch (...)
      {
	free (srcsp);
	throw;
      }
    free (srcsp);
  }


  void collect_srcfiles_matching (string const & pattern,
				  set<char const *> & filtered_srcfiles)
  {
    assert (module);
    assert (cu);

    size_t nfiles;
    Dwarf_Files *srcfiles;

    dwarf_assert ("dwarf_getsrcfiles",
		  dwarf_getsrcfiles (cu, &srcfiles, &nfiles));
    {
    for (size_t i = 0; i < nfiles; ++i)
      {
	char const * fname = dwarf_filesrc (srcfiles, i, NULL, NULL);
	if (fnmatch (pattern.c_str(), fname, 0) == 0)
	  {
	    filtered_srcfiles.insert (fname);
	    if (sess.verbose>2)
	      clog << "selected source file '" << fname << "'\n";
	  }
      }
    }
  }

  void resolve_prologue_endings (map<Dwarf_Addr, func_info> & funcs)
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

    for(map<Dwarf_Addr,func_info>::iterator it = funcs.begin(); it != funcs.end(); it++)
      {
#if 0 /* someday */
        Dwarf_Addr* bkpts = 0;
        int n = dwarf_entry_breakpoints (& it->second.die, & bkpts);
        // ...
        free (bkpts);
#endif

        Dwarf_Addr entrypc = it->first;
        Dwarf_Addr highpc; // NB: highpc is exclusive: [entrypc,highpc)
        func_info* func = &it->second;
        dwfl_assert ("dwarf_highpc", dwarf_highpc (& func->die,
                                                   & highpc));

        if (func->decl_file == 0) func->decl_file = "";

        unsigned entrypc_srcline_idx = 0;
        Dwarf_Line* entrypc_srcline = 0;
        // open-code binary search for exact match
        {
          unsigned l = 0, h = nlines;
          while (l < h)
            {
              entrypc_srcline_idx = (l + h) / 2;
              Dwarf_Addr addr;
              Dwarf_Line *lr = dwarf_onesrcline(lines, entrypc_srcline_idx);
              dwarf_lineaddr (lr, &addr);
              if (addr == entrypc) { entrypc_srcline = lr; break; }
              else if (l + 1 == h) { break; } // ran off bottom of tree
              else if (addr < entrypc) { l = entrypc_srcline_idx; }
              else { h = entrypc_srcline_idx; }
            }
        }
        if (entrypc_srcline == 0)
          throw semantic_error ("missing entrypc dwarf line record for function '"
                                + func->name + "'");

        if (sess.verbose>2)
          clog << "prologue searching function '" << func->name << "'"
               << " 0x" << hex << entrypc << "-0x" << highpc << dec
               << "@" << func->decl_file << ":" << func->decl_line
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
            Dwarf_Addr postprologue_addr;
            Dwarf_Line *lr = dwarf_onesrcline(lines, postprologue_srcline_idx);
            dwarf_lineaddr (lr, &postprologue_addr);
            const char* postprologue_file = dwarf_linesrc (lr, NULL, NULL);
            int postprologue_lineno;
            dwfl_assert ("dwarf_lineno",
                         dwarf_lineno (lr, & postprologue_lineno));

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
                (strcmp (postprologue_file, func->decl_file) || // We have a winner!
                 (postprologue_lineno != func->decl_line)))
              {
                func->prologue_end = postprologue_addr;

                if (sess.verbose>2)
                  {
                    clog << "prologue found function '" << func->name << "'";
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

        // if (strlen(func->decl_file) == 0) func->decl_file = NULL;

      } // loop over functions

    // XXX: how to free lines?
  }


  bool function_entrypc (Dwarf_Addr * addr)
  {
    assert (function);
    return (dwarf_entrypc (function, addr) == 0);
    // XXX: see also _lowpc ?
  }


  bool die_entrypc (Dwarf_Die * die, Dwarf_Addr * addr)
  {
    Dwarf_Attribute attr_mem;
    Dwarf_Attribute *attr = dwarf_attr (die, DW_AT_entry_pc, &attr_mem);
    if (attr != NULL)
      return (dwarf_formaddr (attr, addr) == 0);

    return (dwarf_lowpc (die, addr) == 0);
    // XXX: see also _entrypc ?
  }

  void function_die (Dwarf_Die *d)
  {
    assert (function);
    *d = *function;
  }

  void function_file (char const ** c)
  {
    assert (function);
    assert (c);
    *c = dwarf_decl_file (function);
  }

  void function_line (int *linep)
  {
    assert (function);
    dwarf_decl_line (function, linep);
  }

  bool die_has_pc (Dwarf_Die * die, Dwarf_Addr pc)
  {
    int res = dwarf_haspc (die, pc);
    if (res == -1)
      dwarf_assert ("dwarf_haspc", res);
    return res == 1;
  }


  static void loc2c_error (void *, const char *fmt, ...)
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
  void emit_address (struct obstack *pool, Dwarf_Addr address)
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
    dwfl_assert ("dwfl_addrmodule", mod == NULL);
    int n = dwfl_module_relocations (mod);
    dwfl_assert ("dwfl_module_relocations", n < 0);
    if (n > 0)
      {
	int i = dwfl_module_relocate_address (mod, &address);
	dwfl_assert ("dwfl_module_relocate_address", i < 0);
	const char *modname = dwfl_module_info (mod, NULL, NULL, NULL,
						NULL, NULL, NULL, NULL);
	dwfl_assert ("dwfl_module_info", modname == NULL);
	const char *secname = dwfl_module_relocation_info (mod, i, NULL);
	dwfl_assert ("dwfl_module_relocation_info", secname == NULL);
	if (n > 1 || secname[0] != '\0')
          {
            // This gives us the module name, and section name within the
            // module, for a kernel module (or other ET_REL module object).
            obstack_printf (pool, "({ static unsigned long addr = 0; ");
            obstack_printf (pool, "if (addr==0) addr = _stp_module_relocate (\"%s\",\"%s\",%#" PRIx64 "); ",
                            modname, secname, address);
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

  static void loc2c_emit_address (void *arg, struct obstack *pool,
				  Dwarf_Addr address)
  {
    dwflpp *dwfl = (dwflpp *) arg;
    dwfl->emit_address (pool, address);
  }

  void print_locals(Dwarf_Die *die, ostream &o)
  {
    // Try to get the first child of die.
    bool local_found = false;
    Dwarf_Die child;
    if (dwarf_child (die, &child) == 0)
      {
	do
	  {
	    // Output each sibling's name (that is a variable or
	    // parameter) to 'o'.
	    switch (dwarf_tag (&child))
	      {
	      case DW_TAG_variable:
	      case DW_TAG_formal_parameter:
		o << " " << dwarf_diename (&child);
		local_found = true;
		break;
	      default:
		break;
	      }
	  }
	while (dwarf_siblingof (&child, &child) == 0);
      }

    if (! local_found)
      o << " (none found)";
  }

  Dwarf_Attribute *
  find_variable_and_frame_base (Dwarf_Die *scope_die,
				Dwarf_Addr pc,
				string const & local,
				Dwarf_Die *vardie,
				Dwarf_Attribute *fb_attr_mem)
  {
    Dwarf_Die *scopes;
    int nscopes = 0;
    Dwarf_Attribute *fb_attr = NULL;

    assert (cu);

    if (scope_die)
      nscopes = dwarf_getscopes_die (scope_die, &scopes);
    else
      nscopes = dwarf_getscopes (cu, pc, &scopes);

    if (nscopes == 0)
      {
	throw semantic_error ("unable to find any scopes containing "
			      + lex_cast_hex<string>(pc)
			      + " while searching for local '" + local + "'");
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
			      + " (alternatives:" + alternatives.str ()
			      + ")");
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
  translate_location(struct obstack *pool,
		     Dwarf_Attribute *attr, Dwarf_Addr pc,
		     Dwarf_Attribute *fb_attr,
		     struct location **tail)
  {
    Dwarf_Op *expr;
    size_t len;

    switch (dwarf_getlocation_addr (attr, pc - module_bias, &expr, &len, 1))
      {
      case 1:			/* Should always happen.  */
	if (len > 0)
	  break;
	/* Fall through.  */

      case 0:			/* Shouldn't happen.  */
	throw semantic_error ("not accessible at this address");

      default:			/* Shouldn't happen.  */
      case -1:
	throw semantic_error (string ("dwarf_getlocation_addr failed") +
			      string (dwarf_errmsg (-1)));
      }

    return c_translate_location (pool, &loc2c_error, this,
				 &loc2c_emit_address,
				 1, module_bias,
				 pc, expr, len, tail, fb_attr);
  }

  void
  print_members(Dwarf_Die *vardie, ostream &o)
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
	const char *member = (dwarf_diename_integrate (die) ?: "<anonymous>");
	
	o << " " << member;

	if (dwarf_siblingof (die, &die_mem) != 0)
	  break;
      }
  }

  Dwarf_Die *
  translate_components(struct obstack *pool,
		       struct location **tail,
		       Dwarf_Addr pc,
		       vector<pair<target_symbol::component_type,
		       std::string> > const & components,
		       Dwarf_Die *vardie,
		       Dwarf_Die *die_mem,
		       Dwarf_Attribute *attr_mem)
  {
    Dwarf_Die *die = vardie;
    Dwarf_Die struct_die;
    unsigned i = 0;
    while (i < components.size())
      {
	die = dwarf_formref_die (attr_mem, die_mem);
	const int typetag = dwarf_tag (die);
	switch (typetag)
	  {
	  case DW_TAG_typedef:
	  case DW_TAG_const_type:
	  case DW_TAG_volatile_type:
	    /* Just iterate on the referent type.  */
	    break;

	  case DW_TAG_pointer_type:
	    if (components[i].first == target_symbol::comp_literal_array_index)
              throw semantic_error ("cannot index pointer");
            // XXX: of course, we should support this the same way C does,
            // by explicit pointer arithmetic etc.

	    c_translate_pointer (pool, 1, module_bias, die, tail);
	    break;

	  case DW_TAG_array_type:
	    if (components[i].first == target_symbol::comp_literal_array_index)
	      {
		c_translate_array (pool, 1, module_bias, die, tail,
				   NULL, lex_cast<Dwarf_Word>(components[i].second));
		++i;
	      }
	    else
	      throw semantic_error("bad field '"
				   + components[i].second
				   + "' for array type");
	    break;

	  case DW_TAG_structure_type:
	  case DW_TAG_union_type:
	    struct_die = *die;
	    switch (dwarf_child (die, die_mem))
	      {
	      case 1:		/* No children.  */
		throw semantic_error ("empty struct "
				      + string (dwarf_diename_integrate (die) ?: "<anonymous>"));
		break;
	      case -1:		/* Error.  */
	      default:		/* Shouldn't happen */
		throw semantic_error (string (typetag == DW_TAG_union_type ? "union" : "struct")
				      + string (dwarf_diename_integrate (die) ?: "<anonymous>")
				      + string (dwarf_errmsg (-1)));
		break;

	      case 0:
		break;
	      }

	    while (dwarf_tag (die) != DW_TAG_member
		   || ({ const char *member = dwarf_diename_integrate (die);
		       member == NULL || string(member) != components[i].second; }))
	      if (dwarf_siblingof (die, die_mem) != 0)
	      {
		  stringstream alternatives;
		  print_members (&struct_die, alternatives);
		  throw semantic_error ("field '" + components[i].second
					+ "' not found (alternatives:"
					+ alternatives.str () + ")");
	      }

	    if (dwarf_attr_integrate (die, DW_AT_data_member_location,
				      attr_mem) == NULL)
	      {
		/* Union members don't usually have a location,
		   but just use the containing union's location.  */
		if (typetag != DW_TAG_union_type)
		  throw semantic_error ("no location for field '"
					+ components[i].second
					+ "' :" + string(dwarf_errmsg (-1)));
	      }
	    else
	      translate_location (pool, attr_mem, pc, NULL, tail);
	    ++i;
	    break;

	  case DW_TAG_base_type:
	    throw semantic_error ("field '"
				  + components[i].second
				  + "' vs. base type "
				  + string(dwarf_diename_integrate (die) ?: "<anonymous type>"));
	    break;
	  case -1:
	    throw semantic_error ("cannot find type: " + string(dwarf_errmsg (-1)));
	    break;

	  default:
	    throw semantic_error (string(dwarf_diename_integrate (die) ?: "<anonymous type>")
				  + ": unexpected type tag "
				  + lex_cast<string>(dwarf_tag (die)));
	    break;
	  }

	/* Now iterate on the type in DIE's attribute.  */
	if (dwarf_attr_integrate (die, DW_AT_type, attr_mem) == NULL)
	  throw semantic_error ("cannot get type of field: " + string(dwarf_errmsg (-1)));
      }
    return die;
  }


  Dwarf_Die *
  resolve_unqualified_inner_typedie (Dwarf_Die *typedie_mem,
				     Dwarf_Attribute *attr_mem)
  {
    ;
    Dwarf_Die *typedie;
    int typetag = 0;
    while (1)
      {
	typedie = dwarf_formref_die (attr_mem, typedie_mem);
	if (typedie == NULL)
	  throw semantic_error ("cannot get type: " + string(dwarf_errmsg (-1)));
	typetag = dwarf_tag (typedie);
	if (typetag != DW_TAG_typedef &&
	    typetag != DW_TAG_const_type &&
	    typetag != DW_TAG_volatile_type)
	  break;
	if (dwarf_attr_integrate (typedie, DW_AT_type, attr_mem) == NULL)
	  throw semantic_error ("cannot get type of pointee: " + string(dwarf_errmsg (-1)));
      }
    return typedie;
  }


  void
  translate_final_fetch_or_store (struct obstack *pool,
				  struct location **tail,
				  Dwarf_Addr module_bias,
				  Dwarf_Die *die,
				  Dwarf_Attribute *attr_mem,
				  bool lvalue,
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

    typedie = resolve_unqualified_inner_typedie (&typedie_mem, attr_mem);
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
			      + " for " + diestr);
	break;

      case DW_TAG_structure_type:
      case DW_TAG_union_type:
	dname = dwarf_diename(die);
	diestr = (dname != NULL) ? dname : "<unknown>";
	throw semantic_error ("struct/union '" + diestr
			      + "' is being accessed instead of a member of the struct/union");
	break;

      case DW_TAG_enumeration_type:
      case DW_TAG_base_type:
	ty = pe_long;
	if (lvalue)
	  c_translate_store (pool, 1, module_bias, die, typedie, tail,
			     "THIS->value");
	else
	  c_translate_fetch (pool, 1, module_bias, die, typedie, tail,
			     "THIS->__retvalue");
	break;

      case DW_TAG_array_type:
      case DW_TAG_pointer_type:

	if (lvalue)
	  throw semantic_error ("cannot store into target pointer value");

	{
	  Dwarf_Die pointee_typedie_mem;
	  Dwarf_Die *pointee_typedie;
	  Dwarf_Word pointee_encoding;
	  Dwarf_Word pointee_byte_size = 0;

	  pointee_typedie = resolve_unqualified_inner_typedie (&pointee_typedie_mem, attr_mem);

	  if (dwarf_attr_integrate (pointee_typedie, DW_AT_byte_size, attr_mem))
	    dwarf_formudata (attr_mem, &pointee_byte_size);

	  dwarf_formudata (dwarf_attr_integrate (pointee_typedie, DW_AT_encoding, attr_mem),
			   &pointee_encoding);

	  // We have the pointer: cast it to an integral type via &(*(...))

	  // NB: per bug #1187, at one point char*-like types were
	  // automagically converted here to systemtap string values.
	  // For several reasons, this was taken back out, leaving
	  // pointer-to-string "conversion" (copying) to tapset functions.

	  ty = pe_long;
	  if (typetag == DW_TAG_array_type)
	    c_translate_array (pool, 1, module_bias, typedie, tail, NULL, 0);
	  else
	    c_translate_pointer (pool, 1, module_bias, typedie, tail);
	  c_translate_addressof (pool, 1, module_bias, NULL, pointee_typedie, tail,
				 "THIS->__retvalue");
	}
	break;
      }
  }

  string
  express_as_string (string prelude,
		     string postlude,
		     struct location *head)
  {
    size_t bufsz = 1024;
    char *buf = static_cast<char*>(malloc(bufsz));
    assert(buf);

    FILE *memstream = open_memstream (&buf, &bufsz);
    assert(memstream);

    fprintf(memstream, "{\n");
    fprintf(memstream, prelude.c_str());
    bool deref = c_emit_location (memstream, head, 1);
    fprintf(memstream, postlude.c_str());
    fprintf(memstream, "  goto out;\n");

    // dummy use of deref_fault label, to disable warning if deref() not used
    fprintf(memstream, "if (0) goto deref_fault;\n");

    // XXX: deref flag not reliable; emit fault label unconditionally
    // XXX: print the faulting address, like the user_string/kernel_string
    // tapset functions do
    (void) deref;
    fprintf(memstream,
            "deref_fault:\n"
            "  c->last_error = \"pointer dereference fault\";\n"
            "  goto out;\n");
    fprintf(memstream, "}\n");

    fclose (memstream);
    string result(buf);
    free (buf);
    return result;
  }

  string
  literal_stmt_for_local (Dwarf_Die *scope_die,
			  Dwarf_Addr pc,
			  string const & local,
			  vector<pair<target_symbol::component_type,
			  std::string> > const & components,
			  bool lvalue,
			  exp_type & ty)
  {
    Dwarf_Die vardie;
    Dwarf_Attribute fb_attr_mem, *fb_attr = NULL;

    fb_attr = find_variable_and_frame_base (scope_die, pc, local,
					    &vardie, &fb_attr_mem);

    if (sess.verbose>2)
      clog << "finding location for local '" << local
	   << "' near address " << hex << pc
	   << ", module bias " << module_bias << dec
	   << "\n";

    Dwarf_Attribute attr_mem;
    if (dwarf_attr_integrate (&vardie, DW_AT_location, &attr_mem) == NULL)
      {
	throw semantic_error("failed to retrieve location "
			     "attribute for local '" + local
			     + "' (dieoffset: "
			     + lex_cast_hex<string>(dwarf_dieoffset (&vardie))
			     + ")");
      }

#define obstack_chunk_alloc malloc
#define obstack_chunk_free free

    struct obstack pool;
    obstack_init (&pool);
    struct location *tail = NULL;

    /* Given $foo->bar->baz[NN], translate the location of foo. */

    struct location *head = translate_location (&pool,
						&attr_mem, pc, fb_attr, &tail);

    if (dwarf_attr_integrate (&vardie, DW_AT_type, &attr_mem) == NULL)
      throw semantic_error("failed to retrieve type "
			   "attribute for local '" + local + "'");


    /* Translate the ->bar->baz[NN] parts. */

    Dwarf_Die die_mem, *die = NULL;
    die = translate_components (&pool, &tail, pc, components,
				&vardie, &die_mem, &attr_mem);

    /* Translate the assignment part, either
       x = $foo->bar->baz[NN]
       or
       $foo->bar->baz[NN] = x
    */

    string prelude, postlude;
    translate_final_fetch_or_store (&pool, &tail, module_bias,
				    die, &attr_mem, lvalue,
				    prelude, postlude, ty);

    /* Write the translation to a string. */
    return express_as_string(prelude, postlude, head);
  }


  string
  literal_stmt_for_return (Dwarf_Die *scope_die,
			   Dwarf_Addr pc,
			   vector<pair<target_symbol::component_type,
			   std::string> > const & components,
			   bool lvalue,
			   exp_type & ty)
  {
    if (sess.verbose>2)
	clog << "literal_stmt_for_return: finding return value for "
	     << dwarf_diename (scope_die)
	     << "("
	     << dwarf_diename (cu)
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
	throw semantic_error("failed to retrieve return value location");
      }
    // the function has no return value (e.g. "void" in C)
    else if (nlocops == 0)
      {
	throw semantic_error("function has no return value");
      }

    struct location  *head = c_translate_location (&pool, &loc2c_error, this,
						   &loc2c_emit_address,
						   1, module_bias,
						   pc, locops, nlocops,
						   &tail, NULL);

    /* Translate the ->bar->baz[NN] parts. */

    Dwarf_Attribute attr_mem;
    Dwarf_Attribute *attr = dwarf_attr (scope_die, DW_AT_type, &attr_mem);

    Dwarf_Die vardie_mem;
    Dwarf_Die *vardie = dwarf_formref_die (attr, &vardie_mem);

    Dwarf_Die die_mem, *die = NULL;
    die = translate_components (&pool, &tail, pc, components,
				vardie, &die_mem, &attr_mem);

    /* Translate the assignment part, either
       x = $return->bar->baz[NN]
       or
       $return->bar->baz[NN] = x
    */

    string prelude, postlude;
    translate_final_fetch_or_store (&pool, &tail, module_bias,
				    die, &attr_mem, lvalue,
				    prelude, postlude, ty);

    /* Write the translation to a string. */
    return express_as_string(prelude, postlude, head);
  }


  ~dwflpp()
  {
    if (dwfl)
      dwfl_end(dwfl);
  }
};


enum
function_spec_type
  {
    function_alone,
    function_and_file,
    function_file_and_line
  };


struct dwarf_builder;
struct dwarf_query;


// XXX: This class is a candidate for subclassing to separate
// the relocation vs non-relocation variants.  Likewise for
// kprobe vs kretprobe variants.

struct dwarf_derived_probe: public derived_probe
{
  dwarf_derived_probe (const string& function,
                       const string& filename,
                       int line,
                       const string& module,
                       const string& section,
		       Dwarf_Addr dwfl_addr,
		       Dwarf_Addr addr,
		       dwarf_query & q,
                       Dwarf_Die* scope_die);

  string module;
  string section;
  Dwarf_Addr addr;
  bool has_return;
  bool has_maxactive;
  long maxactive_val;

  void printsig (std::ostream &o) const;

  void join_group (systemtap_session& s);

  // Pattern registration helpers.
  static void register_statement_variants(match_node * root,
					  dwarf_builder * dw);
  static void register_function_variants(match_node * root,
					  dwarf_builder * dw);
  static void register_function_and_statement_variants(match_node * root,
						       dwarf_builder * dw);
  static void register_patterns(match_node * root);
};


struct dwarf_derived_probe_group: public derived_probe_group
{
private:
  multimap<string,dwarf_derived_probe*> probes_by_module;
  typedef multimap<string,dwarf_derived_probe*>::iterator p_b_m_iterator;

public:
  void enroll (dwarf_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


// Helper struct to thread through the dwfl callbacks.
struct dwarf_query
{
  dwarf_query(systemtap_session & sess,
	      probe * base_probe,
	      probe_point * base_loc,
	      dwflpp & dw,
	      map<string, literal *> const & params,
	      vector<derived_probe *> & results);

  systemtap_session & sess;

  // Parameter extractors.
  static bool has_null_param(map<string, literal *> const & params,
			     string const & k);
  static bool get_string_param(map<string, literal *> const & params,
			       string const & k, string & v);
  static bool get_number_param(map<string, literal *> const & params,
			       string const & k, long & v);
  static bool get_number_param(map<string, literal *> const & params,
			       string const & k, Dwarf_Addr & v);

  // Result vector
  vector<derived_probe *> & results;
  void add_probe_point(string const & funcname,
		       char const * filename,
		       int line,
		       Dwarf_Die *scope_die,
		       Dwarf_Addr addr);

  set<string> blacklisted_probes;
  set<string> blacklisted_return_probes;
  void build_blacklist();

  bool blacklisted_p(const string& funcname,
                     const string& filename,
                     int line,
                     const string& module,
                     const string& section,
                     Dwarf_Addr addr);

  // Extracted parameters.
  bool has_kernel;
  string module_val; // has_kernel => module_val = "kernel"
  string function_val;

  bool has_function_str;
  bool has_statement_str;
  bool has_function_num;
  bool has_statement_num;
  string statement_str_val;
  string function_str_val;
  Dwarf_Addr statement_num_val;
  Dwarf_Addr function_num_val;

  bool has_call;
  bool has_inline;
  bool has_return;

  bool has_maxactive;
  long maxactive_val;

  bool has_label;
  string label_val;

  bool has_relative;
  long relative_val;

  bool has_absolute;

  function_spec_type parse_function_spec(string & spec);
  function_spec_type spec_type;
  string function;
  string file;
  int line;

  set<char const *> filtered_srcfiles;

  // Map official entrypc -> func_info object
  map<Dwarf_Addr, inline_instance_info> filtered_inlines;
  map<Dwarf_Addr, func_info> filtered_functions;
  bool choose_next_line;
  Dwarf_Addr entrypc_for_next_line;

  probe * base_probe;
  probe_point * base_loc;
  dwflpp & dw;
};


struct dwarf_builder: public derived_probe_builder
{
  dwflpp *kern_dw;
  dwarf_builder(): kern_dw(0) {}

  void build_no_more (systemtap_session &s)
  {
    if (kern_dw)
      {
        if (s.verbose > 3)
          clog << "dwarf_builder releasing dwflpp" << endl;
        delete kern_dw;
        kern_dw = 0;
      }
  }

  ~dwarf_builder()
  {
    // XXX: in practice, NOTREACHED
    delete kern_dw;
  }

  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<derived_probe *> & finished_results);
};

bool
dwarf_query::has_null_param(map<string, literal *> const & params,
			    string const & k)
{
  map<string, literal *>::const_iterator i = params.find(k);
  if (i != params.end() && i->second == NULL)
    return true;
  return false;
}

bool
dwarf_query::get_string_param(map<string, literal *> const & params,
			      string const & k, string & v)
{
  return derived_probe_builder::get_param (params, k, v);
}

bool
dwarf_query::get_number_param(map<string, literal *> const & params,
			      string const & k, long & v)
{
  int64_t value;
  bool present = derived_probe_builder::get_param (params, k, value);
  v = (long) value;
  return present;
}

bool
dwarf_query::get_number_param(map<string, literal *> const & params,
			      string const & k, Dwarf_Addr & v)
{
  int64_t value;
  bool present = derived_probe_builder::get_param (params, k, value);
  v = (Dwarf_Addr) value;
  return present;
}


dwarf_query::dwarf_query(systemtap_session & sess,
			 probe * base_probe,
			 probe_point * base_loc,
			 dwflpp & dw,
			 map<string, literal *> const & params,
			 vector<derived_probe *> & results)
  : sess(sess),
    results(results),
    base_probe(base_probe),
    base_loc(base_loc),
    dw(dw)
{

  // Reduce the query to more reasonable semantic values (booleans,
  // extracted strings, numbers, etc).

  has_kernel = has_null_param(params, TOK_KERNEL);
  if (has_kernel)
    module_val = "kernel";
  else 
    {
      bool has_module = get_string_param(params, TOK_MODULE, module_val);
      assert (has_module); // no other options are possible by construction
    }

  has_function_str = get_string_param(params, TOK_FUNCTION, function_str_val);
  has_function_num = get_number_param(params, TOK_FUNCTION, function_num_val);

  has_statement_str = get_string_param(params, TOK_STATEMENT, statement_str_val);
  has_statement_num = get_number_param(params, TOK_STATEMENT, statement_num_val);

  has_call = has_null_param(params, TOK_CALL);
  has_inline = has_null_param(params, TOK_INLINE);
  has_return = has_null_param(params, TOK_RETURN);
  has_maxactive = get_number_param(params, TOK_MAXACTIVE, maxactive_val);
  has_absolute = has_null_param(params, TOK_ABSOLUTE);

  if (has_function_str)
    spec_type = parse_function_spec(function_str_val);
  else if (has_statement_str)
    spec_type = parse_function_spec(statement_str_val);

  build_blacklist(); // XXX: why not reuse amongst dwarf_query instances?
}


void
dwarf_query::build_blacklist()
{
  // FIXME: it would be nice if these blacklisted functions were pulled in
  // dynamically, instead of being statically defined here.

  // Most of these are marked __kprobes in newer kernels.  We list
  // them here so the translator can block them on older kernels that
  // don't have the __kprobes function decorator.  This also allows
  // detection of problems at translate- rather than run-time.
  blacklisted_probes.insert("atomic_notifier_call_chain");
  blacklisted_probes.insert("default_do_nmi");
  blacklisted_probes.insert("__die");
  blacklisted_probes.insert("die_nmi");
  blacklisted_probes.insert("do_debug");
  blacklisted_probes.insert("do_general_protection");
  blacklisted_probes.insert("do_int3");
  blacklisted_probes.insert("do_IRQ");
  blacklisted_probes.insert("do_page_fault");
  blacklisted_probes.insert("do_sparc64_fault");
  blacklisted_probes.insert("do_trap");
  blacklisted_probes.insert("dummy_nmi_callback");
  blacklisted_probes.insert("flush_icache_range");
  blacklisted_probes.insert("ia64_bad_break");
  blacklisted_probes.insert("ia64_do_page_fault");
  blacklisted_probes.insert("ia64_fault");
  blacklisted_probes.insert("io_check_error");
  blacklisted_probes.insert("mem_parity_error");
  blacklisted_probes.insert("nmi_watchdog_tick");
  blacklisted_probes.insert("notifier_call_chain");
  blacklisted_probes.insert("oops_begin");
  blacklisted_probes.insert("oops_end");
  blacklisted_probes.insert("program_check_exception");
  blacklisted_probes.insert("single_step_exception");
  blacklisted_probes.insert("sync_regs");
  blacklisted_probes.insert("unhandled_fault");
  blacklisted_probes.insert("unknown_nmi_error");

  blacklisted_probes.insert("_read_trylock");
  blacklisted_probes.insert("_read_lock");
  blacklisted_probes.insert("_read_unlock");
  blacklisted_probes.insert("_write_trylock");
  blacklisted_probes.insert("_write_lock");
  blacklisted_probes.insert("_write_unlock");
  blacklisted_probes.insert("_spin_lock");
  blacklisted_probes.insert("_spin_lock_irqsave");
  blacklisted_probes.insert("_spin_trylock");
  blacklisted_probes.insert("_spin_unlock");
  blacklisted_probes.insert("_spin_unlock_irqrestore");

  // __switch_to is only disallowed on x86_64
  if (sess.architecture == "x86_64")
    blacklisted_probes.insert("__switch_to");

  // These functions don't return, so return probes would never be recovered
  blacklisted_return_probes.insert("do_exit");
  blacklisted_return_probes.insert("sys_exit");
  blacklisted_return_probes.insert("sys_exit_group");
}


function_spec_type
dwarf_query::parse_function_spec(string & spec)
{
  string::const_iterator i = spec.begin(), e = spec.end();

  function.clear();
  file.clear();
  line = 0;

  while (i != e && *i != '@')
    {
      if (*i == ':')
	goto bad;
      function += *i++;
    }

  if (i == e)
    {
      if (sess.verbose>2)
	clog << "parsed '" << spec
	     << "' -> func '" << function
	     << "'\n";
      return function_alone;
    }

  if (i++ == e)
    goto bad;

  while (i != e && *i != ':')
    file += *i++;

  if (i == e)
    {
      if (sess.verbose>2)
	clog << "parsed '" << spec
	     << "' -> func '"<< function
	     << "', file '" << file
	     << "'\n";
      return function_and_file;
    }

  if (i++ == e)
    goto bad;

  try
    {
      line = lex_cast<int>(string(i, e));
      if (sess.verbose>2)
	clog << "parsed '" << spec
	     << "' -> func '"<< function
	     << "', file '" << file
	     << "', line " << line << "\n";
      return function_file_and_line;
    }
  catch (runtime_error & exn)
    {
      goto bad;
    }

 bad:
    throw semantic_error("malformed specification '" + spec + "'",
			 base_probe->tok);
}


// Forward declaration.
static int query_kernel_module (Dwfl_Module *, void **, const char *,
				Dwarf_Addr, void *);


// XXX: pull this into dwflpp
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
dwarf_query::blacklisted_p(const string& funcname,
                           const string& filename,
                           int,
                           const string& module,
                           const string& section,
                           Dwarf_Addr addr)
{
  if (section.substr(0, 6) == string(".init.") ||
      section.substr(0, 6) == string(".exit."))
    {
      // NB: module .exit. routines could be probed in theory:
      // if the exit handler in "struct module" is diverted,
      // first inserting the kprobes
      // then allowing the exit code to run
      // then removing these kprobes
      if (sess.verbose>1)
        clog << " skipping - init/exit";
      return true;
    }

  // Check for function marked '__kprobes'.
  if (module == TOK_KERNEL && in_kprobes_function(sess, addr))
    {
      if (sess.verbose>1)
	clog << " skipping - __kprobes";
      return true;
    }
  
  // Check probe point against blacklist.  XXX: This has to be
  // properly generalized, perhaps via a table populated from script
  // files.  A "noprobe kernel.function("...")"  construct might do
  // the trick.
  if (blacklisted_probes.count(funcname) > 0 ||
      (has_return && blacklisted_return_probes.count(funcname) > 0) ||
      filename == "kernel/kprobes.c" ||
      0 == fnmatch ("arch/*/kernel/kprobes.c", filename.c_str(), 0))
    // XXX: these tests (set lookup, fnmatch) could be combined into a
    // single synthetic compiled regexp, which would allow blacklisted
    // functions to be identified by wildcard instead of exact name.
    {
      if (sess.verbose>1)
	clog << " skipping - blacklisted";
      return true;
    }

  // This probe point is not blacklisted.
  return false;
}



void
dwarf_query::add_probe_point(const string& funcname,
			     const char* filename,
			     int line,
			     Dwarf_Die* scope_die,
			     Dwarf_Addr addr)
{
  dwarf_derived_probe *probe = NULL;
  string reloc_section; // base section for relocation purposes
  Dwarf_Addr reloc_addr = addr; // relocated
  string blacklist_section; // linking section for blacklist purposes
  const string& module = dw.module_name; // "kernel" or other

  assert (! has_absolute); // already handled in dwarf_builder::build()

  if (dwfl_module_relocations (dw.module) > 0)
    {
      // This is arelocatable module; libdwfl already knows its
      // sections, so we can relativize addr.
      int idx = dwfl_module_relocate_address (dw.module, &reloc_addr);
      const char* r_s = dwfl_module_relocation_info (dw.module, idx, NULL);
      if (r_s)
        reloc_section = r_s;
      blacklist_section = reloc_section;
    }
  else
    {
      // This is not a relocatable module, so addr is all set.  To
      // find the section name, must do this the long way - scan
      // through elf section headers.
      Dwarf_Addr baseaddr;
      Elf* elf = dwfl_module_getelf (dw.module, & baseaddr);
      Dwarf_Addr offset = addr - baseaddr;
      // NB: this offset does not end up as reloc_addr, since the latter is
      // only computed differently if load-time relocation is needed.  For
      // non-relocatable modules, this is not the case.
      if (elf)
        {
          // Iterate through section headers to find which one
          // contains the given offset.
          Elf_Scn* scn = 0;
          size_t shstrndx;
          dw.dwfl_assert ("getshstrndx", elf_getshstrndx (elf, &shstrndx));
          while ((scn = elf_nextscn (elf, scn)) != NULL)
            {
              GElf_Shdr shdr_mem;
              GElf_Shdr *shdr = gelf_getshdr (scn, &shdr_mem);
              if (! shdr) continue; // XXX error?

              // check for address inclusion
              GElf_Addr start = shdr->sh_addr;
              GElf_Addr end = start + shdr->sh_size;
              if (! (offset >= start && offset < end))
                continue;

              // check for section name
              blacklist_section =  elf_strptr (elf, shstrndx, shdr->sh_name);
              break;
            }
        }

      reloc_section = "";
    }

  if (sess.verbose > 1)
    {
      clog << "probe " << funcname << "@" << filename << ":" << line;
      if (string(module) == TOK_KERNEL)
        clog << " kernel";
      else
        clog << " module=" << module;
      if (reloc_section != "") clog << " reloc=" << reloc_section;
      if (blacklist_section != "") clog << " section=" << blacklist_section;
      clog << " pc=0x" << hex << addr << dec;
    }
  
  bool bad = blacklisted_p (funcname, filename, line, module, blacklist_section, addr);
  if (sess.verbose > 1)
    clog << endl;

  if (module == TOK_KERNEL)
    {
      // PR 4224: adapt to relocatable kernel by subtracting the _stext address here.
      reloc_addr = addr - sess.sym_stext;
      reloc_section = "_stext"; // a message to runtime's _stp_module_relocate
    }

  if (! bad)
    {
      probe = new dwarf_derived_probe(funcname, filename, line, 
                                      module, reloc_section, addr, reloc_addr, *this, scope_die);
      results.push_back(probe);
    }
}




// The critical determining factor when interpreting a pattern
// string is, perhaps surprisingly: "presence of a lineno". The
// presence of a lineno changes the search strategy completely.
//
// Compare the two cases:
//
//   1. {statement,function}(foo@file.c:lineno)
//      - find the files matching file.c
//      - in each file, find the functions matching foo
//      - query the file for line records matching lineno
//      - iterate over the line records,
//        - and iterate over the functions,
//          - if(haspc(function.DIE, line.addr))
//            - if looking for statements: probe(lineno.addr)
//            - if looking for functions: probe(function.{entrypc,return,etc.})
//
//   2. {statement,function}(foo@file.c)
//      - find the files matching file.c
//      - in each file, find the functions matching foo
//        - probe(function.{entrypc,return,etc.})
//
// Thus the first decision we make is based on the presence of a
// lineno, and we enter entirely different sets of callbacks
// depending on that decision.
//
// Note that the first case is a generalization fo the second, in that
// we could theoretically search through line records for matching
// file names (a "table scan" in rdbms lingo).  Luckily, file names
// are already cached elsewhere, so we can do an "index scan" as an
// optimization.

static void
query_statement (string const & func,
		 char const * file,
		 int line,
		 Dwarf_Die *scope_die,
		 Dwarf_Addr stmt_addr,
		 dwarf_query * q)
{
  try
    {
      q->add_probe_point(func, file ? file : "?",
                         line, scope_die, stmt_addr);
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
    }
}

static void
query_inline_instance_info (Dwarf_Addr entrypc,
			    inline_instance_info & ii,
			    dwarf_query * q)
{
  try
    {
      if (q->has_return)
	{
	  throw semantic_error ("cannot probe .return of inline function '" + ii.name + "'");
	}
      else
	{
	  if (q->sess.verbose>2)
	    clog << "querying entrypc "
		 << hex << entrypc << dec
		 << " of instance of inline '" << ii.name << "'\n";
	  query_statement (ii.name, ii.decl_file, ii.decl_line,
			   &ii.die, entrypc, q);
	}
    }
  catch (semantic_error &e)
    {
      q->sess.print_error (e);
    }
}

static void
query_func_info (Dwarf_Addr entrypc,
		 func_info & fi,
		 dwarf_query * q)
{
  try
    {
      if (q->has_return)
	{
	  // NB. dwarf_derived_probe::emit_registrations will emit a
	  // kretprobe based on the entrypc in this case.
	  query_statement (fi.name, fi.decl_file, fi.decl_line,
			   &fi.die, entrypc, q);
	}
      else
	{
          if (q->sess.prologue_searching)
            {
              if (fi.prologue_end == 0)
                throw semantic_error("could not find prologue-end "
                                     "for probed function '" + fi.name + "'");
              query_statement (fi.name, fi.decl_file, fi.decl_line,
                               &fi.die, fi.prologue_end, q);
            }
          else
            {
              query_statement (fi.name, fi.decl_file, fi.decl_line,
                               &fi.die, entrypc, q);
            }
	}
    }
  catch (semantic_error &e)
    {
      q->sess.print_error (e);
    }
}


static void
query_srcfile_line (Dwarf_Line * line, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);

  Dwarf_Addr addr;
  dwarf_lineaddr(line, &addr);

  for (map<Dwarf_Addr, func_info>::iterator i = q->filtered_functions.begin();
       i != q->filtered_functions.end(); ++i)
    {
      if (q->dw.die_has_pc (&(i->second.die), addr))
	{
	  if (q->sess.verbose>3)
	    clog << "function DIE lands on srcfile\n";
	  if (q->has_statement_str)
	    query_statement (i->second.name, i->second.decl_file,
			     q->line, NULL, addr, q);
	  else
	    query_func_info (i->first, i->second, q);
	}
    }

  for (map<Dwarf_Addr, inline_instance_info>::iterator i
	 = q->filtered_inlines.begin();
       i != q->filtered_inlines.end(); ++i)
    {
      if (q->dw.die_has_pc (&(i->second.die), addr))
	{
	  if (q->sess.verbose>3)
	    clog << "inline instance DIE lands on srcfile\n";
	  if (q->has_statement_str)
	    query_statement (i->second.name, i->second.decl_file,
			     q->line, NULL, addr, q);
	  else
	    query_inline_instance_info (i->first, i->second, q);
	}
    }
}


static int
query_dwarf_inline_instance (Dwarf_Die * die, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);
  assert (!q->has_statement_num);

  try
    {
      if (q->sess.verbose>2)
	clog << "examining inline instance of " << q->dw.function_name << "\n";

      if ((q->has_function_str && ! q->has_call) 
          || q->has_statement_str)
	{
	  if (q->sess.verbose>2)
	    clog << "selected inline instance of " << q->dw.function_name
                 << "\n";

	  Dwarf_Addr entrypc;
	  if (q->dw.die_entrypc (die, &entrypc))
	    {
	      inline_instance_info inl;
	      inl.die = *die;
	      inl.name = q->dw.function_name;
	      q->dw.function_file (&inl.decl_file);
	      q->dw.function_line (&inl.decl_line);
	      q->filtered_inlines[entrypc] = inl;
	    }
	}
      return DWARF_CB_OK;
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
      return DWARF_CB_ABORT;
    }
}

static int
query_dwarf_func (Dwarf_Die * func, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);
  assert (!q->has_statement_num);

  try
    {
      q->dw.focus_on_function (func);

      if (q->dw.func_is_inline ()
          && (! q->has_call) && (! q->has_return)
	  && (((q->has_statement_str || q->has_function_str)
	       && q->dw.function_name_matches(q->function))))
	{
	  if (q->sess.verbose>3)
	    clog << "checking instances of inline " << q->dw.function_name
                 << "\n";
	  q->dw.iterate_over_inline_instances (query_dwarf_inline_instance, arg);
	}
      else if (!q->dw.func_is_inline () && (! q->has_inline))
	{
	  bool record_this_function = false;

	  if ((q->has_statement_str || q->has_function_str)
	      && q->dw.function_name_matches(q->function))
	    {
	      record_this_function = true;
	    }
	  else if (q->has_function_num)
	    {
	      Dwarf_Addr query_addr = q->function_num_val;
              query_addr = q->dw.module_address_to_global(query_addr);

	      Dwarf_Die d;
	      q->dw.function_die (&d);

	      if (q->dw.die_has_pc (&d, query_addr))
		record_this_function = true;
	    }

	  if (record_this_function)
	    {
	      if (q->sess.verbose>2)
		clog << "selected function " << q->dw.function_name << "\n";

	      Dwarf_Addr entrypc;
	      if (q->dw.function_entrypc (&entrypc))
		{
		  func_info func;
		  q->dw.function_die (&func.die);
		  func.name = q->dw.function_name;
		  q->dw.function_file (&func.decl_file);
		  q->dw.function_line (&func.decl_line);
		  q->filtered_functions[entrypc] = func;
		}
	      else
		throw semantic_error("no entrypc found for function '"
				     + q->dw.function_name + "'");
	    }
	}
      return DWARF_CB_OK;
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
      return DWARF_CB_ABORT;
    }
}

static int
query_cu (Dwarf_Die * cudie, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);

  try
    {
      q->dw.focus_on_cu (cudie);

      if (false && q->sess.verbose>2)
        clog << "focused on CU '" << q->dw.cu_name
             << "', in module '" << q->dw.module_name << "'\n";

      if (q->has_statement_str
	  || q->has_function_str || q->has_function_num)
	{
	  q->filtered_srcfiles.clear();
	  q->filtered_functions.clear();
	  q->filtered_inlines.clear();

	  // In this path, we find "abstract functions", record
	  // information about them, and then (depending on lineno
	  // matching) possibly emit one or more of the function's
	  // associated addresses. Unfortunately the control of this
	  // cannot easily be turned inside out.

	  if ((q->has_statement_str || q->has_function_str)
	      && (q->spec_type != function_alone))
	    {
	      // If we have a pattern string with a filename, we need
	      // to elaborate the srcfile mask in question first.
	      q->dw.collect_srcfiles_matching (q->file, q->filtered_srcfiles);

	      // If we have a file pattern and *no* srcfile matches, there's
	      // no need to look further into this CU, so skip.
	      if (q->filtered_srcfiles.empty())
		return DWARF_CB_OK;
	    }

	  // Pick up [entrypc, name, DIE] tuples for all the functions
	  // matching the query, and fill in the prologue endings of them
	  // all in a single pass.
	  q->dw.iterate_over_functions (query_dwarf_func, q);

          if (q->sess.prologue_searching)
            if (! q->filtered_functions.empty())
              q->dw.resolve_prologue_endings (q->filtered_functions);

	  if ((q->has_statement_str || q->has_function_str)
	      && (q->spec_type == function_file_and_line))
	    {
	      // If we have a pattern string with target *line*, we
	      // have to look at lines in all the matched srcfiles.
	      for (set<char const *>::const_iterator i = q->filtered_srcfiles.begin();
		   i != q->filtered_srcfiles.end(); ++i)
		q->dw.iterate_over_srcfile_lines (*i, q->line, q->has_statement_str,
						  query_srcfile_line, q);
	    }
	  else
	    {
	      // Otherwise, simply probe all resolved functions (if
	      // we're scanning functions)
	      if (q->has_statement_str || q->has_function_str || q->has_function_num)
		for (map<Dwarf_Addr, func_info>::iterator i = q->filtered_functions.begin();
		     i != q->filtered_functions.end(); ++i)
		  query_func_info (i->first, i->second, q);

	      // Or all inline instances (if we're scanning inlines)
	      if (q->has_statement_str
                  || ((q->has_function_str || q->has_function_num) && !q->has_call))
		for (map<Dwarf_Addr, inline_instance_info>::iterator i
		       = q->filtered_inlines.begin(); i != q->filtered_inlines.end(); ++i)
		  query_inline_instance_info (i->first, i->second, q);

	    }
	}
      else
        {
	  // Otherwise we have a statement number, and we can just
	  // query it directly within this module.

	  assert (q->has_statement_num);
	  Dwarf_Addr query_addr = q->statement_num_val;
          query_addr = q->dw.module_address_to_global(query_addr);

	  query_statement ("", "", -1, NULL, query_addr, q);
        }
      return DWARF_CB_OK;
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
      return DWARF_CB_ABORT;
    }
}


static int
query_kernel_module (Dwfl_Module *mod,
		     void **,
		     const char *name,
		     Dwarf_Addr,
		     void *arg)
{
  if (TOK_KERNEL == name)
  {
    Dwfl_Module **m = (Dwfl_Module **)arg;

    *m = mod;
    return DWARF_CB_ABORT;
  }
  return DWARF_CB_OK;
}


static int
query_module (Dwfl_Module *mod,
	      void **,
	      const char *name,
              Dwarf_Addr,
	      void *arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);

  try
    {
      q->dw.focus_on_module(mod);

      // If we have enough information in the pattern to skip a module and
      // the module does not match that information, return early.
      if (!q->dw.module_name_matches(q->module_val))
        return DWARF_CB_OK;

      // Don't allow module("*kernel*") type expressions to match the
      // elfutils module "kernel", which we refer to in the probe
      // point syntax exclusively as "kernel.*".
      if (q->dw.module_name == TOK_KERNEL && ! q->has_kernel)
        return DWARF_CB_OK;

      // Validate the machine code in this elf file against the
      // session machine.  This is important, in case the wrong kind
      // of debuginfo is being automagically processed by elfutils.
      // While we can tell i686 apart from x86-64, unfortunately
      // we can't help confusing i586 vs i686 (both EM_386).

      Dwarf_Addr _junk;
      Elf* elf = dwfl_module_getelf (mod, &_junk);
      Ebl* ebl = ebl_openbackend (elf);
      int elf_machine = ebl_get_elfmachine (ebl);
      const char* debug_filename = "";
      const char* main_filename = "";
      (void) dwfl_module_info (mod, NULL, NULL, 
                               NULL, NULL, NULL,
                               & main_filename, 
                               & debug_filename);
      const string& sess_machine = q->sess.architecture;
      string expect_machine;

      switch (elf_machine)
        {
        case EM_386: expect_machine = "i?86"; break; // accept e.g. i586
        case EM_X86_64: expect_machine = "x86_64"; break;
        case EM_PPC: expect_machine = "ppc"; break;
        case EM_PPC64: expect_machine = "ppc64"; break;
        case EM_S390: expect_machine = "s390x"; break;
        case EM_IA_64: expect_machine = "ia64"; break;
          // XXX: fill in some more of these
        default: expect_machine = "?"; break;
        }
     
      if (! debug_filename) debug_filename = main_filename;
      if (! debug_filename) debug_filename = name;

      if (fnmatch (expect_machine.c_str(), sess_machine.c_str(), 0) != 0)
        {
          stringstream msg;
          msg << "ELF machine " << expect_machine << " (code " << elf_machine
              << ") mismatch with target " << sess_machine
              << " in '" << debug_filename << "'";
          throw semantic_error(msg.str ());
        }

      if (q->sess.verbose>2)
	clog << "focused on module '" << q->dw.module_name
	     << " = [0x" << hex << q->dw.module_start
	     << "-0x" << q->dw.module_end
	     << ", bias 0x" << q->dw.module_bias << "]" << dec 
             << " file " << debug_filename
             << " ELF machine " << expect_machine
             << " (code " << elf_machine << ")"
             << "\n";

      if (q->has_function_num || q->has_statement_num)
        {
          // If we have module("foo").function(0xbeef) or
          // module("foo").statement(0xbeef), the address is relative
          // to the start of the module, so we seek the function
          // number plus the module's bias.

          Dwarf_Addr addr;
          if (q->has_function_num)
            addr = q->function_num_val;
          else
            addr = q->statement_num_val;
          
          // NB: we don't need to add the module base address or bias
          // value here (for reasons that may be coincidental).
	  q->dw.query_cu_containing_module_address(addr, q);
        }
      else
        {
          // Otherwise if we have a function("foo") or statement("foo")
          // specifier, we have to scan over all the CUs looking for
          // the function(s) in question
          assert(q->has_function_str || q->has_statement_str);
          q->dw.iterate_over_cus(&query_cu, q);
        }

      // If we know that there will be no more matches, abort early.
      if (q->dw.module_name_final_match(q->module_val))
        return DWARF_CB_ABORT;
      else
        return DWARF_CB_OK;
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
      return DWARF_CB_ABORT;
    }
}


struct var_expanding_copy_visitor: public deep_copy_visitor
{
  static unsigned tick;
  stack<functioncall**> target_symbol_setter_functioncalls;

  var_expanding_copy_visitor() {}
  void visit_assignment (assignment* e);
};


struct dwarf_var_expanding_copy_visitor: public var_expanding_copy_visitor
{
  dwarf_query & q;
  Dwarf_Die *scope_die;
  Dwarf_Addr addr;
  block *add_block;
  probe *add_probe;
  std::map<std::string, symbol *> return_ts_map;

  dwarf_var_expanding_copy_visitor(dwarf_query & q, Dwarf_Die *sd, Dwarf_Addr a):
    q(q), scope_die(sd), addr(a), add_block(NULL), add_probe(NULL) {}
  void visit_target_symbol (target_symbol* e);
};



unsigned var_expanding_copy_visitor::tick = 0;

void
var_expanding_copy_visitor::visit_assignment (assignment* e)
{
  // Our job would normally be to require() the left and right sides
  // into a new assignment. What we're doing is slightly trickier:
  // we're pushing a functioncall** onto a stack, and if our left
  // child sets the functioncall* for that value, we're going to
  // assume our left child was a target symbol -- transformed into a
  // set_target_foo(value) call, and it wants to take our right child
  // as the argument "value".
  //
  // This is why some people claim that languages with
  // constructor-decomposing case expressions have a leg up on
  // visitors.

  functioncall *fcall = NULL;
  expression *new_left, *new_right;

  target_symbol_setter_functioncalls.push (&fcall);
  require<expression*> (this, &new_left, e->left);
  target_symbol_setter_functioncalls.pop ();
  require<expression*> (this, &new_right, e->right);

  if (fcall != NULL)
    {
      // Our left child is informing us that it was a target variable
      // and it has been replaced with a set_target_foo() function
      // call; we are going to provide that function call -- with the
      // right child spliced in as sole argument -- in place of
      // ourselves, in the deep copy we're in the middle of making.

      // FIXME: for the time being, we only support plan $foo = bar,
      // not += or any other op= variant. This is fixable, but a bit
      // ugly.
      if (e->op != "=")
	throw semantic_error ("Operator-assign expressions on target "
			     "variables not implemented", e->tok);

      assert (new_left == fcall);
      fcall->args.push_back (new_right);
      provide <expression*> (this, fcall);
    }
  else
    {
      assignment* n = new assignment;
      n->op = e->op;
      n->tok = e->tok;
      n->left = new_left;
      n->right = new_right;
      provide <assignment*> (this, n);
    }
}


void
dwarf_var_expanding_copy_visitor::visit_target_symbol (target_symbol *e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  bool lvalue = is_active_lvalue(e);
  if (lvalue && !q.sess.guru_mode)
    throw semantic_error("write to target variable not permitted", e->tok);

  if (q.has_return && e->base_name != "$return")
    {
      if (lvalue)
	throw semantic_error("write to target variable not permitted in .return probes", e->tok);

      // Get the full name of the target symbol.
      stringstream ts_name_stream;
      e->print(ts_name_stream);
      string ts_name = ts_name_stream.str();

      // Check and make sure we haven't already seen this target
      // variable in this return probe.  If we have, just return our
      // last replacement.
      map<string, symbol *>::iterator i = return_ts_map.find(ts_name);
      if (i != return_ts_map.end())
	{
	  provide <symbol*> (this, i->second);
	  return;
	}

      // We've got to do several things here to handle target
      // variables in return probes.

      // (1) Synthesize two global arrays.  One is the cache of the
      // target variable and the other contains a thread specific
      // nesting level counter.  The arrays will look like
      // this:
      //
      //   _dwarf_tvar_{name}_{num}
      //   _dwarf_tvar_{name}_{num}_ctr

      string aname = (string("_dwarf_tvar_")
		      + e->base_name.substr(1)
		      + "_" + lex_cast<string>(tick++));
      vardecl* vd = new vardecl;
      vd->name = aname;
      vd->tok = e->tok;
      q.sess.globals.push_back (vd);

      string ctrname = aname + "_ctr";
      vd = new vardecl;
      vd->name = ctrname;
      vd->tok = e->tok;
      q.sess.globals.push_back (vd);

      // (2) Create a new code block we're going to insert at the
      // beginning of this probe to get the cached value into a
      // temporary variable.  We'll replace the target variable
      // reference with the temporary variable reference.  The code
      // will look like this:
      //
      //   _dwarf_tvar_tid = tid()
      //   _dwarf_tvar_{name}_{num}_tmp
      //       = _dwarf_tvar_{name}_{num}[_dwarf_tvar_tid,
      //                    _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]]
      //   delete _dwarf_tvar_{name}_{num}[_dwarf_tvar_tid,
      //                    _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]--]
      //   if (! _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid])
      //       delete _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]

      // (2a) Synthesize the tid temporary expression, which will look
      // like this:
      //
      //   _dwarf_tvar_tid = tid()
      symbol* tidsym = new symbol;
      tidsym->name = string("_dwarf_tvar_tid");
      tidsym->tok = e->tok;

      if (add_block == NULL)
        {
	   add_block = new block;
	   add_block->tok = e->tok;

	   // Synthesize a functioncall to grab the thread id.
	   functioncall* fc = new functioncall;
	   fc->tok = e->tok;
	   fc->function = string("tid");

	   // Assign the tid to '_dwarf_tvar_tid'.
	   assignment* a = new assignment;
	   a->tok = e->tok;
	   a->op = "=";
	   a->left = tidsym;
	   a->right = fc;

	   expr_statement* es = new expr_statement;
	   es->tok = e->tok;
	   es->value = a;
	   add_block->statements.push_back (es);
	}

      // (2b) Synthesize an array reference and assign it to a
      // temporary variable (that we'll use as replacement for the
      // target variable reference).  It will look like this:
      //
      //   _dwarf_tvar_{name}_{num}_tmp
      //       = _dwarf_tvar_{name}_{num}[_dwarf_tvar_tid,
      //                    _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]]

      arrayindex* ai_tvar_base = new arrayindex;
      ai_tvar_base->tok = e->tok;

      symbol* sym = new symbol;
      sym->name = aname;
      sym->tok = e->tok;
      ai_tvar_base->base = sym;

      ai_tvar_base->indexes.push_back(tidsym);

      // We need to create a copy of the array index in its current
      // state so we can have 2 variants of it (the original and one
      // that post-decrements the second index).
      arrayindex* ai_tvar = new arrayindex;
      arrayindex* ai_tvar_postdec = new arrayindex;
      *ai_tvar = *ai_tvar_base;
      *ai_tvar_postdec = *ai_tvar_base;

      // Synthesize the
      // "_dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]" used as the
      // second index into the array.
      arrayindex* ai_ctr = new arrayindex;
      ai_ctr->tok = e->tok;

      sym = new symbol;
      sym->name = ctrname;
      sym->tok = e->tok;
      ai_ctr->base = sym;
      ai_ctr->indexes.push_back(tidsym);
      ai_tvar->indexes.push_back(ai_ctr);

      symbol* tmpsym = new symbol;
      tmpsym->name = aname + "_tmp";
      tmpsym->tok = e->tok;

      assignment* a = new assignment;
      a->tok = e->tok;
      a->op = "=";
      a->left = tmpsym;
      a->right = ai_tvar;

      expr_statement* es = new expr_statement;
      es->tok = e->tok;
      es->value = a;

      add_block->statements.push_back (es);

      // (2c) Add a post-decrement to the second array index and
      // delete the array value.  It will look like this:
      //
      //   delete _dwarf_tvar_{name}_{num}[_dwarf_tvar_tid,
      //                    _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]--]

      post_crement* pc = new post_crement;
      pc->tok = e->tok;
      pc->op = "--";
      pc->operand = ai_ctr;
      ai_tvar_postdec->indexes.push_back(pc);

      delete_statement* ds = new delete_statement;
      ds->tok = e->tok;
      ds->value = ai_tvar_postdec;

      add_block->statements.push_back (ds);

      // (2d) Delete the counter value if it is 0.  It will look like
      // this:
      //   if (! _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid])
      //       delete _dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]
      
      ds = new delete_statement;
      ds->tok = e->tok;
      ds->value = ai_ctr;

      unary_expression *ue = new unary_expression;
      ue->tok = e->tok;
      ue->op = "!";
      ue->operand = ai_ctr;

      if_statement *ifs = new if_statement;
      ifs->tok = e->tok;
      ifs->condition = ue;
      ifs->thenblock = ds;
      ifs->elseblock = NULL;
      
      add_block->statements.push_back (ifs);

      // (3) We need an entry probe that saves the value for us in the
      // global array we created.  Create the entry probe, which will
      // look like this:
      //
      //   probe kernel.function("{function}") {
      //     _dwarf_tvar_tid = tid()
      //     _dwarf_tvar_{name}_{num}[_dwarf_tvar_tid,
      //                       ++_dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]]
      //       = ${param}
      //   }

      if (add_probe == NULL)
        {
	   add_probe = new probe;
	   add_probe->tok = e->tok;

	   // We need the name of the current probe point, minus the
	   // ".return" (or anything after it, such as ".maxactive(N)").
	   // Create a new probe point, copying all the components,
	   // stopping when we see the ".return" component.
	   probe_point* pp = new probe_point;
	   for (unsigned c = 0; c < q.base_loc->components.size(); c++)
	     {
	        if (q.base_loc->components[c]->functor == "return")
		  break;
	        else
		  pp->components.push_back(q.base_loc->components[c]);
	     }
	   pp->tok = e->tok;
	   pp->optional = q.base_loc->optional;
	   add_probe->locations.push_back(pp);

	   add_probe->body = new block;
	   add_probe->body->tok = e->tok;

	   // Synthesize a functioncall to grab the thread id.
	   functioncall* fc = new functioncall;
	   fc->tok = e->tok;
	   fc->function = string("tid");

	   // Assign the tid to '_dwarf_tvar_tid'.
	   assignment* a = new assignment;
	   a->tok = e->tok;
	   a->op = "=";
	   a->left = tidsym;
	   a->right = fc;

	   expr_statement* es = new expr_statement;
	   es->tok = e->tok;
	   es->value = a;
	   add_probe->body->statements.push_back (es);

	   vardecl* vd = new vardecl;
	   vd->tok = e->tok;
	   vd->name = tidsym->name;
	   vd->type = pe_long;
	   vd->set_arity(0);
	   add_probe->locals.push_back(vd);
	}

      // Save the value, like this:
      //     _dwarf_tvar_{name}_{num}[_dwarf_tvar_tid,
      //                       ++_dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]]
      //       = ${param}
      arrayindex* ai_tvar_preinc = new arrayindex;
      *ai_tvar_preinc = *ai_tvar_base;
      
      pre_crement* preinc = new pre_crement;
      preinc->tok = e->tok;
      preinc->op = "++";
      preinc->operand = ai_ctr;
      ai_tvar_preinc->indexes.push_back(preinc);
      
      a = new assignment;
      a->tok = e->tok;
      a->op = "=";
      a->left = ai_tvar_preinc;
      a->right = e;

      es = new expr_statement;
      es->tok = e->tok;
      es->value = a;

      add_probe->body->statements.push_back (es);

      // (4) Provide the '_dwarf_tvar_{name}_{num}_tmp' variable to
      // our parent so it can be used as a substitute for the target
      // symbol.
      provide <symbol*> (this, tmpsym);

      // (5) Remember this replacement since we might be able to reuse
      // it later if the same return probe references this target
      // symbol again.
      return_ts_map[ts_name] = tmpsym;
      return;
    }

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;

  string fname = (string(lvalue ? "_dwarf_tvar_set" : "_dwarf_tvar_get")
		  + "_" + e->base_name.substr(1)
		  + "_" + lex_cast<string>(tick++));

  try
    {
      if (q.has_return && e->base_name == "$return")
        {
	  ec->code = q.dw.literal_stmt_for_return (scope_die,
						   addr,
						   e->components,
						   lvalue,
						   fdecl->type);
	}
      else
        {
	  ec->code = q.dw.literal_stmt_for_local (scope_die,
						  addr,
						  e->base_name.substr(1),
						  e->components,
						  lvalue,
						  fdecl->type);
	}

      if (! lvalue)
        ec->code += "/* pure */";
    }
  catch (const semantic_error& er)
    {
      // We suppress this error message, and pass the unresolved
      // target_symbol to the next pass.  We hope that this value ends
      // up not being referenced after all, so it can be optimized out
      // quietly.
      provide <target_symbol*> (this, e);
      semantic_error* saveme = new semantic_error (er); // copy it
      saveme->tok1 = e->tok; // XXX: token not passed to q.dw code generation routines
      // NB: we can have multiple errors, since a $target variable
      // may be expanded in several different contexts:
      //     function ("*") { $var }
      saveme->chain = e->saved_conversion_error;
      e->saved_conversion_error = saveme;
      delete fdecl;
      delete ec;
      return;
    }

  fdecl->name = fname;
  fdecl->body = ec;
  if (lvalue)
    {
      // Modify the fdecl so it carries a single pe_long formal
      // argument called "value".

      // FIXME: For the time being we only support setting target
      // variables which have base types; these are 'pe_long' in
      // stap's type vocabulary.  Strings and pointers might be
      // reasonable, some day, but not today.

      vardecl *v = new vardecl;
      v->type = pe_long;
      v->name = "value";
      v->tok = e->tok;
      fdecl->formal_args.push_back(v);
    }
  q.sess.functions.push_back(fdecl);

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0;  // NB: must not resolve yet, to ensure inclusion in session

  if (lvalue)
    {
      // Provide the functioncall to our parent, so that it can be
      // used to substitute for the assignment node immediately above
      // us.
      assert(!target_symbol_setter_functioncalls.empty());
      *(target_symbol_setter_functioncalls.top()) = n;
    }

  provide <functioncall*> (this, n);
}


void
dwarf_derived_probe::printsig (ostream& o) const
{
  // Instead of just printing the plain locations, we add a PC value
  // as a comment as a way of telling e.g. apart multiple inlined
  // function instances.  This is distinct from the verbose/clog
  // output, since this part goes into the cache hash calculations.
  sole_location()->print (o);
  o << " /* pc=0x" << hex << addr << dec << " */";
  printsig_nested (o);
}



void
dwarf_derived_probe::join_group (systemtap_session& s)
{
  if (! s.dwarf_derived_probes)
    s.dwarf_derived_probes = new dwarf_derived_probe_group ();
  s.dwarf_derived_probes->enroll (this);
}


dwarf_derived_probe::dwarf_derived_probe(const string& funcname,
                                         const string& filename,
                                         int line,
                                         // module & section speficy a relocation
                                         // base for <addr>, unless section==""
                                         // (equivalently module=="kernel")
                                         const string& module,
                                         const string& section,
                                         // NB: dwfl_addr is the virtualized
                                         // address for this symbol.
                                         Dwarf_Addr dwfl_addr,
                                         // addr is the section-offset for
                                         // actual relocation.
                                         Dwarf_Addr addr,
                                         dwarf_query& q,
                                         Dwarf_Die* scope_die /* may be null */)
  : derived_probe (q.base_probe, 0 /* location-less */),
    module (module), section (section), addr (addr),
    has_return (q.has_return),
    has_maxactive (q.has_maxactive),
    maxactive_val (q.maxactive_val)
{
  // Assert relocation invariants
  if (section == "" && dwfl_addr != addr) // addr should be absolute 
    throw semantic_error ("missing relocation base against", q.base_loc->tok);
  if (section != "" && dwfl_addr == addr) // addr should be an offset
    throw semantic_error ("inconsistent relocation address", q.base_loc->tok);


  this->tok = q.base_probe->tok;

  // Make a target-variable-expanded copy of the probe body
  if (scope_die)
    {
      dwarf_var_expanding_copy_visitor v (q, scope_die, dwfl_addr);
      require <block*> (&v, &(this->body), q.base_probe->body);

      // If during target-variable-expanding the probe, we added a new block
      // of code, add it to the start of the probe.
      if (v.add_block)
        this->body->statements.insert(this->body->statements.begin(), v.add_block);
      
      // If when target-variable-expanding the probe, we added a new
      // probe, add it in a new file to the list of files to be processed.
      if (v.add_probe)
        {
          stapfile *f = new stapfile;
          f->probes.push_back(v.add_probe);
          q.sess.files.push_back(f);
        }
    }
  // else - null scope_die - $target variables will produce an error during translate phase

  // Set the sole element of the "locations" vector as a
  // "reverse-engineered" form of the incoming (q.base_loc) probe
  // point.  This allows a user to see what function / file / line
  // number any particular match of the wildcards.

  vector<probe_point::component*> comps;
  comps.push_back
    (module == TOK_KERNEL
     ? new probe_point::component(TOK_KERNEL)
     : new probe_point::component(TOK_MODULE, new literal_string(module)));

  string fn_or_stmt;
  if (q.has_function_str || q.has_function_num)
    fn_or_stmt = "function";
  else
    fn_or_stmt = "statement";

  if (q.has_function_str || q.has_statement_str)
      {
        string retro_name = funcname;
	if (filename != "")
	  retro_name += ("@" + string (filename));
	if (line != -1)
	  retro_name += (":" + lex_cast<string> (line));
        comps.push_back
          (new probe_point::component
           (fn_or_stmt, new literal_string (retro_name)));
      }
  else if (q.has_function_num || q.has_statement_num)
    {
      Dwarf_Addr retro_addr;
      if (q.has_function_num)
        retro_addr = q.function_num_val;
      else
        retro_addr = q.statement_num_val;
      comps.push_back (new probe_point::component
                       (fn_or_stmt,
                        new literal_number(retro_addr))); // XXX: should be hex if possible

      if (q.has_absolute)
        comps.push_back (new probe_point::component (TOK_ABSOLUTE));
    }

  if (q.has_call)
      comps.push_back (new probe_point::component(TOK_CALL));
  if (q.has_inline)
      comps.push_back (new probe_point::component(TOK_INLINE));
  if (has_return)
    comps.push_back (new probe_point::component(TOK_RETURN));
  if (has_maxactive)
    comps.push_back (new probe_point::component
                     (TOK_MAXACTIVE, new literal_number(maxactive_val)));

  locations.push_back(new probe_point(comps, q.base_loc->tok));
}


void
dwarf_derived_probe::register_statement_variants(match_node * root,
						 dwarf_builder * dw)
{
  root->bind(dw);
}

void
dwarf_derived_probe::register_function_variants(match_node * root,
						dwarf_builder * dw)
{
  root->bind(dw);
  root->bind(TOK_INLINE)->bind(dw);
  root->bind(TOK_CALL)->bind(dw);
  root->bind(TOK_RETURN)->bind(dw);
  root->bind(TOK_RETURN)->bind_num(TOK_MAXACTIVE)->bind(dw);
}

void
dwarf_derived_probe::register_function_and_statement_variants(match_node * root,
							      dwarf_builder * dw)
{
  // Here we match 4 forms:
  //
  // .function("foo")
  // .function(0xdeadbeef)
  // .statement("foo")
  // .statement(0xdeadbeef)

  register_function_variants(root->bind_str(TOK_FUNCTION), dw);
  register_function_variants(root->bind_num(TOK_FUNCTION), dw);
  register_statement_variants(root->bind_str(TOK_STATEMENT), dw);
  register_statement_variants(root->bind_num(TOK_STATEMENT), dw);
}

void
dwarf_derived_probe::register_patterns(match_node * root)
{
  dwarf_builder *dw = new dwarf_builder();

  register_function_and_statement_variants(root->bind(TOK_KERNEL), dw);
  register_function_and_statement_variants(root->bind_str(TOK_MODULE), dw);
  root->bind(TOK_KERNEL)->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)->bind(dw);

  // register_function_and_statement_variants(root->bind_str(TOK_PROCESS), dw);
}


// ------------------------------------------------------------------------

void
dwarf_derived_probe_group::enroll (dwarf_derived_probe* p)
{
  probes_by_module.insert (make_pair (p->module, p));

  // XXX: probes put at the same address should all share a
  // single kprobe/kretprobe, and have their handlers executed
  // sequentially.
}


void
dwarf_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_module.empty()) return;

  s.op->newline() << "/* ---- dwarf probes ---- */";

  // Warn of misconfigured kernels
  s.op->newline() << "#if ! defined(CONFIG_KPROBES)";
  s.op->newline() << "#error \"Need CONFIG_KPROBES!\"";
  s.op->newline() << "#endif";
  s.op->newline();

  // Forward declare the master entry functions
  s.op->newline() << "static int enter_kprobe_probe (struct kprobe *inst,";
  s.op->line() << " struct pt_regs *regs);";
  s.op->newline() << "static int enter_kretprobe_probe (struct kretprobe_instance *inst,";
  s.op->line() << " struct pt_regs *regs);";

  // Emit the actual probe list.
  s.op->newline() << "struct stap_dwarf_probe {";
  s.op->newline(1) << "union { struct kprobe kp; struct kretprobe krp; } u;";
  s.op->newline() << "unsigned return_p:1;";
  s.op->newline() << "unsigned maxactive_p:1;";
  s.op->newline() << "unsigned registered_p:1;";
  s.op->newline() << "const char *module;";
  s.op->newline() << "const char *section;";
  s.op->newline() << "unsigned long address;";
  s.op->newline() << "unsigned long maxactive_val;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline(-1) << "} stap_dwarf_probes[] = {";
  s.op->indent(1);

  for (p_b_m_iterator it = probes_by_module.begin(); it != probes_by_module.end(); it++)
    {
      dwarf_derived_probe* p = it->second;
      s.op->newline() << "{";
      if (p->has_return)
        s.op->line() << " .return_p=1,";
      if (p->has_maxactive)
        s.op->line() << " .maxactive_p=1,";
      s.op->line() << " .address=0x" << hex << p->addr << dec << "UL,";
      s.op->line() << " .module=\"" << p->module << "\",";
      s.op->line() << " .section=\"" << p->section << "\",";
      if (p->has_maxactive)
	s.op->line() << " .maxactive_val=" << p->maxactive_val << "UL,";
      s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
      s.op->line() << " .ph=&" << p->name;
      s.op->line() << " },";
    }

  s.op->newline(-1) << "};";

  // Emit the kprobes callback function
  s.op->newline();
  s.op->newline() << "static int enter_kprobe_probe (struct kprobe *inst,";
  s.op->line() << " struct pt_regs *regs) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = container_of(inst, struct stap_dwarf_probe, u.kp);";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "c->probe_point = sdp->pp;";
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "(*sdp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  // Same for kretprobes
  s.op->newline();
  s.op->newline() << "static int enter_kretprobe_probe (struct kretprobe_instance *inst,";
  s.op->line() << " struct pt_regs *regs) {";
  s.op->newline(1) << "struct kretprobe *krp = inst->rp;";
  s.op->newline() << "struct stap_dwarf_probe *sdp = container_of(krp, struct stap_dwarf_probe, u.krp);";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "c->probe_point = sdp->pp;";
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "c->pi = inst;"; // for assisting runtime's backtrace logic
  s.op->newline() << "(*sdp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";
}


void
dwarf_derived_probe_group::emit_module_init (systemtap_session& s)
{
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "unsigned long relocated_addr = _stp_module_relocate (sdp->module, sdp->section, sdp->address);";
  s.op->newline() << "if (relocated_addr == 0) continue;"; // quietly; assume module is absent
  s.op->newline() << "probe_point = sdp->pp;";
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline(1) << "sdp->u.krp.kp.addr = (void *) relocated_addr;";
  s.op->newline() << "if (sdp->maxactive_p) {";
  s.op->newline(1) << "sdp->u.krp.maxactive = sdp->maxactive_val;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "sdp->u.krp.maxactive = max(10, 4*NR_CPUS);";
  s.op->newline(-1) << "}";
  s.op->newline() << "sdp->u.krp.handler = &enter_kretprobe_probe;";
  s.op->newline() << "rc = register_kretprobe (& sdp->u.krp);";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "sdp->u.kp.addr = (void *) relocated_addr;";
  s.op->newline() << "sdp->u.kp.pre_handler = &enter_kprobe_probe;";
  s.op->newline() << "rc = register_kprobe (& sdp->u.kp);";
  s.op->newline(-1) << "}";
  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "for (j=i-1; j>=0; j--) {"; // partial rollback
  s.op->newline(1) << "struct stap_dwarf_probe *sdp2 = & stap_dwarf_probes[j];";
  s.op->newline() << "if (sdp2->return_p) unregister_kretprobe (&sdp2->u.krp);";
  s.op->newline() << "else unregister_kprobe (&sdp2->u.kp);";
  // NB: we don't have to clear sdp2->registered_p, since the module_exit code is
  // not run for this early-abort case.
  s.op->newline(-1) << "}";
  s.op->newline() << "break;"; // don't attempt to register any more probes
  s.op->newline(-1) << "}";
  s.op->newline() << "else sdp->registered_p = 1;";
  s.op->newline(-1) << "}"; // for loop
}


void
dwarf_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline(1) << "unregister_kretprobe (&sdp->u.krp);";
  s.op->newline() << "atomic_add (sdp->u.krp.nmissed, & skipped_count);";
  s.op->newline() << "atomic_add (sdp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "unregister_kprobe (&sdp->u.kp);";
  s.op->newline() << "atomic_add (sdp->u.kp.nmissed, & skipped_count);";
  s.op->newline(-1) << "}";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";
}



static Dwarf_Addr
lookup_symbol_address (Dwfl_Module *m, const char* wanted)
{
  int syments = dwfl_module_getsymtab(m);
  assert(syments);
  for (int i = 1; i < syments; ++i)
    {
      GElf_Sym sym;
      const char *name = dwfl_module_getsym(m, i, &sym, NULL);
      if (name != NULL && strcmp(name, wanted) == 0)
        return sym.st_value;
    }

  return 0;
}




void
dwarf_builder::build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<derived_probe *> & finished_results)
{
  // NB: the kernel/user dwlfpp objects are long-lived.
  // XXX: but they should be per-session, as this builder object
  // may be reused if we try to cross-instrument multiple targets.

  if (!kern_dw)
    {
      kern_dw = new dwflpp(sess);
      assert(kern_dw);
      kern_dw->setup(true);
    }

  Dwfl_Module* km = 0;
  kern_dw->iterate_over_modules(&query_kernel_module, &km);
  if (km)
    {
      sess.sym_kprobes_text_start = lookup_symbol_address (km, "__kprobes_text_start");
      sess.sym_kprobes_text_end = lookup_symbol_address (km, "__kprobes_text_end");
      sess.sym_stext = lookup_symbol_address (km, "_stext");
      
      if (sess.verbose > 2)
        {
          clog << "control symbols:"
            // abbreviate the names - they're for our debugging only anyway
               << " kts: 0x" << hex << sess.sym_kprobes_text_start
               << " kte: 0x" << sess.sym_kprobes_text_end
               << " stext: 0x" << sess.sym_stext
               << dec << endl;
        }
    }

  dwflpp* dw = kern_dw;
  dwarf_query q(sess, base, location, *dw, parameters, finished_results);

  if (q.has_absolute)
    {
      // assert guru mode for absolute probes 
      if (! q.base_probe->privileged)
        {
          throw semantic_error ("absolute statement probe in unprivileged script", q.base_probe->tok);
        }

      // For kernel.statement(NUM).absolute probe points, we bypass
      // all the debuginfo stuff: We just wire up a
      // dwarf_derived_probe right here and now.
      dwarf_derived_probe* p = 
        new dwarf_derived_probe ("", "", 0, "kernel", "",
                                 q.statement_num_val, q.statement_num_val,
                                 q, 0);
      finished_results.push_back (p);
      return;
    }

  dw->iterate_over_modules(&query_module, &q);
}



// ------------------------------------------------------------------------
// timer derived probes
// ------------------------------------------------------------------------


struct timer_derived_probe: public derived_probe
{
  int64_t interval, randomize;
  bool time_is_msecs; // NB: hrtimers get ms-based probes on modern kernels instead
  timer_derived_probe (probe* p, probe_point* l, int64_t i, int64_t r, bool ms=false);
  virtual void join_group (systemtap_session& s);
};


struct timer_derived_probe_group: public generic_dpg<timer_derived_probe>
{
  void emit_interval (translator_output* o);
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


timer_derived_probe::timer_derived_probe (probe* p, probe_point* l, int64_t i, int64_t r, bool ms):
  derived_probe (p, l), interval (i), randomize (r), time_is_msecs(ms)
{
  if (interval <= 0 || interval > 1000000) // make i and r fit into plain ints
    throw semantic_error ("invalid interval for jiffies timer");
  // randomize = 0 means no randomization
  if (randomize < 0 || randomize > interval)
    throw semantic_error ("invalid randomize for jiffies timer");

  if (locations.size() != 1)
    throw semantic_error ("expect single probe point");
  // so we don't have to loop over them in the other functions
}


void
timer_derived_probe::join_group (systemtap_session& s)
{
  if (! s.timer_derived_probes)
    s.timer_derived_probes = new timer_derived_probe_group ();
  s.timer_derived_probes->enroll (this);
}


void
timer_derived_probe_group::emit_interval (translator_output* o)
{
  o->line() << "({";
  o->newline(1) << "unsigned i = stp->intrv;";
  o->newline() << "if (stp->rnd != 0)";
  o->newline(1) << "i += _stp_random_pm(stp->rnd);";
  o->newline(-1) << "stp->ms ? msecs_to_jiffies(i) : i;";
  o->newline(-1) << "})";
}


void
timer_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- timer probes ---- */";

  s.op->newline() << "struct stap_timer_probe {";
  s.op->newline(1) << "struct timer_list timer_list;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "unsigned intrv, ms, rnd;";
  s.op->newline(-1) << "} stap_timer_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{"; 
      s.op->line() << " .pp=" 
                   << lex_cast_qstring (*probes[i]->sole_location()) << ",";
      s.op->line() << " .ph=&" << probes[i]->name << ",";
      s.op->line() << " .intrv=" << probes[i]->interval << ",";
      s.op->line() << " .ms=" << probes[i]->time_is_msecs << ",";
      s.op->line() << " .rnd=" << probes[i]->randomize;
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();

  s.op->newline() << "static void enter_timer_probe (unsigned long val) {";
  s.op->newline(1) << "struct stap_timer_probe* stp = & stap_timer_probes [val];";
  s.op->newline() << "if ((atomic_read (&session_state) == STAP_SESSION_STARTING) ||";
  s.op->newline() << "    (atomic_read (&session_state) == STAP_SESSION_RUNNING))";
  s.op->newline(1) << "mod_timer (& stp->timer_list, jiffies + ";
  emit_interval (s.op);
  s.op->line() << ");";
  s.op->newline(-1) << "{";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "c->probe_point = stp->pp;";
  s.op->newline() << "(*stp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
}


void
timer_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_timer_probe* stp = & stap_timer_probes [i];";
  s.op->newline() << "probe_point = stp->pp;";
  s.op->newline() << "init_timer (& stp->timer_list);";
  s.op->newline() << "stp->timer_list.function = & enter_timer_probe;";
  s.op->newline() << "stp->timer_list.data = i;"; // NB: important!
  // copy timer renew calculations from above :-(
  s.op->newline() << "stp->timer_list.expires = jiffies + ";
  emit_interval (s.op);
  s.op->line() << ";";
  s.op->newline() << "add_timer (& stp->timer_list);";
  // note: no partial failure rollback is needed: add_timer cannot fail.
  s.op->newline(-1) << "}"; // for loop
}


void
timer_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "del_timer_sync (& stap_timer_probes[i].timer_list);";
  s.op->indent(-1);
}



// ------------------------------------------------------------------------
// profile derived probes
// ------------------------------------------------------------------------
//   On kernels < 2.6.10, this uses the register_profile_notifier API to
//   generate the timed events for profiling; on kernels >= 2.6.10 this
//   uses the register_timer_hook API.  The latter doesn't currently allow
//   simultaneous users, so insertion will fail if the profiler is busy.
//   (Conflicting users may include OProfile, other SystemTap probes, etc.)


struct profile_derived_probe: public derived_probe
{
  profile_derived_probe (systemtap_session &s, probe* p, probe_point* l);
  void join_group (systemtap_session& s);
};


struct profile_derived_probe_group: public generic_dpg<profile_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


profile_derived_probe::profile_derived_probe (systemtap_session &, probe* p, probe_point* l):
  derived_probe(p, l)
{ 
}


void
profile_derived_probe::join_group (systemtap_session& s)
{
  if (! s.profile_derived_probes)
    s.profile_derived_probes = new profile_derived_probe_group ();
  s.profile_derived_probes->enroll (this);
}


struct profile_builder: public derived_probe_builder
{
  profile_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const &,
		     vector<derived_probe *> & finished_results)
  {
    finished_results.push_back(new profile_derived_probe(sess, base, location));
  }
};


// timer.profile probe handlers are hooked up in an entertaining way
// to the underlying kernel facility.  The fact that 2.6.11+ era
// "register_timer_hook" API allows only one consumer *system-wide*
// will give a hint.  We will have a single entry function (and thus
// trivial registration / unregistration), and it will call all probe
// handler functions in sequence.

void
profile_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  // kernels < 2.6.10: use register_profile_notifier API
  // kernels >= 2.6.10: use register_timer_hook API
  s.op->newline() << "/* ---- profile probes ---- */";

  // This function calls all the profiling probe handlers in sequence.
  // The only tricky thing is that the context will be reused amongst
  // them.  While a simple sequence of calls to the individual probe
  // handlers is unlikely to go terribly wrong (with c->last_error
  // being set causing an early return), but for extra assurance, we
  // open-code the same logic here.

  s.op->newline() << "static void enter_all_profile_probes (struct pt_regs *regs) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "c->probe_point = \"timer.profile\";"; // NB: hard-coded for convenience
  s.op->newline() << "c->regs = regs;";

  for (unsigned i=0; i<probes.size(); i++)
    {
      if (i > 0)
        {
          // Some lightweight inter-probe context resetting 
          // XXX: not quite right: MAXERRORS not respected
          s.op->newline() << "c->actionremaining = MAXACTION;";
        }
      s.op->newline() << "if (c->last_error == NULL) " << probes[i]->name << " (c);";
    }
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";

  s.op->newline() << "#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)"; // == using_rpn of yore

  s.op->newline() << "int enter_profile_probes (struct notifier_block *self,"
                  << " unsigned long val, void *data) {";
  s.op->newline(1) << "(void) self; (void) val;";
  s.op->newline() << "enter_all_profile_probes ((struct pt_regs *) data);";
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";
  s.op->newline() << "struct notifier_block stap_profile_notifier = {"
                  << " .notifier_call = & enter_profile_probes };";
  
  s.op->newline() << "#else";

  s.op->newline() << "int enter_profile_probes (struct pt_regs *regs) {";
  s.op->newline(1) << "enter_all_profile_probes (regs);";
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  s.op->newline() << "#endif";
}


void
profile_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "probe_point = \"timer.profile\";"; // NB: hard-coded for convenience
  s.op->newline() << "#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)"; // == using_rpn of yore
  s.op->newline() << "rc = register_profile_notifier (& stap_profile_notifier);";
  s.op->newline() << "#else";
  s.op->newline() << "rc = register_timer_hook (& enter_profile_probes);";
  s.op->newline() << "#endif";
}


void
profile_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)"; // == using_rpn of yore
  s.op->newline() << "unregister_profile_notifier (& stap_profile_notifier);";
  s.op->newline() << "#else";
  s.op->newline() << "unregister_timer_hook (& enter_profile_probes);";
  s.op->newline() << "#endif";
  s.op->indent(-1);
}



// ------------------------------------------------------------------------
// statically inserted macro-based derived probes
// ------------------------------------------------------------------------


struct mark_derived_probe: public derived_probe
{
  mark_derived_probe (systemtap_session &s,
                      const string& probe_name, const string& probe_sig,
                      uintptr_t address, const string& module,
                      probe* base_probe);

  systemtap_session& sess;
  string probe_name, probe_sig;
  uintptr_t address;
  string module;
  string probe_sig_expanded;

  void join_group (systemtap_session& s);
  void emit_probe_context_vars (translator_output* o);
};


struct mark_derived_probe_group: public generic_dpg<mark_derived_probe>
{
public:
  void emit_module_decls (systemtap_session&) {}
  void emit_module_init (systemtap_session&) {}
  void emit_module_exit (systemtap_session&) {}
};


struct mark_var_expanding_copy_visitor: public var_expanding_copy_visitor
{
  mark_var_expanding_copy_visitor(systemtap_session& s,
                                  const string& ms, const string& pn):
    sess (s), mark_signature (ms), probe_name (pn) {}
  systemtap_session& sess;
  string mark_signature;
  string probe_name;

  void visit_target_symbol (target_symbol* e);
};


void
mark_var_expanding_copy_visitor::visit_target_symbol (target_symbol* e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (e->base_name.substr(0,4) != "$arg")
    throw semantic_error ("invalid target symbol for marker, $argN expected", e->tok);
  string argnum_s = e->base_name.substr(4,e->base_name.length()-4);
  int argnum = atoi (argnum_s.c_str());
  if (argnum < 1 || argnum > (int) mark_signature.size())
    throw semantic_error ("invalid marker argument number", e->tok);

  char argtype = mark_signature[argnum-1];

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;

  if (is_active_lvalue (e))
    throw semantic_error("write to marker parameter not permitted", e->tok);

  string fname = string("_mark_tvar_get")
    + "_" + e->base_name.substr(1)
    + "_" + lex_cast<string>(tick++);

  ec->code = string("THIS->__retvalue = CONTEXT->locals[0].")
    + probe_name + string(".__mark_arg")
    + lex_cast<string>(argnum) + string (";");
  ec->code += "/* pure */";
  fdecl->name = fname;
  fdecl->body = ec;
  fdecl->type = (argtype == 'N' ? pe_long :
                 argtype == 'S' ? pe_string :
                 pe_unknown); // cannot happen
  sess.functions.push_back(fdecl);

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session
  provide <functioncall*> (this, n);
}



mark_derived_probe::mark_derived_probe (systemtap_session &s,
                                        const string& p_n,
                                        const string& p_s,
                                        uintptr_t a,
                                        const string& m,
                                        probe* base):
  derived_probe (base, 0), sess (s), probe_name (p_n), probe_sig (p_s),
  address (a), module (m)
{
  // create synthetic probe point
  probe_point* pp = new probe_point;

  probe_point::component* c;
  if (module == "") c = new probe_point::component ("kernel");
  else c = new probe_point::component ("module",
                                    new literal_string (module));
  pp->components.push_back (c);
  c = new probe_point::component ("mark",
                                  new literal_string (probe_name));
  pp->components.push_back (c);
  this->locations.push_back (pp);

  // expand the signature string
  for (unsigned i=0; i<probe_sig.length(); i++)
    {
      if (i > 0)
        probe_sig_expanded += ", ";
      switch (probe_sig[i])
        {
        case 'N': probe_sig_expanded += "int64_t"; break;
        case 'S': probe_sig_expanded += "const char *"; break;
        default:
          throw semantic_error ("unsupported probe signature " + probe_sig,
                                this->tok);
        }
      probe_sig_expanded += " arg" + lex_cast<string>(i+1); // arg1 ...
    }

  // Now make a local-variable-expanded copy of the probe body
  mark_var_expanding_copy_visitor v (sess, probe_sig, name);
  require <block*> (&v, &(this->body), base->body);

  if (sess.verbose > 1)
    clog << "marker-based " << name << " address=0x" << hex << address << dec
         << " signature=" << probe_sig << endl;
}


void
mark_derived_probe::join_group (systemtap_session& s)
{
  throw semantic_error ("incomplete", this->tok);

  if (! s.mark_derived_probes)
    s.mark_derived_probes = new mark_derived_probe_group ();
  s.mark_derived_probes->enroll (this);
}


void
mark_derived_probe::emit_probe_context_vars (translator_output* o)
{
  // Save incoming arguments
  for (unsigned i=0; i<probe_sig.length(); i++)
    {
      string localname = "__mark_arg" + lex_cast<string>(i+1);
      switch (probe_sig[i])
        {
        case 'S': o->newline() << "string_t " << localname << ";"; break;
        case 'N': o->newline() << "int64_t " << localname << ";"; break;
        }
    }
}


#if 0
void
mark_derived_probe::emit_probe_entries (translator_output* o)
{
  assert (this->locations.size() == 1);

  o->newline() << "static void enter_" << name << " (" << probe_sig_expanded << ")";
  o->newline() << "{";
  o->newline(1) << "const char* probe_point = "
               << lex_cast_qstring(* this->locations[0]) << ";";
  emit_probe_prologue (o, "STAP_SESSION_RUNNING");

  // Save incoming arguments
  for (unsigned k=0; k<probe_sig.length(); k++)
    {
      string locals = "c->locals[0]." + name;
      string localname = locals + ".__mark_arg" + lex_cast<string>(k+1);
      string argname = "arg" + lex_cast<string>(k+1);
      switch (probe_sig[k])
        {
        case 'S': o->newline() << "strlcpy (" << localname << ", " << argname
                               << ", MAXSTRINGLEN);"; break;
          // XXX: dupe with c_unparser::c_strcpy
        case 'N': o->newline() << localname << " = " << argname << ";"; break;
        }
    }

  // NB: locals are initialized by probe function itself
  o->newline() << name << " (c);";

  emit_probe_epilogue (o);
  o->newline(-1) << "}";
}


void
mark_derived_probe::emit_registrations_start (translator_output* o,
					      unsigned index)
{
  assert (this->locations.size() == 1);

  o->newline() << "{";
  o->newline(1) << "void (**fn) (" << probe_sig_expanded << ") = (void *)"
                << address << "UL;";

  o->newline() << "#if __HAVE_ARCH_CMPXCHG";
  o->newline() << "unsigned long *fnpp = (unsigned long *) (void *) fn;";
  o->newline() << "unsigned long fnp = (unsigned long) (void *) & enter_" << name << ";";
  o->newline() << "unsigned long oldval = cmpxchg (fnpp, 0, fnp);";
  o->newline() << "if (oldval != 0) rc = 1;"; // XXX: could retry a few times
  o->newline() << "#else";
  // XXX: need proper synchronization for concurrent registration attempts
  o->newline() << "if (*fn == 0) *fn = & enter_" << name << ";";
  o->newline() << "#endif";
  o->newline() << "mb ();";
  o->newline() << "if (*fn != & enter_" << name << ") rc = 1;";

  o->newline(-1) << "}";


  // if one failed, must goto code (output by emit_registrations_end)
  // that will roll back completed registations for this probe
  o->newline() << "if (unlikely (rc)) {";
  o->newline(1) << "probe_point = "
	       << lex_cast_qstring (*this->locations[0]) << ";";
  if (index == 0)
    o->newline() << "goto mark_error;";
  else
    o->newline() << "goto unwind_mark_" << index - 1 << ";";
  o->newline(-1) << "}";
}


void
mark_derived_probe::emit_registrations_end (translator_output* o,
					    unsigned index)
{
  // if one failed, must roll back completed registations for this probe
  o->newline(-1) << "unwind_mark_" << index << ":";
  o->indent(1);
  emit_deregistrations (o);
}


void
mark_derived_probe::emit_deregistrations (translator_output * o)
{
  assert (this->locations.size() == 1);

  o->newline() << "{";
  o->newline(1) << "void (**fn) (" << probe_sig_expanded << ") = (void *)"
                << address << "UL;";
  o->newline() << "#if __HAVE_ARCH_CMPXCHG";
  o->newline() << "unsigned long *fnpp = (unsigned long *) (void *) fn;";
  o->newline() << "unsigned long fnp = (unsigned long) (void *) & enter_" << name << ";";
  o->newline() << "unsigned long oldval = cmpxchg (fnpp, fnp, 0);";
  o->newline() << "if (oldval != fnp) ;"; // XXX: should not happen
  o->newline() << "#else";
  o->newline(0) << "*fn = 0;";
  o->newline() << "#endif";
  o->newline(-1) << "}";
}
#endif


#if 0
void
mark_derived_probe_group::emit_probes (translator_output* op, unparser* up)
{
  for (unsigned i=0; i < probes.size(); i++)
    {
      op->newline ();
      up->emit_probe (probes[i]);
    }
}


void
mark_derived_probe_group::emit_module_init (translator_output* o)
{
  if (probes.size () == 0)
    return;

  // Output the mark probes create function
  o->newline() << "static int register_mark_probes (void) {";
  o->indent(1);
  o->newline() << "int rc = 0;";
  o->newline() << "const char *probe_point;";

  for (unsigned i=0; i < probes.size (); i++)
    probes[i]->emit_registrations_start (o, i);
  
  o->newline() << "goto out;";
  o->newline();

  for (int i=probes.size() - 2; i >= 0; i--)
    probes[i]->emit_registrations_end (o, i);

  o->newline();

  o->newline(-1) << "mark_error:";
  o->newline(1) << "if (unlikely (rc)) {";
  // In case it's just a lower-layer (kprobes) error that set rc but
  // not session_state, do that here to prevent any other BEGIN probe
  // from attempting to run.
  o->newline(1) << "atomic_set (&session_state, STAP_SESSION_ERROR);";
  o->newline() << "_stp_error (\"mark probe %s registration failed, rc=%d\\n\", probe_point, rc);";
  o->newline(-1) << "}\n";

  o->newline(-1) << "out:";
  o->newline(1) << "return rc;";
  o->newline(-1) << "}\n";

  // Output the mark probes destroy function
  o->newline() << "static void unregister_mark_probes (void) {";
  o->indent(1);

  for (unsigned i=0; i < probes.size (); i++)
    {
      probes[i]->emit_deregistrations (o);
      emit_probe_timing(probes[i], o);
    }

  o->newline(-1) << "}\n";
}
#endif


struct symboltable_extract
{
  uintptr_t address;
  string symbol;
  string module;
};


#define PROBE_SYMBOL_PREFIX "__systemtap_mark_"


struct mark_builder: public derived_probe_builder
{
private:
  static const vector<symboltable_extract>* get_symbols (systemtap_session&);

public:
  mark_builder() {}
  void build(systemtap_session & sess,
             probe * base,
             probe_point * location,
             std::map<std::string, literal *> const & parameters,
             vector<derived_probe *> & finished_results);
};


// Until elfutils makes this straightforward, we kludge.
// See also translate.cxx:emit_symbol_data().

const vector<symboltable_extract>*
mark_builder::get_symbols (systemtap_session& sess)
{
  static vector<symboltable_extract>* syms = 0;
  if (syms) return syms; // already computed

  syms = new vector<symboltable_extract>;

  // Process /proc/kallsyms - contains reliable module symbols
  ifstream kallsyms ("/proc/kallsyms");
  while (! kallsyms.eof())
    {
      string addr, type, sym, module;
      kallsyms >> addr >> type >> sym;
      kallsyms >> ws;
      if (kallsyms.peek() == '[')
        {
          string bracketed;
          kallsyms >> bracketed;
          module = bracketed.substr (1, bracketed.length()-2);
        }
      else // kernel symbols come from /boot/System.map*
        continue;

      if (type == "b" || type == "d") // static data/bss
        {
          symboltable_extract e;
          e.address = strtoul (addr.c_str(), 0, 16);
          e.symbol = sym;
          e.module = module;
          syms->push_back (e);
        }
    }
  kallsyms.close ();

  // grab them kernel symbols
  string smname = "/boot/System.map-";
  smname += sess.kernel_release;
  ifstream systemmap (smname.c_str());
  while (! systemmap.eof())
    {
      string addr, type, sym, module;
      systemmap >> addr >> type >> sym;
      module = "";

      if (type == "b" || type == "d") // static data/bss
        {
          symboltable_extract e;
          e.address = strtoul (addr.c_str(), 0, 16);
          e.symbol = sym;
          e.module = module;
          syms->push_back (e);
        }
    }
  systemmap.close ();

  return syms;
}


void
mark_builder::build(systemtap_session & sess,
                      probe * base,
                      probe_point * location,
                      std::map<std::string, literal *> const & parameters,
                      vector<derived_probe *> & finished_results)
{
  const vector<symboltable_extract>* syms = get_symbols (sess);

  string param_module;
  bool has_module = get_param (parameters, "module", param_module);
  bool has_kernel = (parameters.find("kernel") != parameters.end());

  if (! (has_module ^ has_kernel))
    throw semantic_error ("need kernel or module() component", location->tok);

  string param_probe;
  bool has_probe = get_param (parameters, "mark", param_probe);
  if (! has_probe)
    throw semantic_error ("need mark() component", location->tok);

  string symbol_regex = PROBE_SYMBOL_PREFIX "([a-zA-Z0-9_]+)_([NS]*)\\.[0-9]+";
  //                    ^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^   ^^^^^    ^^^^^^
  //                       common prefix        probe name    types    suffix
  regex_t symbol_regex_t;
  int rc = regcomp (& symbol_regex_t, symbol_regex.c_str(), REG_EXTENDED);
  if (rc)
    throw semantic_error ("regcomp '" + symbol_regex + "' failed");

  // cout << "searching for " << symbol_regex << endl;

  for (unsigned i=0; i<syms->size(); i++)
    {
      regmatch_t match[3];
      const symboltable_extract& ext = syms->at(i);
      const char* symstr = ext.symbol.c_str();

      rc = regexec (& symbol_regex_t, symstr, 3, match, 0);
      if (! rc) // match
        {
#if 0
          cout << "match in " << symstr << ":"
               << "[" << match[0].rm_so << "-" << match[0].rm_eo << "],"
               << "[" << match[1].rm_so << "-" << match[1].rm_eo << "],"
               << "[" << match[2].rm_so << "-" << match[2].rm_eo << "]"
               << endl;
#endif

          string probe_name = string (symstr + match[1].rm_so,
                                      (match[1].rm_eo - match[1].rm_so));
          string probe_sig = string (symstr + match[2].rm_so,
                                     (match[2].rm_eo - match[2].rm_so));

          // Below, "rc" has negative polarity: zero iff matching
          rc = (has_module
                ? fnmatch (param_module.c_str(), ext.module.c_str(), 0)
                : (ext.module != "")); // kernel.*
          rc |= fnmatch (param_probe.c_str(), probe_name.c_str(), 0);

          if (! rc)
            {
              // cout << "match (" << probe_name << "):" << probe_sig << endl;

              derived_probe *dp
                = new mark_derived_probe (sess,
                                          probe_name, probe_sig,
                                          ext.address,
                                          ext.module,
                                          base);
              finished_results.push_back (dp);
            }
        }
    }

  //  cout << "done" << endl;

  // It's not a big deal if this is skipped due to an exception.
  regfree (& symbol_regex_t);
}


// ------------------------------------------------------------------------
// hrtimer derived probes
// ------------------------------------------------------------------------
// This is a new timer interface that provides more flexibility in specifying
// intervals, and uses the hrtimer APIs when available for greater precision.
// While hrtimers were added in 2.6.16, the API's weren't exported until
// 2.6.17, so we must check this kernel version before attempting to use
// hrtimers.
//
// * hrtimer_derived_probe: creates a probe point based on the hrtimer APIs.


struct hrtimer_derived_probe: public derived_probe
{
  // set a (generous) maximum of one day in ns
  static const int64_t max_ns_interval = 1000000000LL * 60LL * 60LL * 24LL;

  // 100us seems like a reasonable minimum
  static const int64_t min_ns_interval = 100000LL;

  int64_t interval, randomize;

  hrtimer_derived_probe (probe* p, probe_point* l, int64_t i, int64_t r):
    derived_probe (p, l), interval (i), randomize (r)
  {
    if ((i < min_ns_interval) || (i > max_ns_interval))
      throw semantic_error("interval value out of range");

    // randomize = 0 means no randomization
    if ((r < 0) || (r > i))
      throw semantic_error("randomization value out of range");
  }

  void join_group (systemtap_session& s);
};


struct hrtimer_derived_probe_group: public generic_dpg<hrtimer_derived_probe>
{
  void emit_interval (translator_output* o);
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


void
hrtimer_derived_probe::join_group (systemtap_session& s)
{
  if (! s.hrtimer_derived_probes)
    s.hrtimer_derived_probes = new hrtimer_derived_probe_group ();
  s.hrtimer_derived_probes->enroll (this);
}


void
hrtimer_derived_probe_group::emit_interval (translator_output* o)
{
  o->line() << "({";
  o->newline(1) << "unsigned long nsecs;";
  o->newline() << "int64_t i = stp->intrv;";
  o->newline() << "if (stp->rnd != 0) {";
  // XXX: why not use stp_random_pm instead of this?
  o->newline(1) << "int64_t r;";
  o->newline() << "get_random_bytes(&r, sizeof(r));";
  // ensure that r is positive
  o->newline() << "r &= ((uint64_t)1 << (8*sizeof(r) - 1)) - 1;";
  o->newline() << "r = _stp_mod64(NULL, r, (2*stp->rnd+1));";
  o->newline() << "r -= stp->rnd;";
  o->newline() << "i += r;";
  o->newline(-1) << "}";
  o->newline() << "if (unlikely(i < stap_hrtimer_resolution))";
  o->newline(1) << "i = stap_hrtimer_resolution;";
  o->indent(-1);
  o->newline() << "nsecs = do_div(i, NSEC_PER_SEC);";
  o->newline() << "ktime_set(i, nsecs);";
  o->newline(-1) << "})";
}


void
hrtimer_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- hrtimer probes ---- */";

  s.op->newline() << "unsigned long stap_hrtimer_resolution;"; // init later
  s.op->newline() << "struct stap_hrtimer_probe {";
  s.op->newline(1) << "struct hrtimer hrtimer;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "int64_t intrv, rnd;";
  s.op->newline(-1) << "} stap_hrtimer_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{"; 
      s.op->line() << " .pp=" << lex_cast_qstring (*probes[i]->sole_location()) << ",";
      s.op->line() << " .ph=&" << probes[i]->name << ",";
      s.op->line() << " .intrv=" << probes[i]->interval << "LL,";
      s.op->line() << " .rnd=" << probes[i]->randomize << "LL";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();

  // autoconf: adapt to HRTIMER_REL -> HRTIMER_MODE_REL renaming near 2.6.21
  s.op->newline() << "#ifdef STAPCONF_HRTIMER_REL";
  s.op->newline() << "#define HRTIMER_MODE_REL HRTIMER_REL";
  s.op->newline() << "#endif";
  
  // The function signature changed in 2.6.21.
  s.op->newline() << "#ifdef STAPCONF_HRTIMER_REL";
  s.op->newline() << "static int ";
  s.op->newline() << "#else";
  s.op->newline() << "static enum hrtimer_restart ";
  s.op->newline() << "#endif";
  s.op->newline() << "enter_hrtimer_probe (struct hrtimer *timer) {";

  s.op->newline(1) << "int rc = HRTIMER_NORESTART;"; 
  s.op->newline() << "struct stap_hrtimer_probe *stp = container_of(timer, struct stap_hrtimer_probe, hrtimer);";
  s.op->newline() << "if ((atomic_read (&session_state) == STAP_SESSION_STARTING) ||";
  s.op->newline() << "    (atomic_read (&session_state) == STAP_SESSION_RUNNING)) {";
  // Compute next trigger time
  s.op->newline(1) << "timer->expires = ktime_add (timer->expires,";
  emit_interval (s.op);
  s.op->line() << ");";
  s.op->newline() << "rc = HRTIMER_RESTART;"; 
  s.op->newline(-1) << "}";
  s.op->newline() << "{";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "c->probe_point = stp->pp;";
  s.op->newline() << "(*stp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";
  s.op->newline() << "return rc;";
  s.op->newline(-1) << "}";
}


void
hrtimer_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "{";
  s.op->newline(1) << "struct timespec res;";
  s.op->newline() << "hrtimer_get_res (CLOCK_MONOTONIC, &res);";
  s.op->newline() << "stap_hrtimer_resolution = timespec_to_ns (&res);";
  s.op->newline(-1) << "}";

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_hrtimer_probe* stp = & stap_hrtimer_probes [i];";
  s.op->newline() << "probe_point = stp->pp;";
  s.op->newline() << "hrtimer_init (& stp->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);";
  s.op->newline() << "stp->hrtimer.function = & enter_hrtimer_probe;";
  // There is no hrtimer field to identify *this* (i-th) probe handler
  // callback.  So instead we'll deduce it at entry time.
  s.op->newline() << "(void) hrtimer_start (& stp->hrtimer, ";
  emit_interval (s.op);
  s.op->line() << ", HRTIMER_MODE_REL);";
  // Note: no partial failure rollback is needed: hrtimer_start only
  // "fails" if the timer was already active, which cannot be.
  s.op->newline(-1) << "}"; // for loop
}


void
hrtimer_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "hrtimer_cancel (& stap_hrtimer_probes[i].hrtimer);";
  s.op->indent(-1);
}



struct timer_builder: public derived_probe_builder
{
    virtual void build(systemtap_session & sess,
	probe * base, probe_point * location,
	std::map<std::string, literal *> const & parameters,
	vector<derived_probe *> & finished_results);

    static void register_patterns(match_node *root);
};

void
timer_builder::build(systemtap_session & sess,
    probe * base,
    probe_point * location,
    std::map<std::string, literal *> const & parameters,
    vector<derived_probe *> & finished_results)
{
  int64_t period, rand=0;

  if (!get_param(parameters, "randomize", rand))
    rand = 0;

  if (get_param(parameters, "jiffies", period))
    {
      // always use basic timers for jiffies
      finished_results.push_back(
	  new timer_derived_probe(base, location, period, rand, false));
      return;
    }
  else if (get_param(parameters, "hz", period))
    {
      if (period <= 0)
	throw semantic_error ("frequency must be greater than 0");
      period = (1000000000 + period - 1)/period;
    }
  else if (get_param(parameters, "s", period)
      || get_param(parameters, "sec", period))
    {
      period *= 1000000000;
      rand *= 1000000000;
    }
  else if (get_param(parameters, "ms", period)
      || get_param(parameters, "msec", period))
    {
      period *= 1000000;
      rand *= 1000000;
    }
  else if (get_param(parameters, "us", period)
      || get_param(parameters, "usec", period))
    {
      period *= 1000;
      rand *= 1000;
    }
  else if (get_param(parameters, "ns", period)
      || get_param(parameters, "nsec", period))
    {
      // ok
    }
  else
    throw semantic_error ("unrecognized timer variant");

  // Redirect wallclock-time based probes to hrtimer code on recent
  // enough kernels.
  if (strverscmp(sess.kernel_base_release.c_str(), "2.6.17") < 0)
    {
      // hrtimers didn't exist, so use the old-school timers
      period = (period + 1000000 - 1)/1000000;
      rand = (rand + 1000000 - 1)/1000000;

      finished_results.push_back(
	  new timer_derived_probe(base, location, period, rand, true));
    }
  else
    finished_results.push_back(
	new hrtimer_derived_probe(base, location, period, rand));
}

void
timer_builder::register_patterns(match_node *root)
{
  derived_probe_builder *builder = new timer_builder();

  root = root->bind("timer");

  root->bind_num("s")->bind(builder);
  root->bind_num("s")->bind_num("randomize")->bind(builder);
  root->bind_num("sec")->bind(builder);
  root->bind_num("sec")->bind_num("randomize")->bind(builder);

  root->bind_num("ms")->bind(builder);
  root->bind_num("ms")->bind_num("randomize")->bind(builder);
  root->bind_num("msec")->bind(builder);
  root->bind_num("msec")->bind_num("randomize")->bind(builder);

  root->bind_num("us")->bind(builder);
  root->bind_num("us")->bind_num("randomize")->bind(builder);
  root->bind_num("usec")->bind(builder);
  root->bind_num("usec")->bind_num("randomize")->bind(builder);

  root->bind_num("ns")->bind(builder);
  root->bind_num("ns")->bind_num("randomize")->bind(builder);
  root->bind_num("nsec")->bind(builder);
  root->bind_num("nsec")->bind_num("randomize")->bind(builder);

  root->bind_num("jiffies")->bind(builder);
  root->bind_num("jiffies")->bind_num("randomize")->bind(builder);
  
  root->bind_num("hz")->bind(builder);
}


// ------------------------------------------------------------------------
// perfmon derived probes
// ------------------------------------------------------------------------
// This is a new interface to the perfmon hw.
//


struct perfmon_var_expanding_copy_visitor: public var_expanding_copy_visitor
{
  systemtap_session & sess;
  unsigned counter_number;
  perfmon_var_expanding_copy_visitor(systemtap_session & s, unsigned c):
	  sess(s), counter_number(c) {}
  void visit_target_symbol (target_symbol* e);
};


void
perfmon_var_expanding_copy_visitor::visit_target_symbol (target_symbol *e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;
  bool lvalue = is_active_lvalue(e);

  if (lvalue )
    throw semantic_error("writes to $counter not permitted");

  string fname = string("_perfmon_tvar_get")
		  + "_" + e->base_name.substr(1)
		  + "_" + lex_cast<string>(counter_number);

  if (e->base_name != "$counter")
    throw semantic_error ("target variables not available to perfmon probes");

  ec->code = "THIS->__retvalue = _pfm_pmd_x[" + 
	  lex_cast<string>(counter_number) + "].reg_num;";
  ec->code += "/* pure */";
  fdecl->name = fname;
  fdecl->body = ec;
  fdecl->type = pe_long;
  sess.functions.push_back(fdecl);

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0;  // NB: must not resolve yet, to ensure inclusion in session

  provide <functioncall*> (this, n);
}


enum perfmon_mode
{
  perfmon_count,
  perfmon_sample
};


struct perfmon_derived_probe: public derived_probe
{
protected:
  static unsigned probes_allocated;

public:
  systemtap_session & sess;
  string event;
  perfmon_mode mode;

  perfmon_derived_probe (probe* p, probe_point* l, systemtap_session &s,
			 string e, perfmon_mode m);
  virtual void join_group (systemtap_session& s);
};


struct perfmon_derived_probe_group: public generic_dpg<perfmon_derived_probe>
{
public:
  void emit_module_decls (systemtap_session&) {}
  void emit_module_init (systemtap_session&) {}
  void emit_module_exit (systemtap_session&) {}
};


struct perfmon_builder: public derived_probe_builder
{
  perfmon_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    string event;
    if (!get_param (parameters, "counter", event))
      throw semantic_error("perfmon requires an event");

    sess.perfmon++;

    // XXX: need to revise when doing sampling 
    finished_results.push_back(new perfmon_derived_probe(base, location,
							 sess, event,
							 perfmon_count));
  }
};


unsigned perfmon_derived_probe::probes_allocated;

perfmon_derived_probe::perfmon_derived_probe (probe* p, probe_point* l,
					      systemtap_session &s,
					      string e, perfmon_mode m)
	: derived_probe (p, l), sess(s), event(e), mode(m)
{
  ++probes_allocated;

  // Now make a local-variable-expanded copy of the probe body
  perfmon_var_expanding_copy_visitor v (sess, probes_allocated-1);
  require <block*> (&v, &(this->body), base->body);

  if (sess.verbose > 1)
    clog << "perfmon-based probe" << endl;
}


void
perfmon_derived_probe::join_group (systemtap_session& s)
{
  throw semantic_error ("incomplete", this->tok);

  if (! s.perfmon_derived_probes)
    s.perfmon_derived_probes = new perfmon_derived_probe_group ();
  s.perfmon_derived_probes->enroll (this);
}


#if 0
void
perfmon_derived_probe::emit_registrations_start (translator_output* o,
						 unsigned index)
{
  for (unsigned i=0; i<locations.size(); i++)
    o->newline() << "enter_" << name << "_" << i << " ();";
}


void
perfmon_derived_probe::emit_registrations_end (translator_output * o,
					       unsigned index)
{
}


void
perfmon_derived_probe::emit_deregistrations (translator_output * o)
{
}


void
perfmon_derived_probe::emit_probe_entries (translator_output * o)
{
  o->newline() << "#ifdef STP_TIMING";
  // NB: This variable may be multiply (but identically) defined.
  o->newline() << "static __cacheline_aligned Stat " << "time_" << basest()->name << ";";
  o->newline() << "#endif";

  for (unsigned i=0; i<locations.size(); i++)
    {
      probe_point *l = locations[i];
      o->newline() << "/* location " << i << ": " << *l << " */";
      o->newline() << "static void enter_" << name << "_" << i << " (void) {";

      o->indent(1);
      o->newline() << "const char* probe_point = "
                   << lex_cast_qstring(*l) << ";";
      emit_probe_prologue (o,
                           (mode == perfmon_count ?
                            "STAP_SESSION_STARTING" :
                            "STAP_SESSION_RUNNING"));

      // NB: locals are initialized by probe function itself
      o->newline() << name << " (c);";

      emit_probe_epilogue (o);

      o->newline(-1) << "}\n";
    }
}
#endif


#if 0
void no_pfm_event_error (string s)
{
  string msg(string("Cannot find event:" + s));
  throw semantic_error(msg);
}


void no_pfm_mask_error (string s)
{
  string msg(string("Cannot find mask:" + s));
  throw semantic_error(msg);
}


void
split(const string& s, vector<string>& v, const string & separator)
{
  string::size_type last_pos = s.find_first_not_of(separator, 0);
  string::size_type pos = s.find_first_of(separator, last_pos);

  while (string::npos != pos || string::npos != last_pos) {
    v.push_back(s.substr(last_pos, pos - last_pos));
    last_pos = s.find_first_not_of(separator, pos);
    pos = s.find_first_of(separator, last_pos);
  }
}


void
perfmon_derived_probe_group::emit_probes (translator_output* op, unparser* up)
{
  for (unsigned i=0; i < probes.size(); i++)
    {
      op->newline ();
      up->emit_probe (probes[i]);
    }
}


void
perfmon_derived_probe_group::emit_module_init (translator_output* o)
{
  int ret;
  pfmlib_input_param_t inp;
  pfmlib_output_param_t outp;
  pfarg_pmd_t pd[PFMLIB_MAX_PMDS];
  pfarg_pmc_t pc[PFMLIB_MAX_PMCS];
  pfarg_ctx_t ctx;
  pfarg_load_t load_args;
  pfmlib_options_t pfmlib_options;
  unsigned int max_counters;

  if ( probes.size() == 0)
	  return;
  ret = pfm_initialize();
  if (ret != PFMLIB_SUCCESS)
    throw semantic_error("Unable to generate performance monitoring events (no libpfm)");

  pfm_get_num_counters(&max_counters);

  memset(&pfmlib_options, 0, sizeof(pfmlib_options));
  pfmlib_options.pfm_debug   = 0; /* set to 1 for debug */
  pfmlib_options.pfm_verbose = 0; /* set to 1 for debug */
  pfm_set_options(&pfmlib_options);

  memset(pd, 0, sizeof(pd));
  memset(pc, 0, sizeof(pc));
  memset(&ctx, 0, sizeof(ctx));
  memset(&load_args, 0, sizeof(load_args));

  /*
   * prepare parameters to library.
   */
  memset(&inp,0, sizeof(inp));
  memset(&outp,0, sizeof(outp));

  /* figure out the events */
  for (unsigned i=0; i<probes.size(); ++i)
    {
      if (probes[i]->event == "cycles") {
	if (pfm_get_cycle_event( &inp.pfp_events[i].event) != PFMLIB_SUCCESS)
	  no_pfm_event_error(probes[i]->event);
      } else if (probes[i]->event == "instructions") {
	if (pfm_get_inst_retired_event( &inp.pfp_events[i].event) !=
	    PFMLIB_SUCCESS)
	  no_pfm_event_error(probes[i]->event);
      } else {
	unsigned int event_id = 0;
	unsigned int mask_id = 0;
	vector<string> event_spec;
	split(probes[i]->event, event_spec, ":");
	int num =  event_spec.size();
	int masks = num - 1;

	if (num == 0)
	  throw semantic_error("No events found");

	/* setup event */
	if (pfm_find_event(event_spec[0].c_str(), &event_id) != PFMLIB_SUCCESS)
	  no_pfm_event_error(event_spec[0]);
	inp.pfp_events[i].event = event_id;

	/* set up masks */
	if (masks > PFMLIB_MAX_MASKS_PER_EVENT)
	  throw semantic_error("Too many unit masks specified");

	for (int j=0; j < masks; j++) {
		if (pfm_find_event_mask(event_id, event_spec[j+1].c_str(),
					&mask_id) != PFMLIB_SUCCESS)
	    no_pfm_mask_error(string(event_spec[j+1]));
	  inp.pfp_events[i].unit_masks[j] = mask_id;
	}
	inp.pfp_events[i].num_masks = masks;
      }
    }

  /* number of counters in use */
  inp.pfp_event_count = probes.size();

  // XXX: no elimination of duplicated counters
  if (inp.pfp_event_count>max_counters)
	  throw semantic_error("Too many performance monitoring events.");

  /* count events both in kernel and user-space */
  inp.pfp_dfl_plm   = PFM_PLM0 | PFM_PLM3;

  /* XXX: some cases a perfmon register might be used of watch dog 
     this code doesn't handle that case */

  /* figure out the pmcs for the events */
  if ((ret=pfm_dispatch_events(&inp, NULL, &outp, NULL)) != PFMLIB_SUCCESS)
	  throw semantic_error("Cannot configure events");

  for (unsigned i=0; i < outp.pfp_pmc_count; i++) {
    pc[i].reg_num   = outp.pfp_pmcs[i].reg_num;
    pc[i].reg_value = outp.pfp_pmcs[i].reg_value;
  }

  /*
   * There could be more pmc settings than pmd.
   * Figure out the actual pmds to use.
   */
  for (unsigned i=0, j=0; i < inp.pfp_event_count; i++) {
    pd[i].reg_num   = outp.pfp_pmcs[j].reg_pmd_num;
    for(; j < outp.pfp_pmc_count; j++)
      if (outp.pfp_pmcs[j].reg_evt_idx != i) break;
  }

  // Output the be probes create function
  o->newline() << "static int register_perfmon_probes (void) {";
  o->newline(1) << "int rc = 0;";

  o->newline() << "/* data for perfmon */";
  o->newline() << "static int _pfm_num_pmc = " << outp.pfp_pmc_count << ";";
  o->newline() << "static struct pfarg_pmc _pfm_pmc[" << outp.pfp_pmc_count
	       << "] = {";
  /* output the needed bits for pmc here */
  for (unsigned i=0; i < outp.pfp_pmc_count; i++) {
    o->newline() << "{.reg_num=" << pc[i].reg_num << ", "
		 << ".reg_value=" << lex_cast_hex<string>(pc[i].reg_value)
		 << "},";
  }

  o->newline() << "};";
  o->newline() << "static int _pfm_num_pmd = " << inp.pfp_event_count << ";";
  o->newline() << "static struct pfarg_pmd _pfm_pmd[" << inp.pfp_event_count
	       << "] = {";
  /* output the needed bits for pmd here */
  for (unsigned i=0; i < inp.pfp_event_count; i++) {
    o->newline() << "{.reg_num=" << pd[i].reg_num << ", "
		 << ".reg_value=" << pd[i].reg_value << "},";
  }
  o->newline() << "};";
  o->newline();

  o->newline() << "_pfm_pmc_x=_pfm_pmc;";
  o->newline() << "_pfm_num_pmc_x=_pfm_num_pmc;";
  o->newline() << "_pfm_pmd_x=_pfm_pmd;";
  o->newline() << "_pfm_num_pmd_x=_pfm_num_pmd;";

  // call all the function bodies associated with perfcounters
  for (unsigned i=0; i < probes.size (); i++)
    probes[i]->emit_registrations_start (o,i);

  /* generate call to turn on instrumentation */
  o->newline() << "_pfm_context.ctx_flags |= PFM_FL_SYSTEM_WIDE;";
  o->newline() << "rc = rc || _stp_perfmon_setup(&_pfm_desc, &_pfm_context,";
  o->newline(1) << "_pfm_pmc, _pfm_num_pmc,";
  o->newline() << "_pfm_pmd, _pfm_num_pmd);";
  o->newline(-1);

  o->newline() << "return rc;";
  o->newline(-1) << "}\n";

  // Output the be probes destroy function
  o->newline() << "static void unregister_perfmon_probes (void) {";
  o->newline(1) << "_stp_perfmon_shutdown(_pfm_desc);";
  o->newline(-1) << "}\n";
}
#endif


// ------------------------------------------------------------------------
//  Standard tapset registry.
// ------------------------------------------------------------------------

void
register_standard_tapsets(systemtap_session & s)
{
  s.pattern_root->bind("begin")->bind(new be_builder(true));
  s.pattern_root->bind_num("begin")->bind(new be_builder(true));
  s.pattern_root->bind("end")->bind(new be_builder(false));
  s.pattern_root->bind_num("end")->bind(new be_builder(false));

  s.pattern_root->bind("never")->bind(new never_builder());

  timer_builder::register_patterns(s.pattern_root);
  s.pattern_root->bind("timer")->bind("profile")->bind(new profile_builder());
  s.pattern_root->bind("perfmon")->bind_str("counter")->bind(new perfmon_builder());

  // dwarf-based kernel/module parts
  dwarf_derived_probe::register_patterns(s.pattern_root);

  // marker-based kernel/module parts
  s.pattern_root->bind("kernel")->bind_str("mark")->bind(new mark_builder());
  s.pattern_root->bind_str("module")->bind_str("mark")->bind(new mark_builder());
}


vector<derived_probe_group*>
all_session_groups(systemtap_session& s)
{
  vector<derived_probe_group*> g;
#define DOONE(x) if (s. x##_derived_probes) g.push_back (s. x##_derived_probes)

  // Note that order *is* important here.  We want to make sure we
  // register (actually run) begin probes before any other probe type
  // is run.  Similarly, when unregistering probes, we want to
  // unregister (actually run) end probes after every other probe type
  // has be unregistered.  To do the latter,
  // c_unparser::emit_module_exit() will run this list backwards.
  DOONE(be);
  DOONE(dwarf);
  DOONE(timer);
  DOONE(profile);
  DOONE(mark);
  DOONE(hrtimer);
  DOONE(perfmon);
#undef DOONE
  return g;
}
