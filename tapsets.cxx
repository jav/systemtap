// tapset resolution
// Copyright (C) 2005-2008 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008 James.Bottomley@HansenPartnership.com
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
#include "dwarf_wrappers.h"
#include "auto_free.h"

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
#include <elfutils/libebl.h>
#include <dwarf.h>
#include <elf.h>
#include <obstack.h>
#include <regex.h>
#include <glob.h>
#include <fnmatch.h>
#include <stdio.h>

#include "loc2c.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}


#ifdef PERFMON
#include <perfmon/pfmlib.h>
#include <perfmon/perfmon.h>
#endif

using namespace std;
using namespace __gnu_cxx;

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
// begin/end/error probes are run right during registration / deregistration
// ------------------------------------------------------------------------

static string TOK_BEGIN("begin");
static string TOK_END("end");
static string TOK_ERROR("error");

enum be_t { BEGIN, END, ERROR };

struct be_derived_probe: public derived_probe
{
  be_t type;
  int64_t priority;

  be_derived_probe (probe* p, probe_point* l, be_t t, int64_t pr):
    derived_probe (p, l), type (t), priority (pr) {}

  void join_group (systemtap_session& s);

  static inline bool comp(be_derived_probe const *a,
			  be_derived_probe const *b)
  {
    // This allows the BEGIN/END/ERROR probes to intermingle.
    // But that's OK - they're always treversed with a nested
    // "if (type==FOO)" conditional.
    return a->priority < b->priority;
  }

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
  be_t type;

  be_builder(be_t t) : type(t) {}

  virtual void build(systemtap_session &,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    int64_t priority;
    if ((type == BEGIN && !get_param(parameters, TOK_BEGIN, priority)) ||
        (type == END && !get_param(parameters, TOK_END, priority)) ||
        (type == ERROR && !get_param(parameters, TOK_ERROR, priority)))
      priority = 0;
    finished_results.push_back(
	new be_derived_probe(base, location, type, priority));
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
  o->newline() << "c->last_stmt = 0;";
  o->newline() << "c->last_error = 0;";
  o->newline() << "c->nesting = 0;";
  o->newline() << "c->regs = 0;";
  o->newline() << "c->unwaddr = 0;";
  // reset unwound address cache
  o->newline() << "c->pi = 0;";
  o->newline() << "c->regparm = 0;";
  o->newline() << "c->probe_point = 0;";
  if (! interruptible)
    o->newline() << "c->actionremaining = MAXACTION;";
  else
    o->newline() << "c->actionremaining = MAXACTION_INTERRUPTIBLE;";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "c->statp = 0;";
  o->newline() << "#endif";
  // NB: The following would actually be incorrect.
  // That's because cycles_sum/cycles_base values are supposed to survive
  // between consecutive probes.  Periodically (STP_OVERLOAD_INTERVAL
  // cycles), the values will be reset.
  /*
  o->newline() << "#ifdef STP_OVERLOAD";
  o->newline() << "c->cycles_sum = 0;";
  o->newline() << "c->cycles_base = 0;";
  o->newline() << "#endif";  
  */
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
      o->newline() << "atomic_inc (&error_count);";
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
  s.op->newline() << "void enter_begin_probe (void (*fn)(struct context*), const char* pp) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_STARTING", false, true);
  s.op->newline() << "c->probe_point = pp;";
  s.op->newline() << "(*fn) (c);";
  common_probe_entryfn_epilogue (s.op, false, true);
  s.op->newline(-1) << "}";

  s.op->newline() << "void enter_end_probe (void (*fn)(struct context*), const char* pp) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_STOPPING", false, true);
  s.op->newline() << "c->probe_point = pp;";
  s.op->newline() << "(*fn) (c);";
  common_probe_entryfn_epilogue (s.op, false, true);
  s.op->newline(-1) << "}";

  s.op->newline() << "void enter_error_probe (void (*fn)(struct context*), const char* pp) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_ERROR", false, true);
  s.op->newline() << "c->probe_point = pp;";
  s.op->newline() << "(*fn) (c);";
  common_probe_entryfn_epilogue (s.op, false, true);
  s.op->newline(-1) << "}";

  s.op->newline() << "struct stap_be_probe {";
  s.op->newline(1) << "void (*ph)(struct context*);";
  s.op->newline() << "const char* pp;";
  s.op->newline() << "int type;";
  s.op->newline(-1) << "} stap_be_probes[] = {";
  s.op->indent(1);

  // NB: We emit the table in sorted order here, so we don't have to
  // store the priority numbers as integers and sort at run time.

  sort(probes.begin(), probes.end(), be_derived_probe::comp);

  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{";
      s.op->line() << " .pp="
                   << lex_cast_qstring (*probes[i]->sole_location()) << ",";
      s.op->line() << " .ph=&" << probes[i]->name << ",";
      s.op->line() << " .type=" << probes[i]->type;
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
}

void
be_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_be_probe* stp = & stap_be_probes [i];";
  s.op->newline() << "if (stp->type != " << BEGIN << ") continue;";
  s.op->newline() << "enter_begin_probe (stp->ph, stp->pp);";
  s.op->newline() << "/* rc = 0; */";
  // NB: begin probes that cause errors do not constitute registration
  // failures.  An error message will probably get printed and if
  // MAXERRORS was left at 1, we'll get an stp_exit.  The
  // error-handling probes will be run during the ordinary
  // unregistration phase.
  s.op->newline(-1) << "}";
}

void
be_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_be_probe* stp = & stap_be_probes [i];";
  s.op->newline() << "if (stp->type != " << END << ") continue;";
  s.op->newline() << "enter_end_probe (stp->ph, stp->pp);";
  s.op->newline(-1) << "}";

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_be_probe* stp = & stap_be_probes [i];";
  s.op->newline() << "if (stp->type != " << ERROR << ") continue;";
  s.op->newline() << "enter_error_probe (stp->ph, stp->pp);";
  s.op->newline(-1) << "}";
}



// ------------------------------------------------------------------------
// never probes are never run
// ------------------------------------------------------------------------

static string TOK_NEVER("never");

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
		     literal_map_t const &,
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
static string TOK_PROCESS("process");

// Can we handle this query with just symbol-table info?
enum dbinfo_reqt
{
  dbr_unknown,
  dbr_none,		// kernel.statement(NUM).absolute
  dbr_need_symtab,	// can get by with symbol table if there's no dwarf
  dbr_need_dwarf
};

enum info_status
{
  info_unknown,
  info_present,
  info_absent
};

struct
func_info
{
  func_info()
    : decl_file(NULL), decl_line(-1), addr(0), prologue_end(0), weak(false)
  {
    memset(&die, 0, sizeof(die));
  }
  string name;
  char const * decl_file;
  int decl_line;
  Dwarf_Die die;
  Dwarf_Addr addr;
  Dwarf_Addr entrypc;
  Dwarf_Addr prologue_end;
  bool weak;
  // Comparison functor for list of functions sorted by address. The
  // two versions that take a Dwarf_Addr let us use the STL algorithms
  // upper_bound, equal_range et al., but we don't know whether the
  // searched-for value will be passed as the first or the second
  // argument.
  struct Compare
  {
    bool operator() (const func_info* f1, const func_info* f2) const
    {
      return f1->addr < f2->addr;
    }
    // For doing lookups by address.
    bool operator() (Dwarf_Addr addr, const func_info* f2) const
    {
      return addr < f2->addr;
    }
    bool operator() (const func_info* f1, Dwarf_Addr addr) const
    {
      return f1->addr < addr;
    }
  };
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
  Dwarf_Addr entrypc;
  Dwarf_Die die;
};


struct dwarf_query; // forward decls
struct dwflpp;
struct symbol_table;


struct
module_info
{
  Dwfl_Module* mod;
  const char* name;
  string elf_path;
  Dwarf_Addr addr;
  Dwarf_Addr bias;
  symbol_table *sym_table;
  info_status dwarf_status;	// module has dwarf info?
  info_status symtab_status;	// symbol table cached?

  void get_symtab(dwarf_query *q);

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
  map<string, module_info*> cache;
  bool paths_collected;
  bool dwarf_collected;

  module_cache() : paths_collected(false), dwarf_collected(false) {}
};
typedef struct module_cache module_cache_t;

#ifdef HAVE_TR1_UNORDERED_MAP
typedef tr1::unordered_map<string,Dwarf_Die> cu_function_cache_t;
typedef tr1::unordered_map<string,cu_function_cache_t*> mod_cu_function_cache_t; // module:cu -> function -> die
#else
struct stringhash {
  size_t operator() (const string& s) const { hash<const char*> h; return h(s.c_str()); }
};

typedef hash_map<string,Dwarf_Die,stringhash> cu_function_cache_t;
typedef hash_map<string,cu_function_cache_t*,stringhash> mod_cu_function_cache_t; // module:cu -> function -> die
#endif

struct
symbol_table
{
  module_info *mod_info;	// associated module
  map<string, func_info*> map_by_name;
  vector<func_info*> list_by_addr;
  typedef vector<func_info*>::iterator iterator_t;
  typedef pair<iterator_t, iterator_t> range_t;
#ifdef __powerpc__
  GElf_Word opd_section;
#endif
  // add_symbol doesn't leave symbol table in order; call
  // symbol_table::sort() when done adding symbols.
  void add_symbol(const char *name, bool weak, Dwarf_Addr addr,
                                               Dwarf_Addr *high_addr);
  void sort();
  enum info_status read_symbols(FILE *f, const string& path);
  enum info_status read_from_elf_file(const string& path);
  enum info_status read_from_text_file(const string& path);
  enum info_status get_from_elf();
  void prepare_section_rejection(Dwfl_Module *mod);
  bool reject_section(GElf_Word section);
  void mark_dwarf_redundancies(dwflpp *dw);
  void purge_syscall_stubs();
  func_info *lookup_symbol(const string& name);
  Dwarf_Addr lookup_symbol_address(const string& name);
  func_info *get_func_containing_address(Dwarf_Addr addr);

  symbol_table(module_info *mi) : mod_info(mi) {}
  ~symbol_table();
};

static bool null_die(Dwarf_Die *die)
{
  static Dwarf_Die null = { 0 };
  return (!die || !memcmp(die, &null, sizeof(null)));
}

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

enum line_t { ABSOLUTE, RELATIVE, RANGE, WILDCARD };

typedef vector<inline_instance_info> inline_instance_map_t;
typedef vector<func_info> func_info_map_t;

struct dwflpp
{
  systemtap_session & sess;
  Dwfl * dwfl;

  // These are "current" values we focus on.
  Dwfl_Module * module;
  Dwarf * module_dwarf;
  Dwarf_Addr module_bias;
  module_info * mod_info;

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


  void get_module_dwarf(bool required = false, bool report = true)
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

  void focus_on_module(Dwfl_Module * m, module_info * mi)
  {
    module = m;
    mod_info = mi;
    if (m)
      {
        module_name = default_name(dwfl_module_info(module, NULL,
						&module_start, &module_end,
						NULL, NULL,
						NULL, NULL),
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
      focus_on_module(mod, NULL);
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
    bool t = (fnmatch(pattern.c_str(), module_name.c_str(), 0) == 0);
    if (t && sess.verbose>3)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "module '" << module_name << "'" << "\n";
    return t;
  }
  bool name_has_wildcard(string pattern)
  {
    return (pattern.find('*') != string::npos ||
            pattern.find('?') != string::npos ||
            pattern.find('[') != string::npos);
  }
  bool module_name_final_match(string pattern)
  {
    // Assume module_name_matches().  Can there be any more matches?
    // Not unless the pattern is a wildcard, since module names are
    // presumed unique.
    return !name_has_wildcard(pattern);
  }


  bool function_name_matches_pattern(string name, string pattern)
  {
    bool t = (fnmatch(pattern.c_str(), name.c_str(), 0) == 0);
    if (t && sess.verbose>3)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "function '" << name << "'" << "\n";
    return t;
  }
  bool function_name_matches(string pattern)
  {
    assert(function);
    return function_name_matches_pattern(function_name, pattern);
  }
  bool function_name_final_match(string pattern)
  {
    return module_name_final_match (pattern);
  }


  bool cu_name_matches(string pattern)
  {
    assert(cu);

    // PR 5049: implicit * in front of given path pattern.
    // NB: fnmatch() is used without FNM_PATHNAME.
    string prefixed_pattern = string("*/") + pattern;

    bool t = (fnmatch(pattern.c_str(), cu_name.c_str(), 0) == 0 ||
              fnmatch(prefixed_pattern.c_str(), cu_name.c_str(), 0) == 0);
    if (t && sess.verbose>3)
      clog << "pattern '" << prefixed_pattern << "' "
	   << "matches "
	   << "CU '" << cu_name << "'" << "\n";
    return t;
  }

  dwflpp(systemtap_session & session)
    :
    sess(session),
    dwfl(NULL),
    module(NULL),
    module_dwarf(NULL),
    module_bias(0),
    mod_info(NULL),
    module_start(0),
    module_end(0),
    cu(NULL),
    function(NULL)
  {
  }


  void setup_kernel(bool debuginfo_needed = true)
  {
    if (! sess.module_cache)
      sess.module_cache = new module_cache ();

    // XXX: this is where the session -R parameter could come in
    static char debuginfo_path_arr[] = "-:.debug:/usr/lib/debug:build";
    static char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");

    static char *debuginfo_path = (debuginfo_env_arr ?
                                   debuginfo_env_arr : debuginfo_path_arr);
    static const char *debug_path = (debuginfo_env_arr ?
                                   debuginfo_env_arr : sess.kernel_release.c_str());

    static const Dwfl_Callbacks kernel_callbacks =
      {
	dwfl_linux_kernel_find_elf,
	dwfl_standard_find_debuginfo,
	dwfl_offline_section_address,
        & debuginfo_path
      };

    dwfl = dwfl_begin (&kernel_callbacks);
    if (!dwfl)
      throw semantic_error ("cannot open dwfl");
    dwfl_report_begin (dwfl);
    
    int rc = dwfl_linux_kernel_report_offline (dwfl,
                                               debug_path,
                                               NULL);
    
    if (debuginfo_needed)
      dwfl_assert (string("missing kernel ") +
                   sess.kernel_release +
                   string(" ") +
                   sess.architecture +
                   string(" debuginfo"),
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

  void setup_user(string module_name, bool debuginfo_needed = true)
  {
    // XXX: this is where the session -R parameter could come in
    static char debuginfo_path_arr[] = "-:.debug:/usr/lib/debug:build";
    static char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
    static char *debuginfo_path = (debuginfo_env_arr ?: debuginfo_path_arr);

    static const Dwfl_Callbacks user_callbacks =
      {
        NULL, /* dwfl_linux_kernel_find_elf, */
        dwfl_standard_find_debuginfo,
        dwfl_offline_section_address,
        & debuginfo_path
      };

    dwfl = dwfl_begin (&user_callbacks);
    if (!dwfl)
      throw semantic_error ("cannot open dwfl");
    dwfl_report_begin (dwfl);

    // XXX: need to map module_name to fully-qualified directory names,
    // searching PATH etc.

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
    
    // NB: the result of an _offline call is the assignment of
    // virtualized addresses to relocatable objects such as
    // modules.  These have to be converted to real addresses at
    // run time.  See the dwarf_derived_probe ctor and its caller.

    dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
  }



  // -----------------------------------------------------------------

  void iterate_over_modules(int (* callback)(Dwfl_Module *, void **,
					     const char *, Dwarf_Addr,
					     void *),
			    dwarf_query *data)
  {
    ptrdiff_t off = 0;
    do
      {
        if (pending_interrupts) return;
        off = dwfl_getmodules (dwfl, callback, data, off);
      }
    while (off > 0);
    dwfl_assert("dwfl_getmodules", off == 0);
  }


  // Defined after dwarf_query
  void query_modules(dwarf_query *q);


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

   /* The global alias cache is used to resolve any DIE found in a
   * module that is stubbed out with DW_AT_declaration with a defining
   * DIE found in a different module.  The current assumption is that
   * this only applies to structures and unions, which have a global
   * namespace (it deliberately only traverses program scope), so this
   * cache is indexed by name.  If other declaration lookups were
   * added to it, it would have to be indexed by name and tag
   */
  mod_cu_function_cache_t global_alias_cache;
  static int global_alias_caching_callback(Dwarf_Die *die, void *arg)
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

  Dwarf_Die *declaration_resolve(Dwarf_Die *die)
  {
    const char *name = dwarf_diename(die);

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

  mod_cu_function_cache_t cu_function_cache;

  static int cu_function_caching_callback (Dwarf_Die* func, void *arg)
  {
    cu_function_cache_t* v = static_cast<cu_function_cache_t*>(arg);
    string function_name = dwarf_diename(func);
    (*v)[function_name] = * func;
    return DWARF_CB_OK;
  }

  int iterate_over_functions (int (* callback)(Dwarf_Die * func, void * arg),
                              void * data);
  int iterate_over_globals (int (* callback)(Dwarf_Die *, void *),
				 void * data);

  bool has_single_line_record (dwarf_query * q, char const * srcfile, int lineno);

  void iterate_over_srcfile_lines (char const * srcfile,
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


  void collect_srcfiles_matching (string const & pattern,
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

  void resolve_prologue_endings (func_info_map_t & funcs)
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
          throw semantic_error ("missing entrypc dwarf line record for function '"
                                + it->name + "'");

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


  bool function_entrypc (Dwarf_Addr * addr)
  {
    assert (function);
    return (dwarf_entrypc (function, addr) == 0);
    // XXX: see also _lowpc ?
  }


  bool die_entrypc (Dwarf_Die * die, Dwarf_Addr * addr)
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

  bool die_has_pc (Dwarf_Die & die, Dwarf_Addr pc)
  {
    int res = dwarf_haspc (&die, pc);
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
    dwfl_assert ("dwfl_addrmodule", mod);
    int n = dwfl_module_relocations (mod);
    dwfl_assert ("dwfl_module_relocations", n >= 0);
    int i = dwfl_module_relocate_address (mod, &address);
    dwfl_assert ("dwfl_module_relocate_address", i >= 0);
    const char *modname = dwfl_module_info (mod, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL);
    dwfl_assert ("dwfl_module_info", modname);
    const char *secname = dwfl_module_relocation_info (mod, i, NULL);

    if (n > 0 && !(n == 1 && secname == NULL))
     {
	dwfl_assert ("dwfl_module_relocation_info", secname);
	if (n > 1 || secname[0] != '\0')
          {
            // This gives us the module name, and section name within the
            // module, for a kernel module (or other ET_REL module object).
            obstack_printf (pool, "({ static unsigned long addr = 0; ");
            obstack_printf (pool, "if (addr==0) addr = _stp_module_relocate (\"%s\",\"%s\",%#" PRIx64 "); ",
                            modname, secname, address);
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
		break;
	      default:
		break;
	      }
	  }
	while (dwarf_siblingof (&child, &child) == 0);
      }
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
			      + (alternatives.str() == "" ? "" : (" (alternatives:" + alternatives.str () + ")")));
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
        /* XXX: This would be desirable, but we don't get the target_symbol token,
           and printing that gives us the file:line number too early anyway. */
#if 0
        // Emit a marker to note which field is being access-attempted, to give
        // better error messages if deref() fails.
        string piece = string(...target_symbol token...) + string ("#") + stringify(components[i].second);
        obstack_printf (pool, "c->last_stmt = %s;", lex_cast_qstring(piece).c_str());
#endif

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
            // by explicit pointer arithmetic etc.  PR4166.

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
	    if (dwarf_hasattr(die, DW_AT_declaration))
	      {
		Dwarf_Die *tmpdie = dwflpp::declaration_resolve(die);
		if (tmpdie == NULL)
		  throw semantic_error ("unresolved struct "
					+ string (dwarf_diename_integrate (die) ?: "<anonymous>"));
		*die_mem = *tmpdie;
	      }
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
                                    " for " + diestr);
            }

          if (encoding == DW_ATE_float
              || encoding == DW_ATE_complex_float
              /* XXX || many others? */)
            {
              // clog << "bad type " << encoding << " diestr" << endl;
              throw semantic_error ("unsupported type (encoding " + lex_cast<string>(encoding) + ")" +
                                    " for " + diestr);
            }
        }

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

          if (lvalue)
            {
              ty = pe_long;
              if (typetag == DW_TAG_array_type)
                throw semantic_error ("cannot write to array address");
              assert (typetag == DW_TAG_pointer_type);
              c_translate_pointer_store (pool, 1, module_bias, typedie, tail,
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
                c_translate_array (pool, 1, module_bias, typedie, tail, NULL, 0);
              else
                c_translate_pointer (pool, 1, module_bias, typedie, tail);
              c_translate_addressof (pool, 1, module_bias, NULL, pointee_typedie, tail,
                                     "THIS->__retvalue");
            }
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
  void emit_probe_local_init(translator_output * o);

  // Pattern registration helpers.
  static void register_statement_variants(match_node * root,
					  dwarf_builder * dw);
  static void register_function_variants(match_node * root,
					  dwarf_builder * dw);
  static void register_function_and_statement_variants(match_node * root,
						       dwarf_builder * dw);
  static void register_patterns(match_node * root);
};


struct uprobe_derived_probe: public derived_probe
{
  bool return_p;
  string module; // * => unrestricted
  int pid; // 0 => unrestricted
  string section; // empty => absolute address
  Dwarf_Addr address;
  // bool has_maxactive;
  // long maxactive_val;

  uprobe_derived_probe (const string& function,
                        const string& filename,
                        int line,
                        const string& module,
                        int pid,
                        const string& section,
                        Dwarf_Addr dwfl_addr,
                        Dwarf_Addr addr,
                        dwarf_query & q,
                        Dwarf_Die* scope_die);

  // alternate constructor for process(PID).statement(ADDR).absolute
  uprobe_derived_probe (probe *base,
                        probe_point *location,
                        int pid,
                        Dwarf_Addr addr,
                        bool return_p);

  void printsig (std::ostream &o) const;
  void join_group (systemtap_session& s);
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
struct base_query
{
  base_query(systemtap_session & sess,
	     probe * base_probe,
	     probe_point * base_loc,
	     dwflpp & dw,
	     literal_map_t const & params,
	     vector<derived_probe *> & results);
  virtual ~base_query() {}

  systemtap_session & sess;
  probe * base_probe;
  probe_point * base_loc;
  dwflpp & dw;
  vector<derived_probe *> & results;

  // Parameter extractors.
  static bool has_null_param(literal_map_t const & params,
                             string const & k);
  static bool get_string_param(literal_map_t const & params,
			       string const & k, string & v);
  static bool get_number_param(literal_map_t const & params,
			       string const & k, long & v);
  static bool get_number_param(literal_map_t const & params,
			       string const & k, Dwarf_Addr & v);

  // Extracted parameters.
  bool has_kernel;
  bool has_module;
  bool has_process;
  string module_val; // has_kernel => module_val = "kernel"

  virtual void handle_query_module() = 0;
};


base_query::base_query(systemtap_session & sess,
		       probe * base_probe,
		       probe_point * base_loc,
		       dwflpp & dw,
		       literal_map_t const & params,
		       vector<derived_probe *> & results)
  : sess(sess), base_probe(base_probe), base_loc(base_loc), dw(dw),
    results(results)
{
  has_kernel = has_null_param (params, TOK_KERNEL);
  if (has_kernel)
    module_val = "kernel";

  has_module = get_string_param (params, TOK_MODULE, module_val);
  if (has_module)
    has_process = false;
  else
    {
      has_process = get_string_param(params, TOK_PROCESS, module_val);
      if (has_process) 
        module_val = find_executable (module_val);
    }

  assert (has_kernel || has_process || has_module);
}

bool
base_query::has_null_param(literal_map_t const & params,
			   string const & k)
{
  return derived_probe_builder::has_null_param(params, k);
}


bool
base_query::get_string_param(literal_map_t const & params,
			     string const & k, string & v)
{
  return derived_probe_builder::get_param (params, k, v);
}


bool
base_query::get_number_param(literal_map_t const & params,
			     string const & k, long & v)
{
  int64_t value;
  bool present = derived_probe_builder::get_param (params, k, value);
  v = (long) value;
  return present;
}


bool
base_query::get_number_param(literal_map_t const & params,
			     string const & k, Dwarf_Addr & v)
{
  int64_t value;
  bool present = derived_probe_builder::get_param (params, k, value);
  v = (Dwarf_Addr) value;
  return present;
}

struct dwarf_query : public base_query
{
  dwarf_query(systemtap_session & sess,
	      probe * base_probe,
	      probe_point * base_loc,
	      dwflpp & dw,
	      literal_map_t const & params,
	      vector<derived_probe *> & results);

  virtual void handle_query_module();
  void query_module_dwarf();
  void query_module_symtab();

  void add_probe_point(string const & funcname,
		       char const * filename,
		       int line,
		       Dwarf_Die *scope_die,
		       Dwarf_Addr addr);
  string get_blacklist_section(Dwarf_Addr addr);

  regex_t blacklist_func; // function/statement probes
  regex_t blacklist_func_ret; // only for .return probes
  regex_t blacklist_file; // file name
  void build_blacklist();

  bool blacklisted_p(const string& funcname,
                     const string& filename,
                     int line,
                     const string& module,
                     const string& section,
                     Dwarf_Addr addr);

  // Extracted parameters.
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

  enum dbinfo_reqt dbinfo_reqt;
  enum dbinfo_reqt assess_dbinfo_reqt();

  function_spec_type parse_function_spec(string & spec);
  function_spec_type spec_type;
  string function;
  string file;
  line_t line_type;
  int line[2];
  bool query_done;	// Found exact match

  set<char const *> filtered_srcfiles;

  // Map official entrypc -> func_info object
  inline_instance_map_t filtered_inlines;
  func_info_map_t filtered_functions;
  bool choose_next_line;
  Dwarf_Addr entrypc_for_next_line;
};


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

    for (func_info_map_t::iterator i = q->filtered_functions.begin();
         i != q->filtered_functions.end(); ++i)
      {
        if (q->dw.die_has_pc (i->die, addr))
          {
            if (q->sess.verbose>4)
              clog << "alternative line " << lineno << " accepted: fn=" << i->name << endl;
            return true;
          }
      }

    for (inline_instance_map_t::iterator i = q->filtered_inlines.begin();
         i != q->filtered_inlines.end(); ++i)
      {
        if (q->dw.die_has_pc (i->die, addr))
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

  do {
    /* We're only currently looking for structures and unions,
     * although other types of declarations exist */
    if (dwarf_tag(&die) != DW_TAG_structure_type &&
	dwarf_tag(&die) != DW_TAG_union_type)
      continue;

    rc = (*callback)(&die, data);
    if (rc != DWARF_CB_OK)
      break;

  } while (dwarf_siblingof(&die, &die) == 0);

  return rc;
}

int
dwflpp::iterate_over_functions (int (* callback)(Dwarf_Die * func, void * arg),
                                void * data)
{
  int rc = DWARF_CB_OK;
  assert (module);
  assert (cu);
  dwarf_query * q = static_cast<dwarf_query *>(data);
  
  string key = module_name + ":" + cu_name;
  cu_function_cache_t *v = cu_function_cache[key];
  if (v == 0)
    {
      v = new cu_function_cache_t;
      cu_function_cache[key] = v;
      dwarf_getfuncs (cu, cu_function_caching_callback, v, 0);
      if (q->sess.verbose > 4)
        clog << "function cache " << key << " size " << v->size() << endl;
    }
  
  string subkey = q->function;
  if (v->find(subkey) != v->end())
    {
      Dwarf_Die die = v->find(subkey)->second;
      if (q->sess.verbose > 4)
        clog << "function cache " << key << " hit " << subkey << endl;
      return (*callback)(& die, data);
    }
  else if (name_has_wildcard (subkey))
    {
      for (cu_function_cache_t::iterator it = v->begin(); it != v->end(); it++)
        {
          string func_name = it->first;
          Dwarf_Die die = it->second;
          if (function_name_matches_pattern (func_name, subkey))
            {
              if (q->sess.verbose > 4)
                clog << "function cache " << key << " match " << func_name << " vs " << subkey << endl;
              
              rc = (*callback)(& die, data);
              if (rc != DWARF_CB_OK) break;
            }
        }
    }
  else if (q->has_statement_num) // searching all for kernel.statement
    {
      for (cu_function_cache_t::iterator it = v->begin(); it != v->end(); it++)
        {
          Dwarf_Die die = it->second;
          rc = (*callback)(& die, data);
          if (rc != DWARF_CB_OK) break;
        }
    }
  else // not a wildcard and no match in this CU
    {
      // do nothing
    }
  return rc;
}



struct dwarf_builder: public derived_probe_builder
{
  dwflpp *kern_dw;
  map <string,dwflpp*> user_dw;
  dwarf_builder(): kern_dw(0) {}


  /* NB: not virtual, so can be called from dtor too: */
  void dwarf_build_no_more (bool verbose) 
  {
    if (kern_dw)
      {
        if (verbose)
          clog << "dwarf_builder releasing kernel dwflpp" << endl;
        delete kern_dw;
        kern_dw = 0;
      }

    for (map<string,dwflpp*>::iterator udi = user_dw.begin();
         udi != user_dw.end();
         udi ++)
      {
        if (verbose)
          clog << "dwarf_builder releasing user dwflpp " << udi->first << endl;
        delete udi->second;
      }
    user_dw.erase (user_dw.begin(), user_dw.end());
  }

  void build_no_more (systemtap_session &s)
  {
    dwarf_build_no_more (s.verbose > 3);
  }

  ~dwarf_builder()
  {
    dwarf_build_no_more (false);
  }

  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results);
};


dwarf_query::dwarf_query(systemtap_session & sess,
			 probe * base_probe,
			 probe_point * base_loc,
			 dwflpp & dw,
			 literal_map_t const & params,
			 vector<derived_probe *> & results)
  : base_query(sess, base_probe, base_loc, dw, params, results)
{
  // Reduce the query to more reasonable semantic values (booleans,
  // extracted strings, numbers, etc).
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
  dbinfo_reqt = assess_dbinfo_reqt();
  query_done = false;
}


void
dwarf_query::query_module_dwarf()
{
  if (has_function_num || has_statement_num)
    {
      // If we have module("foo").function(0xbeef) or
      // module("foo").statement(0xbeef), the address is relative
      // to the start of the module, so we seek the function
      // number plus the module's bias.

      Dwarf_Addr addr;
      if (has_function_num)
	addr = function_num_val;
      else
	addr = statement_num_val;

      // NB: we don't need to add the module base address or bias
      // value here (for reasons that may be coincidental).
      dw.query_cu_containing_module_address(addr, this);
    }
  else
    {
      // Otherwise if we have a function("foo") or statement("foo")
      // specifier, we have to scan over all the CUs looking for
      // the function(s) in question
      assert(has_function_str || has_statement_str);
      dw.iterate_over_cus(&query_cu, this);
    }
}

static void query_func_info (Dwarf_Addr entrypc, func_info & fi,
							dwarf_query * q);

void
dwarf_query::query_module_symtab()
{
  // Get the symbol table if it's necessary, sufficient, and not already got.
  if (dbinfo_reqt == dbr_need_dwarf)
    return;

  module_info *mi = dw.mod_info;
  if (dbinfo_reqt == dbr_need_symtab)
    {
      if (mi->symtab_status == info_unknown)
        mi->get_symtab(this);
      if (mi->symtab_status == info_absent)
        return;
    }

  func_info *fi = NULL;
  symbol_table *sym_table = mi->sym_table;

  if (has_function_str)
    {
      // Per dwarf_query::assess_dbinfo_reqt()...
      assert(spec_type == function_alone);
      if (dw.name_has_wildcard(function_str_val))
        {
          // Until we augment the blacklist sufficently...
          if (function_str_val.find_first_not_of("*?") == string::npos)
            {
              // e.g., kernel.function("*")
              cerr << "Error: Pattern '"
                   << function_str_val
                   << "' matches every instruction address in the symbol table,"
                   << endl
                   << "some of which aren't even functions."
                   << "  Please be more precise."
                   << endl;
              return;
            }
          symbol_table::iterator_t iter;
          for (iter = sym_table->list_by_addr.begin();
               iter != sym_table->list_by_addr.end();
               ++iter)
            {
              fi = *iter;
              if (!null_die(&fi->die))
                continue;       // already handled in query_module_dwarf()
              if (dw.function_name_matches_pattern(fi->name, function_str_val))
                query_func_info(fi->addr, *fi, this);
            }
        }
      else
        {
          fi = sym_table->lookup_symbol(function_str_val);
          if (fi && null_die(&fi->die))
            query_func_info(fi->addr, *fi, this);
        }
    }
  else
    {
      assert(has_function_num || has_statement_num);
      // Find the "function" in which the indicated address resides.
      Dwarf_Addr addr =
      		(has_function_num ? function_num_val : statement_num_val);
      fi = sym_table->get_func_containing_address(addr);
      if (!fi)
        {
          cerr << "Warning: address "
               << hex << addr << dec
               << " out of range for module "
               << dw.module_name;
          return;
        }
      if (!null_die(&fi->die))
        {
          // addr looks like it's in the compilation unit containing
          // the indicated function, but query_module_dwarf() didn't
          // match addr to any compilation unit, so addr must be
          // above that cu's address range.
          cerr << "Warning: address "
               << hex << addr << dec
               << " maps to no known compilation unit in module "
               << dw.module_name;
          return;
        }
      query_func_info(fi->addr, *fi, this);
    }
}

void
dwarf_query::handle_query_module()
{
  dw.get_module_dwarf(false,
              (dbinfo_reqt == dbr_need_dwarf || !sess.consult_symtab));
  if (dw.mod_info->dwarf_status == info_present)
    query_module_dwarf();
  // Consult the symbol table if we haven't found all we're looking for.
  // asm functions can show up in the symbol table but not in dwarf.
  if (sess.consult_symtab && !query_done)
    query_module_symtab();
}


void
dwarf_query::build_blacklist()
{
  // No blacklist for userspace.
  if (has_process)
    return;

  // We build up the regexps in these strings

  // Add ^ anchors at the front; $ will be added just before regcomp.

  string blfn = "^(";
  string blfn_ret = "^(";
  string blfile = "^(";

  blfile += "kernel/kprobes.c"; // first alternative, no "|"
  blfile += "|arch/.*/kernel/kprobes.c";

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

  // Lots of locks
  blfn += "|.*raw_.*lock.*";
  blfn += "|.*read_.*lock.*";
  blfn += "|.*write_.*lock.*";
  blfn += "|.*spin_.*lock.*";
  blfn += "|.*rwlock_.*lock.*";
  blfn += "|.*rwsem_.*lock.*";
  blfn += "|.*mutex_.*lock.*";
  blfn += "|raw_.*";
  blfn += "|.*seq_.*lock.*";

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

  blfn += ")$";
  blfn_ret += ")$";
  blfile += ")$";

  if (sess.verbose > 2) 
    {
      clog << "blacklist regexps:" << endl;
      clog << "blfn: " << blfn << endl;
      clog << "blfn_ret: " << blfn_ret << endl;
      clog << "blfile: " << blfile << endl;
    }

  int rc = regcomp (& blacklist_func, blfn.c_str(), REG_NOSUB|REG_EXTENDED);
  if (rc) throw semantic_error ("blacklist_func regcomp failed");
  rc = regcomp (& blacklist_func_ret, blfn_ret.c_str(), REG_NOSUB|REG_EXTENDED);
  if (rc) throw semantic_error ("blacklist_func_ret regcomp failed");
  rc = regcomp (& blacklist_file, blfile.c_str(), REG_NOSUB|REG_EXTENDED);
  if (rc) throw semantic_error ("blacklist_file regcomp failed");
}


function_spec_type
dwarf_query::parse_function_spec(string & spec)
{
  string::const_iterator i = spec.begin(), e = spec.end();

  function.clear();
  file.clear();
  line[0] = 0;
  line[1] = 0;

  while (i != e && *i != '@')
    {
      if (*i == ':' || *i == '+')
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

  while (i != e && *i != ':' && *i != '+')
    file += *i++;
  if (*i == ':') 
    {
      if (*(i + 1) == '*')
	line_type = WILDCARD;
      else
	line_type = ABSOLUTE;
    }
  else if (*i == '+')
    line_type = RELATIVE;

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
      if (line_type != WILDCARD)
	{
	  string::const_iterator dash = i;
	  
	  while (dash != e && *dash != '-')
	    dash++;
	  if (dash == e)
	    line[0] = line[1] = lex_cast<int>(string(i, e));
	  else
	    {
	      line_type = RANGE;
	      line[0] = lex_cast<int>(string(i, dash));
	      line[1] = lex_cast<int>(string(dash + 1, e));
	    }
	}
      
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


#if 0
// Forward declaration.
static int query_kernel_module (Dwfl_Module *, void **, const char *,
				Dwarf_Addr, void *);
#endif


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
  if (has_process)
    return false; // no blacklist for userspace

  if (section.substr(0, 6) == string(".init.") ||
      section.substr(0, 6) == string(".exit.") ||
      section.substr(0, 9) == string(".devinit.") ||
      section.substr(0, 9) == string(".devexit.") ||
      section.substr(0, 9) == string(".cpuinit.") ||
      section.substr(0, 9) == string(".cpuexit.") ||
      section.substr(0, 9) == string(".meminit.") ||
      section.substr(0, 9) == string(".memexit."))
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

  // Check probe point against blacklist.
  int goodfn = regexec (&blacklist_func, funcname.c_str(), 0, NULL, 0);
  if (has_return)
    goodfn = goodfn && regexec (&blacklist_func_ret, funcname.c_str(), 0, NULL, 0);
  int goodfile = regexec (&blacklist_file, filename.c_str(), 0, NULL, 0);

  if (! (goodfn && goodfile))
    {
      if (sess.verbose>1)
	clog << " skipping - blacklisted";
      return true;
    }

  // This probe point is not blacklisted.
  return false;
}

string dwarf_query::get_blacklist_section(Dwarf_Addr addr)
{
       string blacklist_section;
       Dwarf_Addr bias;
       // We prefer dwfl_module_getdwarf to dwfl_module_getelf here,
       // because dwfl_module_getelf can force costly section relocations
       // we don't really need, while either will do for this purpose.
       Elf* elf = (dwarf_getelf (dwfl_module_getdwarf (dw.module, &bias))
		   ?: dwfl_module_getelf (dw.module, &bias));

       Dwarf_Addr offset = addr - bias;
       if (elf)
        {
          Elf_Scn* scn = 0;
          size_t shstrndx;
          dwfl_assert ("getshstrndx", elf_getshstrndx (elf, &shstrndx));
          while ((scn = elf_nextscn (elf, scn)) != NULL)
            {
              GElf_Shdr shdr_mem;
              GElf_Shdr *shdr = gelf_getshdr (scn, &shdr_mem);
              if (! shdr) continue; // XXX error?

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


void
dwarf_query::add_probe_point(const string& funcname,
			     const char* filename,
			     int line,
			     Dwarf_Die* scope_die,
			     Dwarf_Addr addr)
{
  string reloc_section; // base section for relocation purposes
  Dwarf_Addr reloc_addr = addr; // relocated
  string blacklist_section; // linking section for blacklist purposes
  const string& module = dw.module_name; // "kernel" or other

  assert (! has_absolute); // already handled in dwarf_builder::build()

  if (!dw.module)
    {
      assert(module == TOK_KERNEL);
      reloc_section = "";
      blacklist_section = "";
    }
  else if (dwfl_module_relocations (dw.module) > 0)
    {
      // This is a relocatable module; libdwfl already knows its
      // sections, so we can relativize addr.
      int idx = dwfl_module_relocate_address (dw.module, &reloc_addr);
      const char* r_s = dwfl_module_relocation_info (dw.module, idx, NULL);
      if (r_s)
        reloc_section = r_s;
      blacklist_section = reloc_section;

     if(reloc_section == "" && dwfl_module_relocations (dw.module) == 1)
       {
         blacklist_section = this->get_blacklist_section(addr);
         reloc_section = ".dynamic";
       }
    }
  else
    {
      blacklist_section = this->get_blacklist_section(addr);
      reloc_section = ".absolute";
    }

  if (sess.verbose > 1)
    {
      clog << "probe " << funcname << "@" << filename << ":" << line;
      if (string(module) == TOK_KERNEL)
        clog << " kernel";
      else if (has_module)
        clog << " module=" << module;
      else if (has_process)
        clog << " process=" << module;
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
      sess.unwindsym_modules.insert (module);

      if (has_process)
        {
          results.push_back (new uprobe_derived_probe(funcname, filename, line,
                                                      module, 0, reloc_section, addr, reloc_addr,
                                                      *this, scope_die));
        }
      else
        {
          assert (has_kernel || has_module);
          results.push_back (new dwarf_derived_probe(funcname, filename, line,
                                                     module, reloc_section, addr, reloc_addr, 
                                                     *this, scope_die));
        }
    }
}

enum dbinfo_reqt
dwarf_query::assess_dbinfo_reqt()
{
  if (has_absolute)
    {
      // kernel.statement(NUM).absolute
      return dbr_none;
    }
  if (has_inline)
    {
      // kernel.function("f").inline or module("m").function("f").inline
      return dbr_need_dwarf;
    }
  if (has_function_str && spec_type == function_alone)
    {
      // kernel.function("f") or module("m").function("f")
      return dbr_need_symtab;
    }
  if (has_statement_num)
    {
      // kernel.statement(NUM) or module("m").statement(NUM)
      // Technically, all we need is the module offset (or _stext, for
      // the kernel).  But for that we need either the ELF file or (for
      // _stext) the symbol table.  In either case, the symbol table
      // is available, and that allows us to map the NUM (address)
      // to a function, which is goodness.
      return dbr_need_symtab;
    }
  if (has_function_num)
    {
      // kernel.function(NUM) or module("m").function(NUM)
      // Need the symbol table so we can back up from NUM to the
      // start of the function.
      return dbr_need_symtab;
    }
  // Symbol table tells us nothing about source files or line numbers.
  return dbr_need_dwarf;
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
query_inline_instance_info (inline_instance_info & ii,
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
		 << hex << ii.entrypc << dec
		 << " of instance of inline '" << ii.name << "'\n";
	  query_statement (ii.name, ii.decl_file, ii.decl_line,
			   &ii.die, ii.entrypc, q);
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
          if (q->sess.prologue_searching
              && !q->has_statement_str && !q->has_statement_num) // PR 2608
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
query_srcfile_line (const dwarf_line_t& line, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);

  Dwarf_Addr addr = line.addr();

  int lineno = line.lineno();

  for (func_info_map_t::iterator i = q->filtered_functions.begin();
       i != q->filtered_functions.end(); ++i)
    {
      if (q->dw.die_has_pc (i->die, addr))
	{
	  if (q->sess.verbose>3)
	    clog << "function DIE lands on srcfile\n";
	  if (q->has_statement_str)
	    query_statement (i->name, i->decl_file,
			     lineno, // NB: not q->line !
                             &(i->die), addr, q);
	  else
	    query_func_info (i->entrypc, *i, q);
	}
    }

  for (inline_instance_map_t::iterator i
	 = q->filtered_inlines.begin();
       i != q->filtered_inlines.end(); ++i)
    {
      if (q->dw.die_has_pc (i->die, addr))
	{
	  if (q->sess.verbose>3)
	    clog << "inline instance DIE lands on srcfile\n";
	  if (q->has_statement_str)
	    query_statement (i->name, i->decl_file,
			     q->line[0], &(i->die), addr, q);
	  else
	    query_inline_instance_info (*i, q);
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
	      inl.entrypc = entrypc;
	      q->dw.function_file (&inl.decl_file);
	      q->dw.function_line (&inl.decl_line);
	      q->filtered_inlines.push_back(inl);
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

          if (q->dw.function_name_final_match (q->function))
            return DWARF_CB_ABORT;
	}
      else if (!q->dw.func_is_inline () && (! q->has_inline))
	{
	  bool record_this_function = false;

	  if ((q->has_statement_str || q->has_function_str)
	      && q->dw.function_name_matches(q->function))
	    {
	      record_this_function = true;
	    }
	  else if (q->has_function_num || q->has_statement_num)
	    {
              Dwarf_Addr query_addr =
                q->dw.module_address_to_global(q->has_function_num ? q->function_num_val :
                                               q->has_statement_num ? q->statement_num_val :
                                               (assert(0) , 0));
	      Dwarf_Die d;
	      q->dw.function_die (&d);

	      if (q->dw.die_has_pc (d, query_addr))
		record_this_function = true;
	    }

	  if (record_this_function)
	    {
	      if (q->sess.verbose>2)
		clog << "selected function " << q->dw.function_name << "\n";

              func_info func;
              q->dw.function_die (&func.die);
              func.name = q->dw.function_name;
              q->dw.function_file (&func.decl_file);
              q->dw.function_line (&func.decl_line);

              if (q->has_function_num || q->has_function_str || q->has_statement_str)
                {
                  Dwarf_Addr entrypc;
                  if (q->dw.function_entrypc (&entrypc))
                    {
                      func.entrypc = entrypc;
                      q->filtered_functions.push_back (func);
                    }
                  else
                    /* this function just be fully inlined, just ignore it */
                    return DWARF_CB_OK;
                }
              else if (q->has_statement_num)
                {
                  func.entrypc = q->statement_num_val;
                  q->filtered_functions.push_back (func);
                  if (q->dw.function_name_final_match (q->function))
                    return DWARF_CB_ABORT;
                }
              else
                assert(0);

              if (q->dw.function_name_final_match (q->function))
                return DWARF_CB_ABORT;
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
  if (pending_interrupts) return DWARF_CB_ABORT;

  try
    {
      q->dw.focus_on_cu (cudie);

      if (false && q->sess.verbose>2)
        clog << "focused on CU '" << q->dw.cu_name
             << "', in module '" << q->dw.module_name << "'\n";

      if (q->has_statement_str || q->has_statement_num
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
          // Verify that a raw address matches the beginning of a
          // statement. This is a somewhat lame check that the address
          // is at the start of an assembly instruction.
          if (q->has_statement_num)
            {
              Dwarf_Addr queryaddr = q->statement_num_val;
              dwarf_line_t address_line(dwarf_getsrc_die(cudie, queryaddr));
              Dwarf_Addr lineaddr = 0;
              if (address_line)
                lineaddr = address_line.addr();
              if (!address_line || lineaddr != queryaddr)
                {
                  stringstream msg;
                  msg << "address 0x" << hex << queryaddr
                      << "does not match the begining of a statement";
                  throw semantic_error(msg.str());
                }
            }
	  // Pick up [entrypc, name, DIE] tuples for all the functions
	  // matching the query, and fill in the prologue endings of them
	  // all in a single pass.
	  int rc = q->dw.iterate_over_functions (query_dwarf_func, q);
	  if (rc != DWARF_CB_OK)
	    q->query_done = true;

          if (q->sess.prologue_searching 
              && !q->has_statement_str && !q->has_statement_num) // PR 2608
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
						  q->line_type, query_srcfile_line, q);
	    }
	  else
	    {
	      // Otherwise, simply probe all resolved functions.
              for (func_info_map_t::iterator i = q->filtered_functions.begin();
                   i != q->filtered_functions.end(); ++i)
                query_func_info (i->entrypc, *i, q);

	      // And all inline instances (if we're not excluding inlines with ".call")
	      if (! q->has_call)
		for (inline_instance_map_t::iterator i
		       = q->filtered_inlines.begin(); i != q->filtered_inlines.end(); ++i)
		  query_inline_instance_info (*i, q);
	    }
	}
      else
        {
          // Before PR 5787, we used to have this:
#if 0
	  // Otherwise we have a statement number, and we can just
	  // query it directly within this module.
	  assert (q->has_statement_num);
	  Dwarf_Addr query_addr = q->statement_num_val;
          query_addr = q->dw.module_address_to_global(query_addr);

	  query_statement ("", "", -1, NULL, query_addr, q);
#endif
          // But now, we traverse CUs/functions even for
          // statement_num's, for blacklist sensitivity and $var
          // resolution purposes.

          assert (0); // NOTREACHED
        }
      return DWARF_CB_OK;
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
      return DWARF_CB_ABORT;
    }
}


#if 0
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
#endif


static void
validate_module_elf (Dwfl_Module *mod, const char *name,  base_query *q)
{
  // Validate the machine code in this elf file against the
  // session machine.  This is important, in case the wrong kind
  // of debuginfo is being automagically processed by elfutils.
  // While we can tell i686 apart from x86-64, unfortunately
  // we can't help confusing i586 vs i686 (both EM_386).

  Dwarf_Addr bias;
  // We prefer dwfl_module_getdwarf to dwfl_module_getelf here,
  // because dwfl_module_getelf can force costly section relocations
  // we don't really need, while either will do for this purpose.
  Elf* elf = (dwarf_getelf (dwfl_module_getdwarf (mod, &bias))
		  ?: dwfl_module_getelf (mod, &bias));

  GElf_Ehdr ehdr_mem;
  GElf_Ehdr* em = gelf_getehdr (elf, &ehdr_mem);
  if (em == 0) { dwfl_assert ("dwfl_getehdr", dwfl_errno()); }
  int elf_machine = em->e_machine;
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
    case EM_ARM: expect_machine = "armv*"; break;
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



static int
query_module (Dwfl_Module *mod,
              void **,
	      const char *name,
              Dwarf_Addr,
	      void *arg)
{
  base_query *q = static_cast<base_query *>(arg);

  try
    {
      module_info* mi = q->sess.module_cache->cache[name];
      if (mi == 0)
        {
          mi = q->sess.module_cache->cache[name] = new module_info(name);

          const char *path = NULL; // XXX: unbreak this

          if (q->sess.ignore_vmlinux && path && name == TOK_KERNEL)
            {
              // report_kernel() in elfutils found vmlinux, but pretend it didn't.
              // Given a non-null path, returning 1 means keep reporting modules.
              mi->dwarf_status = info_absent;
            }
          else if (path)
            {
              mi->elf_path = path;
            }
      
          // No vmlinux.  Here returning 0 to report_kernel() means go ahead
          // and keep reporting modules.
          mi->dwarf_status = info_absent;
        }
      // OK, enough of that module_info caching business.

      q->dw.focus_on_module(mod, mi);

      // If we have enough information in the pattern to skip a module and
      // the module does not match that information, return early.
      if (!q->dw.module_name_matches(q->module_val))
        return DWARF_CB_OK;

      // Don't allow module("*kernel*") type expressions to match the
      // elfutils module "kernel", which we refer to in the probe
      // point syntax exclusively as "kernel.*".
      if (q->dw.module_name == TOK_KERNEL && ! q->has_kernel)
        return DWARF_CB_OK;

      if (mod)
        validate_module_elf(mod, name, q);
      else
        assert(q->has_kernel);   // and no vmlinux to examine

      if (q->sess.verbose>2)
        cerr << "focused on module '" << q->dw.module_name << "'\n";


      // Collect a few kernel addresses.  XXX: these belong better
      // to the sess.module_info["kernel"] struct.
      if (q->dw.module_name == TOK_KERNEL)
        {
          if (! q->sess.sym_kprobes_text_start)
            q->sess.sym_kprobes_text_start = lookup_symbol_address (mod, "__kprobes_text_start");
          if (! q->sess.sym_kprobes_text_end)
            q->sess.sym_kprobes_text_end = lookup_symbol_address (mod, "__kprobes_text_end");
          if (! q->sess.sym_stext)
            q->sess.sym_stext = lookup_symbol_address (mod, "_stext");
        }

      // Finally, search the module for matches of the probe point.
      q->handle_query_module();


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

void
dwflpp::query_modules(dwarf_query *q)
{
  iterate_over_modules(&query_module, q);
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

  // See if we need to generate a new probe to save/access function
  // parameters from a return probe.  PR 1382.
  if (q.has_return
      && e->base_name != "$return" // not the special return-value variable handled below
      && e->base_name != "$$return") // nor the other special variable handled below
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
           add_probe->body = new block(add_probe->body, es);

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

      add_probe->body = new block(add_probe->body, es);

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

  if (e->base_name == "$$vars"
      || e->base_name == "$$parms"
      || e->base_name == "$$locals"
      || (q.has_return && (e->base_name == "$$return")))
    {
      Dwarf_Die *scopes;
      if (dwarf_getscopes_die (scope_die, &scopes) == 0)
	return;
	
      target_symbol *tsym = new target_symbol;
      print_format* pf = new print_format;

      // Convert $$parms to sprintf of a list of parms and active local vars
      // which we recursively evaluate

      // NB: we synthesize a new token here rather than reusing
      // e->tok, because print_format::print likes to use
      // its tok->content.
      token* pf_tok = new token;
      pf_tok->location = e->tok->location;
      pf_tok->type = tok_identifier;
      pf_tok->content = "sprint";

      pf->tok = pf_tok;
      pf->print_to_stream = false;
      pf->print_with_format = true;
      pf->print_with_delim = false;
      pf->print_with_newline = false;
      pf->print_char = false;

      if (q.has_return && (e->base_name == "$$return"))
        {
          tsym->tok = e->tok;
          tsym->base_name = "$return";
          
          // Ignore any variable that isn't accessible.
          tsym->saved_conversion_error = 0;
          this->visit_target_symbol(tsym); // NB: throws nothing ...
          if (tsym->saved_conversion_error) // ... but this is how we know it happened.
            {

            }
          else
            {
              pf->raw_components += "return";
              pf->raw_components += "=%#x ";
              pf->args.push_back(*(expression**)this->targets.top());
            }
        }
      else
        {
          // non-.return probe: support $$parms, $$vars, $$locals
          Dwarf_Die result;
          if (dwarf_child (&scopes[0], &result) == 0)
            do
              {
                switch (dwarf_tag (&result))
                  {
                  case DW_TAG_variable:
                    if (e->base_name == "$$parms")
                      continue;
                    break;
                  case DW_TAG_formal_parameter:
                    if (e->base_name == "$$locals")
                      continue;
                    break;
                    
                  default:
                    continue;
                  }
                
                const char *diename = dwarf_diename (&result);
                tsym->tok = e->tok;
                tsym->base_name = "$";
                tsym->base_name += diename;
                
                // Ignore any variable that isn't accessible.
                tsym->saved_conversion_error = 0;
                this->visit_target_symbol(tsym); // NB: throws nothing ...
                if (tsym->saved_conversion_error) // ... but this is how we know it happened.
                  {
                    pf->raw_components += diename;
                    pf->raw_components += "=? ";
                  }
                else
                  {
                    pf->raw_components += diename;
                    pf->raw_components += "=%#x ";
                    pf->args.push_back(*(expression**)this->targets.top());
                  }
              }
            while (dwarf_siblingof (&result, &result) == 0);
        }

      pf->components = print_format::string_to_components(pf->raw_components);
      provide <print_format*> (this, pf);

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
      if (q.has_return && (e->base_name == "$return"))
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
  o << " /* pc=" << section << "+0x" << hex << addr << dec << " */";
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
                                         // module & section specify a relocation
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
  : derived_probe (q.base_probe, new probe_point(*q.base_loc) /* .components soon rewritten */ ),
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

  // XXX: hack for strange g++/gcc's
#ifndef USHRT_MAX
#define USHRT_MAX 32767
#endif

  // Range limit maxactive() value
  if (q.has_maxactive && (q.maxactive_val < 0 || q.maxactive_val > USHRT_MAX))
    throw semantic_error ("maxactive value out of range [0,"
                          + lex_cast<string>(USHRT_MAX) + "]",
                          q.base_loc->tok);

  // Make a target-variable-expanded copy of the probe body
  if (!null_die(scope_die))
    {
      dwarf_var_expanding_copy_visitor v (q, scope_die, dwfl_addr);
      require <statement*> (&v, &(this->body), this->body);

      // If during target-variable-expanding the probe, we added a new block
      // of code, add it to the start of the probe.
      if (v.add_block)
        this->body = new block(v.add_block, this->body);

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

  // Reset the sole element of the "locations" vector as a
  // "reverse-engineered" form of the incoming (q.base_loc) probe
  // point.  This allows a user to see what function / file / line
  // number any particular match of the wildcards.

  vector<probe_point::component*> comps;
  if (q.has_kernel)
    comps.push_back (new probe_point::component(TOK_KERNEL));
  else if(q.has_module)
    comps.push_back (new probe_point::component(TOK_MODULE, new literal_string(module)));
  else if(q.has_process)
    comps.push_back (new probe_point::component(TOK_PROCESS, new literal_string(module)));
  else
    assert (0);
     
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

  // Overwrite it.
  this->sole_location()->components = comps;
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

  register_function_and_statement_variants(root->bind_str(TOK_PROCESS), dw);
}

void
dwarf_derived_probe::emit_probe_local_init(translator_output * o)
{
  // emit bsp cache setup
  o->newline() << "bspcache(c->unwaddr, c->regs, c->probe_point);";
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

  // Emit an array of kprobe/kretprobe pointers
  s.op->newline() << "#if defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline() << "static void * stap_unreg_kprobes[" << probes_by_module.size() << "];";
  s.op->newline() << "#endif";

  // Emit the actual probe list.

  // NB: we used to plop a union { struct kprobe; struct kretprobe } into
  // struct stap_dwarf_probe, but it being initialized data makes it add
  // hundreds of bytes of padding per stap_dwarf_probe.  (PR5673)
  s.op->newline() << "struct stap_dwarf_kprobe {";
  s.op->newline(1) << "union { struct kprobe kp; struct kretprobe krp; } u;";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "struct kprobe dummy;";
  s.op->newline() << "#endif";
  s.op->newline(-1) << "} stap_dwarf_kprobes[" << probes_by_module.size() << "];";
  // NB: bss!

  s.op->newline() << "struct stap_dwarf_probe {";
  s.op->newline(1) << "const unsigned return_p:1;";
  s.op->newline() << "const unsigned maxactive_p:1;";
  s.op->newline() << "unsigned registered_p:1;";
  s.op->newline() << "const unsigned short maxactive_val;";

  // Let's find some stats for the three embedded strings.  Maybe they
  // are small and uniform enough to justify putting char[MAX]'s into
  // the array instead of relocated char*'s.
  size_t module_name_max = 0, section_name_max = 0, pp_name_max = 0;
  size_t module_name_tot = 0, section_name_tot = 0, pp_name_tot = 0;
  size_t all_name_cnt = probes_by_module.size(); // for average
  for (p_b_m_iterator it = probes_by_module.begin(); it != probes_by_module.end(); it++)
    {
      dwarf_derived_probe* p = it->second;
#define DOIT(var,expr) do {                             \
        size_t var##_size = (expr) + 1;                 \
        var##_max = max (var##_max, var##_size);        \
        var##_tot += var##_size; } while (0)
      DOIT(module_name, p->module.size());
      DOIT(section_name, p->section.size());
      DOIT(pp_name, lex_cast_qstring(*p->sole_location()).size());
#undef DOIT
    }

  // Decide whether it's worthwhile to use char[] or char* by comparing
  // the amount of average waste (max - avg) to the relocation data size
  // (3 native long words).
#define CALCIT(var)                                                     \
  if ((var##_name_max-(var##_name_tot/all_name_cnt)) < (3 * sizeof(void*))) \
    {                                                                   \
      s.op->newline() << "const char " << #var << "[" << var##_name_max << "];"; \
      if (s.verbose > 2) clog << "stap_dwarf_probe " << #var            \
                              << "[" << var##_name_max << "]" << endl;  \
    }                                                                   \
  else                                                                  \
    {                                                                   \
      s.op->newline() << "const char * const " << #var << ";";                 \
      if (s.verbose > 2) clog << "stap_dwarf_probe *" << #var << endl;  \
    }

  CALCIT(module);
  CALCIT(section);
  CALCIT(pp);

  s.op->newline() << "const unsigned long address;";
  s.op->newline() << "void (* const ph) (struct context*);";
  s.op->newline(-1) << "} stap_dwarf_probes[] = {";
  s.op->indent(1);

  for (p_b_m_iterator it = probes_by_module.begin(); it != probes_by_module.end(); it++)
    {
      dwarf_derived_probe* p = it->second;
      s.op->newline() << "{";
      if (p->has_return)
        s.op->line() << " .return_p=1,";
      if (p->has_maxactive)
        {
          s.op->line() << " .maxactive_p=1,";
          assert (p->maxactive_val >= 0 && p->maxactive_val <= USHRT_MAX);
          s.op->line() << " .maxactive_val=" << p->maxactive_val << ",";
        }
      s.op->line() << " .address=0x" << hex << p->addr << dec << "UL,";
      s.op->line() << " .module=\"" << p->module << "\",";
      s.op->line() << " .section=\"" << p->section << "\",";
      s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
      s.op->line() << " .ph=&" << p->name;
      s.op->line() << " },";
    }

  s.op->newline(-1) << "};";

  // Emit the kprobes callback function
  s.op->newline();
  s.op->newline() << "static int enter_kprobe_probe (struct kprobe *inst,";
  s.op->line() << " struct pt_regs *regs) {";
  // NB: as of PR5673, the kprobe|kretprobe union struct is in BSS
  s.op->newline(1) << "int kprobe_idx = ((uintptr_t)inst-(uintptr_t)stap_dwarf_kprobes)/sizeof(struct stap_dwarf_kprobe);";
  // Check that the index is plausible
  s.op->newline() << "struct stap_dwarf_probe *sdp = &stap_dwarf_probes[";
  s.op->line() << "((kprobe_idx >= 0 && kprobe_idx < " << probes_by_module.size() << ")?";
  s.op->line() << "kprobe_idx:0)"; // NB: at least we avoid memory corruption
  // XXX: it would be nice to give a more verbose error though; BUG_ON later?
  s.op->line() << "];";
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

  // NB: as of PR5673, the kprobe|kretprobe union struct is in BSS
  s.op->newline() << "int kprobe_idx = ((uintptr_t)krp-(uintptr_t)stap_dwarf_kprobes)/sizeof(struct stap_dwarf_kprobe);";
  // Check that the index is plausible
  s.op->newline() << "struct stap_dwarf_probe *sdp = &stap_dwarf_probes[";
  s.op->line() << "((kprobe_idx >= 0 && kprobe_idx < " << probes_by_module.size() << ")?";
  s.op->line() << "kprobe_idx:0)"; // NB: at least we avoid memory corruption
  // XXX: it would be nice to give a more verbose error though; BUG_ON later?
  s.op->line() << "];";

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
  s.op->newline() << "struct stap_dwarf_kprobe *kp = & stap_dwarf_kprobes[i];";
  s.op->newline() << "unsigned long relocated_addr = _stp_module_relocate (sdp->module, sdp->section, sdp->address);";
  s.op->newline() << "if (relocated_addr == 0) continue;"; // quietly; assume module is absent
  s.op->newline() << "probe_point = sdp->pp;"; // for error messages
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline(1) << "kp->u.krp.kp.addr = (void *) relocated_addr;";
  s.op->newline() << "if (sdp->maxactive_p) {";
  s.op->newline(1) << "kp->u.krp.maxactive = sdp->maxactive_val;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "kp->u.krp.maxactive = max(10, 4*NR_CPUS);";
  s.op->newline(-1) << "}";
  s.op->newline() << "kp->u.krp.handler = &enter_kretprobe_probe;";
  // to ensure safeness of bspcache, always use aggr_kprobe on ia64
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "kp->dummy.addr = kp->u.krp.kp.addr;";
  s.op->newline() << "kp->dummy.pre_handler = NULL;";
  s.op->newline() << "rc = register_kprobe (& kp->dummy);";
  s.op->newline() << "if (rc == 0) {";
  s.op->newline(1) << "rc = register_kretprobe (& kp->u.krp);";
  s.op->newline() << "if (rc != 0)";
  s.op->newline(1) << "unregister_kprobe (& kp->dummy);";
  s.op->newline(-2) << "}";
  s.op->newline() << "#else";
  s.op->newline() << "rc = register_kretprobe (& kp->u.krp);";
  s.op->newline() << "#endif";
  s.op->newline(-1) << "} else {";
  // to ensure safeness of bspcache, always use aggr_kprobe on ia64
  s.op->newline(1) << "kp->u.kp.addr = (void *) relocated_addr;";
  s.op->newline() << "kp->u.kp.pre_handler = &enter_kprobe_probe;";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "kp->dummy.addr = kp->u.kp.addr;";
  s.op->newline() << "kp->dummy.pre_handler = NULL;";
  s.op->newline() << "rc = register_kprobe (& kp->dummy);";
  s.op->newline() << "if (rc == 0) {";
  s.op->newline(1) << "rc = register_kprobe (& kp->u.kp);";
  s.op->newline() << "if (rc != 0)";
  s.op->newline(1) << "unregister_kprobe (& kp->dummy);";
  s.op->newline(-2) << "}";
  s.op->newline() << "#else";
  s.op->newline() << "rc = register_kprobe (& kp->u.kp);";
  s.op->newline() << "#endif";
  s.op->newline(-1) << "}";
  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "for (j=i-1; j>=0; j--) {"; // partial rollback
  s.op->newline(1) << "struct stap_dwarf_probe *sdp2 = & stap_dwarf_probes[j];";
  s.op->newline() << "struct stap_dwarf_kprobe *kp2 = & stap_dwarf_kprobes[j];";
  s.op->newline() << "if (sdp2->return_p) unregister_kretprobe (&kp2->u.krp);";
  s.op->newline() << "else unregister_kprobe (&kp2->u.kp);";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "unregister_kprobe (&kp2->dummy);";
  s.op->newline() << "#endif";
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
  //Unregister kprobes by batch interfaces.
  s.op->newline() << "#if defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline() << "j = 0;";
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "struct stap_dwarf_kprobe *kp = & stap_dwarf_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "if (!sdp->return_p)";
  s.op->newline(1) << "stap_unreg_kprobes[j++] = &kp->u.kp;";
  s.op->newline(-2) << "}";
  s.op->newline() << "unregister_kprobes((struct kprobe **)stap_unreg_kprobes, j);";
  s.op->newline() << "j = 0;";
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "struct stap_dwarf_kprobe *kp = & stap_dwarf_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "if (sdp->return_p)";
  s.op->newline(1) << "stap_unreg_kprobes[j++] = &kp->u.krp;";
  s.op->newline(-2) << "}";
  s.op->newline() << "unregister_kretprobes((struct kretprobe **)stap_unreg_kprobes, j);";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "j = 0;";
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "struct stap_dwarf_kprobe *kp = & stap_dwarf_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "stap_unreg_kprobes[j++] = &kp->dummy;";
  s.op->newline(-1) << "}";
  s.op->newline() << "unregister_kprobes((struct kprobe **)stap_unreg_kprobes, j);";
  s.op->newline() << "#endif";
  s.op->newline() << "#endif";

  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "struct stap_dwarf_kprobe *kp = & stap_dwarf_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline(1) << "unregister_kretprobe (&kp->u.krp);";
  s.op->newline() << "#endif";
  s.op->newline() << "atomic_add (kp->u.krp.nmissed, & skipped_count);";
  s.op->newline() << "atomic_add (kp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline(-1) << "} else {";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline(1) << "unregister_kprobe (&kp->u.kp);";
  s.op->newline() << "#endif";
  s.op->newline() << "atomic_add (kp->u.kp.nmissed, & skipped_count);";
  s.op->newline(-1) << "}";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES) && defined(__ia64__)";
  s.op->newline() << "unregister_kprobe (&kp->dummy);";
  s.op->newline() << "#endif";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";
}


void
dwarf_builder::build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results)
{
  // NB: the kernel/user dwlfpp objects are long-lived.
  // XXX: but they should be per-session, as this builder object
  // may be reused if we try to cross-instrument multiple targets.

  dwflpp* dw = 0;

  if (! sess.module_cache)
    sess.module_cache = new module_cache ();

  string module_name;
  if (has_null_param (parameters, TOK_KERNEL)
      || get_param (parameters, TOK_MODULE, module_name))
    {
      // kernel or kernel module target
      if (! kern_dw)
        {
          kern_dw = new dwflpp(sess);
          // XXX: PR 3498
          kern_dw->setup_kernel(); 
        }
      dw = kern_dw;

#if 0
      // Extract some kernel-side blacklist/relocation information.
      // XXX: This really should be per-module rather than per-kernel, since
      // .ko's may conceivably contain __kprobe-marked code
      Dwfl_Module* km = 0;
      kern_dw->iterate_over_modules(&query_kernel_module, &km);
      if (km)
        {
          
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
#endif
    }
  else if (get_param (parameters, TOK_PROCESS, module_name))
    {
      module_name = find_executable (module_name); // canonicalize it

      // user-space target; we use one dwflpp instance per module name
      // (= program or shared library)
      if (user_dw.find(module_name) == user_dw.end())
        {
          dw = new dwflpp(sess);
          // XXX: PR 3498
          dw->setup_user(module_name);
          user_dw[module_name] = dw;
        }
      else
        dw = user_dw[module_name];
    }


  dwarf_query q(sess, base, location, *dw, parameters, finished_results);


  // XXX: kernel.statement.absolute is a special case that requires no
  // dwfl processing.  This code should be in a separate builder.

  if (q.has_kernel && q.has_absolute)
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
      sess.unwindsym_modules.insert ("kernel");
      return;
    }

  dw->query_modules(&q);
}

symbol_table::~symbol_table()
{
  for (iterator_t i = list_by_addr.begin(); i != list_by_addr.end(); ++i)
    delete *i;
}

void
symbol_table::add_symbol(const char *name, bool weak, Dwarf_Addr addr,
                                                      Dwarf_Addr *high_addr)
{
#ifdef __powerpc__
  // Map ".sys_foo" to "sys_foo".
  if (name[0] == '.')
    name++;
#endif
  func_info *fi = new func_info();
  fi->addr = addr;
  fi->name = name;
  fi->weak = weak;
  map_by_name[fi->name] = fi;
  // TODO: Use a multimap in case there are multiple static
  // functions with the same name?
  list_by_addr.push_back(fi);
}

enum info_status
symbol_table::read_symbols(FILE *f, const string& path)
{
  // Based on do_kernel_symbols() in runtime/staprun/symbols.c
  int ret;
  char *name = 0;
  char *mod = 0;
  char type;
  unsigned long long addr;
  Dwarf_Addr high_addr = 0;
  int line = 0;

  // %as (non-POSIX) mallocs space for the string and stores its address.
  while ((ret = fscanf(f, "%llx %c %as [%as", &addr, &type, &name, &mod)) > 0)
    {
      auto_free free_name(name);
      auto_free free_mod(mod);
      line++;
      if (ret < 3)
        {
          cerr << "Symbol table error: Line " 
               << line
               << " of symbol list from "
               << path
               << " is not in correct format: address type name [module]";
          // Caller should delete symbol_table object.
          return info_absent;
        }
      else if (ret > 3)
        {
          // Modules are loaded above the kernel, so if we're getting
          // modules, we're done.
          break;
        }
      if (type == 'T' || type == 't' || type == 'W')
        add_symbol(name, (type == 'W'), (Dwarf_Addr) addr, &high_addr);
    }

  if (list_by_addr.size() < 1)
    {
      cerr << "Symbol table error: "
           << path << " contains no function symbols." << endl;
      return info_absent;
    }
  sort();
  return info_present;
}

// NB: This currently unused.  We use get_from_elf() instead because
// that gives us raw addresses -- which we need for modules -- whereas
// nm provides the address relative to the beginning of the section.
enum info_status
symbol_table::read_from_elf_file(const string &path)
{
  FILE *f;
  string cmd = string("/usr/bin/nm -n --defined-only ") + path;
  f = popen(cmd.c_str(), "r");
  if (!f)
    {
      // nm failures are detected by pclose, not popen.
      cerr << "Internal error reading symbol table from "
           << path << " -- " << strerror (errno);
      return info_absent;
    }
  enum info_status status = read_symbols(f, path);
  if (pclose(f) != 0)
    {
      if (status == info_present)
        cerr << "Warning: nm cannot read symbol table from " << path;
      return info_absent;
    }
  return status;
}

enum info_status
symbol_table::read_from_text_file(const string& path)
{
  FILE *f = fopen(path.c_str(), "r");
  if (!f)
    {
      cerr << "Warning: cannot read symbol table from "
           << path << " -- " << strerror (errno);
      return info_absent;
    }
  enum info_status status = read_symbols(f, path);
  (void) fclose(f);
  return status;
}

void
symbol_table::prepare_section_rejection(Dwfl_Module *mod)
{
#ifdef __powerpc__
  /*
   * The .opd section contains function descriptors that can look
   * just like function entry points.  For example, there's a function
   * descriptor called "do_exit" that links to the entry point ".do_exit".
   * Reject all symbols in .opd.
   */
  opd_section = SHN_UNDEF;
  Dwarf_Addr bias;
  Elf* elf = (dwarf_getelf (dwfl_module_getdwarf (mod, &bias))
                                    ?: dwfl_module_getelf (mod, &bias));
  Elf_Scn* scn = 0;
  size_t shstrndx;

  if (!elf)
    return;
  if (elf_getshstrndx(elf, &shstrndx) != 0)
    return;
  while ((scn = elf_nextscn(elf, scn)) != NULL)
    {
      GElf_Shdr shdr_mem;
      GElf_Shdr *shdr = gelf_getshdr(scn, &shdr_mem);
      if (!shdr)
        continue;
      const char *name = elf_strptr(elf, shstrndx, shdr->sh_name);
      if (!strcmp(name, ".opd"))
        {
          opd_section = elf_ndxscn(scn);
          return;
        }
    }
#endif
}

bool
symbol_table::reject_section(GElf_Word section)
{
  if (section == SHN_UNDEF)
    return true;
#ifdef __powerpc__
  if (section == opd_section)
    return true;
#endif
  return false;
}

enum info_status
symbol_table::get_from_elf()
{
  Dwarf_Addr high_addr = 0;
  Dwfl_Module *mod = mod_info->mod;
  int syments = dwfl_module_getsymtab(mod);
  assert(syments);
  prepare_section_rejection(mod);
  for (int i = 1; i < syments; ++i)
    {
      GElf_Sym sym;
      GElf_Word section;
      const char *name = dwfl_module_getsym(mod, i, &sym, &section);
      if (name && GELF_ST_TYPE(sym.st_info) == STT_FUNC &&
                                                   !reject_section(section))
        add_symbol(name, (GELF_ST_BIND(sym.st_info) == STB_WEAK),
                                              sym.st_value, &high_addr);
    }
  sort();
  return info_present;
}

void
symbol_table::mark_dwarf_redundancies(dwflpp *dw)
{
  // dwflpp.cu_function_cache maps each module_name:cu_name to a
  // vector of Dwarf_Dies, one per function.
  string module_prefix = string(mod_info->name) + ":";

  for (mod_cu_function_cache_t::iterator cu = dw->cu_function_cache.begin();
       cu != dw->cu_function_cache.end(); cu++)
    {
      string key = cu->first;
      if (key.find(module_prefix) == 0)
        {
          // Found a compilation unit in the module of interest.
          // Mark all its functions in the symbol table.
          cu_function_cache_t* v = cu->second;
          assert(v);
          for (cu_function_cache_t::iterator fc = v->begin(); fc != v->end(); fc++)
            {
              Dwarf_Die func = fc->second;
              string func_name = fc->first; // == dwarf_diename(&func);
              // map_by_name[func_name]->die = func;
              map<string, func_info*>::iterator i = map_by_name.find(func_name);
              // Func names can show up in the dwarf but not the symtab (!).
              if (i != map_by_name.end())
                {
                  func_info *fi = i->second;
                  fi->die = func;
                }
            }
        }
    }
}

func_info *
symbol_table::get_func_containing_address(Dwarf_Addr addr)
{
  iterator_t iter = upper_bound(list_by_addr.begin(), list_by_addr.end(), addr,
                              func_info::Compare());
  if (iter == list_by_addr.begin())
    return NULL;
  else
    return *(iter - 1);
}

func_info *
symbol_table::lookup_symbol(const string& name)
{
  map<string, func_info*>::iterator i = map_by_name.find(name);
  if (i == map_by_name.end())
    return NULL;
  return i->second;
}

Dwarf_Addr
symbol_table::lookup_symbol_address(const string& name)
{
  func_info *fi = lookup_symbol(name);
  if (fi)
    return fi->addr;
  return 0;
}

// This is the kernel symbol table.  The kernel macro cond_syscall creates
// a weak symbol for each system call and maps it to sys_ni_syscall.
// For system calls not implemented elsewhere, this weak symbol shows up
// in the kernel symbol table.  Following the precedent of dwarfful stap,
// we refuse to consider such symbols.  Here we delete them from our
// symbol table.
// TODO: Consider generalizing this and/or making it part of blacklist
// processing.
void
symbol_table::purge_syscall_stubs()
{
  Dwarf_Addr stub_addr = lookup_symbol_address("sys_ni_syscall");
  if (stub_addr == 0)
    return;
  range_t purge_range = equal_range(list_by_addr.begin(), list_by_addr.end(),
                                    stub_addr, func_info::Compare());
  for (iterator_t iter = purge_range.first;
       iter != purge_range.second;
       ++iter)
    {
      func_info *fi = *iter;
      if (fi->weak && fi->name != "sys_ni_syscall")
        {
          map_by_name.erase(fi->name);
          delete fi;
          *iter = 0;
        }
    }
  // Range might have null pointer entries that should be erased.
  list_by_addr.erase(remove(purge_range.first, purge_range.second,
                            (func_info*)0),
                     purge_range.second);
}

void
symbol_table::sort()
{
  stable_sort(list_by_addr.begin(), list_by_addr.end(), func_info::Compare());
}

void
module_info::get_symtab(dwarf_query *q)
{
  systemtap_session &sess = q->sess;

  sym_table = new symbol_table(this);
  if (!elf_path.empty())
    {
      if (name == TOK_KERNEL && !sess.kernel_symtab_path.empty())
        cerr << "Warning: reading symbol table from "
             << elf_path
             << " -- ignoring "
             << sess.kernel_symtab_path
             << endl ;;
      symtab_status = sym_table->get_from_elf();
    }
  else
    {
      assert(name == TOK_KERNEL);
      if (sess.kernel_symtab_path.empty())
        {
          symtab_status = info_absent;
          cerr << "Error: Cannot find vmlinux."
               << "  Consider using --kmap instead of --kelf."
               << endl;;
        }
      else
        {
          symtab_status =
      		sym_table->read_from_text_file(sess.kernel_symtab_path);
          if (symtab_status == info_present)
            {
              sess.sym_kprobes_text_start =
                sym_table->lookup_symbol_address("__kprobes_text_start");
              sess.sym_kprobes_text_end =
                sym_table->lookup_symbol_address("__kprobes_text_end");
              sess.sym_stext = sym_table->lookup_symbol_address("_stext");
              bias = sym_table->lookup_symbol_address("_text");
            }
        }
    }
  if (symtab_status == info_absent)
    {
      delete sym_table;
      sym_table = NULL;
      return;
    }

  // If we have dwarf for the same module, mark the redundant symtab
  // entries.
  //
  // In dwarf_query::handle_query_module(), the call to query_module_dwarf()
  // precedes the call to query_module_symtab().  So we should never read
  // a module's symbol table without first having tried to get its dwarf.
  sym_table->mark_dwarf_redundancies(&q->dw);

  if (name == TOK_KERNEL)
    sym_table->purge_syscall_stubs();
}

module_info::~module_info()
{
  if (sym_table)
    delete sym_table;
}



// ------------------------------------------------------------------------
// task_finder derived 'probes': These don't really exist.  The whole
// purpose of the task_finder_derived_probe_group is to make sure that
// stap_start_task_finder()/stap_stop_task_finder() get called only
// once and in the right place.
// ------------------------------------------------------------------------

struct task_finder_derived_probe: public derived_probe
{
  // Dummy constructor for gcc 3.4 compatibility
  task_finder_derived_probe (): derived_probe (0) { assert(0); }
};


struct task_finder_derived_probe_group: public generic_dpg<task_finder_derived_probe>
{
public:
  static void create_session_group (systemtap_session& s);

  void emit_module_decls (systemtap_session& ) { }
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


void
task_finder_derived_probe_group::create_session_group (systemtap_session& s)
{
  if (! s.task_finder_derived_probes)
    s.task_finder_derived_probes = new task_finder_derived_probe_group();
}


void
task_finder_derived_probe_group::emit_module_init (systemtap_session& s)
{
  s.op->newline();
  s.op->newline() << "/* ---- task finder ---- */";
  s.op->newline() << "rc = stap_start_task_finder();";

  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "stap_stop_task_finder();";
  s.op->newline(-1) << "}";
}


void
task_finder_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  s.op->newline();
  s.op->newline() << "/* ---- task finder ---- */";
  s.op->newline() << "stap_stop_task_finder();";
}

// ------------------------------------------------------------------------
// itrace user-space probes
// ------------------------------------------------------------------------


struct itrace_derived_probe: public derived_probe
{
  bool has_path;
  string path;
  int64_t pid;
  int single_step;

  itrace_derived_probe (systemtap_session &s, probe* p, probe_point* l,
                        bool hp, string &pn, int64_t pd, int ss
			);
  void join_group (systemtap_session& s);
};


struct itrace_derived_probe_group: public generic_dpg<itrace_derived_probe>
{
private:
  map<string, vector<itrace_derived_probe*> > probes_by_path;
  typedef map<string, vector<itrace_derived_probe*> >::iterator p_b_path_iterator;
  map<int64_t, vector<itrace_derived_probe*> > probes_by_pid;
  typedef map<int64_t, vector<itrace_derived_probe*> >::iterator p_b_pid_iterator;
  unsigned num_probes;

  void emit_probe_decl (systemtap_session& s, itrace_derived_probe *p);

public:
  itrace_derived_probe_group(): num_probes(0) { }

  void enroll (itrace_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


itrace_derived_probe::itrace_derived_probe (systemtap_session &s,
                                            probe* p, probe_point* l,
                                            bool hp, string &pn, int64_t pd,
											int ss
					    ):
  derived_probe(p, l), has_path(hp), path(pn), pid(pd), single_step(ss)
{
}


void
itrace_derived_probe::join_group (systemtap_session& s)
{
  if (! s.itrace_derived_probes)
      s.itrace_derived_probes = new itrace_derived_probe_group ();

  s.itrace_derived_probes->enroll (this);

  task_finder_derived_probe_group::create_session_group (s);
}

struct itrace_builder: public derived_probe_builder
{
  itrace_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    string path;
    int64_t pid;
    int single_step;

    bool has_path = get_param (parameters, TOK_PROCESS, path);
    bool has_pid = get_param (parameters, TOK_PROCESS, pid);
    assert (has_path || has_pid);

    single_step = 1;

    // If we have a path, we need to validate it.
    if (has_path)
      path = find_executable (path);

    finished_results.push_back(new itrace_derived_probe(sess, base, location,
                                                        has_path, path, pid,
                                                        single_step
							));
  }
};


void
itrace_derived_probe_group::enroll (itrace_derived_probe* p)
{
  if (p->has_path)
    probes_by_path[p->path].push_back(p);
  else
    probes_by_pid[p->pid].push_back(p);
  num_probes++;

  // XXX: multiple exec probes (for instance) for the same path (or
  // pid) should all share a itrace report function, and have their
  // handlers executed sequentially.
}


void
itrace_derived_probe_group::emit_probe_decl (systemtap_session& s,
					     itrace_derived_probe *p)
{
  s.op->newline() << "{";
  s.op->line() << " .tgt={";

  if (p->has_path)
    {
      s.op->line() << " .pathname=\"" << p->path << "\",";
      s.op->line() << " .pid=0,";
    }
  else
    {
      s.op->line() << " .pathname=NULL,";
      s.op->line() << " .pid=" << p->pid << ",";
    }

  s.op->line() << " .callback=&_stp_itrace_probe_cb,";
  s.op->line() << " },";
  s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
  s.op->line() << " .single_step=" << p->single_step << ",";
  s.op->line() << " .ph=&" << p->name << ",";

  s.op->line() << " },";
}


void
itrace_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline();
  s.op->newline() << "/* ---- itrace probes ---- */";
  s.op->newline() << "struct stap_itrace_probe {";
  s.op->indent(1);
  s.op->newline() << "struct stap_task_finder_target tgt;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "int single_step;";
  s.op->newline(-1) << "};";
  s.op->newline() << "static void enter_itrace_probe(struct stap_itrace_probe *p, struct pt_regs *regs, void *data);";
  s.op->newline() << "#include \"itrace.c\"";

  // output routine to call itrace probe
  s.op->newline() << "static void enter_itrace_probe(struct stap_itrace_probe *p, struct pt_regs *regs, void *data) {";
  s.op->indent(1);

  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "c->probe_point = p->pp;";
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "c->data = data;";

  // call probe function
  s.op->newline() << "(*p->ph) (c);";
  common_probe_entryfn_epilogue (s.op);

  s.op->newline() << "return;";
  s.op->newline(-1) << "}";

  // Output task finder callback routine that gets called for all
  // itrace probe types.
  s.op->newline() << "static int _stp_itrace_probe_cb(struct stap_task_finder_target *tgt, struct task_struct *tsk, int register_p, int process_p) {";
  s.op->indent(1);
  s.op->newline() << "int rc = 0;";
  s.op->newline() << "struct stap_itrace_probe *p = container_of(tgt, struct stap_itrace_probe, tgt);";

  s.op->newline() << "if (register_p) ";
  s.op->indent(1);

  s.op->newline() << "rc = usr_itrace_init(p->single_step, tsk->pid, p);";
  s.op->newline(-1) << "else";
  s.op->newline(1) << "remove_usr_itrace_info(find_itrace_info(p->tgt.pid));";
  s.op->newline(-1) << "return rc;";
  s.op->newline(-1) << "}";

  s.op->newline() << "struct stap_itrace_probe stap_itrace_probes[] = {";
  s.op->indent(1);

  // Set up 'process(PATH)' probes
  if (! probes_by_path.empty())
    {
      for (p_b_path_iterator it = probes_by_path.begin();
	   it != probes_by_path.end(); it++)
        {
	  for (unsigned i = 0; i < it->second.size(); i++)
	    {
	      itrace_derived_probe *p = it->second[i];
	      emit_probe_decl(s, p);
	    }
	}
    }

  // Set up 'process(PID)' probes
  if (! probes_by_pid.empty())
    {
      for (p_b_pid_iterator it = probes_by_pid.begin();
	   it != probes_by_pid.end(); it++)
        {
	  for (unsigned i = 0; i < it->second.size(); i++)
	    {
	      itrace_derived_probe *p = it->second[i];
	      emit_probe_decl(s, p);
	    }
	}
    }
  s.op->newline(-1) << "};";
}


void
itrace_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline();
  s.op->newline() << "/* ---- itrace probes ---- */";

  s.op->newline() << "for (i=0; i<" << num_probes << "; i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_itrace_probe *p = &stap_itrace_probes[i];";
  s.op->newline() << "rc = stap_register_task_finder_target(&p->tgt);";
  s.op->newline(-1) << "}";
}


void
itrace_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty()) return;
  s.op->newline();
  s.op->newline() << "/* ---- itrace probes ---- */";
  s.op->newline() << "cleanup_usr_itrace();";
}

// ------------------------------------------------------------------------
// utrace user-space probes
// ------------------------------------------------------------------------

static string TOK_THREAD("thread");
static string TOK_SYSCALL("syscall");

// Note that these flags don't match up exactly with UTRACE_EVENT
// flags (and that's OK).
enum utrace_derived_probe_flags {
  UDPF_NONE,
  UDPF_BEGIN,				// process begin
  UDPF_END,				// process end
  UDPF_THREAD_BEGIN,			// thread begin
  UDPF_THREAD_END,			// thread end
  UDPF_SYSCALL,				// syscall entry
  UDPF_SYSCALL_RETURN,			// syscall exit
  UDPF_NFLAGS
};

struct utrace_derived_probe: public derived_probe
{
  bool has_path;
  string path;
  int64_t pid;
  enum utrace_derived_probe_flags flags;
  bool target_symbol_seen;

  utrace_derived_probe (systemtap_session &s, probe* p, probe_point* l,
                        bool hp, string &pn, int64_t pd,
			enum utrace_derived_probe_flags f);
  void join_group (systemtap_session& s);
};


struct utrace_derived_probe_group: public generic_dpg<utrace_derived_probe>
{
private:
  map<string, vector<utrace_derived_probe*> > probes_by_path;
  typedef map<string, vector<utrace_derived_probe*> >::iterator p_b_path_iterator;
  map<int64_t, vector<utrace_derived_probe*> > probes_by_pid;
  typedef map<int64_t, vector<utrace_derived_probe*> >::iterator p_b_pid_iterator;
  unsigned num_probes;
  bool flags_seen[UDPF_NFLAGS];

  void emit_probe_decl (systemtap_session& s, utrace_derived_probe *p);
  void emit_vm_callback_probe_decl (systemtap_session& s, bool has_path,
				    string path, int64_t pid,
				    string vm_callback);

public:
  utrace_derived_probe_group(): num_probes(0), flags_seen() { }

  void enroll (utrace_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct utrace_var_expanding_copy_visitor: public var_expanding_copy_visitor
{
  utrace_var_expanding_copy_visitor(systemtap_session& s, const string& pn,
				    enum utrace_derived_probe_flags f):
    sess (s), probe_name (pn), flags (f), target_symbol_seen (false) {}

  systemtap_session& sess;
  string probe_name;
  enum utrace_derived_probe_flags flags;
  bool target_symbol_seen;

  void visit_target_symbol (target_symbol* e);
};



utrace_derived_probe::utrace_derived_probe (systemtap_session &s,
                                            probe* p, probe_point* l,
                                            bool hp, string &pn, int64_t pd,
					    enum utrace_derived_probe_flags f):
  derived_probe (p, new probe_point (*l) /* .components soon rewritten */ ),
  has_path(hp), path(pn), pid(pd), flags(f),
  target_symbol_seen(false)
{
  // Make a local-variable-expanded copy of the probe body
  utrace_var_expanding_copy_visitor v (s, name, flags);
  require <statement*> (&v, &(this->body), base->body);
  target_symbol_seen = v.target_symbol_seen;

  // Reset the sole element of the "locations" vector as a
  // "reverse-engineered" form of the incoming (q.base_loc) probe
  // point.  This allows a user to see what program etc.
  // number any particular match of the wildcards.

  vector<probe_point::component*> comps;
  if (hp)
    comps.push_back (new probe_point::component(TOK_PROCESS, new literal_string(path)));
  else
    comps.push_back (new probe_point::component(TOK_PROCESS, new literal_number(pid)));    
  switch (flags)
    {
    case UDPF_THREAD_BEGIN:
      comps.push_back (new probe_point::component(TOK_THREAD));
      comps.push_back (new probe_point::component(TOK_BEGIN));
      break;
    case UDPF_THREAD_END:
      comps.push_back (new probe_point::component(TOK_THREAD));
      comps.push_back (new probe_point::component(TOK_END));
      break;
    case UDPF_SYSCALL:
      comps.push_back (new probe_point::component(TOK_SYSCALL));
      break;
    case UDPF_SYSCALL_RETURN:
      comps.push_back (new probe_point::component(TOK_SYSCALL));
      comps.push_back (new probe_point::component(TOK_RETURN));
      break;
    case UDPF_BEGIN:
      comps.push_back (new probe_point::component(TOK_BEGIN));
      break;
    case UDPF_END:
      comps.push_back (new probe_point::component(TOK_END));
      break;
    default: 
      assert (0);
    }

  // Overwrite it.
  this->sole_location()->components = comps;
}


void
utrace_derived_probe::join_group (systemtap_session& s)
{
  if (! s.utrace_derived_probes)
    {
      s.utrace_derived_probes = new utrace_derived_probe_group ();
    }
  s.utrace_derived_probes->enroll (this);

  task_finder_derived_probe_group::create_session_group (s);
}


void
utrace_var_expanding_copy_visitor::visit_target_symbol (target_symbol* e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (flags != UDPF_SYSCALL && flags != UDPF_SYSCALL_RETURN)
    throw semantic_error ("only \"process(PATH_OR_PID).syscall\" and \"process(PATH_OR_PID).syscall.return\" probes support target symbols",
			  e->tok);

  if (e->base_name != "$syscall")
    throw semantic_error ("invalid target symbol for utrace probe, $syscall expected",
			  e->tok);

  if (e->components.size() > 0)
    {
      switch (e->components[0].first)
	{
	case target_symbol::comp_literal_array_index:
	  throw semantic_error("utrace target variable '$syscall' may not be used as array",
			       e->tok);
	  break;
	case target_symbol::comp_struct_member:
	  throw semantic_error("utrace target variable '$syscall' may not be used as a structure",
			       e->tok);
	  break;
	default:
	  throw semantic_error ("invalid use of utrace target variable '$syscall'",
				e->tok);
	  break;
	}
    }

  bool lvalue = is_active_lvalue(e);
  if (lvalue)
    throw semantic_error("utrace $syscall variable is read-only", e->tok);

  // Remember that we've seen a target variable.
  target_symbol_seen = true;

  // We're going to substitute a synthesized '_syscall_nr_get'
  // function call for the '$syscall' reference.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = "_utrace_syscall_nr";
  n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session

  provide <functioncall*> (this, n);
}


struct utrace_builder: public derived_probe_builder
{
  utrace_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    string path;
    int64_t pid;

    bool has_path = get_param (parameters, TOK_PROCESS, path);
    bool has_pid = get_param (parameters, TOK_PROCESS, pid);
    enum utrace_derived_probe_flags flags = UDPF_NONE;

    assert (has_path || has_pid);

    if (has_null_param (parameters, TOK_THREAD))
      {
	if (has_null_param (parameters, TOK_BEGIN))
	  flags = UDPF_THREAD_BEGIN;
	else if (has_null_param (parameters, TOK_END))
	  flags = UDPF_THREAD_END;
      }
    else if (has_null_param (parameters, TOK_SYSCALL))
      {
	if (has_null_param (parameters, TOK_RETURN))
	  flags = UDPF_SYSCALL_RETURN;
	else
	  flags = UDPF_SYSCALL;
      }
    else if (has_null_param (parameters, TOK_BEGIN))
      flags = UDPF_BEGIN;
    else if (has_null_param (parameters, TOK_END))
      flags = UDPF_END;

    // If we didn't get a path or pid, this means to probe everything.
    // Convert this to a pid-based probe.
    if (! has_path && ! has_pid)
      {
	has_path = false;
	path.clear();
	has_pid = true;
	pid = 0;
      }
    else if (has_path)
      {
        path = find_executable (path);
        sess.unwindsym_modules.insert (path);
      }
    else if (has_pid)
      {
	// We can't probe 'init' (pid 1).  XXX: where does this limitation come from?
	if (pid < 2)
	  throw semantic_error ("process pid must be greater than 1",
				location->tok);
      }

    finished_results.push_back(new utrace_derived_probe(sess, base, location,
                                                        has_path, path, pid,
							flags));
  }
};


void
utrace_derived_probe_group::enroll (utrace_derived_probe* p)
{
  if (p->has_path)
    probes_by_path[p->path].push_back(p);
  else
    probes_by_pid[p->pid].push_back(p);
  num_probes++;
  flags_seen[p->flags] = true;

  // XXX: multiple exec probes (for instance) for the same path (or
  // pid) should all share a utrace report function, and have their
  // handlers executed sequentially.
}


void
utrace_derived_probe_group::emit_probe_decl (systemtap_session& s,
					     utrace_derived_probe *p)
{
  s.op->newline() << "{";
  s.op->line() << " .tgt={";

  if (p->has_path)
    {
      s.op->line() << " .pathname=\"" << p->path << "\",";
      s.op->line() << " .pid=0,";
    }
  else
    {
      s.op->line() << " .pathname=NULL,";
      s.op->line() << " .pid=" << p->pid << ",";
    }

  s.op->line() << " .callback=&_stp_utrace_probe_cb,";
  s.op->line() << " .vm_callback=NULL,";
  s.op->line() << " },";
  s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
  s.op->line() << " .ph=&" << p->name << ",";

  // Handle flags
  switch (p->flags)
    {
    // Look in _stp_utrace_probe_cb for description of why quiesce is
    // used here.
    case UDPF_BEGIN:				// process begin
      s.op->line() << " .flags=(UDPF_BEGIN),";
      s.op->line() << " .ops={ .report_quiesce=stap_utrace_probe_quiesce },";
      s.op->line() << " .events=(UTRACE_STOP|UTRACE_EVENT(QUIESCE)),";
      break;
    case UDPF_THREAD_BEGIN:			// thread begin
      s.op->line() << " .flags=(UDPF_THREAD_BEGIN),";
      s.op->line() << " .ops={ .report_quiesce=stap_utrace_probe_quiesce },";
      s.op->line() << " .events=(UTRACE_STOP|UTRACE_EVENT(QUIESCE)),";
      break;

    // Notice we're not setting up a .ops/.report_death handler for
    // either UDPF_END or UDPF_THREAD_END.  Instead, we'll just call
    // the probe directly when we get notified.
    case UDPF_END:				// process end
      s.op->line() << " .flags=(UDPF_END),";
      break;
    case UDPF_THREAD_END:			// thread end
      s.op->line() << " .flags=(UDPF_THREAD_END),";
      break;

    // For UDPF_SYSCALL/UDPF_SYSCALL_RETURN probes, the .report_death
    // handler isn't strictly necessary.  However, it helps to keep
    // our attaches/detaches symmetrical.  Notice we're using quiesce
    // as a workaround for bug 6841.
    case UDPF_SYSCALL:
      s.op->line() << " .flags=(UDPF_SYSCALL),";
      s.op->line() << " .ops={ .report_syscall_entry=stap_utrace_probe_syscall,  .report_death=stap_utrace_task_finder_report_death, .report_quiesce=stap_utrace_probe_syscall_quiesce },";
      s.op->line() << " .events=(UTRACE_STOP|UTRACE_EVENT(QUIESCE)|UTRACE_EVENT(DEATH)),";
      break;
    case UDPF_SYSCALL_RETURN:
      s.op->line() << " .flags=(UDPF_SYSCALL_RETURN),";
      s.op->line() << " .ops={ .report_syscall_exit=stap_utrace_probe_syscall, .report_death=stap_utrace_task_finder_report_death, .report_quiesce=stap_utrace_probe_syscall_quiesce },";
      s.op->line() << " .events=(UTRACE_STOP|UTRACE_EVENT(QUIESCE)|UTRACE_EVENT(DEATH)),";
      break;

    case UDPF_NONE:
      s.op->line() << " .flags=(UDPF_NONE),";
      s.op->line() << " .ops={ },";
      s.op->line() << " .events=0,";
      break;
    default:
      throw semantic_error ("bad utrace probe flag");
      break;
    }
  s.op->line() << " .engine_attached=0,";
  s.op->line() << " },";
}


void
utrace_derived_probe_group::emit_vm_callback_probe_decl (systemtap_session& s,
							 bool has_path,
							 string path,
							 int64_t pid,
							 string vm_callback)
{
  s.op->newline() << "{";
  s.op->line() << " .tgt={";

  if (has_path)
    {
      s.op->line() << " .pathname=\"" << path << "\",";
      s.op->line() << " .pid=0,";
    }
  else
    {
      s.op->line() << " .pathname=NULL,";
      s.op->line() << " .pid=" << pid << ",";
    }

  s.op->line() << " .callback=NULL,";
  s.op->line() << " .vm_callback=&" << vm_callback << ",";
  s.op->line() << " },";
  s.op->line() << " .pp=\"internal\",";
  s.op->line() << " .ph=NULL,";
  s.op->line() << " .flags=(UDPF_NONE),";
  s.op->line() << " .ops={ NULL },";
  s.op->line() << " .events=0,";
  s.op->line() << " .engine_attached=0,";
  s.op->line() << " },";
}


void
utrace_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline();
  s.op->newline() << "/* ---- utrace probes ---- */";
  s.op->newline() << "#include \"task_finder.c\"";

  s.op->newline() << "enum utrace_derived_probe_flags {";
  s.op->indent(1);
  s.op->newline() << "UDPF_NONE,";
  s.op->newline() << "UDPF_BEGIN,";
  s.op->newline() << "UDPF_END,";
  s.op->newline() << "UDPF_THREAD_BEGIN,";
  s.op->newline() << "UDPF_THREAD_END,";
  s.op->newline() << "UDPF_SYSCALL,";
  s.op->newline() << "UDPF_SYSCALL_RETURN,";
  s.op->newline() << "UDPF_NFLAGS";
  s.op->newline(-1) << "};";

  s.op->newline() << "struct stap_utrace_probe {";
  s.op->indent(1);
  s.op->newline() << "struct stap_task_finder_target tgt;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "enum utrace_derived_probe_flags flags;";
  s.op->newline() << "struct utrace_engine_ops ops;";
  s.op->newline() << "unsigned long events;";
  s.op->newline() << "int engine_attached;";
  s.op->newline(-1) << "};";


  // Output handler function for UDPF_BEGIN and UDPF_THREAD_BEGIN
  if (flags_seen[UDPF_BEGIN] || flags_seen[UDPF_THREAD_BEGIN])
    {
      s.op->newline() << "#ifdef UTRACE_ORIG_VERSION";
      s.op->newline() << "static u32 stap_utrace_probe_quiesce(struct utrace_attached_engine *engine, struct task_struct *tsk) {";
      s.op->newline() << "#else";
      s.op->newline() << "static u32 stap_utrace_probe_quiesce(enum utrace_resume_action action, struct utrace_attached_engine *engine, struct task_struct *tsk, unsigned long event) {";
      s.op->newline() << "#endif";
      s.op->indent(1);
      s.op->newline() << "struct stap_utrace_probe *p = (struct stap_utrace_probe *)engine->data;";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
      s.op->newline() << "c->probe_point = p->pp;";

      // call probe function
      s.op->newline() << "(*p->ph) (c);";
      common_probe_entryfn_epilogue (s.op);

      // we're detaching, so utrace automatically restarts the thread.
      s.op->newline() << "debug_task_finder_detach();";
      s.op->newline() << "return UTRACE_DETACH;";
      s.op->newline(-1) << "}";
    }

  // Output handler function for UDPF_END and UDPF_THREAD_END
  if (flags_seen[UDPF_END] || flags_seen[UDPF_THREAD_END])
    {
      s.op->newline() << "static void stap_utrace_probe_handler(struct task_struct *tsk, struct stap_utrace_probe *p) {";
      s.op->indent(1);

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
      s.op->newline() << "c->probe_point = p->pp;";

      // call probe function
      s.op->newline() << "(*p->ph) (c);";
      common_probe_entryfn_epilogue (s.op);

      s.op->newline() << "return;";
      s.op->newline(-1) << "}";
    }

  // Output handler function for SYSCALL_ENTRY and SYSCALL_EXIT events
  if (flags_seen[UDPF_SYSCALL] || flags_seen[UDPF_SYSCALL_RETURN])
    {
      s.op->newline() << "#ifdef UTRACE_ORIG_VERSION";
      s.op->newline() << "static u32 stap_utrace_probe_syscall_quiesce(struct utrace_attached_engine *engine, struct task_struct *tsk) {";
      s.op->newline() << "#else";
      s.op->newline() << "static u32 stap_utrace_probe_syscall_quiesce(enum utrace_resume_action action, struct utrace_attached_engine *engine, struct task_struct *tsk, unsigned long event) {";
      s.op->newline() << "#endif";
      s.op->indent(1);
      s.op->newline() << "struct stap_utrace_probe *p = (struct stap_utrace_probe *)engine->data;";
      s.op->newline() << "int rc = 0;";

      // Turn off quiesce handling and turn on either syscall entry
      // or exit events.
      s.op->newline() << "if (p->flags == UDPF_SYSCALL)";
      s.op->indent(1);
      s.op->newline() << "rc = utrace_set_events(tsk, engine, UTRACE_EVENT(SYSCALL_ENTRY)|UTRACE_EVENT(DEATH));";
      s.op->indent(-1);
      s.op->newline() << "else if (p->flags == UDPF_SYSCALL_RETURN)";
      s.op->indent(1);
      s.op->newline() << "rc = utrace_set_events(tsk, engine, UTRACE_EVENT(SYSCALL_EXIT)|UTRACE_EVENT(DEATH));";
      s.op->indent(-1);
      s.op->newline() << "if (rc != 0)";
      s.op->indent(1);
      s.op->newline() << "_stp_error(\"utrace_set_events returned error %d on pid %d\", rc, (int)tsk->pid);";
      s.op->indent(-1);

      s.op->newline() << "return UTRACE_RESUME;";
      s.op->newline(-1) << "}";

      s.op->newline() << "#ifdef UTRACE_ORIG_VERSION";
      s.op->newline() << "static u32 stap_utrace_probe_syscall(struct utrace_attached_engine *engine, struct task_struct *tsk, struct pt_regs *regs) {";
      s.op->newline() << "#else";
      s.op->newline() << "static u32 stap_utrace_probe_syscall(enum utrace_resume_action action, struct utrace_attached_engine *engine, struct task_struct *tsk, struct pt_regs *regs) {";
      s.op->newline() << "#endif";

      s.op->indent(1);
      s.op->newline() << "struct stap_utrace_probe *p = (struct stap_utrace_probe *)engine->data;";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
      s.op->newline() << "c->probe_point = p->pp;";
      s.op->newline() << "c->regs = regs;";

      // call probe function
      s.op->newline() << "(*p->ph) (c);";
      common_probe_entryfn_epilogue (s.op);

      s.op->newline() << "return UTRACE_RESUME;";
      s.op->newline(-1) << "}";
    }

  // Output task_finder callback routine that gets called for all
  // utrace probe types.
  s.op->newline() << "static int _stp_utrace_probe_cb(struct stap_task_finder_target *tgt, struct task_struct *tsk, int register_p, int process_p) {";
  s.op->indent(1);
  s.op->newline() << "int rc = 0;";
  s.op->newline() << "struct stap_utrace_probe *p = container_of(tgt, struct stap_utrace_probe, tgt);";
  s.op->newline() << "struct utrace_attached_engine *engine;";

  s.op->newline() << "if (register_p) {";
  s.op->indent(1);

  s.op->newline() << "switch (p->flags) {";
  s.op->indent(1);

  // When receiving a UTRACE_EVENT(CLONE) event, we can't call the
  // begin/thread.begin probe directly.  So, we'll just attach an
  // engine that waits for the thread to quiesce.  When the thread
  // quiesces, then call the probe.
  if (flags_seen[UDPF_BEGIN])
  {
      s.op->newline() << "case UDPF_BEGIN:";
      s.op->indent(1);
      s.op->newline() << "if (process_p) {";
      s.op->indent(1);
      s.op->newline() << "rc = stap_utrace_attach(tsk, &p->ops, p, p->events);";
      s.op->newline() << "if (rc == 0) {";
      s.op->indent(1);
      s.op->newline() << "p->engine_attached = 1;";
      s.op->newline(-1) << "}";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
  }
  if (flags_seen[UDPF_THREAD_BEGIN])
  {
      s.op->newline() << "case UDPF_THREAD_BEGIN:";
      s.op->indent(1);
      s.op->newline() << "if (! process_p) {";
      s.op->indent(1);
      s.op->newline() << "rc = stap_utrace_attach(tsk, &p->ops, p, p->events);";
      s.op->newline() << "if (rc == 0) {";
      s.op->indent(1);
      s.op->newline() << "p->engine_attached = 1;";
      s.op->newline(-1) << "}";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
  }

  // For end/thread_end probes, do nothing at registration time.
  // We'll handle these in the 'register_p == 0' case.
  if (flags_seen[UDPF_END] || flags_seen[UDPF_THREAD_END])
    {
      s.op->newline() << "case UDPF_END:";
      s.op->newline() << "case UDPF_THREAD_END:";
      s.op->indent(1);
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  // Attach an engine for SYSCALL_ENTRY and SYSCALL_EXIT events.
  if (flags_seen[UDPF_SYSCALL] || flags_seen[UDPF_SYSCALL_RETURN])
    {
      s.op->newline() << "case UDPF_SYSCALL:";
      s.op->newline() << "case UDPF_SYSCALL_RETURN:";
      s.op->indent(1);
      s.op->newline() << "rc = stap_utrace_attach(tsk, &p->ops, p, p->events);";
      s.op->newline() << "if (rc == 0) {";
      s.op->indent(1);
      s.op->newline() << "p->engine_attached = 1;";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  s.op->newline() << "default:";
  s.op->indent(1);
  s.op->newline() << "_stp_error(\"unhandled flag value %d at %s:%d\", p->flags, __FUNCTION__, __LINE__);";
  s.op->newline() << "break;";
  s.op->indent(-1);
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";

  // Since this engine could be attached to multiple threads, don't
  // call stap_utrace_detach_ops() here.
  s.op->newline() << "else {";
  s.op->indent(1);
  s.op->newline() << "switch (p->flags) {";
  s.op->indent(1);
  // For death probes, go ahead and call the probe directly.
  if (flags_seen[UDPF_END])
    {
      s.op->newline() << "case UDPF_END:";
      s.op->indent(1);
      s.op->newline() << "if (process_p) {";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_probe_handler(tsk, p);";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }
  if (flags_seen[UDPF_THREAD_END])
    {
      s.op->newline() << "case UDPF_THREAD_END:";
      s.op->indent(1);
      s.op->newline() << "if (! process_p) {";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_probe_handler(tsk, p);";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  // For begin/thread_begin probes, at deregistration time we'll try
  // to detach.  This will only be necessary if the new thread/process
  // got killed before the probe got run in the UTRACE_EVENT(QUIESCE)
  // handler.
  if (flags_seen[UDPF_BEGIN] || flags_seen[UDPF_THREAD_BEGIN]
      || flags_seen[UDPF_SYSCALL] || flags_seen[UDPF_SYSCALL_RETURN])
    {
      s.op->newline() << "case UDPF_BEGIN:";
      s.op->newline() << "case UDPF_THREAD_BEGIN:";
      s.op->newline() << "case UDPF_SYSCALL:";
      s.op->newline() << "case UDPF_SYSCALL_RETURN:";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_detach(tsk, &p->ops);";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  s.op->newline() << "default:";
  s.op->indent(1);
  s.op->newline() << "_stp_error(\"unhandled flag value %d at %s:%d\", p->flags, __FUNCTION__, __LINE__);";
  s.op->newline() << "break;";
  s.op->indent(-1);
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
  s.op->newline() << "return rc;";
  s.op->newline(-1) << "}";

  s.op->newline() << "struct stap_utrace_probe stap_utrace_probes[] = {";
  s.op->indent(1);

  // Set up 'process(PATH)' probes
  if (! probes_by_path.empty())
    {
      for (p_b_path_iterator it = probes_by_path.begin();
	   it != probes_by_path.end(); it++)
        {
	  // Emit a "fake" probe decl that is really a hook for to get
	  // our vm_callback called.
	  string path = it->first;
	  s.op->newline() << "#ifdef DEBUG_TASK_FINDER_VMA";
	  emit_vm_callback_probe_decl (s, true, path, (int64_t)0,
				       "__stp_tf_vm_cb");
	  s.op->newline() << "#endif";

	  for (unsigned i = 0; i < it->second.size(); i++)
	    {
	      utrace_derived_probe *p = it->second[i];
	      emit_probe_decl(s, p);
	    }
	}
    }

  // Set up 'process(PID)' probes
  if (! probes_by_pid.empty())
    {
      for (p_b_pid_iterator it = probes_by_pid.begin();
	   it != probes_by_pid.end(); it++)
        {
	  // Emit a "fake" probe decl that is really a hook for to get
	  // our vm_callback called.
	  s.op->newline() << "#ifdef DEBUG_TASK_FINDER_VMA";
	  emit_vm_callback_probe_decl (s, false, "", it->first,
				       "__stp_tf_vm_cb");
	  s.op->newline() << "#endif";

	  for (unsigned i = 0; i < it->second.size(); i++)
	    {
	      utrace_derived_probe *p = it->second[i];
	      emit_probe_decl(s, p);
	    }
	}
    }
  s.op->newline(-1) << "};";
}


void
utrace_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline();
  s.op->newline() << "/* ---- utrace probes ---- */";

  s.op->newline() << "for (i=0; i<" << num_probes << "; i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_utrace_probe *p = &stap_utrace_probes[i];";
  s.op->newline() << "rc = stap_register_task_finder_target(&p->tgt);";
  s.op->newline(-1) << "}";

  // rollback all utrace probes
  s.op->newline() << "if (rc) {";
  s.op->indent(1);
  s.op->newline() << "for (j=i-1; j>=0; j--) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_utrace_probe *p = &stap_utrace_probes[j];";

  s.op->newline() << "if (p->engine_attached) {";
  s.op->indent(1);
  s.op->newline() << "stap_utrace_detach_ops(&p->ops);";
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";

  s.op->newline(-1) << "}";
}


void
utrace_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty()) return;

  s.op->newline();
  s.op->newline() << "/* ---- utrace probes ---- */";
  s.op->newline() << "for (i=0; i<" << num_probes << "; i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_utrace_probe *p = &stap_utrace_probes[i];";

  s.op->newline() << "if (p->engine_attached) {";
  s.op->indent(1);
  s.op->newline() << "stap_utrace_detach_ops(&p->ops);";
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
}


// ------------------------------------------------------------------------
// user-space probes
// ------------------------------------------------------------------------


struct uprobe_derived_probe_group: public generic_dpg<uprobe_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


uprobe_derived_probe::uprobe_derived_probe (const string& function,
                                            const string& filename,
                                            int line,
                                            const string& module,
                                            int pid,
                                            const string& section,
                                            Dwarf_Addr dwfl_addr,
                                            Dwarf_Addr addr,
                                            dwarf_query & q,
                                            Dwarf_Die* scope_die /* may be null */):
  derived_probe (q.base_probe, new probe_point (*q.base_loc) /* .components soon rewritten */ ),
  return_p (q.has_return), module (module), pid (pid), section (section), address (addr)
{
  // We may receive probes on two types of ELF objects: ET_EXEC or ET_DYN.
  // ET_EXEC ones need no further relocation on the addr(==dwfl_addr), whereas
  // ET_DYN ones do (addr += run-time mmap base address).  We tell these apart
  // by the incoming section value (".absolute" vs. ".dynamic").

  this->tok = q.base_probe->tok;

  // Make a target-variable-expanded copy of the probe body
  if (!null_die(scope_die))
    {
      dwarf_var_expanding_copy_visitor v (q, scope_die, dwfl_addr); // XXX: user-space deref's!
      require <statement*> (&v, &(this->body), this->body);

      // If during target-variable-expanding the probe, we added a new block
      // of code, add it to the start of the probe.
      if (v.add_block)
        this->body = new block(v.add_block, this->body);

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

  // Reset the sole element of the "locations" vector as a
  // "reverse-engineered" form of the incoming (q.base_loc) probe
  // point.  This allows a user to see what function / file / line
  // number any particular match of the wildcards.

  vector<probe_point::component*> comps;
  if(q.has_process)
    comps.push_back (new probe_point::component(TOK_PROCESS, new literal_string(module)));
  else
    assert (0);
     
  string fn_or_stmt;
  if (q.has_function_str || q.has_function_num)
    fn_or_stmt = "function";
  else
    fn_or_stmt = "statement";

  if (q.has_function_str || q.has_statement_str)
      {
        string retro_name = function;
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
  if (return_p)
    comps.push_back (new probe_point::component(TOK_RETURN));
  /*
  if (has_maxactive)
    comps.push_back (new probe_point::component
                     (TOK_MAXACTIVE, new literal_number(maxactive_val)));
  */

  // Overwrite it.
  this->sole_location()->components = comps;
}


uprobe_derived_probe::uprobe_derived_probe (probe *base,
                                            probe_point *location,
                                            int pid,
                                            Dwarf_Addr addr,
                                            bool has_return):
  derived_probe (base, location), // location is not rewritten here
  return_p (has_return), pid (pid), address (addr)
{
}


void
uprobe_derived_probe::printsig (ostream& o) const
{
  // Same as dwarf_derived_probe.
  sole_location()->print (o);
  o << " /* pc=" << section << "+0x" << hex << address << dec << " */";
  printsig_nested (o);
}


void
uprobe_derived_probe::join_group (systemtap_session& s)
{
  if (! s.uprobe_derived_probes)
    s.uprobe_derived_probes = new uprobe_derived_probe_group ();
  s.uprobe_derived_probes->enroll (this);
  task_finder_derived_probe_group::create_session_group (s);
}


struct uprobe_builder: public derived_probe_builder
{
  uprobe_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    int64_t process, address;

    bool b1 = get_param (parameters, TOK_PROCESS, process);
    (void) b1;
    bool b2 = get_param (parameters, TOK_STATEMENT, address);
    (void) b2;
    bool rr = has_null_param (parameters, TOK_RETURN);
    assert (b1 && b2); // by pattern_root construction

    finished_results.push_back(new uprobe_derived_probe(base, location, process, address, rr));
  }
};


void
uprobe_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- user probes ---- */";

  s.need_uprobes = true; // Ask buildrun.cxx to build extra module if needed

  // If uprobes isn't in the kernel, pull it in from the runtime.
  s.op->newline() << "#if defined(CONFIG_UPROBES) || defined(CONFIG_UPROBES_MODULE)";
  s.op->newline() << "#include <linux/uprobes.h>";
  s.op->newline() << "#else";
  s.op->newline() << "#include \"uprobes/uprobes.h\"";
  s.op->newline() << "#endif";
  s.op->newline() << "#include \"task_finder.c\"";

  s.op->newline() << "#ifndef MAXUPROBES";
  s.op->newline() << "#define MAXUPROBES 16"; // maximum possible armed uprobes per process() probe point
  s.op->newline() << "#endif";
  s.op->newline() << "#define NUMUPROBES (MAXUPROBES*" << probes.size() << ")";

  // In .bss, the shared pool of uprobe/uretprobe structs.  These are
  // too big to embed in the initialized .data stap_uprobe_spec array.
  s.op->newline() << "struct stap_uprobe {";
  s.op->newline(1) << "union { struct uprobe up; struct uretprobe urp; };";
  s.op->newline() << "int spec_index;"; // index into stap_uprobe_specs; <0 == free && unregistered
  s.op->newline(-1) << "} stap_uprobes [NUMUPROBES];";
  s.op->newline() << "DEFINE_MUTEX(stap_uprobes_lock);"; // protects against concurrent registration/unregistration

  s.op->newline() << "struct stap_uprobe_spec {";
  s.op->newline(1) << "struct stap_task_finder_target finder;";
  s.op->newline() << "unsigned long address;";
  s.op->newline() << "const char *pathname;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "unsigned return_p:1;";
  s.op->newline(-1) << "} stap_uprobe_specs [] = {";
  s.op->indent(1);
  for (unsigned i =0; i<probes.size(); i++)
    {
      uprobe_derived_probe* p = probes[i];
      s.op->newline() << "{";
      s.op->line() << " .finder = {";
      if (p->pid != 0)
        s.op->line() << " .pid=" << p->pid;
      else if (p->section == ".absolute")
        s.op->line() << " .pathname=" << lex_cast_qstring(p->module) << ", ";
      // else ".dynamic" gets pathname=0, pid=0, activating task_finder "global tracing" 
      s.op->line() << "},";
      if (p->section != ".absolute")
        s.op->line() << " .pathname=" << lex_cast_qstring(p->module) << ", ";
      s.op->line() << " .address=0x" << hex << p->address << dec << "UL,";
      s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
      s.op->line() << " .ph=&" << p->name << ",";
      if (p->return_p) s.op->line() << " .return_p=1,";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";

  s.op->newline() << "static void enter_uprobe_probe (struct uprobe *inst, struct pt_regs *regs) {";
  s.op->newline(1) << "struct stap_uprobe *sup = container_of(inst, struct stap_uprobe, up);";
  s.op->newline() << "struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "if (sup->spec_index < 0 ||"
                  << "sup->spec_index >= " << probes.size() << ") return;"; // XXX: should not happen
  s.op->newline() << "c->probe_point = sups->pp;";
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "(*sups->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";

  s.op->newline() << "static void enter_uretprobe_probe (struct uretprobe_instance *inst, struct pt_regs *regs) {";
  s.op->newline(1) << "struct stap_uprobe *sup = container_of(inst->rp, struct stap_uprobe, urp);";
  s.op->newline() << "struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "if (sup->spec_index < 0 ||"
                  << "sup->spec_index >= " << probes.size() << ") return;"; // XXX: should not happen
  s.op->newline() << "c->probe_point = sups->pp;";
  // XXX: kretprobes saves "c->pi = inst;" too
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "(*sups->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";



  // NB: Because these utrace callbacks only occur before / after
  // userspace instructions run, there is no concurrency control issue
  // between active uprobe callbacks and these registration /
  // unregistration pieces.  

  // We protect the stap_uprobe->spec_index (which also serves as a
  // free/busy flag) value with the outer protective probes_lock
  // mutex, to protect it against concurrent registration /
  // unregistration.  XXX: This should become less naive and let the
  // mutex only protect stap_uprobe slot allocation and let the uprobe
  // calls occur outside the critical section.

  s.op->newline();
  s.op->newline() << "static int stap_uprobe_change (struct task_struct *tsk, int register_p, unsigned long relocation, struct stap_uprobe_spec *sups) {";
  s.op->newline(1) << "int spec_index = (sups - stap_uprobe_specs);";
  s.op->newline() << "int handled_p = 0;";
  s.op->newline() << "int rc = 0;";
  s.op->newline() << "int i;";
  s.op->newline() << "mutex_lock (& stap_uprobes_lock);";

  s.op->newline() << "for (i=0; i<NUMUPROBES; i++) {"; // XXX: slow linear search
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[i];";

  // register new uprobe
  s.op->newline() << "if (register_p && sup->spec_index < 0) {";
  s.op->newline(1) << "sup->spec_index = spec_index;";
  s.op->newline() << "if (sups->return_p) {";
  s.op->newline(1) << "sup->urp.u.pid = tsk->tgid;";
  s.op->newline() << "sup->urp.u.vaddr = relocation + sups->address;";
  s.op->newline() << "sup->urp.handler = &enter_uretprobe_probe;";
  s.op->newline() << "rc = register_uretprobe (& sup->urp);";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "sup->up.pid = tsk->tgid;";
  s.op->newline() << "sup->up.vaddr = relocation + sups->address;";
  s.op->newline() << "sup->up.handler = &enter_uprobe_probe;";
  s.op->newline() << "rc = register_uprobe (& sup->up);";
  s.op->newline(-1) << "}";

  s.op->newline() << "if (rc) {"; // failed to register
  s.op->newline() << "printk (KERN_WARNING \"uprobe failed pid %d addr %p rc %d\\n\", tsk->tgid, (void*)(relocation + sups->address), rc);";
  s.op->newline(1) << "sup->spec_index = -1;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "handled_p = 1;"; // success
  s.op->newline(-1) << "}";
  s.op->newline() << "break;"; // exit free slot search whether or not handled_p

  // unregister old uprobe
  s.op->newline(-1) << "} else if (!register_p && "
                    << "((sups->return_p && sup->urp.u.pid == tsk->tgid) ||" // dying uretprobe
                    << "(!sups->return_p && sup->up.pid == tsk->tgid))) {"; // dying uprobe
  // XXX: or just check sup->spec_index == spec_index?
  s.op->newline(1) << "sup->spec_index = -1;";
  s.op->newline() << "if (sups->return_p) {";
  s.op->newline(1) << "unregister_uretprobe (& sup->urp);";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "unregister_uprobe (& sup->up);";
  s.op->newline(-1) << "}";
  s.op->newline() << "handled_p = 1;";
  // XXX: Do we need to keep searching the array for other processes, or
  // can we break just like for the register-new case?

  s.op->newline(-1) << "}"; // if/else

  s.op->newline(-1) << "}"; // stap_uprobes[] loop
  s.op->newline() << "mutex_unlock (& stap_uprobes_lock);";
  s.op->newline() << "if (! handled_p) {";
  s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
  s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  s.op->newline() << "_stp_exit ();";
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
  
  s.op->newline() << "return 0;"; // XXX: or rc?
  s.op->newline(-1) << "}";
  s.op->assert_0_indent();


  // The task_finder_callback we use for ET_EXEC targets.
  s.op->newline();
  s.op->newline() << "static int stap_uprobe_process_found (struct stap_task_finder_target *tgt, struct task_struct *tsk, int register_p, int process_p) {";

  s.op->newline(1) << "struct stap_uprobe_spec *sups = container_of(tgt, struct stap_uprobe_spec, finder);";
  s.op->newline() << "if (! process_p) return 0;";
  s.op->newline(0) << "return stap_uprobe_change (tsk, register_p, 0, sups);";
  s.op->newline(-1) << "}";

  // The task_finder_vm_callback we use for ET_DYN targets.
  s.op->newline();
  s.op->newline() << "static int stap_uprobe_vmchange_found (struct stap_task_finder_target *tgt, struct task_struct *tsk, int map_p, char *vm_path, unsigned long vm_start, unsigned long vm_end, unsigned long vm_pgoff) {";
  s.op->newline(1) << "struct stap_uprobe_spec *sups = container_of(tgt, struct stap_uprobe_spec, finder);";
  // 1 - shared libraries' executable segments load from offset 0 - ld.so convention
  s.op->newline() << "if (vm_pgoff != 0) return 0;"; 
  // 2 - the shared library we're interested in
  s.op->newline() << "if (strcmp (vm_path, sups->pathname)) return 0;"; 
  // 3 - probe address within the mapping limits; test should not fail
  s.op->newline() << "if (vm_end >= sups->address) return 0;"; 
  s.op->newline(0) << "return stap_uprobe_change (tsk, map_p, vm_start, sups);";
  s.op->newline(-1) << "}";
  s.op->assert_0_indent();


  s.op->newline();
}


void
uprobe_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- user probes ---- */";

  s.op->newline() << "for (j=0; j<NUMUPROBES; j++) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[j];";
  s.op->newline() << "sup->spec_index = -1;"; // free slot
  s.op->newline(-1) << "}";
  s.op->newline() << "mutex_init (& stap_uprobes_lock);";

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_uprobe_spec *sups = & stap_uprobe_specs[i];";
  s.op->newline() << "probe_point = sups->pp;"; // for error messages
  s.op->newline() << "if (sups->finder.pathname) sups->finder.callback = & stap_uprobe_process_found;";
  s.op->newline() << "else if (sups->pathname) sups->finder.vm_callback = & stap_uprobe_vmchange_found;";
  s.op->newline() << "rc = stap_register_task_finder_target (& sups->finder);";

  // NB: if (rc), there is no need (XXX: nor any way) to clean up any
  // finders already registered, since mere registration does not
  // cause any utrace or memory allocation actions.  That happens only
  // later, once the task finder engine starts running.  So, for a
  // partial initialization requiring unwind, we need do nothing.
  s.op->newline() << "if (rc) break;";

  s.op->newline(-1) << "}";
}


void
uprobe_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- user probes ---- */";

  // NB: there is no stap_unregister_task_finder_target call;
  // important stuff like utrace cleanups are done by
  // __stp_task_finder_cleanup()

  s.op->newline() << "for (j=0; j<NUMUPROBES; j++) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[j];";
  s.op->newline() << "struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  s.op->newline() << "if (sup->spec_index < 0) continue;"; // free slot
  s.op->newline() << "if (sups->return_p) unregister_uretprobe (&sup->urp);";
  s.op->newline() << "else unregister_uprobe (&sup->up);";
  s.op->newline() << "sup->spec_index = -1;";
  // s.op->newline() << "atomic_add (sdp->u.krp.nmissed, & skipped_count);";
  // s.op->newline() << "atomic_add (sdp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline(-1) << "}";

  s.op->newline() << "mutex_destroy (& stap_uprobes_lock);";
}



// ------------------------------------------------------------------------
// timer derived probes
// ------------------------------------------------------------------------


static string TOK_TIMER("timer");

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
		     literal_map_t const &,
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
// procfs file derived probes
// ------------------------------------------------------------------------


static string TOK_PROCFS("procfs");
static string TOK_READ("read");
static string TOK_WRITE("write");

struct procfs_derived_probe: public derived_probe
{
  string path;
  bool write;
  bool target_symbol_seen;

  procfs_derived_probe (systemtap_session &, probe* p, probe_point* l, string ps, bool w);
  void join_group (systemtap_session& s);
};


struct procfs_probe_set
{
  procfs_derived_probe* read_probe;
  procfs_derived_probe* write_probe;

  procfs_probe_set () : read_probe (NULL), write_probe (NULL) {}
};


struct procfs_derived_probe_group: public generic_dpg<procfs_derived_probe>
{
private:
  map<string, procfs_probe_set*> probes_by_path;
  typedef map<string, procfs_probe_set*>::iterator p_b_p_iterator;
  bool has_read_probes;
  bool has_write_probes;

public:
  procfs_derived_probe_group () :
    has_read_probes(false), has_write_probes(false) {}

  void enroll (procfs_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct procfs_var_expanding_copy_visitor: public var_expanding_copy_visitor
{
  procfs_var_expanding_copy_visitor(systemtap_session& s, const string& pn,
				    string path, bool write_probe):
    sess (s), probe_name (pn), path (path), write_probe (write_probe),
    target_symbol_seen (false) {}

  systemtap_session& sess;
  string probe_name;
  string path;
  bool write_probe;
  bool target_symbol_seen;

  void visit_target_symbol (target_symbol* e);
};


procfs_derived_probe::procfs_derived_probe (systemtap_session &s, probe* p,
					    probe_point* l, string ps, bool w):
  derived_probe(p, l), path(ps), write(w), target_symbol_seen(false)
{
  // Make a local-variable-expanded copy of the probe body
  procfs_var_expanding_copy_visitor v (s, name, path, write);
  require <statement*> (&v, &(this->body), base->body);
  target_symbol_seen = v.target_symbol_seen;
}


void
procfs_derived_probe::join_group (systemtap_session& s)
{
  if (! s.procfs_derived_probes)
    s.procfs_derived_probes = new procfs_derived_probe_group ();
  s.procfs_derived_probes->enroll (this);
}


void
procfs_derived_probe_group::enroll (procfs_derived_probe* p)
{
  procfs_probe_set *pset;

  if (probes_by_path.count(p->path) == 0)
    {
      pset = new procfs_probe_set;
      probes_by_path[p->path] = pset;
    }
  else
    {
      pset = probes_by_path[p->path];

      // You can only specify 1 read and 1 write probe.
      if (p->write && pset->write_probe != NULL)
	throw semantic_error("only one write procfs probe can exist for procfs path \"" + p->path + "\"");
      else if (! p->write && pset->read_probe != NULL)
	throw semantic_error("only one read procfs probe can exist for procfs path \"" + p->path + "\"");

      // XXX: multiple writes should be acceptable
    }

  if (p->write)
  {
    pset->write_probe = p;
    has_write_probes = true;
  }
  else
  {
    pset->read_probe = p;
    has_read_probes = true;
  }
}


void
procfs_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "/* ---- procfs probes ---- */";

  // Emit the procfs probe data list
  s.op->newline() << "struct stap_procfs_probe {";
  s.op->newline(1)<< "const char *path;";
  s.op->newline() << "const char *read_pp;";
  s.op->newline() << "void (*read_ph) (struct context*);";
  s.op->newline() << "const char *write_pp;";
  s.op->newline() << "void (*write_ph) (struct context*);";
  s.op->newline(-1) << "} stap_procfs_probes[] = {";
  s.op->indent(1);

  for (p_b_p_iterator it = probes_by_path.begin(); it != probes_by_path.end();
       it++)
  {
      procfs_probe_set *pset = it->second;

      s.op->newline() << "{";
      s.op->line() << " .path=" << lex_cast_qstring (it->first) << ",";

      if (pset->read_probe != NULL)
        {
	  s.op->line() << " .read_pp="
		       << lex_cast_qstring (*pset->read_probe->sole_location())
		       << ",";
	  s.op->line() << " .read_ph=&" << pset->read_probe->name << ",";
	}
      else
        {
	  s.op->line() << " .read_pp=NULL,";
	  s.op->line() << " .read_ph=NULL,";
	}

      if (pset->write_probe != NULL)
        {
	  s.op->line() << " .write_pp="
		       << lex_cast_qstring (*pset->write_probe->sole_location())
		       << ",";
	  s.op->line() << " .write_ph=&" << pset->write_probe->name;
	}
      else
        {
	  s.op->line() << " .write_pp=NULL,";
	  s.op->line() << " .write_ph=NULL";
	}
      s.op->line() << " },";
  }
  s.op->newline(-1) << "};";

  if (has_read_probes)
    {
      // Output routine to fill in 'page' with our data.
      s.op->newline();

      s.op->newline() << "static int _stp_procfs_read(char *page, char **start, off_t off, int count, int *eof, void *data) {";

      s.op->newline(1) << "struct stap_procfs_probe *spp = (struct stap_procfs_probe *)data;";
      s.op->newline() << "int bytes = 0;";
      s.op->newline() << "string_t strdata = {'\\0'};";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
      s.op->newline() << "c->probe_point = spp->read_pp;";

      s.op->newline() << "if (c->data == NULL)";
      s.op->newline(1) << "c->data = &strdata;";
      s.op->newline(-1) << "else {";

      s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
      s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      s.op->newline() << "_stp_exit ();";
      s.op->newline(-1) << "}";
      s.op->newline() << "atomic_dec (& c->busy);";
      s.op->newline() << "goto probe_epilogue;";
      s.op->newline(-1) << "}";

      // call probe function (which copies data into strdata)
      s.op->newline() << "(*spp->read_ph) (c);";

      // copy string data into 'page'
      s.op->newline() << "c->data = NULL;";
      s.op->newline() << "bytes = strnlen(strdata, MAXSTRINGLEN - 1);";
      s.op->newline() << "if (off >= bytes)";
      s.op->newline(1) << "*eof = 1;";
      s.op->newline(-1) << "else {";
      s.op->newline(1) << "bytes -= off;";
      s.op->newline() << "if (bytes > count)";
      s.op->newline(1) << "bytes = count;";
      s.op->newline(-1) << "memcpy(page, strdata + off, bytes);";
      s.op->newline() << "*start = page;";
      s.op->newline(-1) << "}";

      common_probe_entryfn_epilogue (s.op);
      s.op->newline() << "return bytes;";

      s.op->newline(-1) << "}";
    }
  if (has_write_probes)
    {
      s.op->newline() << "static int _stp_procfs_write(struct file *file, const char *buffer, unsigned long count, void *data) {";

      s.op->newline(1) << "struct stap_procfs_probe *spp = (struct stap_procfs_probe *)data;";
      s.op->newline() << "string_t strdata = {'\\0'};";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
      s.op->newline() << "c->probe_point = spp->write_pp;";

      s.op->newline() << "if (count > (MAXSTRINGLEN - 1))";
      s.op->newline(1) << "count = MAXSTRINGLEN - 1;";
      s.op->newline(-1) << "_stp_copy_from_user(strdata, buffer, count);";

      s.op->newline() << "if (c->data == NULL)";
      s.op->newline(1) << "c->data = &strdata;";
      s.op->newline(-1) << "else {";

      s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
      s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      s.op->newline() << "_stp_exit ();";
      s.op->newline(-1) << "}";
      s.op->newline() << "atomic_dec (& c->busy);";
      s.op->newline() << "goto probe_epilogue;";
      s.op->newline(-1) << "}";

      // call probe function (which copies data out of strdata)
      s.op->newline() << "(*spp->write_ph) (c);";

      s.op->newline() << "c->data = NULL;";
      common_probe_entryfn_epilogue (s.op);

      s.op->newline() << "return count;";
      s.op->newline(-1) << "}";
    }
}


void
procfs_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "for (i = 0; i < " << probes_by_path.size() << "; i++) {";
  s.op->newline(1) << "struct stap_procfs_probe *spp = &stap_procfs_probes[i];";

  s.op->newline() << "if (spp->read_pp)";
  s.op->newline(1) << "probe_point = spp->read_pp;";
  s.op->newline(-1) << "else";
  s.op->newline(1) << "probe_point = spp->write_pp;";

  s.op->newline(-1) << "rc = _stp_create_procfs(spp->path, i);";

  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "_stp_close_procfs();";
  s.op->newline() << "break;";
  s.op->newline(-1) << "}";

  if (has_read_probes)
    {
      s.op->newline() << "if (spp->read_pp)";
      s.op->newline(1) << "_stp_procfs_files[i]->read_proc = &_stp_procfs_read;";
      s.op->newline(-1) << "else";
      s.op->newline(1) << "_stp_procfs_files[i]->read_proc = NULL;";
      s.op->indent(-1);
    }
  else
    s.op->newline() << "_stp_procfs_files[i]->read_proc = NULL;";

  if (has_write_probes)
    {
      s.op->newline() << "if (spp->write_pp)";
      s.op->newline(1) << "_stp_procfs_files[i]->write_proc = &_stp_procfs_write;";
      s.op->newline(-1) << "else";
      s.op->newline(1) << "_stp_procfs_files[i]->write_proc = NULL;";
      s.op->indent(-1);
    }
  else
    s.op->newline() << "_stp_procfs_files[i]->write_proc = NULL;";

  s.op->newline() << "_stp_procfs_files[i]->data = spp;";
  s.op->newline(-1) << "}"; // for loop
}


void
procfs_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "_stp_close_procfs();";
}


void
procfs_var_expanding_copy_visitor::visit_target_symbol (target_symbol* e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (e->base_name != "$value")
    throw semantic_error ("invalid target symbol for procfs probe, $value expected",
			  e->tok);

  if (e->components.size() > 0)
    {
      switch (e->components[0].first)
	{
	case target_symbol::comp_literal_array_index:
	  throw semantic_error("procfs target variable '$value' may not be used as array",
			       e->tok);
	  break;
	case target_symbol::comp_struct_member:
	  throw semantic_error("procfs target variable '$value' may not be used as a structure",
			       e->tok);
	  break;
	default:
	  throw semantic_error ("invalid use of procfs target variable '$value'",
				e->tok);
	  break;
	}
    }

  bool lvalue = is_active_lvalue(e);
  if (write_probe && lvalue)
    throw semantic_error("procfs $value variable is read-only in a procfs write probe", e->tok);
  else if (! write_probe && ! lvalue)
    throw semantic_error("procfs $value variable cannot be read in a procfs read probe", e->tok);

  // Remember that we've seen a target variable.
  target_symbol_seen = true;

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;

  string fname = (string(lvalue ? "_procfs_value_set" : "_procfs_value_get")
		  + "_" + lex_cast<string>(tick++));
  string locvalue = "CONTEXT->data";

  if (! lvalue)
    ec->code = string("strlcpy (THIS->__retvalue, ") + locvalue
      + string(", MAXSTRINGLEN); /* pure */");
  else
    ec->code = string("strlcpy (") + locvalue
      + string(", THIS->value, MAXSTRINGLEN);");

  fdecl->name = fname;
  fdecl->body = ec;
  fdecl->type = pe_string;

  if (lvalue)
    {
      // Modify the fdecl so it carries a single pe_string formal
      // argument called "value".

      vardecl *v = new vardecl;
      v->type = pe_string;
      v->name = "value";
      v->tok = e->tok;
      fdecl->formal_args.push_back(v);
    }
  sess.functions.push_back(fdecl);

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session

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


struct procfs_builder: public derived_probe_builder
{
  procfs_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results);
};


void
procfs_builder::build(systemtap_session & sess,
		      probe * base,
		      probe_point * location,
		      literal_map_t const & parameters,
		      vector<derived_probe *> & finished_results)
{
  string path;
  bool has_procfs = get_param(parameters, TOK_PROCFS, path);
  bool has_read = (parameters.find(TOK_READ) != parameters.end());
  bool has_write = (parameters.find(TOK_WRITE) != parameters.end());

  // If no procfs path, default to "command".  The runtime will do
  // this for us, but if we don't do it here, we'll think the
  // following 2 probes are attached to different paths:
  //
  //   probe procfs("command").read {}"
  //   probe procfs.write {}

  if (! has_procfs)
    path = "command";
  // If we have a path, we need to validate it.
  else
    {
      string::size_type start_pos, end_pos;
      string component;
      start_pos = 0;
      while ((end_pos = path.find('/', start_pos)) != string::npos)
        {
	  // Make sure it doesn't start with '/'.
	  if (end_pos == 0)
	    throw semantic_error ("procfs path cannot start with a '/'",
				  location->tok);

	  component = path.substr(start_pos, end_pos - start_pos);
	  // Make sure it isn't empty.
	  if (component.size() == 0)
	    throw semantic_error ("procfs path component cannot be empty",
				  location->tok);
	  // Make sure it isn't relative.
	  else if (component == "." || component == "..")
	    throw semantic_error ("procfs path cannot be relative (and contain '.' or '..')", location->tok);

	  start_pos = end_pos + 1;
	}
      component = path.substr(start_pos);
      // Make sure it doesn't end with '/'.
      if (component.size() == 0)
	throw semantic_error ("procfs path cannot end with a '/'", location->tok);
      // Make sure it isn't relative.
      else if (component == "." || component == "..")
	throw semantic_error ("procfs path cannot be relative (and contain '.' or '..')", location->tok);
    }

  if (!(has_read ^ has_write))
    throw semantic_error ("need read/write component", location->tok);

  finished_results.push_back(new procfs_derived_probe(sess, base, location,
						      path, has_write));
}



// ------------------------------------------------------------------------
// statically inserted macro-based derived probes
// ------------------------------------------------------------------------

static string TOK_MARK("mark");
static string TOK_FORMAT("format");

struct mark_arg
{
  bool str;
  string c_type;
  exp_type stp_type;
};

struct mark_derived_probe: public derived_probe
{
  mark_derived_probe (systemtap_session &s,
                      const string& probe_name, const string& probe_format,
                      probe* base_probe, probe_point* location);

  systemtap_session& sess;
  string probe_name, probe_format;
  vector <struct mark_arg *> mark_args;
  bool target_symbol_seen;

  void join_group (systemtap_session& s);
  void emit_probe_context_vars (translator_output* o);
  void initialize_probe_context_vars (translator_output* o);

  void parse_probe_format ();
};


struct mark_derived_probe_group: public generic_dpg<mark_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct mark_var_expanding_copy_visitor: public var_expanding_copy_visitor
{
  mark_var_expanding_copy_visitor(systemtap_session& s,
                                  const string& pn,
				  vector <struct mark_arg *> &mark_args):
    sess (s), probe_name (pn), mark_args (mark_args),
    target_symbol_seen (false) {}
  systemtap_session& sess;
  string probe_name;
  vector <struct mark_arg *> &mark_args;
  bool target_symbol_seen;

  void visit_target_symbol (target_symbol* e);
  void visit_target_symbol_arg (target_symbol* e);
  void visit_target_symbol_format (target_symbol* e);
};


void
hex_dump(unsigned char *data, size_t len)
{
  // Dump data
  size_t idx = 0;
  while (idx < len)
    {
      string char_rep;

      clog << "  0x" << setfill('0') << setw(8) << hex << internal << idx;

      for (int i = 0; i < 4; i++)
        {
	  clog << " ";
	  size_t limit = idx + 4;
	  while (idx < len && idx < limit)
	    {
	      clog << setfill('0') << setw(2)
		   << ((unsigned) *((unsigned char *)data + idx));
	      if (isprint(*((char *)data + idx)))
		char_rep += *((char *)data + idx);
	      else
		char_rep += '.';
	      idx++;
	    }
	  while (idx < limit)
	    {
	      clog << "  ";
	      idx++;
	    }
	}
      clog << " " << char_rep << dec << setfill(' ') << endl;
    }
}


void
mark_var_expanding_copy_visitor::visit_target_symbol_arg (target_symbol* e)
{
  string argnum_s = e->base_name.substr(4,e->base_name.length()-4);
  int argnum = atoi (argnum_s.c_str());

  if (argnum < 1 || argnum > (int)mark_args.size())
    throw semantic_error ("invalid marker argument number", e->tok);

  if (is_active_lvalue (e))
    throw semantic_error("write to marker parameter not permitted", e->tok);

  if (e->components.size() > 0)
    {
      switch (e->components[0].first)
	{
	case target_symbol::comp_literal_array_index:
	  throw semantic_error("marker argument may not be used as array",
			       e->tok);
	  break;
	case target_symbol::comp_struct_member:
	  throw semantic_error("marker argument may not be used as a structure",
			       e->tok);
	  break;
	default:
	  throw semantic_error ("invalid marker argument use", e->tok);
	  break;
	}
    }

  // Remember that we've seen a target variable.
  target_symbol_seen = true;

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;

  string fname = string("_mark_tvar_get")
    + "_" + e->base_name.substr(1)
    + "_" + lex_cast<string>(tick++);

  if (mark_args[argnum-1]->stp_type == pe_long)
    ec->code = string("THIS->__retvalue = CONTEXT->locals[0].")
      + probe_name + string(".__mark_arg")
      + lex_cast<string>(argnum) + string (";");
  else
    ec->code = string("strlcpy (THIS->__retvalue, CONTEXT->locals[0].")
      + probe_name + string(".__mark_arg")
      + lex_cast<string>(argnum) + string (", MAXSTRINGLEN);");
  ec->code += "/* pure */";
  fdecl->name = fname;
  fdecl->body = ec;
  fdecl->type = mark_args[argnum-1]->stp_type;
  sess.functions.push_back(fdecl);

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session
  provide <functioncall*> (this, n);
}


void
mark_var_expanding_copy_visitor::visit_target_symbol_format (target_symbol* e)
{
  static bool function_synthesized = false;

  if (is_active_lvalue (e))
    throw semantic_error("write to marker format not permitted", e->tok);

  if (e->components.size() > 0)
    {
      switch (e->components[0].first)
	{
	case target_symbol::comp_literal_array_index:
	  throw semantic_error("marker format may not be used as array",
			       e->tok);
	  break;
	case target_symbol::comp_struct_member:
	  throw semantic_error("marker format may not be used as a structure",
			       e->tok);
	  break;
	default:
	  throw semantic_error ("invalid marker format use", e->tok);
	  break;
	}
    }

  string fname = string("_mark_format_get");

  // Synthesize a function (if not already synthesized).
  if (! function_synthesized)
    {
      function_synthesized = true;
      functiondecl *fdecl = new functiondecl;
      fdecl->tok = e->tok;
      embeddedcode *ec = new embeddedcode;
      ec->tok = e->tok;

      ec->code = string("strlcpy (THIS->__retvalue, CONTEXT->data, MAXSTRINGLEN); /* pure */");
      fdecl->name = fname;
      fdecl->body = ec;
      fdecl->type = pe_string;
      sess.functions.push_back(fdecl);
    }

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session
  provide <functioncall*> (this, n);
}

void
mark_var_expanding_copy_visitor::visit_target_symbol (target_symbol* e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (e->base_name.substr(0,4) == "$arg")
    visit_target_symbol_arg (e);
  else if (e->base_name == "$format")
    visit_target_symbol_format (e);
  else
    throw semantic_error ("invalid target symbol for marker, $argN or $format expected",
			  e->tok);
}



mark_derived_probe::mark_derived_probe (systemtap_session &s,
                                        const string& p_n,
                                        const string& p_f,
                                        probe* base, probe_point* loc):
  derived_probe (base, new probe_point(*loc) /* .components soon rewritten */),
  sess (s), probe_name (p_n), probe_format (p_f),
  target_symbol_seen (false)
{
  // create synthetic probe point name; preserve condition
  vector<probe_point::component*> comps;
  comps.push_back (new probe_point::component (TOK_KERNEL));
  comps.push_back (new probe_point::component (TOK_MARK, new literal_string (probe_name)));
  comps.push_back (new probe_point::component (TOK_FORMAT, new literal_string (probe_format)));
  this->sole_location()->components = comps;

  // expand the marker format
  parse_probe_format();

  // Now make a local-variable-expanded copy of the probe body
  mark_var_expanding_copy_visitor v (sess, name, mark_args);
  require <statement*> (&v, &(this->body), base->body);
  target_symbol_seen = v.target_symbol_seen;

  if (sess.verbose > 2)
    clog << "marker-based " << name << " mark=" << probe_name
	 << " fmt='" << probe_format << "'" << endl;
}


static int
skip_atoi(const char **s)
{
  int i = 0;
  while (isdigit(**s))
    i = i * 10 + *((*s)++) - '0';
  return i;
}


void
mark_derived_probe::parse_probe_format()
{
  const char *fmt = probe_format.c_str();
  int qualifier;		// 'h', 'l', or 'L' for integer fields
  mark_arg *arg;

  for (; *fmt ; ++fmt)
    {
      if (*fmt != '%')
        {
	  /* Skip text */
	  continue;
	}

repeat:
      ++fmt;

      // skip conversion flags (if present)
      switch (*fmt)
        {
	case '-':
	case '+':
	case ' ':
	case '#':
	case '0':
	  goto repeat;
	}

      // skip minimum field witdh (if present)
      if (isdigit(*fmt))
	skip_atoi(&fmt);

      // skip precision (if present)
      if (*fmt == '.')
        {
	  ++fmt;
	  if (isdigit(*fmt))
	    skip_atoi(&fmt);
	}

      // get the conversion qualifier (if present)
      qualifier = -1;
      if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L')
        {
	  qualifier = *fmt;
	  ++fmt;
	  if (qualifier == 'l' && *fmt == 'l')
	    {
	      qualifier = 'L';
	      ++fmt;
	    }
	}

      // get the conversion type
      switch (*fmt)
        {
	case 'c':
	  arg = new mark_arg;
	  arg->str = false;
	  arg->c_type = "int";
	  arg->stp_type = pe_long;
	  mark_args.push_back(arg);
	  continue;

	case 's':
	  arg = new mark_arg;
	  arg->str = true;
	  arg->c_type = "char *";
	  arg->stp_type = pe_string;
	  mark_args.push_back(arg);
	  continue;

	case 'p':
	  arg = new mark_arg;
	  arg->str = false;
	  // This should really be 'void *'.  But, then we'll get a
	  // compile error when we assign the void pointer to an
	  // integer without a cast.  So, we use 'long' instead, since
	  // it should have the same size as 'void *'.
	  arg->c_type = "long";
	  arg->stp_type = pe_long;
	  mark_args.push_back(arg);
	  continue;

	case '%':
	  continue;

	case 'o':
	case 'X':
	case 'x':
	case 'd':
	case 'i':
	case 'u':
	  // fall through...
	  break;

	default:
	  if (!*fmt)
	    --fmt;
	  continue;
	}

      arg = new mark_arg;
      arg->str = false;
      arg->stp_type = pe_long;
      switch (qualifier)
        {
	case 'L':
	  arg->c_type = "long long";
	  break;

	case 'l':
	  arg->c_type = "long";
	  break;

	case 'h':
	  arg->c_type = "short";
	  break;

	default:
	  arg->c_type = "int";
	  break;
	}
      mark_args.push_back(arg);
    }
}


void
mark_derived_probe::join_group (systemtap_session& s)
{
  if (! s.mark_derived_probes)
    {
      s.mark_derived_probes = new mark_derived_probe_group ();

      // Make sure <linux/marker.h> is included early.
      embeddedcode *ec = new embeddedcode;
      ec->tok = NULL;
      ec->code = string("#if ! defined(CONFIG_MARKERS)\n")
	+ string("#error \"Need CONFIG_MARKERS!\"\n")
	+ string("#endif\n")
	+ string("#include <linux/marker.h>\n");

      s.embeds.push_back(ec);
    }
  s.mark_derived_probes->enroll (this);
}


void
mark_derived_probe::emit_probe_context_vars (translator_output* o)
{
  // If we haven't seen a target symbol for this probe, quit.
  if (! target_symbol_seen)
    return;

  for (unsigned i = 0; i < mark_args.size(); i++)
    {
      string localname = "__mark_arg" + lex_cast<string>(i+1);
      switch (mark_args[i]->stp_type)
        {
	case pe_long:
	  o->newline() << "int64_t " << localname << ";";
	  break;
	case pe_string:
	  o->newline() << "string_t " << localname << ";";
	  break;
	default:
	  throw semantic_error ("cannot expand unknown type");
	  break;
	}
    }
}


void
mark_derived_probe::initialize_probe_context_vars (translator_output* o)
{
  // If we haven't seen a target symbol for this probe, quit.
  if (! target_symbol_seen)
    return;

  bool deref_fault_needed = false;
  for (unsigned i = 0; i < mark_args.size(); i++)
    {
      string localname = "l->__mark_arg" + lex_cast<string>(i+1);
      switch (mark_args[i]->stp_type)
        {
	case pe_long:
	  o->newline() << localname << " = va_arg(*c->mark_va_list, "
		       << mark_args[i]->c_type << ");";
	  break;

	case pe_string:
	  // We're assuming that this is a kernel string (this code is
	  // basically the guts of kernel_string), not a user string.
	  o->newline() << "{ " << mark_args[i]->c_type
		       << " tmp_str = va_arg(*c->mark_va_list, "
		       << mark_args[i]->c_type << ");";
	  o->newline() << "deref_string (" << localname
		       << ", tmp_str, MAXSTRINGLEN); }";
	  deref_fault_needed = true;
	  break;

	default:
	  throw semantic_error ("cannot expand unknown type");
	  break;
	}
    }
  if (deref_fault_needed)
    // Need to report errors?
    o->newline() << "deref_fault: ;";
}


void
mark_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty())
    return;

  s.op->newline() << "/* ---- marker probes ---- */";

  s.op->newline() << "struct stap_marker_probe {";
  s.op->newline(1) << "const char * const name;";
  s.op->newline() << "const char * const format;";
  s.op->newline() << "const char * const pp;";
  s.op->newline() << "void (* const ph) (struct context *);";

  s.op->newline(-1) << "} stap_marker_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{";
      s.op->line() << " .name=" << lex_cast_qstring(probes[i]->probe_name)
		   << ",";
      s.op->line() << " .format=" << lex_cast_qstring(probes[i]->probe_format)
		   << ",";
      s.op->line() << " .pp=" << lex_cast_qstring (*probes[i]->sole_location())
		   << ",";
      s.op->line() << " .ph=&" << probes[i]->name;
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();


  // Emit the marker callback function
  s.op->newline();
  s.op->newline() << "static void enter_marker_probe (void *probe_data, void *call_data, const char *fmt, va_list *args) {";
  s.op->newline(1) << "struct stap_marker_probe *smp = (struct stap_marker_probe *)probe_data;";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING");
  s.op->newline() << "c->probe_point = smp->pp;";
  s.op->newline() << "c->data = (char *)smp->format;";

  s.op->newline() << "c->mark_va_list = args;";
  s.op->newline() << "(*smp->ph) (c);";
  s.op->newline() << "c->mark_va_list = NULL;";
  s.op->newline() << "c->data = NULL;";

  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";

  return;
}


void
mark_derived_probe_group::emit_module_init (systemtap_session &s)
{
  if (probes.size () == 0)
    return;

  s.op->newline() << "/* init marker probes */";
  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_marker_probe *smp = &stap_marker_probes[i];";
  s.op->newline() << "probe_point = smp->pp;";
  s.op->newline() << "rc = marker_probe_register(smp->name, smp->format, enter_marker_probe, smp);";
  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "for (j=i-1; j>=0; j--) {"; // partial rollback
  s.op->newline(1) << "struct stap_marker_probe *smp2 = &stap_marker_probes[j];";
  s.op->newline() << "marker_probe_unregister(smp2->name, enter_marker_probe, smp2);";
  s.op->newline(-1) << "}";
  s.op->newline() << "break;"; // don't attempt to register any more probes
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}"; // for loop
}


void
mark_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty())
    return;

  s.op->newline() << "/* deregister marker probes */";
  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_marker_probe *smp = &stap_marker_probes[i];";
  s.op->newline() << "marker_probe_unregister(smp->name, enter_marker_probe, smp);";
  s.op->newline(-1) << "}"; // for loop
}


struct mark_builder: public derived_probe_builder
{
private:
  bool cache_initialized;
  typedef multimap<string, string> mark_cache_t;
  typedef multimap<string, string>::const_iterator mark_cache_const_iterator_t;
  typedef pair<mark_cache_const_iterator_t, mark_cache_const_iterator_t>
    mark_cache_const_iterator_pair_t;
  mark_cache_t mark_cache;

public:
  mark_builder(): cache_initialized(false) {}

  void build_no_more (systemtap_session &s)
  {
    if (! mark_cache.empty())
      {
        if (s.verbose > 3)
          clog << "mark_builder releasing cache" << endl;
	mark_cache.clear();
      }
  }

  void build(systemtap_session & sess,
             probe * base,
             probe_point * location,
             literal_map_t const & parameters,
             vector<derived_probe *> & finished_results);
};


void
mark_builder::build(systemtap_session & sess,
		    probe * base,
		    probe_point *loc,
		    literal_map_t const & parameters,
		    vector<derived_probe *> & finished_results)
{
  string mark_str_val;
  bool has_mark_str = get_param (parameters, TOK_MARK, mark_str_val);
  string mark_format_val;
  bool has_mark_format = get_param (parameters, TOK_FORMAT, mark_format_val);
  assert (has_mark_str);
  (void) has_mark_str;

  if (! cache_initialized)
    {
      cache_initialized = true;
      string module_markers_path = "/lib/modules/" + sess.kernel_release
	  + "/build/Module.markers";

      ifstream module_markers;
      module_markers.open(module_markers_path.c_str(), ifstream::in);
      if (! module_markers)
        {
	  if (sess.verbose>3)
	    clog << module_markers_path << " cannot be opened: "
		 << strerror(errno) << endl;
	  return;
	}

      string name, module, format;
      do
        {
	  module_markers >> name >> module;
	  getline(module_markers, format);

	  // trim leading whitespace
	  string::size_type notwhite = format.find_first_not_of(" \t");
	  format.erase(0, notwhite);

	  // If the format is empty, make sure we add back a space
	  // character, which is what MARK_NOARGS expands to.
	  if (format.length() == 0)
	    format = " ";

	  if (sess.verbose>3)
	    clog << "'" << name << "' '" << module << "' '" << format
		 << "'" << endl;

	  if (mark_cache.count(name) > 0)
	    {
	      // If we have 2 markers with the same we've got 2 cases:
	      // different format strings or duplicate format strings.
	      // If an existing marker in the cache doesn't have the
	      // same format string, add this marker.
	      mark_cache_const_iterator_pair_t ret;
	      mark_cache_const_iterator_t it;
	      bool matching_format_string = false;
		  
	      ret = mark_cache.equal_range(name);
	      for (it = ret.first; it != ret.second; ++it)
	        {
		  if (format == it->second)
		    {
		      matching_format_string = true;
		      break;
		    }
		}

	      if (! matching_format_string)
	        mark_cache.insert(pair<string,string>(name, format));
	  }
	  else
	    mark_cache.insert(pair<string,string>(name, format));
	}
      while (! module_markers.eof());
      module_markers.close();
    }

  // Search marker list for matching markers
  for (mark_cache_const_iterator_t it = mark_cache.begin();
       it != mark_cache.end(); it++)
    {
      // Below, "rc" has negative polarity: zero iff matching.
      int rc = fnmatch(mark_str_val.c_str(), it->first.c_str(), 0);
      if (! rc)
        {
	  bool add_result = true;

	  // Match format strings (if the user specified one)
	  if (has_mark_format && fnmatch(mark_format_val.c_str(),
					 it->second.c_str(), 0))
	    add_result = false;

	  if (add_result)
	    {
	      derived_probe *dp
		= new mark_derived_probe (sess,
					  it->first, it->second,
					  base, loc);
	      finished_results.push_back (dp);
	    }
	}
    }
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
	literal_map_t const & parameters,
	vector<derived_probe *> & finished_results);

    static void register_patterns(match_node *root);
};

void
timer_builder::build(systemtap_session & sess,
    probe * base,
    probe_point * location,
    literal_map_t const & parameters,
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

  root = root->bind(TOK_TIMER);

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

  if (e->components.size() > 0)
    {
      switch (e->components[0].first)
	{
	case target_symbol::comp_literal_array_index:
	  throw semantic_error("perfmon probe '$counter' variable may not be used as array",
			       e->tok);
	  break;
	case target_symbol::comp_struct_member:
	  throw semantic_error("perfmon probe '$counter' variable may not be used as a structure",
			       e->tok);
	  break;
	default:
	  throw semantic_error ("invalid use of perfmon probe '$counter' variable",
				e->tok);
	  break;
	}
    }

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
		     literal_map_t const & parameters,
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
  require <statement*> (&v, &(this->body), base->body);

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
  s.pattern_root->bind(TOK_BEGIN)->bind(new be_builder(BEGIN));
  s.pattern_root->bind_num(TOK_BEGIN)->bind(new be_builder(BEGIN));
  s.pattern_root->bind(TOK_END)->bind(new be_builder(END));
  s.pattern_root->bind_num(TOK_END)->bind(new be_builder(END));
  s.pattern_root->bind(TOK_ERROR)->bind(new be_builder(ERROR));
  s.pattern_root->bind_num(TOK_ERROR)->bind(new be_builder(ERROR));

  s.pattern_root->bind(TOK_NEVER)->bind(new never_builder());

  timer_builder::register_patterns(s.pattern_root);
  s.pattern_root->bind(TOK_TIMER)->bind("profile")->bind(new profile_builder());
  s.pattern_root->bind("perfmon")->bind_str("counter")
    ->bind(new perfmon_builder());

  // dwarf-based kprobe/uprobe parts
  dwarf_derived_probe::register_patterns(s.pattern_root);

  // XXX: user-space starter set
  s.pattern_root->bind_num(TOK_PROCESS)
    ->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)
    ->bind(new uprobe_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)
    ->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)->bind(TOK_RETURN)
    ->bind(new uprobe_builder ());

  // utrace user-space probes
  s.pattern_root->bind_str(TOK_PROCESS)->bind(TOK_BEGIN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)->bind(TOK_BEGIN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind(TOK_PROCESS)->bind(TOK_BEGIN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_str(TOK_PROCESS)->bind(TOK_END)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)->bind(TOK_END)
    ->bind(new utrace_builder ());
  s.pattern_root->bind(TOK_PROCESS)->bind(TOK_END)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_str(TOK_PROCESS)->bind(TOK_THREAD)->bind(TOK_BEGIN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)->bind(TOK_THREAD)->bind(TOK_BEGIN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind(TOK_PROCESS)->bind(TOK_THREAD)->bind(TOK_BEGIN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_str(TOK_PROCESS)->bind(TOK_THREAD)->bind(TOK_END)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)->bind(TOK_THREAD)->bind(TOK_END)
    ->bind(new utrace_builder ());
  s.pattern_root->bind(TOK_PROCESS)->bind(TOK_THREAD)->bind(TOK_END)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_str(TOK_PROCESS)->bind(TOK_SYSCALL)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)->bind(TOK_SYSCALL)
    ->bind(new utrace_builder ());
  s.pattern_root->bind(TOK_PROCESS)->bind(TOK_SYSCALL)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_str(TOK_PROCESS)->bind(TOK_SYSCALL)->bind(TOK_RETURN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)->bind(TOK_SYSCALL)->bind(TOK_RETURN)
    ->bind(new utrace_builder ());
  s.pattern_root->bind(TOK_PROCESS)->bind(TOK_SYSCALL)->bind(TOK_RETURN)
    ->bind(new utrace_builder ());

  // itrace user-space probes
  s.pattern_root->bind_str(TOK_PROCESS)->bind("itrace")
    ->bind(new itrace_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)->bind("itrace")
    ->bind(new itrace_builder ());

  // marker-based parts
  s.pattern_root->bind(TOK_KERNEL)->bind_str(TOK_MARK)
    ->bind(new mark_builder());
  s.pattern_root->bind(TOK_KERNEL)->bind_str(TOK_MARK)->bind_str(TOK_FORMAT)
    ->bind(new mark_builder());

  // procfs parts
  s.pattern_root->bind(TOK_PROCFS)->bind(TOK_READ)->bind(new procfs_builder());
  s.pattern_root->bind_str(TOK_PROCFS)->bind(TOK_READ)
    ->bind(new procfs_builder());
  s.pattern_root->bind(TOK_PROCFS)->bind(TOK_WRITE)->bind(new procfs_builder());
  s.pattern_root->bind_str(TOK_PROCFS)->bind(TOK_WRITE)
    ->bind(new procfs_builder());
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
  DOONE(uprobe);
  DOONE(timer);
  DOONE(profile);
  DOONE(mark);
  DOONE(hrtimer);
  DOONE(perfmon);
  DOONE(procfs);

  // Another "order is important" item.  We want to make sure we
  // "register" the dummy task_finder probe group after all probe
  // groups that use the task_finder.
  DOONE(utrace);
  DOONE(itrace);
  DOONE(task_finder);
#undef DOONE
  return g;
}
