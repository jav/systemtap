// tapset resolution
// Copyright (C) 2005-2012 Red Hat Inc.
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
#include "task_finder.h"
#include "translate.h"
#include "session.h"
#include "util.h"
#include "buildrun.h"
#include "dwarf_wrappers.h"
#include "auto_free.h"
#include "hash.h"
#include "dwflpp.h"
#include "setupdwfl.h"
#include <gelf.h>

#include "sdt_types.h"

#include <cstdlib>
#include <algorithm>
#include <deque>
#include <iostream>
#include <fstream>
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
#include <glob.h>
#include <fnmatch.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <regex.h>
#include <unistd.h>
#include <wordexp.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}


using namespace std;
using namespace __gnu_cxx;



// ------------------------------------------------------------------------

string
common_probe_init (derived_probe* p)
{
  assert(p->session_index != (unsigned)-1);
  return "(&stap_probes[" + lex_cast(p->session_index) + "])";
}


void
common_probe_entryfn_prologue (translator_output* o, string statestr,
                               string probe, string probe_type,
			       bool overload_processing)
{
  o->newline() << "#ifdef STP_ALIBI";
  o->newline() << "atomic_inc(&(" << probe << "->alibi));";
  o->newline() << "#else";

  o->newline() << "struct context* __restrict__ c;";
  o->newline() << "#if !INTERRUPTIBLE";
  o->newline() << "unsigned long flags;";
  o->newline() << "#endif";

  if (overload_processing)
    o->newline() << "#if defined(STP_TIMING) || defined(STP_OVERLOAD)";
  else
    o->newline() << "#ifdef STP_TIMING";
  o->newline() << "cycles_t cycles_atstart = get_cycles ();";
  o->newline() << "#endif";

  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "Stat stat = " << probe << "->timing;";
  o->newline() << "#endif";

  o->newline() << "#if INTERRUPTIBLE";
  o->newline() << "preempt_disable ();";
  o->newline() << "#else";
  o->newline() << "local_irq_save (flags);";
  o->newline() << "#endif";

  // Check for enough free enough stack space
  o->newline() << "if (unlikely ((((unsigned long) (& c)) & (THREAD_SIZE-1))"; // free space
  o->newline(1) << "< (MINSTACKSPACE + sizeof (struct thread_info)))) {"; // needed space
  // XXX: may need porting to platforms where task_struct is not at bottom of kernel stack
  // NB: see also CONFIG_DEBUG_STACKOVERFLOW
  o->newline() << "atomic_inc (& skipped_count);";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "atomic_inc (& skipped_count_lowstack);";
  o->newline() << "#endif";
  o->newline() << "goto probe_epilogue;";
  o->newline(-1) << "}";

  o->newline() << "if (atomic_read (&session_state) != " << statestr << ")";
  o->newline(1) << "goto probe_epilogue;";
  o->indent(-1);

  o->newline() << "c = contexts[smp_processor_id()];";
  o->newline() << "if (atomic_inc_return (& c->busy) != 1) {";
  o->newline(1) << "#if !INTERRUPTIBLE";
  o->newline() << "atomic_inc (& skipped_count);";
  o->newline() << "#endif";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "atomic_inc (& skipped_count_reentrant);";
  o->newline() << "#ifdef DEBUG_REENTRANCY";
  o->newline() << "_stp_warn (\"Skipped %s due to %s residency on cpu %u\\n\", "
               << probe << "->pp, c->probe_point ?: \"?\", smp_processor_id());";
  // NB: There is a conceivable race condition here with reading
  // c->probe_point, knowing that this other probe is sort of running.
  // However, in reality, it's interrupted.  Plus even if it were able
  // to somehow start again, and stop before we read c->probe_point,
  // at least we have that   ?: "?"  bit in there to avoid a NULL deref.
  o->newline() << "#endif";
  o->newline() << "#endif";
  o->newline() << "atomic_dec (& c->busy);";
  o->newline() << "goto probe_epilogue;";
  o->newline(-1) << "}";
  o->newline();
  o->newline() << "c->last_stmt = 0;";
  o->newline() << "c->last_error = 0;";
  o->newline() << "c->nesting = -1;"; // NB: PR10516 packs locals[] tighter
  o->newline() << "c->uregs = 0;";
  o->newline() << "c->kregs = 0;";
  o->newline() << "#if defined __ia64__";
  o->newline() << "c->unwaddr = 0;";
  o->newline() << "#endif";
  o->newline() << "c->probe_point = " << probe << "->pp;";
  o->newline() << "#ifdef STP_NEED_PROBE_NAME";
  o->newline() << "c->probe_name = " << probe << "->pn;";
  o->newline() << "#endif";
  o->newline() << "c->probe_type = " << probe_type << ";";
  // reset Individual Probe State union
  o->newline() << "memset(&c->ips, 0, sizeof(c->ips));";
  o->newline() << "c->probe_flags = 0;";
  o->newline() << "#ifdef STAP_NEED_REGPARM"; // i386 or x86_64 register.stp
  o->newline() << "c->regparm = 0;";
  o->newline() << "#endif";

  o->newline() << "#if INTERRUPTIBLE";
  o->newline() << "c->actionremaining = MAXACTION_INTERRUPTIBLE;";
  o->newline() << "#else";
  o->newline() << "c->actionremaining = MAXACTION;";
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
                               bool overload_processing,
                               bool suppress_handler_errors)
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
  o->newline() << "if (likely (stat)) _stp_stat_add(stat, cycles_elapsed);";
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
      // NB: this is not suppressible via --suppress-runtime-errors,
      // because this is a system safety metric that we cannot trust
      // unprivileged users to override.
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

  o->newline() << "c->probe_point = 0;"; // vacated
  o->newline() << "#ifdef STP_NEED_PROBE_NAME";
  o->newline() << "c->probe_name = 0;";
  o->newline() << "#endif";
  o->newline() << "c->probe_type = 0;";


  o->newline() << "if (unlikely (c->last_error && c->last_error[0])) {";
  o->indent(1);
  if (suppress_handler_errors) // PR 13306
    { 
      o->newline() << "atomic_inc (& error_count);";
    }
  else
    {
      o->newline() << "if (c->last_stmt != NULL)";
      o->newline(1) << "_stp_softerror (\"%s near %s\", c->last_error, c->last_stmt);";
      o->newline(-1) << "else";
      o->newline(1) << "_stp_softerror (\"%s\", c->last_error);";
      o->indent(-1);
      o->newline() << "atomic_inc (& error_count);";
      o->newline() << "if (atomic_read (& error_count) > MAXERRORS) {";
      o->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      o->newline() << "_stp_exit ();";
      o->newline(-1) << "}";
    }

  o->newline(-1) << "}";


  o->newline() << "atomic_dec (&c->busy);";

  o->newline(-1) << "probe_epilogue:"; // context is free
  o->indent(1);

  if (! suppress_handler_errors) // PR 13306
    {
      // Check for excessive skip counts.
      o->newline() << "if (unlikely (atomic_read (& skipped_count) > MAXSKIPPED)) {";
      o->newline(1) << "if (unlikely (pseudo_atomic_cmpxchg(& session_state, STAP_SESSION_RUNNING, STAP_SESSION_ERROR) == STAP_SESSION_RUNNING))";
      o->newline() << "_stp_error (\"Skipped too many probes, check MAXSKIPPED or try again with stap -t for more details.\");";
      o->newline(-1) << "}";
    }

  o->newline() << "#if INTERRUPTIBLE";
  o->newline() << "preempt_enable_no_resched ();";
  o->newline() << "#else";
  o->newline() << "local_irq_restore (flags);";
  o->newline() << "#endif";

  o->newline() << "#endif // STP_ALIBI";
}


// ------------------------------------------------------------------------

// ------------------------------------------------------------------------
//  Dwarf derived probes.  "We apologize for the inconvience."
// ------------------------------------------------------------------------

static const string TOK_KERNEL("kernel");
static const string TOK_MODULE("module");
static const string TOK_FUNCTION("function");
static const string TOK_INLINE("inline");
static const string TOK_CALL("call");
static const string TOK_EXPORTED("exported");
static const string TOK_RETURN("return");
static const string TOK_MAXACTIVE("maxactive");
static const string TOK_STATEMENT("statement");
static const string TOK_ABSOLUTE("absolute");
static const string TOK_PROCESS("process");
static const string TOK_PROVIDER("provider");
static const string TOK_MARK("mark");
static const string TOK_TRACE("trace");
static const string TOK_LABEL("label");
static const string TOK_LIBRARY("library");
static const string TOK_PLT("plt");

static int query_cu (Dwarf_Die * cudie, void * arg);
static void query_addr(Dwarf_Addr addr, dwarf_query *q);

// Can we handle this query with just symbol-table info?
enum dbinfo_reqt
{
  dbr_unknown,
  dbr_none,		// kernel.statement(NUM).absolute
  dbr_need_symtab,	// can get by with symbol table if there's no dwarf
  dbr_need_dwarf
};


struct base_query; // forward decls
struct dwarf_query;
struct dwflpp;
struct symbol_table;


struct
symbol_table
{
  module_info *mod_info;	// associated module
  map<string, func_info*> map_by_name;
  multimap<Dwarf_Addr, func_info*> map_by_addr;
  typedef multimap<Dwarf_Addr, func_info*>::iterator iterator_t;
  typedef pair<iterator_t, iterator_t> range_t;
#ifdef __powerpc__
  GElf_Word opd_section;
#endif
  void add_symbol(const char *name, bool weak, bool descriptor,
                  Dwarf_Addr addr, Dwarf_Addr *high_addr);
  enum info_status read_symbols(FILE *f, const string& path);
  enum info_status read_from_elf_file(const string& path,
				      systemtap_session &sess);
  enum info_status read_from_text_file(const string& path,
				       systemtap_session &sess);
  enum info_status get_from_elf();
  void prepare_section_rejection(Dwfl_Module *mod);
  bool reject_section(GElf_Word section);
  void purge_syscall_stubs();
  func_info *lookup_symbol(const string& name);
  Dwarf_Addr lookup_symbol_address(const string& name);
  func_info *get_func_containing_address(Dwarf_Addr addr);
  func_info *get_first_func();

  symbol_table(module_info *mi) : mod_info(mi) {}
  ~symbol_table();
};

static bool null_die(Dwarf_Die *die)
{
  static Dwarf_Die null;
  return (!die || !memcmp(die, &null, sizeof(null)));
}


enum
function_spec_type
  {
    function_alone,
    function_and_file,
    function_file_and_line
  };


struct dwarf_builder;
struct dwarf_var_expanding_visitor;


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
  string path;
  bool has_process;
  bool has_return;
  bool has_maxactive;
  bool has_library;
  long maxactive_val;
  // dwarf_derived_probe_group::emit_module_decls uses this to emit sdt kprobe definition
  string user_path;
  string user_lib;
  bool access_vars;

  unsigned saved_longs, saved_strings;
  dwarf_derived_probe* entry_handler;

  void printsig (std::ostream &o) const;
  virtual void join_group (systemtap_session& s);
  void emit_probe_local_init(translator_output * o);
  void getargs(std::list<std::string> &arg_set) const;

  void emit_privilege_assertion (translator_output*);
  void print_dupe_stamp(ostream& o);

  // Pattern registration helpers.
  static void register_statement_variants(match_node * root,
					  dwarf_builder * dw,
					  privilege_t privilege);
  static void register_function_variants(match_node * root,
					 dwarf_builder * dw,
					 privilege_t privilege);
  static void register_function_and_statement_variants(systemtap_session& s,
						       match_node * root,
						       dwarf_builder * dw,
						       privilege_t privilege);
  static void register_sdt_variants(systemtap_session& s,
				    match_node * root,
				    dwarf_builder * dw);
  static void register_plt_variants(systemtap_session& s,
				    match_node * root,
				    dwarf_builder * dw);
  static void register_patterns(systemtap_session& s);

protected:
  dwarf_derived_probe(probe *base,
                      probe_point *location,
                      Dwarf_Addr addr,
                      bool has_return):
    derived_probe(base, location), addr(addr), has_process(0),
    has_return(has_return), has_maxactive(0), has_library(0),
    maxactive_val(0), access_vars(false), saved_longs(0),
    saved_strings(0), entry_handler(0)
  {}

private:
  list<string> args;
  void saveargs(dwarf_query& q, Dwarf_Die* scope_die, Dwarf_Addr dwfl_addr);
};


struct uprobe_derived_probe: public dwarf_derived_probe
{
  int pid; // 0 => unrestricted

  uprobe_derived_probe (const string& function,
                        const string& filename,
                        int line,
                        const string& module,
                        const string& section,
                        Dwarf_Addr dwfl_addr,
                        Dwarf_Addr addr,
                        dwarf_query & q,
                        Dwarf_Die* scope_die):
    dwarf_derived_probe(function, filename, line, module, section,
                        dwfl_addr, addr, q, scope_die), pid(0)
  {}

  // alternate constructor for process(PID).statement(ADDR).absolute
  uprobe_derived_probe (probe *base,
                        probe_point *location,
                        int pid,
                        Dwarf_Addr addr,
                        bool has_return):
    dwarf_derived_probe(base, location, addr, has_return), pid(pid)
  {}

  void join_group (systemtap_session& s);

  void emit_privilege_assertion (translator_output*);
  void print_dupe_stamp(ostream& o) { print_dupe_stamp_unprivileged_process_owner (o); }
  void getargs(std::list<std::string> &arg_set) const;
  void saveargs(int nargs);
private:
  list<string> args;
};

struct dwarf_derived_probe_group: public derived_probe_group
{
private:
  multimap<string,dwarf_derived_probe*> probes_by_module;
  typedef multimap<string,dwarf_derived_probe*>::iterator p_b_m_iterator;

public:
  dwarf_derived_probe_group() {}
  void enroll (dwarf_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_refresh (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


// Helper struct to thread through the dwfl callbacks.
struct base_query
{
  base_query(dwflpp & dw, literal_map_t const & params);
  base_query(dwflpp & dw, const string & module_val);
  virtual ~base_query() {}

  systemtap_session & sess;
  dwflpp & dw;

  // Parameter extractors.
  static bool has_null_param(literal_map_t const & params,
                             string const & k);
  static bool get_string_param(literal_map_t const & params,
			       string const & k, string & v);
  static bool get_number_param(literal_map_t const & params,
			       string const & k, long & v);
  static bool get_number_param(literal_map_t const & params,
			       string const & k, Dwarf_Addr & v);
  static void query_library_callback (void *object, const char *data);
  static void query_plt_callback (void *object, const char *link, size_t addr);
  virtual void query_library (const char *data) = 0;
  virtual void query_plt (const char *link, size_t addr) = 0;


  // Extracted parameters.
  bool has_kernel;
  bool has_module;
  bool has_process;
  bool has_library;
  bool has_plt;
  bool has_statement;
  string module_val; // has_kernel => module_val = "kernel"
  string path;	     // executable path if module is a .so
  string plt_val;    // has_plt => plt wildcard

  virtual void handle_query_module() = 0;
};


base_query::base_query(dwflpp & dw, literal_map_t const & params):
  sess(dw.sess), dw(dw), has_library(false), has_plt(false), has_statement(false)
{
  has_kernel = has_null_param (params, TOK_KERNEL);
  if (has_kernel)
    module_val = "kernel";

  has_module = get_string_param (params, TOK_MODULE, module_val);
  if (has_module)
    has_process = false;
  else
    {
      string library_name;
      long statement_num_val;
      has_process = get_string_param(params, TOK_PROCESS, module_val);
      has_library = get_string_param (params, TOK_LIBRARY, library_name);
      if ((has_plt = has_null_param (params, TOK_PLT)))
        plt_val = "*";
      else has_plt = get_string_param (params, TOK_PLT, plt_val);
      if (has_plt)
	sess.consult_symtab = true;
      has_statement = get_number_param(params, TOK_STATEMENT, statement_num_val);

      if (has_process)
        module_val = find_executable (module_val, sess.sysroot, sess.sysenv);
      if (has_library)
        {
          if (! contains_glob_chars (library_name))
            {
              path = path_remove_sysroot(sess, module_val);
              module_val = find_executable (library_name, sess.sysroot,
                                            sess.sysenv, "LD_LIBRARY_PATH");
	      if (module_val.find('/') == string::npos)
		{
		  // We didn't find library_name so use iterate_over_libraries
		  module_val = path;
		  path = library_name;
		}
            }
          else
            path = library_name;
        }
    }

  assert (has_kernel || has_process || has_module);
}

base_query::base_query(dwflpp & dw, const string & module_val)
  : sess(dw.sess), dw(dw), has_library(false), has_plt(false), has_statement(false),
    module_val(module_val)
{
  // NB: This uses '/' to distinguish between kernel modules and userspace,
  // which means that userspace modules won't get any PATH searching.
  if (module_val.find('/') == string::npos)
    {
      has_kernel = (module_val == TOK_KERNEL);
      has_module = !has_kernel;
      has_process = false;
    }
  else
    {
      has_kernel = has_module = false;
      has_process = true;
    }
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
  dwarf_query(probe * base_probe,
	      probe_point * base_loc,
	      dwflpp & dw,
	      literal_map_t const & params,
	      vector<derived_probe *> & results,
	      const string user_path,
	      const string user_lib);

  vector<derived_probe *> & results;
  set<string> inlined_non_returnable; // function names
  probe * base_probe;
  probe_point * base_loc;
  string user_path;
  string user_lib;

  virtual void handle_query_module();
  void query_module_dwarf();
  void query_module_symtab();
  void query_library (const char *data);
  void query_plt (const char *entry, size_t addr);

  void add_probe_point(string const & funcname,
		       char const * filename,
		       int line,
		       Dwarf_Die *scope_die,
		       Dwarf_Addr addr);

  // Track addresses we've already seen in a given module
  set<Dwarf_Addr> alias_dupes;

  // Track inlines we've already seen as well
  // NB: this can't be compared just by entrypc, as inlines can overlap
  set<inline_instance_info> inline_dupes;

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
  bool has_exported;
  bool has_inline;
  bool has_return;

  bool has_maxactive;
  long maxactive_val;

  bool has_label;
  string label_val;

  bool has_relative;
  long relative_val;

  bool has_absolute;

  bool has_mark;

  enum dbinfo_reqt dbinfo_reqt;
  enum dbinfo_reqt assess_dbinfo_reqt();

  void parse_function_spec(const string & spec);
  function_spec_type spec_type;
  vector<string> scopes;
  string function;
  string file;
  line_t line_type;
  int line[2];
  bool query_done;	// Found exact match

  set<string> filtered_srcfiles;

  // Map official entrypc -> func_info object
  inline_instance_map_t filtered_inlines;
  func_info_map_t filtered_functions;
  bool choose_next_line;
  Dwarf_Addr entrypc_for_next_line;

  void query_module_functions ();
};


static void delete_session_module_cache (systemtap_session& s); // forward decl


struct dwarf_builder: public derived_probe_builder
{
  map <string,dwflpp*> kern_dw; /* NB: key string could be a wildcard */
  map <string,dwflpp*> user_dw;
  string user_path;
  string user_lib;
  dwarf_builder() {}

  dwflpp *get_kern_dw(systemtap_session& sess, const string& module)
  {
    if (kern_dw[module] == 0)
      kern_dw[module] = new dwflpp(sess, module, true); // might throw
    return kern_dw[module];
  }

  dwflpp *get_user_dw(systemtap_session& sess, const string& module)
  {
    if (user_dw[module] == 0)
      user_dw[module] = new dwflpp(sess, module, false); // might throw
    return user_dw[module];
  }

  /* NB: not virtual, so can be called from dtor too: */
  void dwarf_build_no_more (bool)
  {
    delete_map(kern_dw);
    delete_map(user_dw);
  }

  void build_no_more (systemtap_session &s)
  {
    dwarf_build_no_more (s.verbose > 3);
    delete_session_module_cache (s);
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


dwarf_query::dwarf_query(probe * base_probe,
			 probe_point * base_loc,
			 dwflpp & dw,
			 literal_map_t const & params,
			 vector<derived_probe *> & results,
			 const string user_path,
			 const string user_lib)
  : base_query(dw, params), results(results),
    base_probe(base_probe), base_loc(base_loc),
    user_path(user_path), user_lib(user_lib), has_relative(false),
    relative_val(0), choose_next_line(false), entrypc_for_next_line(0)
{
  // Reduce the query to more reasonable semantic values (booleans,
  // extracted strings, numbers, etc).
  has_function_str = get_string_param(params, TOK_FUNCTION, function_str_val);
  has_function_num = get_number_param(params, TOK_FUNCTION, function_num_val);

  has_statement_str = get_string_param(params, TOK_STATEMENT, statement_str_val);
  has_statement_num = get_number_param(params, TOK_STATEMENT, statement_num_val);

  has_label = get_string_param(params, TOK_LABEL, label_val);

  has_call = has_null_param(params, TOK_CALL);
  has_exported = has_null_param(params, TOK_EXPORTED);
  has_inline = has_null_param(params, TOK_INLINE);
  has_return = has_null_param(params, TOK_RETURN);
  has_maxactive = get_number_param(params, TOK_MAXACTIVE, maxactive_val);
  has_absolute = has_null_param(params, TOK_ABSOLUTE);
  has_mark = false;

  if (has_function_str)
    parse_function_spec(function_str_val);
  else if (has_statement_str)
    parse_function_spec(statement_str_val);

  dbinfo_reqt = assess_dbinfo_reqt();
  query_done = false;
}


func_info_map_t *
get_filtered_functions(dwarf_query *q)
{
  return &q->filtered_functions;
}


inline_instance_map_t *
get_filtered_inlines(dwarf_query *q)
{
  return &q->filtered_inlines;
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
      Dwarf_Addr addr = has_function_num ?
        function_num_val : statement_num_val;

      // These are raw addresses, we need to know what the elf_bias
      // is to feed it to libdwfl based functions.
      Dwarf_Addr elf_bias;
      Elf *elf = dwfl_module_getelf (dw.module, &elf_bias);
      assert(elf);
      addr += elf_bias;
      query_addr(addr, this);
    }
  else
    {
      // Otherwise if we have a function("foo") or statement("foo")
      // specifier, we have to scan over all the CUs looking for
      // the function(s) in question
      assert(has_function_str || has_statement_str);

      // For simple cases, no wildcard and no source:line, we can do a very
      // quick function lookup in a module-wide cache.
      if (spec_type == function_alone &&
          !dw.name_has_wildcard(function) &&
          !startswith(function, "_Z"))
        query_module_functions();
      else
        dw.iterate_over_cus(&query_cu, this, false);
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
              cerr << _F("Error: Pattern '%s' matches every single "
                         "instruction address in the symbol table,\n"
                         "some of which aren't even functions.\n", function_str_val.c_str()) << endl;
              return;
            }
          symbol_table::iterator_t iter;
          for (iter = sym_table->map_by_addr.begin();
               iter != sym_table->map_by_addr.end();
               ++iter)
            {
              fi = iter->second;
              if (!null_die(&fi->die))
                continue;       // already handled in query_module_dwarf()
              if (dw.function_name_matches_pattern(fi->name, function_str_val))
                query_func_info(fi->addr, *fi, this);
            }
        }
      else
        {
          fi = sym_table->lookup_symbol(function_str_val);
          if (fi && !fi->descriptor && null_die(&fi->die))
            query_func_info(fi->addr, *fi, this);
        }
    }
  else
    {
      assert(has_function_num || has_statement_num);
      // Find the "function" in which the indicated address resides.
      Dwarf_Addr addr =
      		(has_function_num ? function_num_val : statement_num_val);
      if (has_plt)
        {
          // Use the raw address from the .plt
          fi = sym_table->get_first_func();
          fi->addr = addr;
        }
      else
        fi = sym_table->get_func_containing_address(addr);

      if (!fi)
        {
          sess.print_warning(_F("address %#" PRIx64 " out of range for module %s",
                  addr, dw.module_name.c_str()));
          return;
        }
      if (!null_die(&fi->die))
        {
          // addr looks like it's in the compilation unit containing
          // the indicated function, but query_module_dwarf() didn't
          // match addr to any compilation unit, so addr must be
          // above that cu's address range.
          sess.print_warning(_F("address %#" PRIx64 " maps to no known compilation unit in module %s",
                       addr, dw.module_name.c_str()));
          return;
        }
      query_func_info(fi->addr, *fi, this);
    }
}

void
dwarf_query::handle_query_module()
{
  bool report = dbinfo_reqt == dbr_need_dwarf || !sess.consult_symtab;
  dw.get_module_dwarf(false, report);

  // prebuild the symbol table to resolve aliases
  dw.mod_info->get_symtab(this);

  // reset the dupe-checking for each new module
  alias_dupes.clear();
  inline_dupes.clear();

  if (dw.mod_info->dwarf_status == info_present)
    query_module_dwarf();

  // Consult the symbol table if we haven't found all we're looking for.
  // asm functions can show up in the symbol table but not in dwarf.
  if (sess.consult_symtab && !query_done)
    query_module_symtab();
}


void
dwarf_query::parse_function_spec(const string & spec)
{
  line_type = ABSOLUTE;
  line[0] = line[1] = 0;

  size_t src_pos, line_pos, dash_pos, scope_pos;

  // look for named scopes
  scope_pos = spec.rfind("::");
  if (scope_pos != string::npos)
    {
      tokenize_cxx(spec.substr(0, scope_pos), scopes);
      scope_pos += 2;
    }
  else
    scope_pos = 0;

  // look for a source separator
  src_pos = spec.find('@', scope_pos);
  if (src_pos == string::npos)
    {
      function = spec.substr(scope_pos);
      spec_type = function_alone;
    }
  else
    {
      function = spec.substr(scope_pos, src_pos - scope_pos);

      // look for a line-number separator
      line_pos = spec.find_first_of(":+", src_pos);
      if (line_pos == string::npos)
        {
          file = spec.substr(src_pos + 1);
          spec_type = function_and_file;
        }
      else
        {
          file = spec.substr(src_pos + 1, line_pos - src_pos - 1);

          // classify the line spec
          spec_type = function_file_and_line;
          if (spec[line_pos] == '+')
            line_type = RELATIVE;
          else if (spec[line_pos + 1] == '*' &&
                   spec.length() == line_pos + 2)
            line_type = WILDCARD;
          else
            line_type = ABSOLUTE;

          if (line_type != WILDCARD)
            try
              {
                // try to parse either N or N-M
                dash_pos = spec.find('-', line_pos + 1);
                if (dash_pos == string::npos)
                  line[0] = line[1] = lex_cast<int>(spec.substr(line_pos + 1));
                else
                  {
                    line_type = RANGE;
                    line[0] = lex_cast<int>(spec.substr(line_pos + 1,
                                                        dash_pos - line_pos - 1));
                    line[1] = lex_cast<int>(spec.substr(dash_pos + 1));
                  }
              }
            catch (runtime_error & exn)
              {
                goto bad;
              }
        }
    }

  if (function.empty() ||
      (spec_type != function_alone && file.empty()))
    goto bad;

  if (sess.verbose > 2)
    {
      //clog << "parsed '" << spec << "'";
      clog << _F("parse '%s'", spec.c_str());

      if (!scopes.empty())
        clog << ", scope '" << scopes[0] << "'";
      for (unsigned i = 1; i < scopes.size(); ++i)
        clog << "::'" << scopes[i] << "'";

      clog << ", func '" << function << "'";

      if (spec_type != function_alone)
        clog << ", file '" << file << "'";

      if (spec_type == function_file_and_line)
        {
          clog << ", line ";
          switch (line_type)
            {
            case ABSOLUTE:
              clog << line[0];
              break;

            case RELATIVE:
              clog << "+" << line[0];
              break;

            case RANGE:
              clog << line[0] << " - " << line[1];
              break;

            case WILDCARD:
              clog << "*";
              break;
            }
        }

      clog << endl;
    }

  return;

bad:
  throw semantic_error(_F("malformed specification '%s'", spec.c_str()),
                       base_probe->tok);
}

string path_remove_sysroot(const systemtap_session& sess, const string& path)
{
  size_t pos;
  string retval = path;
  if (!sess.sysroot.empty() &&
      (pos = retval.find(sess.sysroot)) != string::npos)
    retval.replace(pos, sess.sysroot.length(), "/");
  return retval;
}

void
dwarf_query::add_probe_point(const string& dw_funcname,
			     const char* filename,
			     int line,
			     Dwarf_Die* scope_die,
			     Dwarf_Addr addr)
{
  string reloc_section; // base section for relocation purposes
  Dwarf_Addr reloc_addr; // relocated
  const string& module = dw.module_name; // "kernel" or other
  string funcname = dw_funcname;

  assert (! has_absolute); // already handled in dwarf_builder::build()

  if (!has_plt)
    reloc_addr = dw.relocate_address(addr, reloc_section);
  else
    {
      // Set the reloc_section but use the plt entry for reloc_addr
      dw.relocate_address(addr, reloc_section);
      reloc_addr = addr;
    }

  // If we originally used the linkage name, then let's call it that way
  const char* linkage_name;
  if (scope_die && startswith (this->function, "_Z")
      && (linkage_name = dwarf_linkage_name (scope_die)))
    funcname = linkage_name;

  if (sess.verbose > 1)
    {
      clog << _("probe ") << funcname << "@" << filename << ":" << line;
      if (string(module) == TOK_KERNEL)
        clog << _(" kernel");
      else if (has_module)
        clog << _(" module=") << module;
      else if (has_process)
        clog << _(" process=") << module;
      if (reloc_section != "") clog << " reloc=" << reloc_section;
      clog << " pc=0x" << hex << addr << dec;
    }

  bool bad = dw.blacklisted_p (funcname, filename, line, module,
                               addr, has_return);
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
          string module_tgt = path_remove_sysroot(sess, module);
          results.push_back (new uprobe_derived_probe(funcname, filename, line,
                                                      module_tgt, reloc_section, addr, reloc_addr,
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
      q->add_probe_point(func, file ? file : "",
                         line, scope_die, stmt_addr);
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
    }
}

static void
query_addr(Dwarf_Addr addr, dwarf_query *q)
{
  dwflpp &dw = q->dw;

  if (q->sess.verbose > 2)
    clog << "query_addr 0x" << hex << addr << dec << endl;

  // First pick which CU contains this address
  Dwarf_Die* cudie = dw.query_cu_containing_address(addr);
  if (!cudie) // address could be wildly out of range
    return;
  dw.focus_on_cu(cudie);

  // Now compensate for the dw bias
  addr -= dw.module_bias;

  // Per PR5787, we look up the scope die even for
  // statement_num's, for blacklist sensitivity and $var
  // resolution purposes.

  // Find the scopes containing this address
  vector<Dwarf_Die> scopes = dw.getscopes(addr);
  if (scopes.empty())
    return;

  // Look for the innermost containing function
  Dwarf_Die *fnscope = NULL;
  for (size_t i = 0; i < scopes.size(); ++i)
    {
      int tag = dwarf_tag(&scopes[i]);
      if ((tag == DW_TAG_subprogram && !q->has_inline) ||
          (tag == DW_TAG_inlined_subroutine &&
           !q->has_call && !q->has_return && !q->has_exported))
        {
          fnscope = &scopes[i];
          break;
        }
    }
  if (!fnscope)
    return;
  dw.focus_on_function(fnscope);

  Dwarf_Die *scope = q->has_function_num ? fnscope : &scopes[0];

  const char *file = dwarf_decl_file(fnscope);
  int line;
  dwarf_decl_line(fnscope, &line);

  // Function probes should reset the addr to the function entry
  // and possibly perform prologue searching
  if (q->has_function_num)
    {
      dw.die_entrypc(fnscope, &addr);
      if (dwarf_tag(fnscope) == DW_TAG_subprogram &&
          (q->sess.prologue_searching || q->has_process)) // PR 6871
        {
          func_info func;
          func.die = *fnscope;
          func.name = dw.function_name;
          func.decl_file = file;
          func.decl_line = line;
          func.entrypc = addr;

          func_info_map_t funcs(1, func);
          dw.resolve_prologue_endings (funcs);
          if (q->has_return) // PR13200
            {
              if (q->sess.verbose > 2)
                clog << "ignoring prologue for .return probes" << endl;
            }
          else
            {
              if (funcs[0].prologue_end)
                addr = funcs[0].prologue_end;
            }
        }
    }
  else
    {
      dwarf_line_t address_line(dwarf_getsrc_die(cudie, addr));
      if (address_line)
        {
          file = address_line.linesrc();
          line = address_line.lineno();
        }

      // Verify that a raw address matches the beginning of a
      // statement. This is a somewhat lame check that the address
      // is at the start of an assembly instruction.  Mark probes are in the
      // middle of a macro and thus not strictly at a statement beginning.
      // Guru mode may override this check.
      if (!q->has_mark && (!address_line || address_line.addr() != addr))
        {
          stringstream msg;
          msg << _F("address %#" PRIx64 " does not match the beginning of a statement",
                    addr);
          if (address_line)
            msg << _F(" (try %#" PRIx64 ")", address_line.addr());
          else
            msg << _F(" (no line info found for '%s', in module '%s')",
                      dw.cu_name().c_str(), dw.module_name.c_str());
          if (! q->sess.guru_mode)
            throw semantic_error(msg.str());
          else
           q->sess.print_warning(msg.str());
        }
    }

  // Build a probe at this point
  query_statement(dw.function_name, file, line, scope, addr, q);
}

static void
query_label (string const & func,
             char const * label,
             char const * file,
             int line,
             Dwarf_Die *scope_die,
             Dwarf_Addr stmt_addr,
             dwarf_query * q)
{
  assert (q->has_statement_str || q->has_function_str);

  size_t i = q->results.size();

  // weed out functions whose decl_file isn't one of
  // the source files that we actually care about
  if (q->spec_type != function_alone &&
      q->filtered_srcfiles.count(file) == 0)
    return;

  query_statement(func, file, line, scope_die, stmt_addr, q);

  // after the fact, insert the label back into the derivation chain
  probe_point::component* ppc =
    new probe_point::component(TOK_LABEL, new literal_string (label));
  for (; i < q->results.size(); ++i)
    {
      derived_probe* p = q->results[i];
      probe_point* pp = new probe_point(*p->locations[0]);
      pp->components.push_back (ppc);
      p->base = p->base->create_alias(p->locations[0], pp);
    }
}

static void
query_inline_instance_info (inline_instance_info & ii,
			    dwarf_query * q)
{
  try
    {
      assert (! q->has_return); // checked by caller already
      if (q->sess.verbose>2)
        clog << _F("querying entrypc %#" PRIx64 " of instance of inline '%s'\n",
                   ii.entrypc, ii.name.c_str());
      query_statement (ii.name, ii.decl_file, ii.decl_line,
                       &ii.die, ii.entrypc, q);
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
          if (fi.prologue_end != 0 && q->has_return) // PR13200
            {
              if (q->sess.verbose > 2)
                clog << "ignoring prologue for .return probes" << endl;
            }
	  query_statement (fi.name, fi.decl_file, fi.decl_line,
			   &fi.die, entrypc, q);
	}
      else
	{
          if (fi.prologue_end != 0)
            {
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
query_srcfile_label (const dwarf_line_t& line, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);

  Dwarf_Addr addr = line.addr();

  for (func_info_map_t::iterator i = q->filtered_functions.begin();
       i != q->filtered_functions.end(); ++i)
    if (q->dw.die_has_pc (i->die, addr))
      q->dw.iterate_over_labels (&i->die, q->label_val, i->name,
                                 q, query_label);

  for (inline_instance_map_t::iterator i = q->filtered_inlines.begin();
       i != q->filtered_inlines.end(); ++i)
    if (q->dw.die_has_pc (i->die, addr))
      q->dw.iterate_over_labels (&i->die, q->label_val, i->name,
                                 q, query_label);
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
	    clog << _("function DIE lands on srcfile\n");
	  if (q->has_statement_str)
            {
              Dwarf_Die scope;
              q->dw.inner_die_containing_pc(i->die, addr, scope);
              query_statement (i->name, i->decl_file,
                               lineno, // NB: not q->line !
                               &scope, addr, q);
            }
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
	    clog << _("inline instance DIE lands on srcfile\n");
	  if (q->has_statement_str)
            {
              Dwarf_Die scope;
              q->dw.inner_die_containing_pc(i->die, addr, scope);
              query_statement (i->name, i->decl_file,
                               q->line[0], &scope, addr, q);
            }
	  else
	    query_inline_instance_info (*i, q);
	}
    }
}


bool
inline_instance_info::operator<(const inline_instance_info& other) const
{
  if (entrypc != other.entrypc)
    return entrypc < other.entrypc;

  if (decl_line != other.decl_line)
    return decl_line < other.decl_line;

  int cmp = name.compare(other.name);
  if (!cmp)
    cmp = strcmp(decl_file, other.decl_file);
  return cmp < 0;
}


static int
query_dwarf_inline_instance (Dwarf_Die * die, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);
  assert (q->has_statement_str || q->has_function_str);
  assert (!q->has_call && !q->has_return && !q->has_exported);

  try
    {
      if (q->sess.verbose>2)
        clog << _F("selected inline instance of %s\n", q->dw.function_name.c_str());

      Dwarf_Addr entrypc;
      if (q->dw.die_entrypc (die, &entrypc))
        {
          inline_instance_info inl;
          inl.die = *die;
          inl.name = q->dw.function_name;
          inl.entrypc = entrypc;
          q->dw.function_file (&inl.decl_file);
          q->dw.function_line (&inl.decl_line);

          // make sure that this inline hasn't already
          // been matched from a different CU
          if (q->inline_dupes.insert(inl).second)
            q->filtered_inlines.push_back(inl);
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
query_dwarf_func (Dwarf_Die * func, base_query * bq)
{
  dwarf_query * q = static_cast<dwarf_query *>(bq);
  assert (q->has_statement_str || q->has_function_str);

  // weed out functions whose decl_file isn't one of
  // the source files that we actually care about
  if (q->spec_type != function_alone &&
      q->filtered_srcfiles.count(dwarf_decl_file(func)?:"") == 0)
    return DWARF_CB_OK;

  try
    {
      q->dw.focus_on_function (func);

      if (!q->dw.function_scope_matches(q->scopes))
        return DWARF_CB_OK;

      // make sure that this function address hasn't
      // already been matched under an aliased name
      Dwarf_Addr addr;
      if (!q->dw.func_is_inline() &&
          dwarf_entrypc(func, &addr) == 0 &&
          !q->alias_dupes.insert(addr).second)
        return DWARF_CB_OK;

      if (q->dw.func_is_inline () && (! q->has_call) && (! q->has_return) && (! q->has_exported))
	{
          if (q->sess.verbose>3)
            clog << _F("checking instances of inline %s\n", q->dw.function_name.c_str());
          q->dw.iterate_over_inline_instances (query_dwarf_inline_instance, q);
	}
      else if (q->dw.func_is_inline () && (q->has_return)) // PR 11553
	{
          q->inlined_non_returnable.insert (q->dw.function_name);
	}
      else if (!q->dw.func_is_inline () && (! q->has_inline))
	{
          if (q->has_exported && !q->dw.func_is_exported ())
            return DWARF_CB_OK;
          if (q->sess.verbose>2)
            clog << _F("selected function %s\n", q->dw.function_name.c_str());

          func_info func;
          q->dw.function_die (&func.die);
          func.name = q->dw.function_name;
          q->dw.function_file (&func.decl_file);
          q->dw.function_line (&func.decl_line);

          Dwarf_Addr entrypc;
          if (q->dw.function_entrypc (&entrypc))
            {
              func.entrypc = entrypc;
              q->filtered_functions.push_back (func);
            }
          /* else this function is fully inlined, just ignore it */
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
  assert (q->has_statement_str || q->has_function_str);

  if (pending_interrupts) return DWARF_CB_ABORT;

  try
    {
      q->dw.focus_on_cu (cudie);

      if (false && q->sess.verbose>2)
        clog << _F("focused on CU '%s', in module '%s'\n",
                   q->dw.cu_name().c_str(), q->dw.module_name.c_str());

      q->filtered_srcfiles.clear();
      q->filtered_functions.clear();
      q->filtered_inlines.clear();

      // In this path, we find "abstract functions", record
      // information about them, and then (depending on lineno
      // matching) possibly emit one or more of the function's
      // associated addresses. Unfortunately the control of this
      // cannot easily be turned inside out.

      if (q->spec_type != function_alone)
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
      int rc = q->dw.iterate_over_functions (query_dwarf_func, q, q->function);
      if (rc != DWARF_CB_OK)
        q->query_done = true;

      if ((q->sess.prologue_searching || q->has_process) // PR 6871
          && !q->has_statement_str) // PR 2608
        if (! q->filtered_functions.empty())
          q->dw.resolve_prologue_endings (q->filtered_functions);
      // NB: we could skip the resolve_prologue_endings() call here for has_return case (PR13200),
      // but don't have to.  We can resolve the prologue, just not actually use it in query_addr().

      if (q->spec_type == function_file_and_line)
        {
          // .statement(...:NN) often gets mixed up with .function(...:NN)
          if (q->has_function_str)
            q->sess.print_warning (_("For probing a particular line, use a "
                                   ".statement() probe, not .function()"), 
                                   q->base_probe->tok);

          // If we have a pattern string with target *line*, we
          // have to look at lines in all the matched srcfiles.
          void (* callback) (const dwarf_line_t&, void*) =
            q->has_label ? query_srcfile_label : query_srcfile_line;
          for (set<string>::const_iterator i = q->filtered_srcfiles.begin();
               i != q->filtered_srcfiles.end(); ++i)
            q->dw.iterate_over_srcfile_lines (i->c_str(), q->line, q->has_statement_str,
                                              q->line_type, callback, q->function, q);
        }
      else if (q->has_label)
        {
          for (func_info_map_t::iterator i = q->filtered_functions.begin();
               i != q->filtered_functions.end(); ++i)
            q->dw.iterate_over_labels (&i->die, q->label_val, i->name,
                                       q, query_label);

          for (inline_instance_map_t::iterator i = q->filtered_inlines.begin();
               i != q->filtered_inlines.end(); ++i)
            q->dw.iterate_over_labels (&i->die, q->label_val, i->name,
                                       q, query_label);
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
      return DWARF_CB_OK;
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
      return DWARF_CB_ABORT;
    }
}


void
dwarf_query::query_module_functions ()
{
  try
    {
      filtered_srcfiles.clear();
      filtered_functions.clear();
      filtered_inlines.clear();

      // Collect all module functions so we know which CUs are interesting
      int rc = dw.iterate_single_function(query_dwarf_func, this, function);
      if (rc != DWARF_CB_OK)
        {
          query_done = true;
          return;
        }

      set<void*> used_cus; // by cu->addr
      vector<Dwarf_Die> cus;
      Dwarf_Die cu_mem;

      for (func_info_map_t::iterator i = filtered_functions.begin();
           i != filtered_functions.end(); ++i)
        if (dwarf_diecu(&i->die, &cu_mem, NULL, NULL) &&
            used_cus.insert(cu_mem.addr).second)
          cus.push_back(cu_mem);

      for (inline_instance_map_t::iterator i = filtered_inlines.begin();
           i != filtered_inlines.end(); ++i)
        if (dwarf_diecu(&i->die, &cu_mem, NULL, NULL) &&
            used_cus.insert(cu_mem.addr).second)
          cus.push_back(cu_mem);

      // Reset the dupes since we didn't actually collect them the first time
      alias_dupes.clear();
      inline_dupes.clear();

      // Run the query again on the individual CUs
      for (vector<Dwarf_Die>::iterator i = cus.begin(); i != cus.end(); ++i)
        query_cu(&*i, this);
    }
  catch (const semantic_error& e)
    {
      sess.print_error (e);
    }
}


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

  string expect_machine; // to match sess.machine (i.e., kernel machine)
  string expect_machine2;

  // NB: See also the 'uname -m' squashing done in main.cxx.
  switch (elf_machine)
    {
      // x86 and ppc are bi-architecture; a 64-bit kernel
      // can normally run either 32-bit or 64-bit *userspace*.
    case EM_386:
      expect_machine = "i?86";
      if (! q->has_process) break; // 32-bit kernel/module
      /* FALLSTHROUGH */
    case EM_X86_64:
      expect_machine2 = "x86_64";
      break;
    case EM_PPC:
    case EM_PPC64:
      expect_machine = "powerpc";
      break;
    case EM_S390: expect_machine = "s390"; break;
    case EM_IA_64: expect_machine = "ia64"; break;
    case EM_ARM: expect_machine = "arm*"; break;
      // XXX: fill in some more of these
    default: expect_machine = "?"; break;
    }

  if (! debug_filename) debug_filename = main_filename;
  if (! debug_filename) debug_filename = name;

  if (fnmatch (expect_machine.c_str(), sess_machine.c_str(), 0) != 0 &&
      fnmatch (expect_machine2.c_str(), sess_machine.c_str(), 0) != 0)
    {
      stringstream msg;
      msg << _F("ELF machine %s|%s (code %d) mismatch with target %s in '%s'",
                expect_machine.c_str(), expect_machine2.c_str(), elf_machine,
                sess_machine.c_str(), debug_filename);
      throw semantic_error(msg.str ());
    }

  if (q->sess.verbose>1)
    clog << _F("focused on module '%s' = [%#" PRIx64 "-%#" PRIx64 ", bias %#" PRIx64 
               " file %s ELF machine %s|%s (code %d)\n",
               q->dw.module_name.c_str(), q->dw.module_start, q->dw.module_end,
               q->dw.module_bias, debug_filename, expect_machine.c_str(),
               expect_machine2.c_str(), elf_machine);
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
              Dwarf_Addr addr,
	      void *arg)
{
  base_query *q = static_cast<base_query *>(arg);

  try
    {
      module_info* mi = q->sess.module_cache->cache[name];
      if (mi == 0)
        {
          mi = q->sess.module_cache->cache[name] = new module_info(name);

          mi->mod = mod;
          mi->addr = addr;

          const char* debug_filename = "";
          const char* main_filename = "";
          (void) dwfl_module_info (mod, NULL, NULL,
                                   NULL, NULL, NULL,
                                   & main_filename,
                                   & debug_filename);

          if (q->sess.ignore_vmlinux && name == TOK_KERNEL)
            {
              // report_kernel() in elfutils found vmlinux, but pretend it didn't.
              // Given a non-null path, returning 1 means keep reporting modules.
              mi->dwarf_status = info_absent;
            }
          else if (debug_filename || main_filename)
            {
              mi->elf_path = debug_filename ?: main_filename;
            }
          else if (name == TOK_KERNEL)
            {
              mi->dwarf_status = info_absent;
            }
        }
      // OK, enough of that module_info caching business.

      q->dw.focus_on_module(mod, mi);

      // If we have enough information in the pattern to skip a module and
      // the module does not match that information, return early.
      if (!q->dw.module_name_matches(q->module_val))
        return pending_interrupts ? DWARF_CB_ABORT : DWARF_CB_OK;

      // Don't allow module("*kernel*") type expressions to match the
      // elfutils module "kernel", which we refer to in the probe
      // point syntax exclusively as "kernel.*".
      if (q->dw.module_name == TOK_KERNEL && ! q->has_kernel)
        return pending_interrupts ? DWARF_CB_ABORT : DWARF_CB_OK;

      if (mod)
        validate_module_elf(mod, name, q);
      else
        assert(q->has_kernel);   // and no vmlinux to examine

      if (q->sess.verbose>2)
        cerr << _F("focused on module '%s'\n", q->dw.module_name.c_str());


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

      // We either have a wildcard or an unresolved library
      if (q->has_library && (contains_glob_chars (q->path)
			     || q->path.find('/') == string::npos))
        // handle .library(GLOB)
        q->dw.iterate_over_libraries (&q->query_library_callback, q);
      // .plt is translated to .plt.statement(N).  We only want to iterate for the
      // .plt case
      else if (q->has_plt && ! q->has_statement)
        q->dw.iterate_over_plt (q, &q->query_plt_callback);
      else
        // search the module for matches of the probe point.
        q->handle_query_module();

      // If we know that there will be no more matches, abort early.
      if (q->dw.module_name_final_match(q->module_val) || pending_interrupts)
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
base_query::query_library_callback (void *q, const char *data)
{
  base_query *me = (base_query*)q;
  me->query_library (data);
}


void
query_one_library (const char *library, dwflpp & dw,
    const string user_lib, probe * base_probe, probe_point *base_loc,
    vector<derived_probe *> & results)
{
  if (dw.function_name_matches_pattern(library, "*" + user_lib))
    {
      string library_path = find_executable (library, "", dw.sess.sysenv,
                                             "LD_LIBRARY_PATH");
      probe_point* specific_loc = new probe_point(*base_loc);
      specific_loc->optional = true;
      vector<probe_point::component*> derived_comps;

      vector<probe_point::component*>::iterator it;
      for (it = specific_loc->components.begin();
          it != specific_loc->components.end(); ++it)
        if ((*it)->functor == TOK_LIBRARY)
          derived_comps.push_back(new probe_point::component(TOK_LIBRARY,
              new literal_string(library_path)));
        else
          derived_comps.push_back(*it);
      probe_point* derived_loc = new probe_point(*specific_loc);
      derived_loc->components = derived_comps;
      probe *new_base = base_probe->create_alias(derived_loc, specific_loc);
      derive_probes(dw.sess, new_base, results);
      if (dw.sess.verbose > 2)
        clog << _("module=") << library_path;
    }
}


void
dwarf_query::query_library (const char *library)
{
  query_one_library (library, dw, user_lib, base_probe, base_loc, results);
}

struct plt_expanding_visitor: public var_expanding_visitor
{
  plt_expanding_visitor(const string & entry):
    entry (entry)
  {
  }
  const string & entry;

  void visit_target_symbol (target_symbol* e);
};


void
base_query::query_plt_callback (void *q, const char *entry, size_t address)
{
  base_query *me = (base_query*)q;
  if (me->dw.function_name_matches_pattern (entry, me->plt_val))
    me->query_plt (entry, address);
}


void
query_one_plt (const char *entry, long addr, dwflpp & dw,
    probe * base_probe, probe_point *base_loc,
    vector<derived_probe *> & results)
{
      probe_point* specific_loc = new probe_point(*base_loc);
      specific_loc->optional = true;
      vector<probe_point::component*> derived_comps;

      if (dw.sess.verbose > 2)
        clog << _F("plt entry=%s\n", entry);

      // query_module_symtab requires .plt to recognize that it can set the probe at
      // a plt entry so we convert process.plt to process.plt.statement
      vector<probe_point::component*>::iterator it;
      for (it = specific_loc->components.begin();
          it != specific_loc->components.end(); ++it)
        if ((*it)->functor == TOK_PLT)
          {
            derived_comps.push_back(*it);
            derived_comps.push_back(new probe_point::component(TOK_STATEMENT,
                new literal_number(addr)));
          }
        else
          derived_comps.push_back(*it);
      probe_point* derived_loc = new probe_point(*specific_loc);
      derived_loc->components = derived_comps;
      probe *new_base = base_probe->create_alias(derived_loc, specific_loc);
      string e = string(entry);
      plt_expanding_visitor pltv (e);
      pltv.replace (new_base->body);
      derive_probes(dw.sess, new_base, results);
}


void
dwarf_query::query_plt (const char *entry, size_t address)
{
  query_one_plt (entry, address, dw, base_probe, base_loc, results);
}

// This would more naturally fit into elaborate.cxx:semantic_pass_symbols,
// but the needed declaration for module_cache is not available there.
// Nor for that matter in session.cxx.  Only in this CU is that field ever
// set (in query_module() above), so we clean it up here too.
static void
delete_session_module_cache (systemtap_session& s)
{
  if (s.module_cache) {
    if (s.verbose > 3)
      clog << _("deleting module_cache") << endl;
    delete s.module_cache;
    s.module_cache = 0;
  }
}


struct dwarf_var_expanding_visitor: public var_expanding_visitor
{
  dwarf_query & q;
  Dwarf_Die *scope_die;
  Dwarf_Addr addr;
  block *add_block;
  block *add_call_probe; // synthesized from .return probes with saved $vars
  bool add_block_tid, add_call_probe_tid;
  unsigned saved_longs, saved_strings; // data saved within kretprobes
  map<std::string, expression *> return_ts_map;
  vector<Dwarf_Die> scopes;
  bool visited;

  dwarf_var_expanding_visitor(dwarf_query & q, Dwarf_Die *sd, Dwarf_Addr a):
    q(q), scope_die(sd), addr(a), add_block(NULL), add_call_probe(NULL),
    add_block_tid(false), add_call_probe_tid(false),
    saved_longs(0), saved_strings(0), visited(false) {}
  expression* gen_mapped_saved_return(expression* e, const string& name);
  expression* gen_kretprobe_saved_return(expression* e);
  void visit_target_symbol_saved_return (target_symbol* e);
  void visit_target_symbol_context (target_symbol* e);
  void visit_target_symbol (target_symbol* e);
  void visit_cast_op (cast_op* e);
  void visit_entry_op (entry_op* e);
private:
  vector<Dwarf_Die>& getcuscope(target_symbol *e);
  vector<Dwarf_Die>& getscopes(target_symbol *e);
};


unsigned var_expanding_visitor::tick = 0;


var_expanding_visitor::var_expanding_visitor (): op()
{
  // FIXME: for the time being, by default we only support plain '$foo
  // = bar', not '+=' or any other op= variant. This is fixable, but a
  // bit ugly.
  //
  // If derived classes desire to add additional operator support, add
  // new operators to this list in the derived class constructor.
  valid_ops.insert ("=");
}


bool
var_expanding_visitor::rewrite_lvalue(const token* tok, const std::string& eop,
                                      expression*& lvalue, expression*& rvalue)
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

  // Let visit_target_symbol know what operator it should handle.
  const string* old_op = op;
  op = &eop;

  target_symbol_setter_functioncalls.push (&fcall);
  replace (lvalue);
  target_symbol_setter_functioncalls.pop ();
  replace (rvalue);

  op = old_op;

  if (fcall != NULL)
    {
      // Our left child is informing us that it was a target variable
      // and it has been replaced with a set_target_foo() function
      // call; we are going to provide that function call -- with the
      // right child spliced in as sole argument -- in place of
      // ourselves, in the var expansion we're in the middle of making.

      if (valid_ops.find (eop) == valid_ops.end ())
        {
	  // Build up a list of supported operators.
	  string ops;
	  std::set<string>::iterator i;
          int valid_ops_size = 0;
	  for (i = valid_ops.begin(); i != valid_ops.end(); i++)
          {
	    ops += " " + *i + ",";
            valid_ops_size++;
          }
	  ops.resize(ops.size() - 1);	// chop off the last ','

	  // Throw the error.
	  throw semantic_error (_F(ngettext("Only the following assign operator is implemented on target variables: %s",
                                            "Only the following assign operators are implemented on target variables: %s",
                                           valid_ops_size), ops.c_str()), tok);

	}

      assert (lvalue == fcall);
      if (rvalue)
        fcall->args.push_back (rvalue);
      provide (fcall);
      return true;
    }
  else
    return false;
}


void
var_expanding_visitor::visit_assignment (assignment* e)
{
  if (!rewrite_lvalue (e->tok, e->op, e->left, e->right))
    provide (e);
}


void
var_expanding_visitor::visit_pre_crement (pre_crement* e)
{
  expression *dummy = NULL;
  if (!rewrite_lvalue (e->tok, e->op, e->operand, dummy))
    provide (e);
}


void
var_expanding_visitor::visit_post_crement (post_crement* e)
{
  expression *dummy = NULL;
  if (!rewrite_lvalue (e->tok, e->op, e->operand, dummy))
    provide (e);
}


void
var_expanding_visitor::visit_delete_statement (delete_statement* s)
{
  string fakeop = "delete";
  expression *dummy = NULL;
  if (!rewrite_lvalue (s->tok, fakeop, s->value, dummy))
    provide (s);
}


void
var_expanding_visitor::visit_defined_op (defined_op* e)
{
  bool resolved = true;

  defined_ops.push (e);
  try {
    // NB: provide<>/require<> are NOT typesafe.  So even though a defined_op is
    // defined with a target_symbol* operand, a subsidiary call may attempt to
    // rewrite it to a general expression* instead, and require<> happily
    // casts to/from void*, causing possible memory corruption.  We use
    // expression* here, being the general case of rewritten $variable.
    expression *foo1 = e->operand;
    foo1 = require (foo1);

    // NB: Formerly, we had some curious cases to consider here, depending on what
    // various visit_target_symbol() implementations do for successful or
    // erroneous resolutions.  Some would signal a visit_target_symbol failure
    // with an exception, with a set flag within the target_symbol, or nothing
    // at all.
    //
    // Now, failures always have to be signalled with a
    // saved_conversion_error being chained to the target_symbol.
    // Successes have to result in an attempted rewrite of the
    // target_symbol (via provide()).
    //
    // Edna Mode: "no capes".  fche: "no exceptions".

    // dwarf stuff: success: rewrites to a function; failure: retains target_symbol, sets saved_conversion_error
    //
    // sdt-kprobes sdt.h: success: string or functioncall; failure: semantic_error
    //
    // sdt-uprobes: success: string or no op; failure: no op; expect derived/synthetic
    //              dwarf probe to take care of it.
    //              But this is rather unhelpful.  So we rig the sdt_var_expanding_visitor
    //              to pass through @defined() to the synthetic dwarf probe.
    //
    // utrace: success: rewrites to function; failure: semantic_error
    //
    // procfs: success: rewrites to function; failure: semantic_error

    target_symbol* foo2 = dynamic_cast<target_symbol*> (foo1);
    if (foo2 && foo2->saved_conversion_error) // failing
      resolved = false;
    else if (foo2) // unresolved but not marked failing
      {
        // There are some visitors that won't touch certain target_symbols,
        // e.g. dwarf_var_expanding_visitor won't resolve @cast.  We should
        // leave it for now so some other visitor can have a chance.
        e->operand = foo2;
        provide (e);
        return;
      }
    else // resolved, rewritten to some other expression type
      resolved = true;
  } catch (const semantic_error& e) {
    assert (0); // should not happen
  }
  defined_ops.pop ();

  literal_number* ln = new literal_number (resolved ? 1 : 0);
  ln->tok = e->tok;
  provide (ln);
}


struct dwarf_pretty_print
{
  dwarf_pretty_print (dwflpp& dw, vector<Dwarf_Die>& scopes, Dwarf_Addr pc,
                      const string& local, bool userspace_p,
                      const target_symbol& e):
    dw(dw), local(local), scopes(scopes), pc(pc), pointer(NULL),
    userspace_p(userspace_p), deref_p(true)
  {
    init_ts (e);
    dw.type_die_for_local (scopes, pc, local, ts, &base_type);
  }

  dwarf_pretty_print (dwflpp& dw, Dwarf_Die *scope_die, Dwarf_Addr pc,
                      bool userspace_p, const target_symbol& e):
    dw(dw), scopes(1, *scope_die), pc(pc), pointer(NULL),
    userspace_p(userspace_p), deref_p(true)
  {
    init_ts (e);
    dw.type_die_for_return (&scopes[0], pc, ts, &base_type);
  }

  dwarf_pretty_print (dwflpp& dw, Dwarf_Die *type_die, expression* pointer,
                      bool deref_p, bool userspace_p, const target_symbol& e):
    dw(dw), pc(0), pointer(pointer), pointer_type(*type_die),
    userspace_p(userspace_p), deref_p(deref_p)
  {
    init_ts (e);
    dw.type_die_for_pointer (type_die, ts, &base_type);
  }

  functioncall* expand ();
  ~dwarf_pretty_print () { delete ts; }

private:
  dwflpp& dw;
  target_symbol* ts;
  bool print_full;
  Dwarf_Die base_type;

  string local;
  vector<Dwarf_Die> scopes;
  Dwarf_Addr pc;

  expression* pointer;
  Dwarf_Die pointer_type;

  const bool userspace_p, deref_p;

  void recurse (Dwarf_Die* type, target_symbol* e,
                print_format* pf, bool top=false);
  void recurse_bitfield (Dwarf_Die* type, target_symbol* e,
                         print_format* pf);
  void recurse_base (Dwarf_Die* type, target_symbol* e,
                     print_format* pf);
  void recurse_array (Dwarf_Die* type, target_symbol* e,
                      print_format* pf, bool top);
  void recurse_pointer (Dwarf_Die* type, target_symbol* e,
                        print_format* pf, bool top);
  void recurse_struct (Dwarf_Die* type, target_symbol* e,
                       print_format* pf, bool top);
  void recurse_struct_members (Dwarf_Die* type, target_symbol* e,
                               print_format* pf, int& count);
  bool print_chars (Dwarf_Die* type, target_symbol* e, print_format* pf);

  void init_ts (const target_symbol& e);
  expression* deref (target_symbol* e);
  bool push_deref (print_format* pf, const string& fmt, target_symbol* e);
};


void
dwarf_pretty_print::init_ts (const target_symbol& e)
{
  // Work with a new target_symbol so we can modify arguments
  ts = new target_symbol (e);

  if (ts->addressof)
    throw semantic_error(_("cannot take address of pretty-printed variable"), ts->tok);

  if (ts->components.empty() ||
      ts->components.back().type != target_symbol::comp_pretty_print)
    throw semantic_error(_("invalid target_symbol for pretty-print"), ts->tok);
  print_full = ts->components.back().member.length() > 1;
  ts->components.pop_back();
}


functioncall*
dwarf_pretty_print::expand ()
{
  static unsigned tick = 0;

  // function pretty_print_X([pointer], [arg1, arg2, ...]) {
  //   try {
  //     return sprintf("{.foo=...}", (ts)->foo, ...)
  //   } catch {
  //     return "ERROR"
  //   }
  // }

  // Create the function decl and call.

  functiondecl *fdecl = new functiondecl;
  fdecl->tok = ts->tok;
  fdecl->synthetic = true;
  fdecl->name = "_dwarf_pretty_print_" + lex_cast(tick++);
  fdecl->type = pe_string;

  functioncall* fcall = new functioncall;
  fcall->tok = ts->tok;
  fcall->function = fdecl->name;
  fcall->type = pe_string;

  // If there's a <pointer>, replace it with a new var and make that
  // the first function argument.
  if (pointer)
    {
      vardecl *v = new vardecl;
      v->type = pe_long;
      v->name = "pointer";
      v->tok = ts->tok;
      fdecl->formal_args.push_back (v);
      fcall->args.push_back (pointer);

      symbol* sym = new symbol;
      sym->tok = ts->tok;
      sym->name = v->name;
      pointer = sym;
    }

  // For each expression argument, replace it with a function argument.
  for (unsigned i = 0; i < ts->components.size(); ++i)
    if (ts->components[i].type == target_symbol::comp_expression_array_index)
      {
        vardecl *v = new vardecl;
        v->type = pe_long;
        v->name = "index" + lex_cast(i);
        v->tok = ts->tok;
        fdecl->formal_args.push_back (v);
        fcall->args.push_back (ts->components[i].expr_index);

        symbol* sym = new symbol;
        sym->tok = ts->tok;
        sym->name = v->name;
        ts->components[i].expr_index = sym;
      }

  // Create the return sprintf.
  token* pf_tok = new token(*ts->tok);
  pf_tok->content = "sprintf";
  print_format* pf = print_format::create(pf_tok);
  return_statement* rs = new return_statement;
  rs->tok = ts->tok;
  rs->value = pf;

  // Recurse into the actual values.
  recurse (&base_type, ts, pf, true);
  pf->components = print_format::string_to_components(pf->raw_components);

  // Create the try-catch net
  try_block* tb = new try_block;
  tb->tok = ts->tok;
  tb->try_block = rs;
  tb->catch_error_var = 0;
  return_statement* rs2 = new return_statement;
  rs2->tok = ts->tok;
  rs2->value = new literal_string ("ERROR");
  rs2->value->tok = ts->tok;
  tb->catch_block = rs2;
  fdecl->body = tb;

  fdecl->join (dw.sess);
  return fcall;
}


void
dwarf_pretty_print::recurse (Dwarf_Die* start_type, target_symbol* e,
                             print_format* pf, bool top)
{
  Dwarf_Die type;
  dw.resolve_unqualified_inner_typedie (start_type, &type, e);

  switch (dwarf_tag(&type))
    {
    default:
      // XXX need a warning?
      // throw semantic_error ("unsupported type (tag " + lex_cast(dwarf_tag(&type))
      //                       + ") for " + dwarf_type_name(&type), e->tok);
      pf->raw_components.append("?");
      break;

    case DW_TAG_enumeration_type:
    case DW_TAG_base_type:
      recurse_base (&type, e, pf);
      break;

    case DW_TAG_array_type:
      recurse_array (&type, e, pf, top);
      break;

    case DW_TAG_pointer_type:
    case DW_TAG_reference_type:
    case DW_TAG_rvalue_reference_type:
      recurse_pointer (&type, e, pf, top);
      break;

    case DW_TAG_subroutine_type:
      push_deref (pf, "<function>:%p", e);
      break;

    case DW_TAG_union_type:
    case DW_TAG_structure_type:
    case DW_TAG_class_type:
      recurse_struct (&type, e, pf, top);
      break;
    }
}


// Bit fields are handled as a special-case combination of recurse() and
// recurse_base(), only called from recurse_struct_members().  The main
// difference is that the value is always printed numerically, even if the
// underlying type is a char.
void
dwarf_pretty_print::recurse_bitfield (Dwarf_Die* start_type, target_symbol* e,
                                      print_format* pf)
{
  Dwarf_Die type;
  dw.resolve_unqualified_inner_typedie (start_type, &type, e);

  int tag = dwarf_tag(&type);
  if (tag != DW_TAG_base_type && tag != DW_TAG_enumeration_type)
    {
      // XXX need a warning?
      // throw semantic_error ("unsupported bitfield type (tag " + lex_cast(tag)
      //                       + ") for " + dwarf_type_name(&type), e->tok);
      pf->raw_components.append("?");
      return;
    }

  Dwarf_Attribute attr;
  Dwarf_Word encoding = (Dwarf_Word) -1;
  dwarf_formudata (dwarf_attr_integrate (&type, DW_AT_encoding, &attr),
                   &encoding);
  switch (encoding)
    {
    case DW_ATE_float:
    case DW_ATE_complex_float:
      // XXX need a warning?
      // throw semantic_error ("unsupported bitfield type (encoding " + lex_cast(encoding)
      //                       + ") for " + dwarf_type_name(&type), e->tok);
      pf->raw_components.append("?");
      break;

    case DW_ATE_unsigned:
    case DW_ATE_unsigned_char:
      push_deref (pf, "%u", e);
      break;

    case DW_ATE_signed:
    case DW_ATE_signed_char:
    default:
      push_deref (pf, "%i", e);
      break;
    }
}


void
dwarf_pretty_print::recurse_base (Dwarf_Die* type, target_symbol* e,
                                  print_format* pf)
{
  Dwarf_Attribute attr;
  Dwarf_Word encoding = (Dwarf_Word) -1;
  dwarf_formudata (dwarf_attr_integrate (type, DW_AT_encoding, &attr),
                   &encoding);
  switch (encoding)
    {
    case DW_ATE_float:
    case DW_ATE_complex_float:
      // XXX need a warning?
      // throw semantic_error ("unsupported type (encoding " + lex_cast(encoding)
      //                       + ") for " + dwarf_type_name(type), e->tok);
      pf->raw_components.append("?");
      break;

    case DW_ATE_signed_char:
    case DW_ATE_unsigned_char:
      // Use escapes to make sure that non-printable characters
      // don't interrupt our stream (especially '\0' values).
      push_deref (pf, "'%#c'", e);
      break;

    case DW_ATE_unsigned:
      push_deref (pf, "%u", e);
      break;

    case DW_ATE_signed:
    default:
      push_deref (pf, "%i", e);
      break;
    }
}


void
dwarf_pretty_print::recurse_array (Dwarf_Die* type, target_symbol* e,
                                   print_format* pf, bool top)
{
  if (!top && !print_full)
    {
      pf->raw_components.append("[...]");
      return;
    }

  Dwarf_Die childtype;
  dwarf_attr_die (type, DW_AT_type, &childtype);

  if (print_chars (&childtype, e, pf))
    return;

  pf->raw_components.append("[");

  // We print the array up to the first 5 elements.
  // XXX how can we determine the array size?
  // ... for now, just print the first element
  // NB: limit to 32 args; see PR10750 and c_unparser::visit_print_format.
  unsigned i, size = 1;
  for (i=0; i < size && i < 5 && pf->args.size() < 32; ++i)
    {
      if (i > 0)
        pf->raw_components.append(", ");
      target_symbol* e2 = new target_symbol(*e);
      e2->components.push_back (target_symbol::component(e->tok, i));
      recurse (&childtype, e2, pf);
    }
  if (i < size || 1/*XXX until real size is known */)
    pf->raw_components.append(", ...");
  pf->raw_components.append("]");
}


void
dwarf_pretty_print::recurse_pointer (Dwarf_Die* type, target_symbol* e,
                                     print_format* pf, bool top)
{
  // We chase to top-level pointers, but leave the rest alone
  bool void_p = true;
  Dwarf_Die pointee;
  if (dwarf_attr_die (type, DW_AT_type, &pointee))
    {
      try
        {
          dw.resolve_unqualified_inner_typedie (&pointee, &pointee, e);
          void_p = false;
        }
      catch (const semantic_error&) {}
    }

  if (!void_p)
    {
      if (print_chars (&pointee, e, pf))
        return;

      if (top)
        {
          recurse (&pointee, e, pf, top);
          return;
        }
    }

  push_deref (pf, "%p", e);
}


void
dwarf_pretty_print::recurse_struct (Dwarf_Die* type, target_symbol* e,
                                    print_format* pf, bool top)
{
  if (dwarf_hasattr(type, DW_AT_declaration))
    {
      Dwarf_Die *resolved = dw.declaration_resolve(type);
      if (!resolved)
        {
          // could be an error, but for now just stub it
          // throw semantic_error ("unresolved " + dwarf_type_name(type), e->tok);
          pf->raw_components.append("{...}");
          return;
        }
      type = resolved;
    }

  int count = 0;
  pf->raw_components.append("{");
  if (top || print_full)
    recurse_struct_members (type, e, pf, count);
  else
    pf->raw_components.append("...");
  pf->raw_components.append("}");
}


void
dwarf_pretty_print::recurse_struct_members (Dwarf_Die* type, target_symbol* e,
                                            print_format* pf, int& count)
{
  /* With inheritance, a subclass may mask member names of parent classes, so
   * our search among the inheritance tree must be breadth-first rather than
   * depth-first (recursive).  The type die is still our starting point.  When
   * we encounter a masked name, just skip it. */
  set<string> dupes;
  deque<Dwarf_Die> inheritees(1, *type);
  for (; !inheritees.empty(); inheritees.pop_front())
    {
      Dwarf_Die child, childtype;
      if (dwarf_child (&inheritees.front(), &child) == 0)
        do
          {
            target_symbol* e2 = e;

            // skip static members
            if (dwarf_hasattr(&child, DW_AT_declaration))
              continue;

            int tag = dwarf_tag (&child);

            if (tag != DW_TAG_member && tag != DW_TAG_inheritance)
              continue;

            dwarf_attr_die (&child, DW_AT_type, &childtype);

            if (tag == DW_TAG_inheritance)
              {
                inheritees.push_back(childtype);
                continue;
              }

            int childtag = dwarf_tag (&childtype);
            const char *member = dwarf_diename (&child);

            // "_vptr.foo" members are C++ virtual function tables,
            // which (generally?) aren't interesting for users.
            if (member && startswith(member, "_vptr."))
              continue;

            // skip inheritance-masked duplicates
            if (member && !dupes.insert(member).second)
              continue;

            if (++count > 1)
              pf->raw_components.append(", ");

            // NB: limit to 32 args; see PR10750 and c_unparser::visit_print_format.
            if (pf->args.size() >= 32)
              {
                pf->raw_components.append("...");
                break;
              }

            if (member)
              {
                pf->raw_components.append(".");
                pf->raw_components.append(member);

                e2 = new target_symbol(*e);
                e2->components.push_back (target_symbol::component(e->tok, member));
              }
            else if (childtag == DW_TAG_union_type)
              pf->raw_components.append("<union>");
            else if (childtag == DW_TAG_structure_type)
              pf->raw_components.append("<class>");
            else if (childtag == DW_TAG_class_type)
              pf->raw_components.append("<struct>");
            pf->raw_components.append("=");

            if (dwarf_hasattr_integrate (&child, DW_AT_bit_offset))
              recurse_bitfield (&childtype, e2, pf);
            else
              recurse (&childtype, e2, pf);
          }
        while (dwarf_siblingof (&child, &child) == 0);
    }
}


bool
dwarf_pretty_print::print_chars (Dwarf_Die* start_type, target_symbol* e,
                                 print_format* pf)
{
  Dwarf_Die type;
  dw.resolve_unqualified_inner_typedie (start_type, &type, e);
  const char *name = dwarf_diename (&type);
  if (name && (name == string("char") || name == string("unsigned char")))
    {
      if (push_deref (pf, "\"%s\"", e))
        {
          // steal the last arg for a string access
          assert (!pf->args.empty());
          functioncall* fcall = new functioncall;
          fcall->tok = e->tok;
          fcall->function = userspace_p ? "user_string2" : "kernel_string2";
          fcall->args.push_back (pf->args.back());
          expression *err_msg = new literal_string ("<unknown>");
          err_msg->tok = e->tok;
          fcall->args.push_back (err_msg);
          pf->args.back() = fcall;
        }
      return true;
    }
  return false;
}

// PR10601: adapt to kernel-vs-userspace loc2c-runtime
static const string EMBEDDED_FETCH_DEREF_KERNEL = string("\n")
  + "#define fetch_register k_fetch_register\n"
  + "#define store_register k_store_register\n"
  + "#define deref kderef\n"
  + "#define store_deref store_kderef\n";

static const string EMBEDDED_FETCH_DEREF_USER = string("\n")
  + "#define fetch_register u_fetch_register\n"
  + "#define store_register u_store_register\n"
  + "#define deref uderef\n"
  + "#define store_deref store_uderef\n";

#define EMBEDDED_FETCH_DEREF(U) \
  (U ? EMBEDDED_FETCH_DEREF_USER : EMBEDDED_FETCH_DEREF_KERNEL)

static const string EMBEDDED_FETCH_DEREF_DONE = string("\n")
  + "#undef fetch_register\n"
  + "#undef store_register\n"
  + "#undef deref\n"
  + "#undef store_deref\n";

expression*
dwarf_pretty_print::deref (target_symbol* e)
{
  static unsigned tick = 0;

  if (!deref_p)
    {
      assert (pointer && e->components.empty());
      return pointer;
    }

  // Synthesize a function to dereference the dwarf fields,
  // with a pointer parameter that is the base tracepoint variable
  functiondecl *fdecl = new functiondecl;
  fdecl->synthetic = true;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;

  fdecl->name = "_dwarf_pretty_print_deref_" + lex_cast(tick++);
  fdecl->body = ec;

  // Synthesize a functioncall.
  functioncall* fcall = new functioncall;
  fcall->tok = e->tok;
  fcall->function = fdecl->name;

  ec->code += EMBEDDED_FETCH_DEREF(userspace_p);

  if (pointer)
    {
      ec->code += dw.literal_stmt_for_pointer (&pointer_type, e,
                                               false, fdecl->type);

      vardecl *v = new vardecl;
      v->type = pe_long;
      v->name = "pointer";
      v->tok = e->tok;
      fdecl->formal_args.push_back(v);
      fcall->args.push_back(pointer);
    }
  else if (!local.empty())
    ec->code += dw.literal_stmt_for_local (scopes, pc, local, e,
                                           false, fdecl->type);
  else
    ec->code += dw.literal_stmt_for_return (&scopes[0], pc, e,
                                            false, fdecl->type);

  // Any non-literal indexes need to be passed in too.
  for (unsigned i = 0; i < e->components.size(); ++i)
    if (e->components[i].type == target_symbol::comp_expression_array_index)
      {
        vardecl *v = new vardecl;
        v->type = pe_long;
        v->name = "index" + lex_cast(i);
        v->tok = e->tok;
        fdecl->formal_args.push_back(v);
        fcall->args.push_back(e->components[i].expr_index);
      }

  ec->code += "/* pure */";
  ec->code += "/* unprivileged */";

  ec->code += EMBEDDED_FETCH_DEREF_DONE;

  fdecl->join (dw.sess);
  return fcall;
}


bool
dwarf_pretty_print::push_deref (print_format* pf, const string& fmt,
                                target_symbol* e)
{
  expression* e2 = NULL;
  try
    {
      e2 = deref (e);
    }
  catch (const semantic_error&)
    {
      pf->raw_components.append ("?");
      return false;
    }
  pf->raw_components.append (fmt);
  pf->args.push_back (e2);
  return true;
}


void
dwarf_var_expanding_visitor::visit_target_symbol_saved_return (target_symbol* e)
{
  // Get the full name of the target symbol.
  stringstream ts_name_stream;
  e->print(ts_name_stream);
  string ts_name = ts_name_stream.str();

  // Check and make sure we haven't already seen this target
  // variable in this return probe.  If we have, just return our
  // last replacement.
  map<string, expression *>::iterator i = return_ts_map.find(ts_name);
  if (i != return_ts_map.end())
    {
      provide (i->second);
      return;
    }

  // Attempt the expansion directly first, so if there's a problem with the
  // variable we won't have a bogus entry probe lying around.  Like in
  // saveargs(), we pretend for a moment that we're not in a .return.
  bool saved_has_return = q.has_return;
  q.has_return = false;
  expression *repl = e;
  replace (repl);
  q.has_return = saved_has_return;
  target_symbol* n = dynamic_cast<target_symbol*>(repl);
  if (n && n->saved_conversion_error)
    {
      provide (repl);
      return;
    }

  expression *exp;
  if (!q.has_process &&
      strverscmp(q.sess.kernel_base_release.c_str(), "2.6.25") >= 0)
    exp = gen_kretprobe_saved_return(repl);
  else
    exp = gen_mapped_saved_return(repl, e->sym_name());

  // Provide the variable to our parent so it can be used as a
  // substitute for the target symbol.
  provide (exp);

  // Remember this replacement since we might be able to reuse
  // it later if the same return probe references this target
  // symbol again.
  return_ts_map[ts_name] = exp;
}

expression*
dwarf_var_expanding_visitor::gen_mapped_saved_return(expression* e,
                                                     const string& name)
{
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
                  + name
                  + "_" + lex_cast(tick++));
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
    }

  if (!add_block_tid)
    {
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
      add_block_tid = true;
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
  //   probe kernel.function("{function}").call {
  //     _dwarf_tvar_tid = tid()
  //     _dwarf_tvar_{name}_{num}[_dwarf_tvar_tid,
  //                       ++_dwarf_tvar_{name}_{num}_ctr[_dwarf_tvar_tid]]
  //       = ${param}
  //   }

  if (add_call_probe == NULL)
    {
      add_call_probe = new block;
      add_call_probe->tok = e->tok;
    }

  if (!add_call_probe_tid)
    {
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
      add_call_probe = new block(add_call_probe, es);
      add_call_probe_tid = true;
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

  add_call_probe = new block(add_call_probe, es);

  // (4) Provide the '_dwarf_tvar_{name}_{num}_tmp' variable to
  // our parent so it can be used as a substitute for the target
  // symbol.
  return tmpsym;
}


expression*
dwarf_var_expanding_visitor::gen_kretprobe_saved_return(expression* e)
{
  // The code for this is simple.
  //
  // .call:
  //   _set_kretprobe_long(index, $value)
  //
  // .return:
  //   _get_kretprobe_long(index)
  //
  // (or s/long/string/ for things like $$parms)

  unsigned index;
  string setfn, getfn;

  // We need the caller to predetermine the type of the expression!
  switch (e->type)
    {
    case pe_string:
      index = saved_strings++;
      setfn = "_set_kretprobe_string";
      getfn = "_get_kretprobe_string";
      break;
    case pe_long:
      index = saved_longs++;
      setfn = "_set_kretprobe_long";
      getfn = "_get_kretprobe_long";
      break;
    default:
      throw semantic_error(_("unknown type to save in kretprobe"), e->tok);
    }

  // Create the entry code
  //   _set_kretprobe_{long|string}(index, $value)

  if (add_call_probe == NULL)
    {
      add_call_probe = new block;
      add_call_probe->tok = e->tok;
    }

  functioncall* set_fc = new functioncall;
  set_fc->tok = e->tok;
  set_fc->function = setfn;
  set_fc->args.push_back(new literal_number(index));
  set_fc->args.back()->tok = e->tok;
  set_fc->args.push_back(e);

  expr_statement* set_es = new expr_statement;
  set_es->tok = e->tok;
  set_es->value = set_fc;

  add_call_probe->statements.push_back(set_es);

  // Create the return code
  //   _get_kretprobe_{long|string}(index)

  functioncall* get_fc = new functioncall;
  get_fc->tok = e->tok;
  get_fc->function = getfn;
  get_fc->args.push_back(new literal_number(index));
  get_fc->args.back()->tok = e->tok;

  return get_fc;
}


void
dwarf_var_expanding_visitor::visit_target_symbol_context (target_symbol* e)
{
  if (null_die(scope_die))
    return;

  target_symbol *tsym = new target_symbol(*e);

  bool pretty = (!e->components.empty() &&
                 e->components[0].type == target_symbol::comp_pretty_print);
  string format = pretty ? "=%s" : "=%#x";

  // Convert $$parms to sprintf of a list of parms and active local vars
  // which we recursively evaluate

  // NB: we synthesize a new token here rather than reusing
  // e->tok, because print_format::print likes to use
  // its tok->content.
  token* pf_tok = new token(*e->tok);
  pf_tok->type = tok_identifier;
  pf_tok->content = "sprintf";

  print_format* pf = print_format::create(pf_tok);

  if (q.has_return && (e->name == "$$return"))
    {
      tsym->name = "$return";

      // Ignore any variable that isn't accessible.
      tsym->saved_conversion_error = 0;
      expression *texp = tsym;
      replace (texp); // NB: throws nothing ...
      if (tsym->saved_conversion_error) // ... but this is how we know it happened.
        {

        }
      else
        {
          pf->raw_components += "return";
          pf->raw_components += format;
          pf->args.push_back(texp);
        }
    }
  else
    {
      // non-.return probe: support $$parms, $$vars, $$locals
      bool first = true;
      Dwarf_Die result;
      vector<Dwarf_Die> scopes = q.dw.getscopes(scope_die);
      for (unsigned i = 0; i < scopes.size(); ++i)
        {
          if (dwarf_tag(&scopes[i]) == DW_TAG_compile_unit)
            break; // we don't want file-level variables
          if (dwarf_child (&scopes[i], &result) == 0)
            do
              {
                switch (dwarf_tag (&result))
                  {
                  case DW_TAG_variable:
                    if (e->name == "$$parms")
                      continue;
                    break;
                  case DW_TAG_formal_parameter:
                    if (e->name == "$$locals")
                      continue;
                    break;

                  default:
                    continue;
                  }

                const char *diename = dwarf_diename (&result);
                if (! diename) continue;

                if (! first)
                  pf->raw_components += " ";
                pf->raw_components += diename;
                first = false;

                // Write a placeholder for ugly aggregates
                Dwarf_Die type;
                if (!pretty && dwarf_attr_die(&result, DW_AT_type, &type))
                  {
                    q.dw.resolve_unqualified_inner_typedie(&type, &type, e);
                    switch (dwarf_tag(&type))
                      {
                      case DW_TAG_union_type:
                      case DW_TAG_structure_type:
                      case DW_TAG_class_type:
                        pf->raw_components += "={...}";
                        continue;

                      case DW_TAG_array_type:
                        pf->raw_components += "=[...]";
                        continue;
                      }
                  }

                tsym->name = "$";
                tsym->name += diename;

                // Ignore any variable that isn't accessible.
                tsym->saved_conversion_error = 0;
                expression *texp = tsym;
                replace (texp); // NB: throws nothing ...
                if (tsym->saved_conversion_error) // ... but this is how we know it happened.
                  {
                    if (q.sess.verbose>2)
                      {
                        for (const semantic_error *c = tsym->saved_conversion_error;
                             c != 0;
                             c = c->chain) {
                            clog << _("variable location problem: ") << c->what() << endl;
                        }
                      }

                    pf->raw_components += "=?";
                  }
                else
                  {
                    pf->raw_components += format;
                    pf->args.push_back(texp);
                  }
              }
            while (dwarf_siblingof (&result, &result) == 0);
        }
    }

  pf->components = print_format::string_to_components(pf->raw_components);
  pf->type = pe_string;
  provide (pf);
}


void
dwarf_var_expanding_visitor::visit_target_symbol (target_symbol *e)
{
  assert(e->name.size() > 0
	 && ((e->name[0] == '$' && e->target_name == "")
	      || (e->name == "@var" && e->target_name != "")));
  visited = true;
  bool defined_being_checked = (defined_ops.size() > 0 && (defined_ops.top()->operand == e));
  // In this mode, we avoid hiding errors or generating extra code such as for .return saved $vars

  try
    {
      bool lvalue = is_active_lvalue(e);
      if (lvalue && !q.sess.guru_mode)
        throw semantic_error(_("write to target variable not permitted; need stap -g"), e->tok);

      // XXX: process $context vars should be writable

      // See if we need to generate a new probe to save/access function
      // parameters from a return probe.  PR 1382.
      if (q.has_return
          && !defined_being_checked
          && e->name != "$return" // not the special return-value variable handled below
          && e->name != "$$return") // nor the other special variable handled below
        {
          if (lvalue)
            throw semantic_error(_("write to target variable not permitted in .return probes"), e->tok);
          visit_target_symbol_saved_return(e);
          return;
        }

      if (e->name == "$$vars" || e->name == "$$parms" || e->name == "$$locals"
          || (q.has_return && (e->name == "$$return")))
        {
          if (lvalue)
            throw semantic_error(_("cannot write to context variable"), e->tok);

          if (e->addressof)
            throw semantic_error(_("cannot take address of context variable"), e->tok);

          e->assert_no_components("dwarf", true);

          visit_target_symbol_context(e);
          return;
        }

      if (!e->components.empty() &&
          e->components.back().type == target_symbol::comp_pretty_print)
        {
          if (lvalue)
            throw semantic_error(_("cannot write to pretty-printed variable"), e->tok);

          if (q.has_return && (e->name == "$return"))
            {
              dwarf_pretty_print dpp (q.dw, scope_die, addr,
                                      q.has_process, *e);
              dpp.expand()->visit(this);
            }
          else
            {
              dwarf_pretty_print dpp (q.dw, getscopes(e), addr,
                                      e->sym_name(),
                                      q.has_process, *e);
              dpp.expand()->visit(this);
            }
          return;
        }

      // Synthesize a function.
      functiondecl *fdecl = new functiondecl;
      fdecl->synthetic = true;
      fdecl->tok = e->tok;
      embeddedcode *ec = new embeddedcode;
      ec->tok = e->tok;

      string fname = (string(lvalue ? "_dwarf_tvar_set" : "_dwarf_tvar_get")
                      + "_" + e->sym_name()
                      + "_" + lex_cast(tick++));

      ec->code += EMBEDDED_FETCH_DEREF(q.has_process);

      if (q.has_return && (e->name == "$return"))
        {
	  ec->code += q.dw.literal_stmt_for_return (scope_die,
						   addr,
						   e,
						   lvalue,
						   fdecl->type);
	}
      else
        {
	  ec->code += q.dw.literal_stmt_for_local (getscopes(e),
						  addr,
						  e->sym_name(),
						  e,
						  lvalue,
						  fdecl->type);
	}

      if (! lvalue)
        ec->code += "/* pure */";

      ec->code += "/* unprivileged */";
      ec->code += EMBEDDED_FETCH_DEREF_DONE;

      fdecl->name = fname;
      fdecl->body = ec;

      // Any non-literal indexes need to be passed in too.
      for (unsigned i = 0; i < e->components.size(); ++i)
        if (e->components[i].type == target_symbol::comp_expression_array_index)
          {
            vardecl *v = new vardecl;
            v->type = pe_long;
            v->name = "index" + lex_cast(i);
            v->tok = e->tok;
            fdecl->formal_args.push_back(v);
          }

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
      fdecl->join (q.sess);

      // Synthesize a functioncall.
      functioncall* n = new functioncall;
      n->tok = e->tok;
      n->function = fname;
      n->type = fdecl->type;

      // Any non-literal indexes need to be passed in too.
      for (unsigned i = 0; i < e->components.size(); ++i)
        if (e->components[i].type == target_symbol::comp_expression_array_index)
          n->args.push_back(require(e->components[i].expr_index));

      if (lvalue)
        {
          // Provide the functioncall to our parent, so that it can be
          // used to substitute for the assignment node immediately above
          // us.
          assert(!target_symbol_setter_functioncalls.empty());
          *(target_symbol_setter_functioncalls.top()) = n;
        }

      provide (n);
    }
  catch (const semantic_error& er)
    {
      // We suppress this error message, and pass the unresolved
      // target_symbol to the next pass.  We hope that this value ends
      // up not being referenced after all, so it can be optimized out
      // quietly.
      e->chain (er);
      provide (e);
    }
}


void
dwarf_var_expanding_visitor::visit_cast_op (cast_op *e)
{
  // Fill in our current module context if needed
  if (e->module.empty())
    e->module = q.dw.module_name;

  var_expanding_visitor::visit_cast_op(e);
}


void
dwarf_var_expanding_visitor::visit_entry_op (entry_op *e)
{
  expression *repl = e;
  if (q.has_return)
    {
      // expand the operand as if it weren't a return probe
      q.has_return = false;
      replace (e->operand);
      q.has_return = true;

      // XXX it would be nice to use gen_kretprobe_saved_return when available,
      // but it requires knowing the types already, which is problematic for
      // arbitrary expressons.
      repl = gen_mapped_saved_return (e->operand, "entry");
    }
  provide (repl);
}

vector<Dwarf_Die>&
dwarf_var_expanding_visitor::getcuscope(target_symbol *e)
{
  Dwarf_Off cu_off = 0;
  const char *cu_name = NULL;

  string prefixed_srcfile = string("*/") + e->cu_name;

  Dwarf_Off off = 0;
  size_t cuhl;
  Dwarf_Off noff;
  Dwarf_Off module_bias;
  Dwarf *dw = dwfl_module_getdwarf(q.dw.module, &module_bias);
  while (dwarf_nextcu (dw, off, &noff, &cuhl, NULL, NULL, NULL) == 0)
    {
      Dwarf_Die die_mem;
      Dwarf_Die *die;
      die = dwarf_offdie (dw, off + cuhl, &die_mem);
      const char *die_name = dwarf_diename (die);

      if (strcmp (die_name, e->cu_name.c_str()) == 0) // Perfect match.
	{
	  cu_name = die_name;
	  cu_off = off + cuhl;
	  break;
	}

      if (fnmatch(prefixed_srcfile.c_str(), die_name, 0) == 0)
	if (cu_name == NULL || strlen (die_name) < strlen (cu_name))
	  {
	    cu_name = die_name;
	    cu_off = off + cuhl;
	  }
      off = noff;
    }

  if (cu_name == NULL)
    throw semantic_error ("unable to find CU '" + e->cu_name + "'"
			  + " while searching for '" + e->target_name + "'",
			  e->tok);

  vector<Dwarf_Die> *cu_scope = new vector<Dwarf_Die>;
  Dwarf_Die cu_die;
  dwarf_offdie (dw, cu_off, &cu_die);
  cu_scope->push_back(cu_die);
  return *cu_scope;
}

vector<Dwarf_Die>&
dwarf_var_expanding_visitor::getscopes(target_symbol *e)
{
  // "static globals" can only be found in the top-level CU.
  if (e->name == "@var" && e->cu_name != "")
    return this->getcuscope(e);

  if (scopes.empty())
    {
      scopes = q.dw.getscopes(scope_die);
      if (scopes.empty())
        //throw semantic_error (_F("unable to find any scopes containing %d", addr), e->tok);
        //                        ((scope_die == NULL) ? "" : (string (" in ") + (dwarf_diename(scope_die) ?: "<unknown>") + "(" + (dwarf_diename(q.dw.cu) ?: "<unknown>") ")" ))
        throw semantic_error ("unable to find any scopes containing "
                              + lex_cast_hex(addr)
                              + ((scope_die == NULL) ? ""
                                 : (string (" in ")
                                    + (dwarf_diename(scope_die) ?: "<unknown>")
                                    + "(" + (dwarf_diename(q.dw.cu) ?: "<unknown>")
                                    + ")"))
                              + " while searching for local '"
                              + e->sym_name() + "'",
                              e->tok);
    }
  return scopes;
}


struct dwarf_cast_expanding_visitor: public var_expanding_visitor
{
  systemtap_session& s;
  dwarf_builder& db;

  dwarf_cast_expanding_visitor(systemtap_session& s, dwarf_builder& db):
    s(s), db(db) {}
  void visit_cast_op (cast_op* e);
  void filter_special_modules(string& module);
};


struct dwarf_cast_query : public base_query
{
  cast_op& e;
  const bool lvalue;
  const bool userspace_p;
  functioncall*& result;

  dwarf_cast_query(dwflpp& dw, const string& module, cast_op& e, bool lvalue,
                   const bool userspace_p, functioncall*& result):
    base_query(dw, module), e(e), lvalue(lvalue),
    userspace_p(userspace_p), result(result) {}

  void handle_query_module();
  void query_library (const char *) {}
  void query_plt (const char *entry, size_t addr) {}
};


void
dwarf_cast_query::handle_query_module()
{
  static unsigned tick = 0;

  if (result)
    return;

  // look for the type in any CU
  Dwarf_Die* type_die = NULL;
  if (startswith(e.type_name, "class "))
    {
      // normalize to match dwflpp::global_alias_caching_callback
      string struct_name = "struct " + e.type_name.substr(6);
      type_die = dw.declaration_resolve_other_cus(struct_name);
    }
  else
    type_die = dw.declaration_resolve_other_cus(e.type_name);

  // NB: We now index the types as "struct name"/"union name"/etc. instead of
  // just "name".  But since we didn't require users to be explicit before, and
  // actually sort of discouraged it, we must be flexible now.  So if a lookup
  // fails with a bare name, try augmenting it.
  if (!type_die &&
      !startswith(e.type_name, "class ") &&
      !startswith(e.type_name, "struct ") &&
      !startswith(e.type_name, "union ") &&
      !startswith(e.type_name, "enum "))
    {
      type_die = dw.declaration_resolve_other_cus("struct " + e.type_name);
      if (!type_die)
        type_die = dw.declaration_resolve_other_cus("union " + e.type_name);
      if (!type_die)
        type_die = dw.declaration_resolve_other_cus("enum " + e.type_name);
    }

  if (!type_die)
    return;

  string code;
  exp_type type = pe_long;

  try
    {
      Dwarf_Die cu_mem;
      dw.focus_on_cu(dwarf_diecu(type_die, &cu_mem, NULL, NULL));

      if (!e.components.empty() &&
          e.components.back().type == target_symbol::comp_pretty_print)
        {
          if (lvalue)
            throw semantic_error(_("cannot write to pretty-printed variable"), e.tok);

          dwarf_pretty_print dpp(dw, type_die, e.operand, true, userspace_p, e);
          result = dpp.expand();
          return;
        }

      code = dw.literal_stmt_for_pointer (type_die, &e, lvalue, type);
    }
  catch (const semantic_error& er)
    {
      // NB: we can have multiple errors, since a @cast
      // may be attempted using several different modules:
      //     @cast(ptr, "type", "module1:module2:...")
      e.chain (er);
    }

  if (code.empty())
    return;

  string fname = (string(lvalue ? "_dwarf_cast_set" : "_dwarf_cast_get")
		  + "_" + e.sym_name()
		  + "_" + lex_cast(tick++));

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->synthetic = true;
  fdecl->tok = e.tok;
  fdecl->type = type;
  fdecl->name = fname;

  embeddedcode *ec = new embeddedcode;
  ec->tok = e.tok;
  fdecl->body = ec;

  ec->code += EMBEDDED_FETCH_DEREF(userspace_p);
  ec->code += code;

  // Give the fdecl an argument for the pointer we're trying to cast
  vardecl *v1 = new vardecl;
  v1->type = pe_long;
  v1->name = "pointer";
  v1->tok = e.tok;
  fdecl->formal_args.push_back(v1);

  // Any non-literal indexes need to be passed in too.
  for (unsigned i = 0; i < e.components.size(); ++i)
    if (e.components[i].type == target_symbol::comp_expression_array_index)
      {
        vardecl *v = new vardecl;
        v->type = pe_long;
        v->name = "index" + lex_cast(i);
        v->tok = e.tok;
        fdecl->formal_args.push_back(v);
      }

  if (lvalue)
    {
      // Modify the fdecl so it carries a second pe_long formal
      // argument called "value".

      // FIXME: For the time being we only support setting target
      // variables which have base types; these are 'pe_long' in
      // stap's type vocabulary.  Strings and pointers might be
      // reasonable, some day, but not today.

      vardecl *v2 = new vardecl;
      v2->type = pe_long;
      v2->name = "value";
      v2->tok = e.tok;
      fdecl->formal_args.push_back(v2);
    }
  else
    ec->code += "/* pure */";

  ec->code += "/* unprivileged */";
  ec->code += EMBEDDED_FETCH_DEREF_DONE;

  fdecl->join (dw.sess);

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e.tok;
  n->function = fname;
  n->args.push_back(e.operand);

  // Any non-literal indexes need to be passed in too.
  for (unsigned i = 0; i < e.components.size(); ++i)
    if (e.components[i].type == target_symbol::comp_expression_array_index)
      n->args.push_back(e.components[i].expr_index);

  result = n;
}


void dwarf_cast_expanding_visitor::filter_special_modules(string& module)
{
  // look for "<path/to/header>" or "kernel<path/to/header>"
  // for those cases, build a module including that header
  if (module[module.size() - 1] == '>' &&
      (module[0] == '<' || startswith(module, "kernel<")))
    {
      string cached_module;
      if (s.use_cache)
        {
          // see if the cached module exists
          cached_module = find_typequery_hash(s, module);
          if (!cached_module.empty() && !s.poison_cache)
            {
              int fd = open(cached_module.c_str(), O_RDONLY);
              if (fd != -1)
                {
                  if (s.verbose > 2)
                    //TRANSLATORS: Here we're using a cached module.
                    clog << _("Pass 2: using cached ") << cached_module << endl;
                  module = cached_module;
                  close(fd);
                  return;
                }
            }
        }

      // no cached module, time to make it
      if (make_typequery(s, module) == 0)
        {
          // try to save typequery in the cache
          if (s.use_cache)
            copy_file(module, cached_module, s.verbose > 2);
        }
    }
}


void dwarf_cast_expanding_visitor::visit_cast_op (cast_op* e)
{
  bool lvalue = is_active_lvalue(e);
  if (lvalue && !s.guru_mode)
    throw semantic_error(_("write to @cast context variable not permitted; need stap -g"), e->tok);

  if (e->module.empty())
    e->module = "kernel"; // "*" may also be reasonable to search all kernel modules

  functioncall* result = NULL;

  // split the module string by ':' for alternatives
  vector<string> modules;
  tokenize(e->module, modules, ":");
  bool userspace_p=false; // PR10601
  for (unsigned i = 0; !result && i < modules.size(); ++i)
    {
      string& module = modules[i];
      filter_special_modules(module);

      // NB: This uses '/' to distinguish between kernel modules and userspace,
      // which means that userspace modules won't get any PATH searching.
      dwflpp* dw;
      try
	{
          userspace_p=is_user_module (module);
	  if (! userspace_p)
	    {
	      // kernel or kernel module target
	      dw = db.get_kern_dw(s, module);
	    }
	  else
	    {
              module = find_executable (module, "", s.sysenv); // canonicalize it
	      dw = db.get_user_dw(s, module);
	    }
	}
      catch (const semantic_error& er)
	{
	  /* ignore and go to the next module */
	  continue;
	}

      dwarf_cast_query q (*dw, module, *e, lvalue, userspace_p, result);
      dw->iterate_over_modules(&query_module, &q);
    }

  if (!result)
    {
      // We pass the unresolved cast_op to the next pass, and hope
      // that this value ends up not being referenced after all, so
      // it can be optimized out quietly.
      provide (e);
      return;
    }

  if (lvalue)
    {
      // Provide the functioncall to our parent, so that it can be
      // used to substitute for the assignment node immediately above
      // us.
      assert(!target_symbol_setter_functioncalls.empty());
      *(target_symbol_setter_functioncalls.top()) = result;
    }

  result->visit (this);
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
  // skip probes which are paired entry-handlers
  if (!has_return && (saved_longs || saved_strings))
    return;

  if (! s.dwarf_derived_probes)
    s.dwarf_derived_probes = new dwarf_derived_probe_group ();
  s.dwarf_derived_probes->enroll (this);
}


static bool
kernel_supports_inode_uprobes(systemtap_session& s)
{
  // The arch-supports is new to the builtin inode-uprobes, so it makes a
  // reasonable indicator of the new API.  Else we'll need an autoconf...
  // see also buildrun.cxx:kernel_built_uprobs()
  return (s.kernel_config["CONFIG_ARCH_SUPPORTS_UPROBES"] == "y"
          && s.kernel_config["CONFIG_UPROBES"] == "y");
}


void
check_process_probe_kernel_support(systemtap_session& s)
{
  // If we've got utrace, we're good to go.
  if (s.kernel_config["CONFIG_UTRACE"] == "y")
    return;

  // We don't have utrace.  For process probes that aren't
  // uprobes-based, we just need the task_finder.  The task_finder
  // needs CONFIG_TRACEPOINTS and specific tracepoints.  There is a
  // specific autoconf test for its needs.
  //
  // We'll just require CONFIG_TRACEPOINTS here as a quick-and-dirty
  // approximation.
  if (! s.need_uprobes && s.kernel_config["CONFIG_TRACEPOINTS"] == "y")
    return;

  // For uprobes-based process probes, we need the task_finder plus
  // the builtin inode-uprobes.
  if (s.need_uprobes
      && s.kernel_config["CONFIG_TRACEPOINTS"] == "y"
      && kernel_supports_inode_uprobes(s))
    return;

  throw semantic_error (_("process probes not available without kernel CONFIG_UTRACE or CONFIG_TRACEPOINTS/CONFIG_ARCH_SUPPORTS_UPROBES/CONFIG_UPROBES"));
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
  : derived_probe (q.base_probe, q.base_loc, true /* .components soon rewritten */ ),
    module (module), section (section), addr (addr),
    path (q.path),
    has_process (q.has_process),
    has_return (q.has_return),
    has_maxactive (q.has_maxactive),
    has_library (q.has_library),
    maxactive_val (q.maxactive_val),
    user_path (q.user_path),
    user_lib (q.user_lib),
    access_vars(false),
    saved_longs(0), saved_strings(0),
    entry_handler(0)
{
  if (user_lib.size() != 0)
    has_library = true;

  if (q.has_process)
    {
      // We may receive probes on two types of ELF objects: ET_EXEC or ET_DYN.
      // ET_EXEC ones need no further relocation on the addr(==dwfl_addr), whereas
      // ET_DYN ones do (addr += run-time mmap base address).  We tell these apart
      // by the incoming section value (".absolute" vs. ".dynamic").
      // XXX Assert invariants here too?

      // inode-uprobes needs an offset rather than an absolute VM address.
      if (kernel_supports_inode_uprobes(q.dw.sess) &&
          section == ".absolute" && addr == dwfl_addr &&
          addr >= q.dw.module_start && addr < q.dw.module_end)
        this->addr = addr - q.dw.module_start;
    }
  else
    {
      // Assert kernel relocation invariants
      if (section == "" && dwfl_addr != addr) // addr should be absolute
        throw semantic_error (_("missing relocation basis"), tok);
      if (section != "" && dwfl_addr == addr) // addr should be an offset
        throw semantic_error (_("inconsistent relocation address"), tok);
    }

  // XXX: hack for strange g++/gcc's
#ifndef USHRT_MAX
#define USHRT_MAX 32767
#endif

  // Range limit maxactive() value
  if (has_maxactive && (maxactive_val < 0 || maxactive_val > USHRT_MAX))
    throw semantic_error (_F("maxactive value out of range [0,%s]",
                          lex_cast(USHRT_MAX).c_str()), q.base_loc->components.front()->tok);

  // Expand target variables in the probe body
  if (!null_die(scope_die))
    {
      // XXX: user-space deref's for q.has_process!
      dwarf_var_expanding_visitor v (q, scope_die, dwfl_addr);
      v.replace (this->body);
      if (!q.has_process)
        access_vars = v.visited;

      // If during target-variable-expanding the probe, we added a new block
      // of code, add it to the start of the probe.
      if (v.add_block)
        this->body = new block(v.add_block, this->body);

      // If when target-variable-expanding the probe, we need to synthesize a
      // sibling function-entry probe.  We don't go through the whole probe derivation
      // business (PR10642) that could lead to wildcard/alias resolution, or for that
      // dwarf-induced duplication.
      if (v.add_call_probe)
        {
          assert (q.has_return && !q.has_call);

          // We temporarily replace q.base_probe.
          statement* old_body = q.base_probe->body;
          q.base_probe->body = v.add_call_probe;
          q.has_return = false;
          q.has_call = true;

          if (q.has_process)
            entry_handler = new uprobe_derived_probe (funcname, filename, line,
                                                      module, section, dwfl_addr,
                                                      addr, q, scope_die);
          else
            entry_handler = new dwarf_derived_probe (funcname, filename, line,
                                                     module, section, dwfl_addr,
                                                     addr, q, scope_die);

          saved_longs = entry_handler->saved_longs = v.saved_longs;
          saved_strings = entry_handler->saved_strings = v.saved_strings;

          q.results.push_back (entry_handler);

          q.has_return = true;
          q.has_call = false;
          q.base_probe->body = old_body;
        }
      // Save the local variables for listing mode
      if (q.sess.listing_mode_vars)
         saveargs(q, scope_die, dwfl_addr);
    }
  // else - null scope_die - $target variables will produce an error during translate phase

  // PR10820: null scope die, local variables aren't accessible, not necessary to invoke saveargs

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
          {
            retro_name += ("@" + string (filename));
            if (line > 0)
              retro_name += (":" + lex_cast (line));
          }
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
                        new literal_number(retro_addr, true)));

      if (q.has_absolute)
        comps.push_back (new probe_point::component (TOK_ABSOLUTE));
    }

  if (q.has_call)
      comps.push_back (new probe_point::component(TOK_CALL));
  if (q.has_exported)
      comps.push_back (new probe_point::component(TOK_EXPORTED));
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
dwarf_derived_probe::saveargs(dwarf_query& q, Dwarf_Die* scope_die,
                              Dwarf_Addr dwfl_addr)
{
  if (null_die(scope_die))
    return;

  bool verbose = q.sess.verbose > 2;

  if (verbose)
    clog << _F("saveargs: examining '%s' (dieoffset: %#" PRIx64 ")\n", (dwarf_diename(scope_die)?: "unknown"), dwarf_dieoffset(scope_die));

  if (has_return)
    {
      /* Only save the return value if it has a type. */
      string type_name;
      Dwarf_Die type_die;
      if (dwarf_attr_die (scope_die, DW_AT_type, &type_die) &&
          dwarf_type_name(&type_die, type_name))
        args.push_back("$return:"+type_name);

      else if (verbose)
        clog << _F("saveargs: failed to retrieve type name for return value (dieoffset: %s)\n",
                   lex_cast_hex(dwarf_dieoffset(scope_die)).c_str());
    }

  Dwarf_Die arg;
  vector<Dwarf_Die> scopes = q.dw.getscopes(scope_die);
  for (unsigned i = 0; i < scopes.size(); ++i)
    {
      if (dwarf_tag(&scopes[i]) == DW_TAG_compile_unit)
        break; // we don't want file-level variables
      if (dwarf_child (&scopes[i], &arg) == 0)
        do
          {
            switch (dwarf_tag (&arg))
              {
              case DW_TAG_variable:
              case DW_TAG_formal_parameter:
                break;

              default:
                continue;
              }

            /* Ignore this local if it has no name. */
            const char *arg_name = dwarf_diename (&arg);
            if (!arg_name)
              {
                if (verbose)
                  clog << _F("saveargs: failed to retrieve name for local (dieoffset: %s)\n",
                             lex_cast_hex(dwarf_dieoffset(&arg)).c_str());
                continue;
              }

            if (verbose)
              clog << _F("saveargs: finding location for local '%s' (dieoffset: %s)\n",
                         arg_name, lex_cast_hex(dwarf_dieoffset(&arg)).c_str());

            /* Ignore this local if it has no location (or not at this PC). */
            /* NB: It still may not be directly accessible, e.g. if it is an
             * aggregate type, implicit_pointer, etc., but the user can later
             * figure out how to access the interesting parts. */
            Dwarf_Attribute attr_mem;
            if (!dwarf_attr_integrate (&arg, DW_AT_const_value, &attr_mem))
              {
                Dwarf_Op *expr;
                size_t len;
                if (!dwarf_attr_integrate (&arg, DW_AT_location, &attr_mem))
                  {
                    if (verbose)
                      clog << _F("saveargs: failed to resolve the location for local '%s' (dieoffset: %s)\n",
                                  arg_name, lex_cast_hex(dwarf_dieoffset(&arg)).c_str());
                    continue;
                  }
                else if (!(dwarf_getlocation_addr(&attr_mem, dwfl_addr, &expr,
                                                  &len, 1) == 1 && len > 0))
                  {
                    if (verbose)
                      clog << _F("saveargs: local '%s' (dieoffset: %s) is not available at this address (%s)\n",
                                 arg_name, lex_cast_hex(dwarf_dieoffset(&arg)).c_str(), lex_cast_hex(dwfl_addr).c_str());
                    continue;
                  }
              }

            /* Ignore this local if it has no type. */
            string type_name;
            Dwarf_Die type_die;
            if (!dwarf_attr_die (&arg, DW_AT_type, &type_die) ||
                !dwarf_type_name(&type_die, type_name))
              {
                if (verbose)
                  clog << _F("saveargs: failed to retrieve type name for local '%s' (dieoffset: %s)\n",
                             arg_name, lex_cast_hex(dwarf_dieoffset(&arg)).c_str());
                continue;
              }

            /* This local looks good -- save it! */
            args.push_back("$"+string(arg_name)+":"+type_name);
          }
        while (dwarf_siblingof (&arg, &arg) == 0);
    }
}


void
dwarf_derived_probe::getargs(std::list<std::string> &arg_set) const
{
  arg_set.insert(arg_set.end(), args.begin(), args.end());
}


void
dwarf_derived_probe::emit_privilege_assertion (translator_output* o)
{
  if (has_process)
    {
      // These probes are allowed for unprivileged users, but only in the
      // context of processes which they own.
      emit_process_owner_assertion (o);
      return;
    }

  // Other probes must contain the default assertion which aborts
  // if executed by an unprivileged user.
  derived_probe::emit_privilege_assertion (o);
}


void
dwarf_derived_probe::print_dupe_stamp(ostream& o)
{
  if (has_process)
    {
      // These probes are allowed for unprivileged users, but only in the
      // context of processes which they own.
      print_dupe_stamp_unprivileged_process_owner (o);
      return;
    }

  // Other probes must contain the default dupe stamp
  derived_probe::print_dupe_stamp (o);
}


void
dwarf_derived_probe::register_statement_variants(match_node * root,
						 dwarf_builder * dw,
						 privilege_t privilege)
{
  root
    ->bind_privilege(privilege)
    ->bind(dw);
}

void
dwarf_derived_probe::register_function_variants(match_node * root,
						dwarf_builder * dw,
						privilege_t privilege)
{
  root
    ->bind_privilege(privilege)
    ->bind(dw);
  root->bind(TOK_CALL)
    ->bind_privilege(privilege)
    ->bind(dw);
  root->bind(TOK_EXPORTED)
    ->bind_privilege(privilege)
    ->bind(dw);
  root->bind(TOK_RETURN)
    ->bind_privilege(privilege)
    ->bind(dw);

  // For process probes / uprobes, .maxactive() is unused.
  if (! pr_contains (privilege, pr_stapusr))
    {
      root->bind(TOK_RETURN)
        ->bind_num(TOK_MAXACTIVE)->bind(dw);
    }
}

void
dwarf_derived_probe::register_function_and_statement_variants(
  systemtap_session& s,
  match_node * root,
  dwarf_builder * dw,
  privilege_t privilege
)
{
  // Here we match 4 forms:
  //
  // .function("foo")
  // .function(0xdeadbeef)
  // .statement("foo")
  // .statement(0xdeadbeef)

  match_node *fv_root = root->bind_str(TOK_FUNCTION);
  register_function_variants(fv_root, dw, privilege);
  // ROOT.function("STRING") always gets the .inline and .label variants.
  fv_root->bind(TOK_INLINE)
    ->bind_privilege(privilege)
    ->bind(dw);
  fv_root->bind_str(TOK_LABEL)
    ->bind_privilege(privilege)
    ->bind(dw);

  fv_root = root->bind_num(TOK_FUNCTION);
  register_function_variants(fv_root, dw, privilege);
  // ROOT.function(NUMBER).inline is deprecated in release 1.7 and removed thereafter.
  if (strverscmp(s.compatible.c_str(), "1.7") <= 0)
    {
      fv_root->bind(TOK_INLINE)
	->bind_privilege(privilege)
	->bind(dw);
    }

  register_statement_variants(root->bind_str(TOK_STATEMENT), dw, privilege);
  register_statement_variants(root->bind_num(TOK_STATEMENT), dw, privilege);
}

void
dwarf_derived_probe::register_sdt_variants(systemtap_session& s,
					   match_node * root,
					   dwarf_builder * dw)
{
  root->bind_str(TOK_MARK)
    ->bind_privilege(pr_all)
    ->bind(dw);
  root->bind_str(TOK_PROVIDER)->bind_str(TOK_MARK)
    ->bind_privilege(pr_all)
    ->bind(dw);
}

void
dwarf_derived_probe::register_plt_variants(systemtap_session& s,
					   match_node * root,
					   dwarf_builder * dw)
{
  root->bind(TOK_PLT)
    ->bind_privilege(pr_all)
    ->bind(dw);
  root->bind_str(TOK_PLT)
    ->bind_privilege(pr_all)
    ->bind(dw);
  root->bind(TOK_PLT)->bind_num(TOK_STATEMENT)
    ->bind_privilege(pr_all)
    ->bind(dw);
  root->bind_str(TOK_PLT)->bind_num(TOK_STATEMENT)
    ->bind_privilege(pr_all)
    ->bind(dw);
}

void
dwarf_derived_probe::register_patterns(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  dwarf_builder *dw = new dwarf_builder();

  update_visitor *filter = new dwarf_cast_expanding_visitor(s, *dw);
  s.code_filters.push_back(filter);

  register_function_and_statement_variants(s, root->bind(TOK_KERNEL), dw, pr_privileged);
  register_function_and_statement_variants(s, root->bind_str(TOK_MODULE), dw, pr_privileged);
  root->bind(TOK_KERNEL)->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)
    ->bind(dw);

  match_node* uprobes[] = {
      root->bind(TOK_PROCESS),
      root->bind_str(TOK_PROCESS),
      root->bind(TOK_PROCESS)->bind_str(TOK_LIBRARY),
      root->bind_str(TOK_PROCESS)->bind_str(TOK_LIBRARY),
  };
  for (size_t i = 0; i < sizeof(uprobes) / sizeof(*uprobes); ++i)
    {
      register_function_and_statement_variants(s, uprobes[i], dw, pr_all);
      register_sdt_variants(s, uprobes[i], dw);
      register_plt_variants(s, uprobes[i], dw);
    }
}

void
dwarf_derived_probe::emit_probe_local_init(translator_output * o)
{
  if (access_vars)
    {
      // if accessing $variables, emit bsp cache setup for speeding up
      o->newline() << "#if defined __ia64__";
      o->newline() << "bspcache(c->unwaddr, c->kregs);";
      o->newline() << "#endif";
    }
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

  s.op->newline() << "#ifndef KRETACTIVE";
  s.op->newline() << "#define KRETACTIVE (max(15,6*(int)num_possible_cpus()))";
  s.op->newline() << "#endif";

  // Forward decls
  s.op->newline() << "#include \"kprobes-common.h\"";

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
  s.op->newline() << "static struct stap_dwarf_kprobe stap_dwarf_kprobes[" << probes_by_module.size() << "];";
  // NB: bss!

  s.op->newline() << "static struct stap_dwarf_probe {";
  s.op->newline(1) << "const unsigned return_p:1;";
  s.op->newline() << "const unsigned maxactive_p:1;";
  s.op->newline() << "const unsigned optional_p:1;";
  s.op->newline() << "unsigned registered_p:1;";
  s.op->newline() << "const unsigned short maxactive_val;";

  // data saved in the kretprobe_instance packet
  s.op->newline() << "const unsigned short saved_longs;";
  s.op->newline() << "const unsigned short saved_strings;";

  // Let's find some stats for the embedded strings.  Maybe they
  // are small and uniform enough to justify putting char[MAX]'s into
  // the array instead of relocated char*'s.
  size_t module_name_max = 0, section_name_max = 0;
  size_t module_name_tot = 0, section_name_tot = 0;
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
#undef CALCIT

  s.op->newline() << "const unsigned long address;";
  s.op->newline() << "struct stap_probe * const probe;";
  s.op->newline() << "struct stap_probe * const entry_probe;";
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
      if (p->saved_longs || p->saved_strings)
        {
          if (p->saved_longs)
            s.op->line() << " .saved_longs=" << p->saved_longs << ",";
          if (p->saved_strings)
            s.op->line() << " .saved_strings=" << p->saved_strings << ",";
          if (p->entry_handler)
            s.op->line() << " .entry_probe=" << common_probe_init (p->entry_handler) << ",";
        }
      if (p->locations[0]->optional)
        s.op->line() << " .optional_p=1,";
      s.op->line() << " .address=(unsigned long)0x" << hex << p->addr << dec << "ULL,";
      s.op->line() << " .module=\"" << p->module << "\",";
      s.op->line() << " .section=\"" << p->section << "\",";
      s.op->line() << " .probe=" << common_probe_init (p) << ",";
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
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->probe",
				 "_STP_PROBE_HANDLER_KPROBE");
  s.op->newline() << "c->kregs = regs;";

  // Make it look like the IP is set as it wouldn't have been replaced
  // by a breakpoint instruction when calling real probe handler. Reset
  // IP regs on return, so we don't confuse kprobes. PR10458
  s.op->newline() << "{";
  s.op->indent(1);
  s.op->newline() << "unsigned long kprobes_ip = REG_IP(c->kregs);";
  s.op->newline() << "SET_REG_IP(regs, (unsigned long) inst->addr);";
  s.op->newline() << "(*sdp->probe->ph) (c);";
  s.op->newline() << "SET_REG_IP(regs, kprobes_ip);";
  s.op->newline(-1) << "}";

  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  // Same for kretprobes
  s.op->newline();
  s.op->newline() << "static int enter_kretprobe_common (struct kretprobe_instance *inst,";
  s.op->line() << " struct pt_regs *regs, int entry) {";
  s.op->newline(1) << "struct kretprobe *krp = inst->rp;";

  // NB: as of PR5673, the kprobe|kretprobe union struct is in BSS
  s.op->newline() << "int kprobe_idx = ((uintptr_t)krp-(uintptr_t)stap_dwarf_kprobes)/sizeof(struct stap_dwarf_kprobe);";
  // Check that the index is plausible
  s.op->newline() << "struct stap_dwarf_probe *sdp = &stap_dwarf_probes[";
  s.op->line() << "((kprobe_idx >= 0 && kprobe_idx < " << probes_by_module.size() << ")?";
  s.op->line() << "kprobe_idx:0)"; // NB: at least we avoid memory corruption
  // XXX: it would be nice to give a more verbose error though; BUG_ON later?
  s.op->line() << "];";

  s.op->newline() << "struct stap_probe *sp = entry ? sdp->entry_probe : sdp->probe;";
  s.op->newline() << "if (sp) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sp",
				 "_STP_PROBE_HANDLER_KRETPROBE");
  s.op->newline() << "c->kregs = regs;";

  // for assisting runtime's backtrace logic and accessing kretprobe data packets
  s.op->newline() << "c->ips.krp.pi = inst;";
  s.op->newline() << "c->ips.krp.pi_longs = sdp->saved_longs;";

  // Make it look like the IP is set as it wouldn't have been replaced
  // by a breakpoint instruction when calling real probe handler. Reset
  // IP regs on return, so we don't confuse kprobes. PR10458
  s.op->newline() << "{";
  s.op->newline(1) << "unsigned long kprobes_ip = REG_IP(c->kregs);";
  s.op->newline() << "if (entry)";
  s.op->newline(1) << "SET_REG_IP(regs, (unsigned long) inst->rp->kp.addr);";
  s.op->newline(-1) << "else";
  s.op->newline(1) << "SET_REG_IP(regs, (unsigned long)inst->ret_addr);";
  s.op->newline(-1) << "(sp->ph) (c);";
  s.op->newline() << "SET_REG_IP(regs, kprobes_ip);";
  s.op->newline(-1) << "}";

  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline(-1) << "}";
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  s.op->newline();
  s.op->newline() << "static int enter_kretprobe_probe (struct kretprobe_instance *inst,";
  s.op->line() << " struct pt_regs *regs) {";
  s.op->newline(1) << "return enter_kretprobe_common(inst, regs, 0);";
  s.op->newline(-1) << "}";

  s.op->newline();
  s.op->newline() << "static int enter_kretprobe_entry_probe (struct kretprobe_instance *inst,";
  s.op->line() << " struct pt_regs *regs) {";
  s.op->newline(1) << "return enter_kretprobe_common(inst, regs, 1);";
  s.op->newline(-1) << "}";

  s.op->newline();
}


void
dwarf_derived_probe_group::emit_module_init (systemtap_session& s)
{
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "struct stap_dwarf_kprobe *kp = & stap_dwarf_kprobes[i];";
  s.op->newline() << "unsigned long relocated_addr = _stp_kmodule_relocate (sdp->module, sdp->section, sdp->address);";
  s.op->newline() << "if (relocated_addr == 0) continue;"; // quietly; assume module is absent
  s.op->newline() << "probe_point = sdp->probe->pp;"; // for error messages
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline(1) << "kp->u.krp.kp.addr = (void *) relocated_addr;";
  s.op->newline() << "if (sdp->maxactive_p) {";
  s.op->newline(1) << "kp->u.krp.maxactive = sdp->maxactive_val;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "kp->u.krp.maxactive = KRETACTIVE;";
  s.op->newline(-1) << "}";
  s.op->newline() << "kp->u.krp.handler = &enter_kretprobe_probe;";
  s.op->newline() << "#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)";
  s.op->newline() << "if (sdp->entry_probe) {";
  s.op->newline(1) << "kp->u.krp.entry_handler = &enter_kretprobe_entry_probe;";
  s.op->newline() << "kp->u.krp.data_size = sdp->saved_longs * sizeof(int64_t) + ";
  s.op->newline() << "                      sdp->saved_strings * MAXSTRINGLEN;";
  s.op->newline(-1) << "}";
  s.op->newline() << "#endif";
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
  s.op->newline() << "if (rc) {"; // PR6749: tolerate a failed register_*probe.
  s.op->newline(1) << "sdp->registered_p = 0;";
  s.op->newline() << "if (!sdp->optional_p)";
  s.op->newline(1) << "_stp_warn (\"probe %s (address 0x%lx) registration error (rc %d)\", probe_point, (unsigned long) relocated_addr, rc);";
  s.op->newline(-1) << "rc = 0;"; // continue with other probes
  // XXX: shall we increment numskipped?
  s.op->newline(-1) << "}";

#if 0 /* pre PR 6749; XXX consider making an option */
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
#endif

  s.op->newline() << "else sdp->registered_p = 1;";
  s.op->newline(-1) << "}"; // for loop
}


void
dwarf_derived_probe_group::emit_module_refresh (systemtap_session& s)
{
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarf_probe *sdp = & stap_dwarf_probes[i];";
  s.op->newline() << "struct stap_dwarf_kprobe *kp = & stap_dwarf_kprobes[i];";
  s.op->newline() << "unsigned long relocated_addr = _stp_kmodule_relocate (sdp->module, sdp->section, sdp->address);";
  s.op->newline() << "int rc;";

  // new module arrived?
  s.op->newline() << "if (sdp->registered_p == 0 && relocated_addr != 0) {";
  s.op->newline(1) << "if (sdp->return_p) {";
  s.op->newline(1) << "kp->u.krp.kp.addr = (void *) relocated_addr;";
  s.op->newline() << "if (sdp->maxactive_p) {";
  s.op->newline(1) << "kp->u.krp.maxactive = sdp->maxactive_val;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "kp->u.krp.maxactive = KRETACTIVE;";
  s.op->newline(-1) << "}";
  s.op->newline() << "kp->u.krp.handler = &enter_kretprobe_probe;";
  s.op->newline() << "#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)";
  s.op->newline() << "if (sdp->entry_probe) {";
  s.op->newline(1) << "kp->u.krp.entry_handler = &enter_kretprobe_entry_probe;";
  s.op->newline() << "kp->u.krp.data_size = sdp->saved_longs * sizeof(int64_t) + ";
  s.op->newline() << "                      sdp->saved_strings * MAXSTRINGLEN;";
  s.op->newline(-1) << "}";
  s.op->newline() << "#endif";
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
  s.op->newline() << "if (rc == 0) sdp->registered_p = 1;";

  // old module disappeared?
  s.op->newline(-1) << "} else if (sdp->registered_p == 1 && relocated_addr == 0) {";
  s.op->newline(1) << "if (sdp->return_p) {";
  s.op->newline(1) << "unregister_kretprobe (&kp->u.krp);";
  s.op->newline() << "atomic_add (kp->u.krp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/1 on '%s': %d\\n\", sdp->probe->pp, kp->u.krp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline() << "atomic_add (kp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/2 on '%s': %lu\\n\", sdp->probe->pp, kp->u.krp.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "unregister_kprobe (&kp->u.kp);";
  s.op->newline() << "atomic_add (kp->u.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kprobe on '%s': %lu\\n\", sdp->probe->pp, kp->u.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "}";
  s.op->newline() << "#if defined(__ia64__)";
  s.op->newline() << "unregister_kprobe (&kp->dummy);";
  s.op->newline() << "#endif";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";

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
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/1 on '%s': %d\\n\", sdp->probe->pp, kp->u.krp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline() << "atomic_add (kp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/2 on '%s': %lu\\n\", sdp->probe->pp, kp->u.krp.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "} else {";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline(1) << "unregister_kprobe (&kp->u.kp);";
  s.op->newline() << "#endif";
  s.op->newline() << "atomic_add (kp->u.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kprobe on '%s': %lu\\n\", sdp->probe->pp, kp->u.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "}";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES) && defined(__ia64__)";
  s.op->newline() << "unregister_kprobe (&kp->dummy);";
  s.op->newline() << "#endif";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";
}

static void sdt_v3_tokenize(const string& str, vector<string>& tokens)
{
  string::size_type pos;
  string::size_type lastPos = str.find_first_not_of(" ", 0);
  string::size_type nextAt = str.find("@", lastPos);
  while (lastPos != string::npos)
   {
     pos = nextAt + 1;
     nextAt = str.find("@", pos);
     if (nextAt == string::npos)
       pos = string::npos;
     else
       pos = str.rfind(" ", nextAt);

     tokens.push_back(str.substr(lastPos, pos - lastPos));
     lastPos = str.find_first_not_of(" ", pos);
   }
}


struct sdt_uprobe_var_expanding_visitor: public var_expanding_visitor
{
  enum regwidths {QI, QIh, HI, SI, DI};
  sdt_uprobe_var_expanding_visitor(systemtap_session& s,
                                   int elf_machine,
                                   const string & process_name,
				   const string & provider_name,
				   const string & probe_name,
				   stap_sdt_probe_type probe_type,
				   const string & arg_string,
				   int ac):
    session (s), elf_machine (elf_machine), process_name (process_name),
    provider_name (provider_name), probe_name (probe_name),
    probe_type (probe_type), arg_count ((unsigned) ac)
  {
    /* Register name mapping table depends on the elf machine of this particular
       probe target process/file, not upon the host.  So we can't just
       #ifdef _i686_ etc. */

#define DRI(name,num,width)  dwarf_regs[name]=make_pair(num,width)
    if (elf_machine == EM_X86_64) {
      DRI ("%rax", 0, DI); DRI ("%eax", 0, SI); DRI ("%ax", 0, HI); 
      	 DRI ("%al", 0, QI); DRI ("%ah", 0, QIh); 
      DRI ("%rdx", 1, DI); DRI ("%edx", 1, SI); DRI ("%dx", 1, HI);
         DRI ("%dl", 1, QI); DRI ("%dh", 1, QIh);
      DRI ("%rcx", 2, DI); DRI ("%ecx", 2, SI); DRI ("%cx", 2, HI);
         DRI ("%cl", 2, QI); DRI ("%ch", 2, QIh);
      DRI ("%rbx", 3, DI); DRI ("%ebx", 3, SI); DRI ("%bx", 3, HI);
         DRI ("%bl", 3, QI); DRI ("%bh", 3, QIh); 
      DRI ("%rsi", 4, DI); DRI ("%esi", 4, SI); DRI ("%si", 4, HI); 
         DRI ("%sil", 4, QI);
      DRI ("%rdi", 5, DI); DRI ("%edi", 5, SI); DRI ("%di", 5, HI);
         DRI ("%dil", 5, QI);
      DRI ("%rbp", 6, DI); DRI ("%ebp", 6, SI); DRI ("%bp", 6, HI);
      DRI ("%rsp", 7, DI); DRI ("%esp", 7, SI); DRI ("%sp", 7, HI);
      DRI ("%r8", 8, DI); DRI ("%r8d", 8, SI); DRI ("%r8w", 8, HI);
         DRI ("%r8b", 8, QI);
      DRI ("%r9", 9, DI); DRI ("%r9d", 9, SI); DRI ("%r9w", 9, HI);
         DRI ("%r9b", 9, QI);
      DRI ("%r10", 10, DI); DRI ("%r10d", 10, SI); DRI ("%r10w", 10, HI);
         DRI ("%r10b", 10, QI);
      DRI ("%r11", 11, DI); DRI ("%r11d", 11, SI); DRI ("%r11w", 11, HI);
         DRI ("%r11b", 11, QI);
      DRI ("%r12", 12, DI); DRI ("%r12d", 12, SI); DRI ("%r12w", 12, HI);
         DRI ("%r12b", 12, QI);
      DRI ("%r13", 13, DI); DRI ("%r13d", 13, SI); DRI ("%r13w", 13, HI);
         DRI ("%r13b", 13, QI);
      DRI ("%r14", 14, DI); DRI ("%r14d", 14, SI); DRI ("%r14w", 14, HI);
         DRI ("%r14b", 14, QI);
      DRI ("%r15", 15, DI); DRI ("%r15d", 15, SI); DRI ("%r15w", 15, HI);
         DRI ("%r15b", 15, QI);
    } else if (elf_machine == EM_386) {
      DRI ("%eax", 0, SI); DRI ("%ax", 0, HI); DRI ("%al", 0, QI);
         DRI ("%ah", 0, QIh);
      DRI ("%ecx", 1, SI); DRI ("%cx", 1, HI); DRI ("%cl", 1, QI);
         DRI ("%ch", 1, QIh);
      DRI ("%edx", 2, SI); DRI ("%dx", 2, HI); DRI ("%dl", 2, QI);
         DRI ("%dh", 2, QIh);
      DRI ("%ebx", 3, SI); DRI ("%bx", 3, HI); DRI ("%bl", 3, QI);
         DRI ("%bh", 3, QIh);
      DRI ("%esp", 4, SI); DRI ("%sp", 4, HI); 
      DRI ("%ebp", 5, SI); DRI ("%bp", 5, HI);
      DRI ("%esi", 6, SI); DRI ("%si", 6, HI); DRI ("%sil", 6, QI);
      DRI ("%edi", 7, SI); DRI ("%di", 7, HI); DRI ("%dil", 7, QI);
    } else if (elf_machine == EM_PPC || elf_machine == EM_PPC64) {
      DRI ("%r0", 0, DI);
      DRI ("%r1", 1, DI);
      DRI ("%r2", 2, DI);
      DRI ("%r3", 3, DI);
      DRI ("%r4", 4, DI);
      DRI ("%r5", 5, DI);
      DRI ("%r6", 6, DI);
      DRI ("%r7", 7, DI);
      DRI ("%r8", 8, DI);
      DRI ("%r9", 9, DI);
      DRI ("%r10", 10, DI);
      DRI ("%r11", 11, DI);
      DRI ("%r12", 12, DI);
      DRI ("%r13", 13, DI);
      DRI ("%r14", 14, DI);
      DRI ("%r15", 15, DI);
      DRI ("%r16", 16, DI);
      DRI ("%r17", 17, DI);
      DRI ("%r18", 18, DI);
      DRI ("%r19", 19, DI);
      DRI ("%r20", 20, DI);
      DRI ("%r21", 21, DI);
      DRI ("%r22", 22, DI);
      DRI ("%r23", 23, DI);
      DRI ("%r24", 24, DI);
      DRI ("%r25", 25, DI);
      DRI ("%r26", 26, DI);
      DRI ("%r27", 27, DI);
      DRI ("%r28", 28, DI);
      DRI ("%r29", 29, DI);
      DRI ("%r30", 30, DI);
      DRI ("%r31", 31, DI);
      // PR11821: unadorned register "names" without -mregnames
      DRI ("0", 0, DI);
      DRI ("1", 1, DI);
      DRI ("2", 2, DI);
      DRI ("3", 3, DI);
      DRI ("4", 4, DI);
      DRI ("5", 5, DI);
      DRI ("6", 6, DI);
      DRI ("7", 7, DI);
      DRI ("8", 8, DI);
      DRI ("9", 9, DI);
      DRI ("10", 10, DI);
      DRI ("11", 11, DI);
      DRI ("12", 12, DI);
      DRI ("13", 13, DI);
      DRI ("14", 14, DI);
      DRI ("15", 15, DI);
      DRI ("16", 16, DI);
      DRI ("17", 17, DI);
      DRI ("18", 18, DI);
      DRI ("19", 19, DI);
      DRI ("20", 20, DI);
      DRI ("21", 21, DI);
      DRI ("22", 22, DI);
      DRI ("23", 23, DI);
      DRI ("24", 24, DI);
      DRI ("25", 25, DI);
      DRI ("26", 26, DI);
      DRI ("27", 27, DI);
      DRI ("28", 28, DI);
      DRI ("29", 29, DI);
      DRI ("30", 30, DI);
      DRI ("31", 31, DI);
    } else if (elf_machine == EM_S390) {
      DRI ("%r0", 0, DI);
      DRI ("%r1", 1, DI);
      DRI ("%r2", 2, DI);
      DRI ("%r3", 3, DI);
      DRI ("%r4", 4, DI);
      DRI ("%r5", 5, DI);
      DRI ("%r6", 6, DI);
      DRI ("%r7", 7, DI);
      DRI ("%r8", 8, DI);
      DRI ("%r9", 9, DI);
      DRI ("%r10", 10, DI);
      DRI ("%r11", 11, DI);
      DRI ("%r12", 12, DI);
      DRI ("%r13", 13, DI);
      DRI ("%r14", 14, DI);
      DRI ("%r15", 15, DI);
    } else if (elf_machine == EM_ARM) {
      DRI ("r0", 0, SI);
      DRI ("r1", 1, SI);
      DRI ("r2", 2, SI);
      DRI ("r3", 3, SI);
      DRI ("r4", 4, SI);
      DRI ("r5", 5, SI);
      DRI ("r6", 6, SI);
      DRI ("r7", 7, SI);
      DRI ("r8", 8, SI);
      DRI ("r9", 9, SI);
      DRI ("sl", 10, SI);
      DRI ("fp", 11, SI);
      DRI ("ip", 12, SI);
      DRI ("sp", 13, SI);
      DRI ("lr", 14, SI);
      DRI ("pc", 15, SI);
    } else if (arg_count) {
      /* permit this case; just fall back to dwarf */
    }
#undef DRI

    need_debug_info = false;
    if (probe_type == uprobe3_type)
      {
        sdt_v3_tokenize(arg_string, arg_tokens);
        assert(arg_count <= 12);
      }
    else
      {
        tokenize(arg_string, arg_tokens, " ");
        assert(arg_count <= 10);
      }
  }

  systemtap_session& session;
  int elf_machine;
  const string & process_name;
  const string & provider_name;
  const string & probe_name;
  stap_sdt_probe_type probe_type;
  unsigned arg_count;
  vector<string> arg_tokens;
  map<string, pair<unsigned,int> > dwarf_regs;
  bool need_debug_info;

  void visit_target_symbol (target_symbol* e);
  void visit_target_symbol_arg (target_symbol* e);
  void visit_target_symbol_context (target_symbol* e);
  void visit_cast_op (cast_op* e);
};


void
sdt_uprobe_var_expanding_visitor::visit_target_symbol_context (target_symbol* e)
{
  if (e->addressof)
    throw semantic_error(_("cannot take address of context variable"), e->tok);

  if (e->name == "$$name")
    {
      literal_string *myname = new literal_string (probe_name);
      myname->tok = e->tok;
      provide(myname);
      return;
    }

  else if (e->name == "$$provider")
    {
      literal_string *myname = new literal_string (provider_name);
      myname->tok = e->tok;
      provide(myname);
      return;
    }

  else if (e->name == "$$vars" || e->name == "$$parms")
    {
      e->assert_no_components("sdt", true);

      // Convert $$vars to sprintf of a list of vars which we recursively evaluate
      // NB: we synthesize a new token here rather than reusing
      // e->tok, because print_format::print likes to use
      // its tok->content.
      token* pf_tok = new token(*e->tok);
      pf_tok->content = "sprintf";

      print_format* pf = print_format::create(pf_tok);

      for (unsigned i = 1; i <= arg_count; ++i)
        {
          if (i > 1)
            pf->raw_components += " ";
          target_symbol *tsym = new target_symbol;
          tsym->tok = e->tok;
          tsym->name = "$arg" + lex_cast(i);
          pf->raw_components += tsym->name;
          tsym->components = e->components;

          expression *texp = require (tsym);
          if (!e->components.empty() &&
              e->components[0].type == target_symbol::comp_pretty_print)
            pf->raw_components += "=%s";
          else
            pf->raw_components += "=%#x";
          pf->args.push_back(texp);
        }

      pf->components = print_format::string_to_components(pf->raw_components);
      provide (pf);
    }
  else
    assert(0); // shouldn't get here
}


void
sdt_uprobe_var_expanding_visitor::visit_target_symbol_arg (target_symbol *e)
{
  try
    {
      unsigned argno = 0; // the N in $argN
      try
	{
	  if (startswith(e->name, "$arg"))
	    argno = lex_cast<unsigned>(e->name.substr(4));
	}
      catch (const runtime_error& f) // non-integral $arg suffix: e.g. $argKKKSDF
	{
          argno = 0;
	}

      if (arg_count == 0 || // a sdt.h variant without .probe-stored arg_count
          argno < 1 || argno > arg_count) // a $argN with out-of-range N
	{
	  // NB: Either
	  // 1) uprobe1_type $argN or $FOO (we don't know the arg_count)
	  // 2) uprobe2_type $FOO (no probe args)
	  // both of which get resolved later.
	  need_debug_info = true;
	  provide(e);
	  return;
	}

      assert (arg_tokens.size() >= argno);
      string asmarg = arg_tokens[argno-1];   // $arg1 => arg_tokens[0]

      // Now we try to parse this thing, which is an assembler operand
      // expression.  If we can't, we warn, back down to need_debug_info
      // and hope for the best.  Here is the syntax for a few architectures.
      // Note that the power iN syntax is only for V3 sdt.h; gcc emits the i.
      //
      //      literal	reg	reg	reg +	base+index*size+offset
      //	      	      indirect offset
      // x86	$N	%rR	(%rR)	N(%rR)  O(%bR,%iR,S)
      // power	iN	R	(R)	N(R)
      // ia64	N	rR	[r16]	
      // s390	N	%rR	0(rR)	N(r15)
      // arm	#N	rR	[rR]	[rR, #N]

      expression* argexpr = 0; // filled in in case of successful parse

      string percent_regnames;
      string regnames;
      vector<string> matches;
      long precision;
      int rc;

      // Parse the leading length

      if (asmarg.find('@') != string::npos)
	{
	  precision = lex_cast<int>(asmarg.substr(0, asmarg.find('@')));
	  asmarg = asmarg.substr(asmarg.find('@')+1);
	}
      else
	{
	  // V1/V2 do not have precision field so default to signed long
	  // V3 asm does not have precision field so default to unsigned long
	  if (probe_type == uprobe3_type)
	    precision = sizeof(long); // this is an asm probe
	  else
	    precision = -sizeof(long);
	}

      // test for a numeric literal.
      // Only accept (signed) decimals throughout. XXX

      // PR11821.  NB: on powerpc, literals are not prefixed with $,
      // so this regex does not match.  But that's OK, since without
      // -mregnames, we can't tell them apart from register numbers
      // anyway.  With -mregnames, we could, if gcc somehow
      // communicated to us the presence of that option, but alas it
      // doesn't.  http://gcc.gnu.org/PR44995.
      rc = regexp_match (asmarg, "^[i\\$#][-]?[0-9][0-9]*$", matches);
      if (! rc)
        {
	  string sn = matches[0].substr(1);
	  int64_t n;
	  try
	    {
	      // We have to pay attention to the size & sign, as gcc sometimes
	      // propagates constants that don't quite match, like a negative
	      // value to fill an unsigned type.
	      switch (precision)
		{
		case -1: n = lex_cast<  int8_t>(sn); break;
		case  1: n = lex_cast< uint8_t>(sn); break;
		case -2: n = lex_cast< int16_t>(sn); break;
		case  2: n = lex_cast<uint16_t>(sn); break;
		case -4: n = lex_cast< int32_t>(sn); break;
		case  4: n = lex_cast<uint32_t>(sn); break;
		default:
		case -8: n = lex_cast< int64_t>(sn); break;
		case  8: n = lex_cast<uint64_t>(sn); break;
		}
	    }
	  catch (std::runtime_error&)
	    {
	      goto not_matched;
	    }
	  literal_number* ln = new literal_number(n);
	  ln->tok = e->tok;
          argexpr = ln;
          goto matched;
        }

      if (dwarf_regs.empty())
	goto not_matched;
      
      // Build regex pieces out of the known dwarf_regs.  We keep two separate
      // lists: ones with the % prefix (and thus unambigiuous even despite PR11821),
      // and ones with no prefix (and thus only usable in unambiguous contexts).
      for (map<string,pair<unsigned,int> >::iterator ri = dwarf_regs.begin(); ri != dwarf_regs.end(); ri++)
        {
          string regname = ri->first;
          assert (regname != "");
          regnames += string("|")+regname;
          if (regname[0]=='%')
            percent_regnames += string("|")+regname;
        }
      // clip off leading |
      regnames = regnames.substr(1);
      if (percent_regnames != "")
          percent_regnames = percent_regnames.substr(1);

      // test for REGISTER
      // NB: Because PR11821, we must use percent_regnames here.
      if (elf_machine == EM_PPC || elf_machine == EM_PPC64 || elf_machine == EM_ARM)
	rc = regexp_match (asmarg, string("^(")+regnames+string(")$"), matches);
      else
	rc = regexp_match (asmarg, string("^(")+percent_regnames+string(")$"), matches);
      if (! rc)
        {
          string regname = matches[1];
	  map<string,pair<unsigned,int> >::iterator ri = dwarf_regs.find (regname);
          if (ri != dwarf_regs.end()) // known register
            {
              embedded_expr *get_arg1 = new embedded_expr;
	      string width_adjust;
	      switch (ri->second.second)
		{
		case QI: width_adjust = ") & 0xff)"; break;
		case QIh: width_adjust = ">>8) & 0xff)"; break;
		case HI:
		  // preserve 16 bit register signness
		  width_adjust = ") & 0xffff)";
		  if (precision < 0)
		    width_adjust += " << 48 >> 48";
		  break;
		case SI:
		  // preserve 32 bit register signness
		  width_adjust = ") & 0xffffffff)";
		  if (precision < 0)
		    width_adjust += " << 32 >> 32";
		  break;
		default: width_adjust = "))";
		}
              string type = "";
	      if (probe_type == uprobe3_type)
		type = (precision < 0
			? "(int" : "(uint") + lex_cast(abs(precision) * 8) + "_t)";
	      type = type + "((";
              get_arg1->tok = e->tok;
              get_arg1->code = string("/* unprivileged */ /* pure */")
                + string(" ((int64_t)") + type
                + (is_user_module (process_name)
                   ? string("u_fetch_register(")
                   : string("k_fetch_register("))
                + lex_cast(dwarf_regs[regname].first) + string("))")
		+ width_adjust;
              argexpr = get_arg1;
              goto matched;
            }
          // invalid register name, fall through
        }

      int reg, offset1;
      // test for OFFSET(REGISTER) where OFFSET is +-N+-N+-N
      // NB: Despite PR11821, we can use regnames here, since the parentheses
      // make things unambiguous. (Note: gdb/stap-probe.c also parses this)
      // On ARM test for [REGISTER, OFFSET]
     if (elf_machine == EM_ARM)
       {
         rc = regexp_match (asmarg, string("^\\[(")+regnames+string("), #([+-]?[0-9]+)([+-][0-9]*)?([+-][0-9]*)?\\]$"), matches);
         reg = 1;
         offset1 = 2;
       }
     else
       {
         rc = regexp_match (asmarg, string("^([+-]?[0-9]*)([+-][0-9]*)?([+-][0-9]*)?[(](")+regnames+string(")[)]$"), matches);
         reg = 4;
         offset1 = 1;
       }
      if (! rc)
        {
          string regname;
          int64_t disp = 0;
          if (matches[reg].length())
            regname = matches[reg];
          if (dwarf_regs.find (regname) == dwarf_regs.end())
            goto not_matched;

          for (int i=offset1; i <= (offset1 + 2); i++)
            if (matches[i].length())
              try
                {
                  disp += lex_cast<int64_t>(matches[i]); // should decode positive/negative hex/decimal
                }
                catch (const runtime_error& f) // unparseable offset
                  {
                    goto not_matched; // can't just 'break' out of
                                      // this case or use a sentinel
                                      // value, unfortunately
                  }

                  // synthesize user_long(%{fetch_register(R)%} + D)
                  embedded_expr *get_arg1 = new embedded_expr;
                  get_arg1->tok = e->tok;
                  get_arg1->code = string("/* unprivileged */ /* pure */")
                    + (is_user_module (process_name)
                       ? string("u_fetch_register(")
                       : string("k_fetch_register("))
                    + lex_cast(dwarf_regs[regname].first) + string(")");
                  // XXX: may we ever need to cast that to a narrower type?

                  literal_number* inc = new literal_number(disp);
                  inc->tok = e->tok;

                  binary_expression *be = new binary_expression;
                  be->tok = e->tok;
                  be->left = get_arg1;
                  be->op = "+";
                  be->right = inc;

                  functioncall *fc = new functioncall;
		  switch (precision)
		    {
		    case 1: case -1:
		      fc->function = "user_int8"; break;
		    case 2:
		      fc->function = "user_uint16"; break;
		    case -2:
		      fc->function = "user_int16"; break;
		    case 4:
		      fc->function = "user_uint32"; break;
		    case -4:
		      fc->function = "user_int32"; break;
                    case 8: case -8:
		      fc->function = "user_int64"; break;
		    default: fc->function = "user_long";
		    }
                  fc->tok = e->tok;
                  fc->args.push_back(be);

                  argexpr = fc;
                  goto matched;
                }

      // test for OFFSET(BASE_REGISTER,INDEX_REGISTER[,SCALE]) where OFFSET is +-N+-N+-N
      // NB: Despite PR11821, we can use regnames here, since the parentheses
      // make things unambiguous. (Note: gdb/stap-probe.c also parses this)
      rc = regexp_match (asmarg, string("^([+-]?[0-9]*)([+-][0-9]*)?([+-][0-9]*)?[(](")+regnames+string("),(")+regnames+string(")(,[1248])?[)]$"), matches);
      if (! rc)
        {
          string baseregname;
          string indexregname;
          int64_t disp = 0;
          short scale = 1;

          if (matches[6].length())
            try
              {
                scale = lex_cast<short>(matches[6].substr(1)); // NB: skip the comma!
                // We could verify that scale is one of 1,2,4,8,
                // but it doesn't really matter.  An erroneous
                // address merely results in run-time errors.
        }
            catch (const runtime_error &f) // unparseable scale
              {
                  goto not_matched;
              }

          if (matches[4].length())
            baseregname = matches[4];
          if (dwarf_regs.find (baseregname) == dwarf_regs.end())
            goto not_matched;

          if (matches[5].length())
            indexregname = matches[5];
          if (dwarf_regs.find (indexregname) == dwarf_regs.end())
            goto not_matched;

          for (int i = 1; i <= 3; i++) // up to three OFFSET terms
            if (matches[i].length())
              try
                {
                  disp += lex_cast<int64_t>(matches[i]); // should decode positive/negative hex/decimal
                }
              catch (const runtime_error& f) // unparseable offset
                {
                  goto not_matched; // can't just 'break' out of
                  // this case or use a sentinel
                  // value, unfortunately
                }

          // synthesize user_long(%{fetch_register(R1)+fetch_register(R2)*N%} + D)

          embedded_expr *get_arg1 = new embedded_expr;
          string regfn = is_user_module (process_name)
            ? string("u_fetch_register")
            : string("k_fetch_register"); // NB: in practice sdt.h probes are for userspace only

          get_arg1->tok = e->tok;
          get_arg1->code = string("/* unprivileged */ /* pure */")
            + regfn + string("(")+lex_cast(dwarf_regs[baseregname].first)+string(")")
            + string("+(")
            + regfn + string("(")+lex_cast(dwarf_regs[indexregname].first)+string(")")
            + string("*")
            + lex_cast(scale)
            + string(")");

          // NB: could plop this +DISPLACEMENT bit into the embedded-c expression too
          literal_number* inc = new literal_number(disp);
          inc->tok = e->tok;

          binary_expression *be = new binary_expression;
          be->tok = e->tok;
          be->left = get_arg1;
          be->op = "+";
          be->right = inc;

          functioncall *fc = new functioncall;
          switch (precision)
            {
            case 1: case -1:
              fc->function = "user_int8"; break;
            case 2:
              fc->function = "user_uint16"; break;
            case -2:
              fc->function = "user_int16"; break;
            case 4:
              fc->function = "user_uint32"; break;
            case -4:
              fc->function = "user_int32"; break;
            case 8: case -8:
              fc->function = "user_int64"; break;
            default: fc->function = "user_long";
            }
          fc->tok = e->tok;
          fc->args.push_back(be);

          argexpr = fc;
          goto matched;
        }


    not_matched:
      // The asmarg operand was not recognized.  Back down to dwarf.
      if (! session.suppress_warnings)
        {
          if (probe_type == UPROBE3_TYPE)
            session.print_warning (_F("Can't parse SDT_V3 operand '%s'", asmarg.c_str()), e->tok);
          else // must be *PROBE2; others don't get asm operands
            session.print_warning (_F("Downgrading SDT_V2 probe argument to dwarf, can't parse '%s'", 
                                      asmarg.c_str()), e->tok);
        }
      assert (argexpr == 0);
      need_debug_info = true;
      provide (e);
      return;

    matched:
      assert (argexpr != 0);

      if (session.verbose > 2)
        //TRANSLATORS: We're mapping the operand to a new expression*.
        clog << _F("mapped asm operand %s to ", asmarg.c_str()) << *argexpr << endl;

      if (e->components.empty()) // We have a scalar
        {
          if (e->addressof)
            throw semantic_error(_("cannot take address of sdt variable"), e->tok);
          provide (argexpr);
          return;
        }
      else  // $var->foo
        {
          cast_op *cast = new cast_op;
          cast->name = "@cast";
          cast->tok = e->tok;
          cast->operand = argexpr;
          cast->components = e->components;
          cast->type_name = probe_name + "_arg" + lex_cast(argno);
          cast->module = process_name;
          cast->visit(this);
          return;
        }

      /* NOTREACHED */
    }
  catch (const semantic_error &er)
    {
      e->chain (er);
      provide (e);
    }
}


void
sdt_uprobe_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  try
    {
      assert(e->name.size() > 0
	     && ((e->name[0] == '$' && e->target_name == "")
		 || (e->name == "@var" && e->target_name != "")));

      if (e->name == "$$name" || e->name == "$$provider" || e->name == "$$parms" || e->name == "$$vars")
        visit_target_symbol_context (e);
      else
        visit_target_symbol_arg (e);
    }
  catch (const semantic_error &er)
    {
      e->chain (er);
      provide (e);
    }
}


void
sdt_uprobe_var_expanding_visitor::visit_cast_op (cast_op* e)
{
  // Fill in our current module context if needed
  if (e->module.empty())
    e->module = process_name;

  var_expanding_visitor::visit_cast_op(e);
}


void
plt_expanding_visitor::visit_target_symbol (target_symbol *e)
{
  try
    {
      if (e->name == "$$name")
	{
	  literal_string *myname = new literal_string (entry);
	  myname->tok = e->tok;
	  provide(myname);
	  return;
	}

      // variable not found -> throw a semantic error
      // (only to be caught right away, but this may be more complex later...)
      string alternatives = "$$name";
      throw semantic_error(_F("unable to find plt variable '%s' (alternatives: %s)",
                              e->name.c_str(), alternatives.c_str()), e->tok);
    }
  catch (const semantic_error &er)
    {
      e->chain (er);
      provide (e);
    }
}


struct sdt_query : public base_query
{
  sdt_query(probe * base_probe, probe_point * base_loc,
            dwflpp & dw, literal_map_t const & params,
            vector<derived_probe *> & results, const string user_lib);

  void query_library (const char *data);
  void query_plt (const char *entry, size_t addr) {}
  void handle_query_module();

private:
  stap_sdt_probe_type probe_type;
  enum { probe_section=0, note_section=1, unknown_section=-1 } probe_loc;
  probe * base_probe;
  probe_point * base_loc;
  literal_map_t const & params;
  vector<derived_probe *> & results;
  string pp_mark;
  string pp_provider;
  string user_lib;

  set<string> probes_handled;

  Elf_Data *pdata;
  size_t probe_scn_offset;
  size_t probe_scn_addr;
  uint64_t arg_count;
  GElf_Addr base;
  GElf_Addr pc;
  string arg_string;
  string probe_name;
  string provider_name;
  Dwarf_Addr semaphore;

  bool init_probe_scn();
  bool get_next_probe();
  void iterate_over_probe_entries();
  void handle_probe_entry();

  static void setup_note_probe_entry_callback (void *object, int type, const char *data, size_t len);
  void setup_note_probe_entry (int type, const char *data, size_t len);

  void convert_probe(probe *base);
  void record_semaphore(vector<derived_probe *> & results, unsigned start);
  probe* convert_location();
  bool have_uprobe() {return probe_type == uprobe1_type || probe_type == uprobe2_type || probe_type == uprobe3_type;}
  bool have_debuginfo_uprobe(bool need_debug_info)
  {return probe_type == uprobe1_type
      || ((probe_type == uprobe2_type || probe_type == uprobe3_type)
	  && need_debug_info);}
  bool have_debuginfoless_uprobe() {return probe_type == uprobe2_type || probe_type == uprobe3_type;}
};


sdt_query::sdt_query(probe * base_probe, probe_point * base_loc,
                     dwflpp & dw, literal_map_t const & params,
                     vector<derived_probe *> & results, const string user_lib):
  base_query(dw, params), probe_type(unknown_probe_type),
  probe_loc(unknown_section), base_probe(base_probe),
  base_loc(base_loc), params(params), results(results), user_lib(user_lib),
  probe_scn_offset(0), probe_scn_addr(0), arg_count(0), base(0), pc(0),
  semaphore(0)
{
  assert(get_string_param(params, TOK_MARK, pp_mark));
  get_string_param(params, TOK_PROVIDER, pp_provider); // pp_provider == "" -> unspecified

  // PR10245: permit usage of dtrace-y "-" separator in marker name;
  // map it to double-underscores.
  size_t pos = 0;
  while (1) // there may be more than one
    {
      size_t i = pp_mark.find("-", pos);
      if (i == string::npos) break;
      pp_mark.replace (i, 1, "__");
      pos = i+1; // resume searching after the inserted __
    }

  // XXX: same for pp_provider?
}


void
sdt_query::handle_probe_entry()
{
  if (! have_uprobe()
      && !probes_handled.insert(probe_name).second)
    return;

  if (sess.verbose > 3)
    {
      //TRANSLATORS: Describing what probe type (kprobe or uprobe) the probe 
      //TRANSLATORS: is matched to.
      clog << _F("matched probe_name %s probe type ", probe_name.c_str());
      switch (probe_type)
	{
	case uprobe1_type:
	  clog << "uprobe1 at 0x" << hex << pc << dec << endl;
	  break;
	case uprobe2_type:
	  clog << "uprobe2 at 0x" << hex << pc << dec << endl;
	  break;
	case uprobe3_type:
	  clog << "uprobe3 at 0x" << hex << pc << dec << endl;
	  break;
	default:
	  clog << "unknown!" << endl;
	  break;
	}
    }

  // Extend the derivation chain
  probe *new_base = convert_location();
  probe_point *new_location = new_base->locations[0];

  bool need_debug_info = false;

  // We could get the Elf* from either dwarf_getelf(dwfl_module_getdwarf(...))
  // or dwfl_module_getelf(...).  We only need it for the machine type, which
  // should be the same.  The bias is used for relocating debuginfoless probes,
  // though, so that must come from the possibly-prelinked ELF file, not DWARF.
  Dwarf_Addr bias;
  Elf* elf = dwfl_module_getelf (dw.mod_info->mod, &bias);

  /* Figure out the architecture of this particular ELF file.  The
     dwarfless register-name mappings depend on it. */
  GElf_Ehdr ehdr_mem;
  GElf_Ehdr* em = gelf_getehdr (elf, &ehdr_mem);
  if (em == 0) { dwfl_assert ("dwfl_getehdr", dwfl_errno()); }
  int elf_machine = em->e_machine;
  sdt_uprobe_var_expanding_visitor svv (sess, elf_machine, module_val,
					provider_name, probe_name,
					probe_type, arg_string, arg_count);
  svv.replace (new_base->body);
  need_debug_info = svv.need_debug_info;

  unsigned i = results.size();

  // XXX: why not derive_probes() in the uprobes case too?
  literal_map_t params;
  for (unsigned i = 0; i < new_location->components.size(); ++i)
   {
      probe_point::component *c = new_location->components[i];
      params[c->functor] = c->arg;
   }

  dwarf_query q(new_base, new_location, dw, params, results, "", "");
  q.has_mark = true; // enables mid-statement probing

  // V2 probes need dwarf info in case of a variable reference
  if (have_debuginfo_uprobe(need_debug_info))
    dw.iterate_over_modules(&query_module, &q);
  else if (have_debuginfoless_uprobe())
    {
      string section;
      Dwarf_Addr reloc_addr = q.statement_num_val + bias;
      if (dwfl_module_relocations (q.dw.mod_info->mod) > 0)
        {
	  dwfl_module_relocate_address (q.dw.mod_info->mod, &reloc_addr);
	  section = ".dynamic";
        }
      else
	section = ".absolute";

      uprobe_derived_probe* p =
	new uprobe_derived_probe ("", "", 0,
				  path_remove_sysroot(sess,q.module_val),
				  section,
				  q.statement_num_val, reloc_addr, q, 0);
      p->saveargs (arg_count);
      results.push_back (p);
    }
  sess.unwindsym_modules.insert (dw.module_name);
  record_semaphore(results, i);
}


void
sdt_query::handle_query_module()
{
  if (!init_probe_scn())
    return;

  if (sess.verbose > 3)
    clog << "TOK_MARK: " << pp_mark << " TOK_PROVIDER: " << pp_provider << endl;

  if (probe_loc == note_section)
    {
      GElf_Shdr shdr_mem;
      GElf_Shdr *shdr = dw.get_section (".stapsdt.base", &shdr_mem);

      if (shdr)
	base = shdr->sh_addr;
      else
	base = 0;
      dw.iterate_over_notes ((void*) this, &sdt_query::setup_note_probe_entry_callback);
    }
  else if (probe_loc == probe_section)
    iterate_over_probe_entries ();
}


bool
sdt_query::init_probe_scn()
{
  Elf* elf;
  GElf_Shdr shdr_mem;

  GElf_Shdr *shdr = dw.get_section (".note.stapsdt", &shdr_mem);
  if (shdr)
    {
      probe_loc = note_section;
      return true;
    }

  shdr = dw.get_section (".probes", &shdr_mem, &elf);
  if (shdr)
    {
      pdata = elf_getdata_rawchunk (elf, shdr->sh_offset, shdr->sh_size, ELF_T_BYTE);
      probe_scn_offset = 0;
      probe_scn_addr = shdr->sh_addr;
      assert (pdata != NULL);
      if (sess.verbose > 4)
        clog << "got .probes elf scn_addr@0x" << probe_scn_addr << ", size: "
             << pdata->d_size << endl;
      probe_loc = probe_section;
      return true;
    }
  else
    return false;
}

void
sdt_query::setup_note_probe_entry_callback (void *object, int type, const char *data, size_t len)
{
  sdt_query *me = (sdt_query*)object;
  me->setup_note_probe_entry (type, data, len);
}


void
sdt_query::setup_note_probe_entry (int type, const char *data, size_t len)
{
  //  if (nhdr.n_namesz == sizeof _SDT_NOTE_NAME
  //      && !memcmp (data->d_buf + name_off,
  //		  _SDT_NOTE_NAME, sizeof _SDT_NOTE_NAME))

  // probes are in the .note.stapsdt section
#define _SDT_NOTE_TYPE 3
  if (type != _SDT_NOTE_TYPE)
    return;

  union
  {
    Elf64_Addr a64[3];
    Elf32_Addr a32[3];
  } buf;
  Dwarf_Addr bias;
  Elf* elf = (dwfl_module_getelf (dw.mod_info->mod, &bias));
  Elf_Data dst =
    {
      &buf, ELF_T_ADDR, EV_CURRENT,
      gelf_fsize (elf, ELF_T_ADDR, 3, EV_CURRENT), 0, 0
    };
  assert (dst.d_size <= sizeof buf);

  if (len < dst.d_size + 3)
    return;

  Elf_Data src =
    {
      (void *) data, ELF_T_ADDR, EV_CURRENT,
      dst.d_size, 0, 0
    };

  if (gelf_xlatetom (elf, &dst, &src,
		      elf_getident (elf, NULL)[EI_DATA]) == NULL)
    printf ("gelf_xlatetom: %s", elf_errmsg (-1));

  probe_type = uprobe3_type;
  const char * provider = data + dst.d_size;
  provider_name = provider;
  const char *name = (const char*)memchr (provider, '\0', data + len - provider);
  probe_name = ++name;

  // Did we find a matching probe?
  if (! (dw.function_name_matches_pattern (probe_name, pp_mark)
	 && ((pp_provider == "")
	     || dw.function_name_matches_pattern (provider_name, pp_provider))))
    return;

  const char *args = (const char*)memchr (name, '\0', data + len - name);
  if (args++ == NULL ||
      memchr (args, '\0', data + len - name) != data + len - 1)
    if (name == NULL)
      return;
  arg_string = args;

  arg_count = 0;
  for (unsigned i = 0; i < arg_string.length(); i++)
    if (arg_string[i] == '@')
      arg_count += 1;
  
  GElf_Addr base_ref;
  if (gelf_getclass (elf) == ELFCLASS32)
    {
      pc = buf.a32[0];
      base_ref = buf.a32[1];
      semaphore = buf.a32[2];
    }
  else
    {
      pc = buf.a64[0];
      base_ref = buf.a64[1];
      semaphore = buf.a64[2];
    }

  semaphore += base - base_ref;
  pc += base - base_ref;

  // The semaphore also needs the ELF bias added now, so
  // record_semaphore can properly relocate it later.
  semaphore += bias;

  if (sess.verbose > 4)
    clog << _F(" saw .note.stapsdt %s%s ", probe_name.c_str(), (provider_name != "" ? _(" (provider ")+provider_name+") " : "").c_str()) << "@0x" << hex << pc << dec << endl;

  handle_probe_entry();
}


void
sdt_query::iterate_over_probe_entries()
{
  // probes are in the .probe section
  while (probe_scn_offset < pdata->d_size)
    {
      stap_sdt_probe_entry_v1 *pbe_v1 = (stap_sdt_probe_entry_v1 *) ((char*)pdata->d_buf + probe_scn_offset);
      stap_sdt_probe_entry_v2 *pbe_v2 = (stap_sdt_probe_entry_v2 *) ((char*)pdata->d_buf + probe_scn_offset);
      probe_type = (stap_sdt_probe_type)(pbe_v1->type_a);
      if (! have_uprobe())
	{
	  // Unless this is a mangled .probes section, this happens
	  // because the name of the probe comes first, followed by
	  // the sentinel.
	  if (sess.verbose > 5)
            clog << _F("got unknown probe_type : 0x%x", probe_type) << endl;
	  probe_scn_offset += sizeof(__uint32_t);
	  continue;
	}
      if ((long)pbe_v1 % sizeof(__uint64_t)) // we have stap_sdt_probe_entry_v1.type_b
	{
	  pbe_v1 = (stap_sdt_probe_entry_v1*)((char*)pbe_v1 - sizeof(__uint32_t));
	  if (pbe_v1->type_b != uprobe1_type)
	    continue;
	}

      if (probe_type == uprobe1_type)
	{
	  if (pbe_v1->name == 0) // No name possibly means we have a .so with a relocation
	    return;
	  semaphore = 0;
	  probe_name = (char*)((char*)pdata->d_buf + pbe_v1->name - (char*)probe_scn_addr);
          provider_name = ""; // unknown
	  pc = pbe_v1->arg;
	  arg_count = 0;
	  probe_scn_offset += sizeof (stap_sdt_probe_entry_v1);
	}
      else if (probe_type == uprobe2_type)
	{
	  if (pbe_v2->name == 0) // No name possibly means we have a .so with a relocation
	    return;
	  semaphore = pbe_v2->semaphore;
	  probe_name = (char*)((char*)pdata->d_buf + pbe_v2->name - (char*)probe_scn_addr);
	  provider_name = (char*)((char*)pdata->d_buf + pbe_v2->provider - (char*)probe_scn_addr);
	  arg_count = pbe_v2->arg_count;
	  pc = pbe_v2->pc;
	  if (pbe_v2->arg_string)
	    arg_string = (char*)((char*)pdata->d_buf + pbe_v2->arg_string - (char*)probe_scn_addr);
	  // skip over pbe_v2, probe_name text and provider text
	  probe_scn_offset = ((long)(pbe_v2->name) - (long)(probe_scn_addr)) + probe_name.length();
	  probe_scn_offset += sizeof (__uint32_t) - probe_scn_offset % sizeof (__uint32_t);
	}
      if (sess.verbose > 4)
	clog << _("saw .probes ") << probe_name << (provider_name != "" ? _(" (provider ")+provider_name+") " : "")
	     << "@0x" << hex << pc << dec << endl;

      if (dw.function_name_matches_pattern (probe_name, pp_mark)
          && ((pp_provider == "") || dw.function_name_matches_pattern (provider_name, pp_provider)))
	handle_probe_entry ();
    }
}


void
sdt_query::record_semaphore (vector<derived_probe *> & results, unsigned start)
{
  for (unsigned i=0; i<2; i++) {
    // prefer with-provider symbol; look without provider prefix for backward compatibility only
    string semaphore = (i==0 ? (provider_name+"_") : "") + probe_name + "_semaphore";
    // XXX: multiple addresses?
    if (sess.verbose > 2)
      clog << _F("looking for semaphore symbol %s ", semaphore.c_str());

    Dwarf_Addr addr;
    if (this->semaphore)
      addr = this->semaphore;
    else
      addr  = lookup_symbol_address(dw.module, semaphore.c_str());
    if (addr)
      {
        if (dwfl_module_relocations (dw.module) > 0)
          dwfl_module_relocate_address (dw.module, &addr);
        // XXX: relocation basis?
        for (unsigned i = start; i < results.size(); ++i)
          results[i]->sdt_semaphore_addr = addr;
        if (sess.verbose > 2)
          clog << _(", found at 0x") << hex << addr << dec << endl;
        return;
      }
    else
      if (sess.verbose > 2)
        clog << _(", not found") << endl;
  }
}


void
sdt_query::convert_probe (probe *base)
{
  block *b = new block;
  b->tok = base->body->tok;

  // Generate: if (arg1 != mark("label")) next;
  functioncall *fc = new functioncall;
  fc->function = "ulong_arg";
  fc->tok = b->tok;
  literal_number* num = new literal_number(1);
  num->tok = b->tok;
  fc->args.push_back(num);

  functioncall *fcus = new functioncall;
  fcus->function = "user_string";
  fcus->type = pe_string;
  fcus->tok = b->tok;
  fcus->args.push_back(fc);

  if_statement *is = new if_statement;
  is->thenblock = new next_statement;
  is->elseblock = NULL;
  is->tok = b->tok;
  is->thenblock->tok = b->tok;
  comparison *be = new comparison;
  be->op = "!=";
  be->tok = b->tok;
  be->left = fcus;
  be->right = new literal_string(probe_name);
  be->right->tok = b->tok;
  is->condition = be;
  b->statements.push_back(is);

  // Now replace the body
  b->statements.push_back(base->body);
  base->body = b;
}


probe*
sdt_query::convert_location ()
{
  probe_point* specific_loc = new probe_point(*base_loc);
  vector<probe_point::component*> derived_comps;

  vector<probe_point::component*>::iterator it;
  for (it = specific_loc->components.begin();
       it != specific_loc->components.end(); ++it)
    if ((*it)->functor == TOK_PROCESS)
      {
        // copy the process name
        derived_comps.push_back(*it);
      }
    else if ((*it)->functor == TOK_LIBRARY)
      {
        // copy the library name for process probes
        derived_comps.push_back(*it);
      }
    else if ((*it)->functor == TOK_PROVIDER)
      {
        // replace the possibly wildcarded arg with the specific provider name
        *it = new probe_point::component(TOK_PROVIDER,
                                         new literal_string(provider_name));
      }
    else if ((*it)->functor == TOK_MARK)
      {
        // replace the possibly wildcarded arg with the specific marker name
        *it = new probe_point::component(TOK_MARK,
                                         new literal_string(probe_name));

	if (sess.verbose > 3)
	  switch (probe_type)
	    {
	    case uprobe1_type:
              clog << _("probe_type == uprobe1, use statement addr: 0x")
		   << hex << pc << dec << endl;
	      break;
	    case uprobe2_type:
              clog << _("probe_type == uprobe2, use statement addr: 0x")
		   << hex << pc << dec << endl;
            break;
	    case uprobe3_type:
              clog << _("probe_type == uprobe3, use statement addr: 0x")
		   << hex << pc << dec << endl;
	      break;
	    default:
              clog << _F("probe_type == use_uprobe_no_dwarf, use label name: _stapprobe1_%s",
                         pp_mark.c_str()) << endl;
          }

        switch (probe_type)
          {
          case uprobe1_type:
          case uprobe2_type:
          case uprobe3_type:
            // process("executable").statement(probe_arg)
            derived_comps.push_back
              (new probe_point::component(TOK_STATEMENT,
                                          new literal_number(pc, true)));
            break;

          default: // deprecated
            // process("executable").function("*").label("_stapprobe1_MARK_NAME")
            derived_comps.push_back
              (new probe_point::component(TOK_FUNCTION,
                                          new literal_string("*")));
            derived_comps.push_back
              (new probe_point::component(TOK_LABEL,
                                          new literal_string("_stapprobe1_" + pp_mark)));
            break;
          }
      }

  probe_point* derived_loc = new probe_point(*specific_loc);
  derived_loc->components = derived_comps;
  return base_probe->create_alias(derived_loc, specific_loc);
}


void
sdt_query::query_library (const char *library)
{
    query_one_library (library, dw, user_lib, base_probe, base_loc, results);
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
  literal_map_t filled_parameters = parameters;

  string module_name;
  if (has_null_param (parameters, TOK_KERNEL))
    {
      dw = get_kern_dw(sess, "kernel");
    }
  else if (get_param (parameters, TOK_MODULE, module_name))
    {
      size_t dash_pos = 0;
      while((dash_pos=module_name.find('-'))!=string::npos)
        module_name.replace(int(dash_pos),1,"_");
      filled_parameters[TOK_MODULE] = new literal_string(module_name);
      // NB: glob patterns get expanded later, during the offline
      // elfutils module listing.
      dw = get_kern_dw(sess, module_name);
    }
  else if (get_param (parameters, TOK_PROCESS, module_name) || has_null_param(parameters, TOK_PROCESS))
      {
      module_name = sess.sysroot + module_name;
      if(has_null_param(filled_parameters, TOK_PROCESS))
        {
          wordexp_t words;
          int rc = wordexp(sess.cmd.c_str(), &words, WRDE_NOCMD|WRDE_UNDEF);
          if(rc || words.we_wordc <= 0)
            throw semantic_error(_("unspecified process probe is invalid without a -c COMMAND"));
          module_name = sess.sysroot + words.we_wordv[0];
          filled_parameters[TOK_PROCESS] = new literal_string(module_name);// this needs to be used in place of the blank map
          // in the case of TOK_MARK we need to modify locations as well
          if(location->components[0]->functor==TOK_PROCESS &&
            location->components[0]->arg == 0)
            location->components[0]->arg = new literal_string(module_name);
          wordfree (& words);
        } 

      // PR6456  process("/bin/*")  glob handling
      if (contains_glob_chars (module_name))
        {
          // Expand glob via rewriting the probe-point process("....")
          // parameter, asserted to be the first one.

          assert (location->components.size() > 0);
          assert (location->components[0]->functor == TOK_PROCESS);
          assert (location->components[0]->arg);
          literal_string* lit = dynamic_cast<literal_string*>(location->components[0]->arg);
          assert (lit);

          // Evaluate glob here, and call derive_probes recursively with each match.
          glob_t the_blob;
          int rc = glob (module_name.c_str(), 0, NULL, & the_blob);
          if (rc)
            throw semantic_error (_F("glob %s error (%s)", module_name.c_str(), lex_cast(rc).c_str() ));
          for (unsigned i = 0; i < the_blob.gl_pathc; ++i)
            {
              assert_no_interrupts();

              const char* globbed = the_blob.gl_pathv[i];
              struct stat st;

              if (access (globbed, X_OK) == 0
                  && stat (globbed, &st) == 0
                  && S_ISREG (st.st_mode)) // see find_executable()
                {
                  // Need to call canonicalize here, in order to path-expand
                  // patterns like process("stap*").  Otherwise it may go through
                  // to the next round of expansion as ("stap"), leading to a $PATH
                  // search that's not consistent with the glob search already done.

                  char *cf = canonicalize_file_name (globbed);
                  if (cf) globbed = cf;

                  // synthesize a new probe_point, with the glob-expanded string
                  probe_point *pp = new probe_point (*location);
                  // PR13338: quote results to prevent recursion
                  string eglobbed = escape_glob_chars (globbed);

                  if (sess.verbose > 1)
                    clog << _F("Expanded process(\"%s\") to process(\"%s\")",
                               module_name.c_str(), eglobbed.c_str()) << endl;
                  string eglobbed_tgt = path_remove_sysroot(sess, eglobbed);

                  probe_point::component* ppc = new probe_point::component (TOK_PROCESS,
                                                    new literal_string (eglobbed_tgt));
                  ppc->tok = location->components[0]->tok; // overwrite [0] slot, pattern matched above
                  pp->components[0] = ppc;

                  probe* new_probe = new probe (*base, pp);

                  // We override "optional = true" here, as if the
                  // wildcarded probe point was given a "?" suffix.

                  // This is because wildcard probes will be expected
                  // by users to apply only to some subset of the
                  // matching binaries, in the sense of "any", rather
                  // than "all", sort of similarly how
                  // module("*").function("...") patterns work.

                  derive_probes (sess, new_probe, finished_results,
                                 true /* NB: not location->optional */ );
                }
            }

          globfree (& the_blob);
          return; // avoid falling through
        }

      // PR13338: unquote glob results
      module_name = unescape_glob_chars (module_name);
      user_path = find_executable (module_name, "", sess.sysenv); // canonicalize it

      // if the executable starts with "#!", we look for the interpreter of the script
      {
         ifstream script_file (user_path.c_str () );

         if (script_file.good ())
         {
           string line;

           getline (script_file, line);

           if (line.compare (0, 2, "#!") == 0)
           {
              string path_head = line.substr(2);

              // remove white spaces at the beginning of the string
              size_t p2 = path_head.find_first_not_of(" \t");

              if (p2 != string::npos)
              {
                string path = path_head.substr(p2);

                // remove white spaces at the end of the string
                p2 = path.find_last_not_of(" \t\n");
                if (string::npos != p2)
                  path.erase(p2+1);

                // handle "#!/usr/bin/env" redirect
                size_t offset = 0;
                if (path.compare(0, sizeof("/bin/env")-1, "/bin/env") == 0)
                {
                  offset = sizeof("/bin/env")-1;
                }
                else if (path.compare(0, sizeof("/usr/bin/env")-1, "/usr/bin/env") == 0)
                {
                  offset = sizeof("/usr/bin/env")-1;
                }

                if (offset != 0)
                {
                    size_t p3 = path.find_first_not_of(" \t", offset);

                    if (p3 != string::npos)
                    {
                       string env_path = path.substr(p3);
                       user_path = find_executable (env_path, sess.sysroot,
                                                    sess.sysenv);
                    }
                }
                else
                {
                  user_path = find_executable (path, sess.sysroot, sess.sysenv);
                }

                struct stat st;

                if (access (user_path.c_str(), X_OK) == 0
                  && stat (user_path.c_str(), &st) == 0
                  && S_ISREG (st.st_mode)) // see find_executable()
                {
                  if (sess.verbose > 1)
                    clog << _F("Expanded process(\"%s\") to process(\"%s\")",
                               module_name.c_str(), user_path.c_str()) << endl;

                  assert (location->components.size() > 0);
                  assert (location->components[0]->functor == TOK_PROCESS);
                  assert (location->components[0]->arg);
                  literal_string* lit = dynamic_cast<literal_string*>(location->components[0]->arg);
                  assert (lit);

                  // synthesize a new probe_point, with the expanded string
                  probe_point *pp = new probe_point (*location);
                  string user_path_tgt = path_remove_sysroot(sess, user_path);
                  probe_point::component* ppc = new probe_point::component (TOK_PROCESS,
                                                                            new literal_string (user_path_tgt.c_str()));
                  ppc->tok = location->components[0]->tok; // overwrite [0] slot, pattern matched above
                  pp->components[0] = ppc;

                  probe* new_probe = new probe (*base, pp);

                  derive_probes (sess, new_probe, finished_results);

                  script_file.close();
                  return;
                }
              }
           }
         }
         script_file.close();
      }

      if (get_param (parameters, TOK_LIBRARY, user_lib)
	  && user_lib.length() && ! contains_glob_chars (user_lib))
	{
	  module_name = find_executable (user_lib, sess.sysroot, sess.sysenv,
					 "LD_LIBRARY_PATH");
	  if (module_name.find('/') == string::npos)
	    // We didn't find user_lib so use iterate_over_libraries
	    module_name = user_path;
	}
      else
	module_name = user_path; // canonicalize it

      if (kernel_supports_inode_uprobes(sess))
        {
          // XXX: autoconf this?
          if (has_null_param(parameters, TOK_RETURN))
            throw semantic_error
              (_("process return probes not available with inode-based uprobes"));
        }
      // There is a similar check in pass 4 (buildrun), but it is
      // needed here too to make sure alternatives for optional
      // (? or !) process probes are disposed and/or alternatives
      // are selected.
      check_process_probe_kernel_support(sess);

      // user-space target; we use one dwflpp instance per module name
      // (= program or shared library)
      dw = get_user_dw(sess, module_name);
    }

  if (sess.verbose > 3)
    clog << _F("dwarf_builder::build for %s", module_name.c_str()) << endl;

  string dummy_mark_name; // NB: PR10245: dummy value, need not substitute - => __
  if (get_param(parameters, TOK_MARK, dummy_mark_name))
    {
      sdt_query sdtq(base, location, *dw, filled_parameters, finished_results, user_lib);
      dw->iterate_over_modules(&query_module, &sdtq);
      return;
    }

  unsigned results_pre = finished_results.size();
  dwarf_query q(base, location, *dw, filled_parameters, finished_results, user_path, user_lib);

  // XXX: kernel.statement.absolute is a special case that requires no
  // dwfl processing.  This code should be in a separate builder.
  if (q.has_kernel && q.has_absolute)
    {
      // assert guru mode for absolute probes
      if (! q.base_probe->privileged)
        {
          throw semantic_error (_("absolute statement probe in unprivileged script; need stap -g"),
                                q.base_probe->tok);
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

  dw->iterate_over_modules(&query_module, &q);


  // PR11553 special processing: .return probes requested, but
  // some inlined function instances matched.
  unsigned i_n_r = q.inlined_non_returnable.size();
  unsigned results_post = finished_results.size();
  if (i_n_r > 0)
    {
      if ((results_pre == results_post) && (! sess.suppress_warnings)) // no matches; issue warning
        {
          string quicklist;
          for (set<string>::iterator it = q.inlined_non_returnable.begin();
               it != q.inlined_non_returnable.end();
               it++)
            {
              quicklist += " " + (*it);
              if (quicklist.size() > 80) // heuristic, don't make an overlong report line
                {
                  quicklist += " ...";
                  break;
                }
            }

          sess.print_warning (_F(ngettext("cannot probe .return of %u inlined function %s",
                                          "cannot probe .return of %u inlined functions %s",
                                           quicklist.size()), i_n_r, quicklist.c_str()));
          // There will be also a "no matches" semantic error generated.
        }
      if (sess.verbose > 1)
        clog << _F(ngettext("skipped .return probe of %u inlined function",
                            "skipped .return probe of %u inlined functions", i_n_r), i_n_r) << endl;
      if ((sess.verbose > 3) || (sess.verbose > 2 && results_pre == results_post)) // issue details with high verbosity
        {
          for (set<string>::iterator it = q.inlined_non_returnable.begin();
               it != q.inlined_non_returnable.end();
               it++)
            clog << (*it) << " ";
          clog << endl;
        }
    } // i_n_r > 0
}

symbol_table::~symbol_table()
{
  delete_map(map_by_addr);
}

void
symbol_table::add_symbol(const char *name, bool weak, bool descriptor,
                         Dwarf_Addr addr, Dwarf_Addr */*high_addr*/)
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
  fi->descriptor = descriptor;
  map_by_name[fi->name] = fi;
  // TODO: Use a multimap in case there are multiple static
  // functions with the same name?
  map_by_addr.insert(make_pair(addr, fi));
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
          cerr << _F("Symbol table error: Line %d of symbol list from %s is not in correct format: address type name [module]\n",
                     line, path.c_str());
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
        add_symbol(name, (type == 'W'), false, (Dwarf_Addr) addr, &high_addr);
    }

  if (map_by_addr.size() < 1)
    {
      cerr << _F("Symbol table error: %s contains no function symbols.\n",
                 path.c_str()) << endl;
      return info_absent;
    }
  return info_present;
}

// NB: This currently unused.  We use get_from_elf() instead because
// that gives us raw addresses -- which we need for modules -- whereas
// nm provides the address relative to the beginning of the section.
enum info_status
symbol_table::read_from_elf_file(const string &path,
				 systemtap_session &sess)
{
  vector<string> cmd;
  cmd.push_back("/usr/bin/nm");
  cmd.push_back("-n");
  cmd.push_back("--defined-only");
  cmd.push_back("path");

  FILE *f;
  int child_fd;
  pid_t child = stap_spawn_piped(sess.verbose, cmd, NULL, &child_fd);
  if (child <= 0 || !(f = fdopen(child_fd, "r")))
    {
      // nm failures are detected by stap_waitpid
      cerr << _F("Internal error reading symbol table from %s -- %s\n",
                 path.c_str(), strerror(errno));
      return info_absent;
    }
  enum info_status status = read_symbols(f, path);
  if (fclose(f) || stap_waitpid(sess.verbose, child))
    {
      if (status == info_present)
        sess.print_warning("nm cannot read symbol table from " + path);
      return info_absent;
    }
  return status;
}

enum info_status
symbol_table::read_from_text_file(const string& path,
				  systemtap_session &sess)
{
  FILE *f = fopen(path.c_str(), "r");
  if (!f)
    {
      sess.print_warning("cannot read symbol table from " + path + " -- " + strerror(errno));
      return info_absent;
    }
  enum info_status status = read_symbols(f, path);
  (void) fclose(f);
  return status;
}

void
symbol_table::prepare_section_rejection(Dwfl_Module *mod __attribute__ ((unused)))
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
  if (elf_getshdrstrndx (elf, &shstrndx) != 0)
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
      if (name && GELF_ST_TYPE(sym.st_info) == STT_FUNC)
        add_symbol(name, (GELF_ST_BIND(sym.st_info) == STB_WEAK),
                   reject_section(section), sym.st_value, &high_addr);
    }
  return info_present;
}

func_info *
symbol_table::get_func_containing_address(Dwarf_Addr addr)
{
  iterator_t iter = map_by_addr.upper_bound(addr);
  if (iter == map_by_addr.begin())
    return NULL;
  else
    return (--iter)->second;
}

func_info *
symbol_table::get_first_func()
{
  iterator_t iter = map_by_addr.begin();
  return (iter)->second;
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
  range_t purge_range = map_by_addr.equal_range(stub_addr);
  for (iterator_t iter = purge_range.first;
       iter != purge_range.second;
       )
    {
      func_info *fi = iter->second;
      if (fi->weak && fi->name != "sys_ni_syscall")
        {
          map_by_name.erase(fi->name);
          map_by_addr.erase(iter++);
          delete fi;
        }
      else
        iter++;
    }
}

void
module_info::get_symtab(dwarf_query *q)
{
  systemtap_session &sess = q->sess;

  if (symtab_status != info_unknown)
    return;

  sym_table = new symbol_table(this);
  if (!elf_path.empty())
    {
      if (name == TOK_KERNEL && !sess.kernel_symtab_path.empty())
        sess.print_warning("reading symbol table from " + elf_path + " -- ignoring " + sess.kernel_symtab_path.c_str());
      symtab_status = sym_table->get_from_elf();
    }
  else
    {
      assert(name == TOK_KERNEL);
      if (sess.kernel_symtab_path.empty())
        {
          symtab_status = info_absent;
          cerr << _("Error: Cannot find vmlinux.\n"
                  "  Consider using --kmap instead of --kelf.")
               << endl;;
        }
      else
        {
          symtab_status =
	    sym_table->read_from_text_file(sess.kernel_symtab_path, sess);
          if (symtab_status == info_present)
            {
              sess.sym_kprobes_text_start =
                sym_table->lookup_symbol_address("__kprobes_text_start");
              sess.sym_kprobes_text_end =
                sym_table->lookup_symbol_address("__kprobes_text_end");
              sess.sym_stext = sym_table->lookup_symbol_address("_stext");
            }
        }
    }
  if (symtab_status == info_absent)
    {
      delete sym_table;
      sym_table = NULL;
      return;
    }

  if (name == TOK_KERNEL)
    sym_table->purge_syscall_stubs();
}

// update_symtab reconciles data between the elf symbol table and the dwarf
// function enumeration.  It updates the symbol table entries with the dwarf
// die that describes the function, which also signals to query_module_symtab
// that a statement probe isn't needed.  In return, it also adds aliases to the
// function table for names that share the same addr/die.
void
module_info::update_symtab(cu_function_cache_t *funcs)
{
  if (!sym_table)
    return;

  cu_function_cache_t new_funcs;

  for (cu_function_cache_t::iterator func = funcs->begin();
       func != funcs->end(); func++)
    {
      // optimization: inlines will never be in the symbol table
      if (dwarf_func_inline(&func->second) != 0)
        continue;

      // XXX We may want to make additional efforts to match mangled elf names
      // to dwarf too.  MIPS_linkage_name can help, but that's sometimes
      // missing, so we may also need to try matching by address.  See also the
      // notes about _Z in dwflpp::iterate_over_functions().

      func_info *fi = sym_table->lookup_symbol(func->first);
      if (!fi)
        continue;

      // iterate over all functions at the same address
      symbol_table::range_t er = sym_table->map_by_addr.equal_range(fi->addr);
      for (symbol_table::iterator_t it = er.first; it != er.second; ++it)
        {
          // update this function with the dwarf die
          it->second->die = func->second;

          // if this function is a new alias, then
          // save it to merge into the function cache
          if (it->second != fi)
            new_funcs.insert(make_pair(it->second->name, it->second->die));
        }
    }

  // add all discovered aliases back into the function cache
  // NB: this won't replace any names that dwarf may have already found
  funcs->insert(new_funcs.begin(), new_funcs.end());
}

module_info::~module_info()
{
  if (sym_table)
    delete sym_table;
}

// ------------------------------------------------------------------------
// user-space probes
// ------------------------------------------------------------------------


struct uprobe_derived_probe_group: public generic_dpg<uprobe_derived_probe>
{
private:
  string make_pbm_key (uprobe_derived_probe* p) {
    return p->path + "|" + p->module + "|" + p->section + "|" + lex_cast(p->pid);
  }

  void emit_module_maxuprobes (systemtap_session& s);

  // Using our own utrace-based uprobes
  void emit_module_utrace_decls (systemtap_session& s);
  void emit_module_utrace_init (systemtap_session& s);
  void emit_module_utrace_exit (systemtap_session& s);

  // Using the upstream inode-based uprobes
  void emit_module_inode_decls (systemtap_session& s);
  void emit_module_inode_init (systemtap_session& s);
  void emit_module_inode_exit (systemtap_session& s);

public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


void
uprobe_derived_probe::join_group (systemtap_session& s)
{
  if (! s.uprobe_derived_probes)
    s.uprobe_derived_probes = new uprobe_derived_probe_group ();
  s.uprobe_derived_probes->enroll (this);
  enable_task_finder(s);

  // Ask buildrun.cxx to build extra module if needed, and
  // signal staprun to load that module.  If we're using the builtin
  // inode-uprobes, we still need to know that it is required.
  s.need_uprobes = true;
}


void
uprobe_derived_probe::getargs(std::list<std::string> &arg_set) const
{
  dwarf_derived_probe::getargs(arg_set);
  arg_set.insert(arg_set.end(), args.begin(), args.end());
}


void
uprobe_derived_probe::saveargs(int nargs)
{
  for (int i = 1; i <= nargs; i++)
    args.push_back("$arg" + lex_cast (i) + ":long");
}


void
uprobe_derived_probe::emit_privilege_assertion (translator_output* o)
{
  // These probes are allowed for unprivileged users, but only in the
  // context of processes which they own.
  emit_process_owner_assertion (o);
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

    if (kernel_supports_inode_uprobes(sess))
      throw semantic_error (_("absolute process probes not available with inode-based uprobes"));

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
uprobe_derived_probe_group::emit_module_maxuprobes (systemtap_session& s)
{
  // We'll probably need at least this many:
  unsigned minuprobes = probes.size();
  // .. but we don't want so many that .bss is inflated (PR10507):
  unsigned uprobesize = 64;
  unsigned maxuprobesmem = 10*1024*1024; // 10 MB
  unsigned maxuprobes = maxuprobesmem / uprobesize;

  // Let's choose a value on the geometric middle.  This should end up
  // between minuprobes and maxuprobes.  It's OK if this number turns
  // out to be < minuprobes or > maxuprobes.  At worst, we get a
  // run-time error of one kind (too few: missed uprobe registrations)
  // or another (too many: vmalloc errors at module load time).
  unsigned default_maxuprobes = (unsigned)sqrt((double)minuprobes * (double)maxuprobes);

  s.op->newline() << "#ifndef MAXUPROBES";
  s.op->newline() << "#define MAXUPROBES " << default_maxuprobes;
  s.op->newline() << "#endif";
}


void
uprobe_derived_probe_group::emit_module_utrace_decls (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- utrace uprobes ---- */";
  // If uprobes isn't in the kernel, pull it in from the runtime.

  s.op->newline() << "#if defined(CONFIG_UPROBES) || defined(CONFIG_UPROBES_MODULE)";
  s.op->newline() << "#include <linux/uprobes.h>";
  s.op->newline() << "#else";
  s.op->newline() << "#include \"uprobes/uprobes.h\"";
  s.op->newline() << "#endif";
  s.op->newline() << "#ifndef UPROBES_API_VERSION";
  s.op->newline() << "#define UPROBES_API_VERSION 1";
  s.op->newline() << "#endif";

  emit_module_maxuprobes (s);

  // Forward decls
  s.op->newline() << "#include \"uprobes-common.h\"";

  // In .bss, the shared pool of uprobe/uretprobe structs.  These are
  // too big to embed in the initialized .data stap_uprobe_spec array.
  // XXX: consider a slab cache or somesuch for stap_uprobes
  s.op->newline() << "static struct stap_uprobe stap_uprobes [MAXUPROBES];";
  s.op->newline() << "DEFINE_MUTEX(stap_uprobes_lock);"; // protects against concurrent registration/unregistration

  s.op->assert_0_indent();

  // Assign task-finder numbers as we build up the stap_uprobe_tf table.
  // This means we process probes[] in two passes.
  map <string,unsigned> module_index;
  unsigned module_index_ctr = 0;

  // not const since embedded task_finder_target struct changes
  s.op->newline() << "static struct stap_uprobe_tf stap_uprobe_finders[] = {";
  s.op->indent(1);
  for (unsigned i=0; i<probes.size(); i++)
    {
      uprobe_derived_probe *p = probes[i];
      string pbmkey = make_pbm_key (p);
      if (module_index.find (pbmkey) == module_index.end())
        {
          module_index[pbmkey] = module_index_ctr++;

          s.op->newline() << "{";
          // NB: it's essential that make_pbm_key() use all of and
          // only the same fields as we're about to emit.
          s.op->line() << " .finder={";
          if (p->pid != 0)
            s.op->line() << " .pid=" << p->pid << ",";

          if (p->section == "") // .statement(addr).absolute
            s.op->line() << " .callback=&stap_uprobe_process_found,";
          else if (p->section == ".absolute") // proxy for ET_EXEC -> exec()'d program
            {
              s.op->line() << " .procname=" << lex_cast_qstring(p->module) << ",";
              s.op->line() << " .callback=&stap_uprobe_process_found,";
            }
	  else if (p->section != ".absolute") // ET_DYN
            {
	      if (p->has_library)
	        s.op->line() << " .procname=\"" << p->path << "\", ";
              s.op->line() << " .mmap_callback=&stap_uprobe_mmap_found, ";
              s.op->line() << " .munmap_callback=&stap_uprobe_munmap_found, ";
              s.op->line() << " .callback=&stap_uprobe_process_munmap,";
            }
          s.op->line() << " },";
          if (p->module != "")
            s.op->line() << " .pathname=" << lex_cast_qstring(p->module) << ", ";
          s.op->line() << " },";
        }
      else
        { } // skip it in this pass, already have a suitable stap_uprobe_tf slot for it.
    }
  s.op->newline(-1) << "};";

  s.op->assert_0_indent();

   // NB: read-only structure
  s.op->newline() << "static const struct stap_uprobe_spec stap_uprobe_specs [] = {";
  s.op->indent(1);
  for (unsigned i =0; i<probes.size(); i++)
    {
      uprobe_derived_probe* p = probes[i];
      s.op->newline() << "{";
      string key = make_pbm_key (p);
      unsigned value = module_index[key];
      if (value != 0)
        s.op->line() << " .tfi=" << value << ",";
      s.op->line() << " .address=(unsigned long)0x" << hex << p->addr << dec << "ULL,";
      s.op->line() << " .probe=" << common_probe_init (p) << ",";

      if (p->sdt_semaphore_addr != 0)
        s.op->line() << " .sdt_sem_offset=(unsigned long)0x"
                     << hex << p->sdt_semaphore_addr << dec << "ULL,";

      if (p->has_return)
        s.op->line() << " .return_p=1,";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";

  s.op->assert_0_indent();

  s.op->newline() << "static void enter_uprobe_probe (struct uprobe *inst, struct pt_regs *regs) {";
  s.op->newline(1) << "struct stap_uprobe *sup = container_of(inst, struct stap_uprobe, up);";
  s.op->newline() << "const struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sups->probe",
				 "_STP_PROBE_HANDLER_UPROBE");
  s.op->newline() << "if (sup->spec_index < 0 || "
                  << "sup->spec_index >= " << probes.size() << ") {";
  s.op->newline(1) << "_stp_error (\"bad spec_index %d (max " << probes.size()
		   << "): %s\", sup->spec_index, c->probe_point);";
  s.op->newline() << "atomic_dec (&c->busy);";
  s.op->newline() << "goto probe_epilogue;";
  s.op->newline(-1) << "}";
  s.op->newline() << "c->uregs = regs;";
  s.op->newline() << "c->probe_flags |= _STP_PROBE_STATE_USER_MODE;";

  // Make it look like the IP is set as it would in the actual user
  // task when calling real probe handler. Reset IP regs on return, so
  // we don't confuse uprobes. PR10458
  s.op->newline() << "{";
  s.op->indent(1);
  s.op->newline() << "unsigned long uprobes_ip = REG_IP(c->uregs);";
  s.op->newline() << "SET_REG_IP(regs, inst->vaddr);";
  s.op->newline() << "(*sups->probe->ph) (c);";
  s.op->newline() << "SET_REG_IP(regs, uprobes_ip);";
  s.op->newline(-1) << "}";

  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline(-1) << "}";

  s.op->newline() << "static void enter_uretprobe_probe (struct uretprobe_instance *inst, struct pt_regs *regs) {";
  s.op->newline(1) << "struct stap_uprobe *sup = container_of(inst->rp, struct stap_uprobe, urp);";
  s.op->newline() << "const struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sups->probe",
				 "_STP_PROBE_HANDLER_URETPROBE");
  s.op->newline() << "c->ips.ri = inst;";
  s.op->newline() << "if (sup->spec_index < 0 || "
                  << "sup->spec_index >= " << probes.size() << ") {";
  s.op->newline(1) << "_stp_error (\"bad spec_index %d (max " << probes.size()
		   << "): %s\", sup->spec_index, c->probe_point);";
  s.op->newline() << "atomic_dec (&c->busy);";
  s.op->newline() << "goto probe_epilogue;";
  s.op->newline(-1) << "}";

  s.op->newline() << "c->uregs = regs;";
  s.op->newline() << "c->probe_flags |= _STP_PROBE_STATE_USER_MODE;";

  // Make it look like the IP is set as it would in the actual user
  // task when calling real probe handler. Reset IP regs on return, so
  // we don't confuse uprobes. PR10458
  s.op->newline() << "{";
  s.op->indent(1);
  s.op->newline() << "unsigned long uprobes_ip = REG_IP(c->uregs);";
  s.op->newline() << "SET_REG_IP(regs, inst->ret_addr);";
  s.op->newline() << "(*sups->probe->ph) (c);";
  s.op->newline() << "SET_REG_IP(regs, uprobes_ip);";
  s.op->newline(-1) << "}";

  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline(-1) << "}";

  s.op->newline();
  s.op->newline() << "#include \"uprobes-common.c\"";
  s.op->newline();
}


void
uprobe_derived_probe_group::emit_module_utrace_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- utrace uprobes ---- */";

  s.op->newline() << "for (j=0; j<MAXUPROBES; j++) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[j];";
  s.op->newline() << "sup->spec_index = -1;"; // free slot
  // NB: we assume the rest of the struct (specificaly, sup->up) is
  // initialized to zero.  This is so that we can use
  // sup->up->kdata = NULL for "really free!"  PR 6829.
  s.op->newline(-1) << "}";
  s.op->newline() << "mutex_init (& stap_uprobes_lock);";

  // Set up the task_finders
  s.op->newline() << "for (i=0; i<sizeof(stap_uprobe_finders)/sizeof(stap_uprobe_finders[0]); i++) {";
  s.op->newline(1) << "struct stap_uprobe_tf *stf = & stap_uprobe_finders[i];";
  s.op->newline() << "probe_point = stf->pathname;"; // for error messages; XXX: would prefer pp() or something better
  s.op->newline() << "rc = stap_register_task_finder_target (& stf->finder);";

  // NB: if (rc), there is no need (XXX: nor any way) to clean up any
  // finders already registered, since mere registration does not
  // cause any utrace or memory allocation actions.  That happens only
  // later, once the task finder engine starts running.  So, for a
  // partial initialization requiring unwind, we need do nothing.
  s.op->newline() << "if (rc) break;";

  s.op->newline(-1) << "}";
}


void
uprobe_derived_probe_group::emit_module_utrace_exit (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- utrace uprobes ---- */";

  // NB: there is no stap_unregister_task_finder_target call;
  // important stuff like utrace cleanups are done by
  // __stp_task_finder_cleanup() via stap_stop_task_finder().
  //
  // This function blocks until all callbacks are completed, so there
  // is supposed to be no possibility of any registration-related code starting
  // to run in parallel with our shutdown here.  So we don't need to protect the
  // stap_uprobes[] array with the mutex.

  s.op->newline() << "for (j=0; j<MAXUPROBES; j++) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[j];";
  s.op->newline() << "const struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  s.op->newline() << "if (sup->spec_index < 0) continue;"; // free slot

  // PR10655: decrement that ENABLED semaphore
  s.op->newline() << "if (sup->sdt_sem_address) {";
  s.op->newline(1) << "unsigned short sdt_semaphore;"; // NB: fixed size
  s.op->newline() << "pid_t pid = (sups->return_p ? sup->urp.u.pid : sup->up.pid);";
  s.op->newline() << "struct task_struct *tsk;";
  s.op->newline() << "rcu_read_lock();";

  // Do a pid->task_struct* lookup.  For 2.6.24+, this code assumes
  // that the pid is always in the global namespace, not in any
  // private namespace.
  s.op->newline() << "#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)";
  // We'd like to call find_task_by_pid_ns() here, but it isn't
  // exported.  So, we call what it calls...
  s.op->newline() << "  tsk = pid_task(find_pid_ns(pid, &init_pid_ns), PIDTYPE_PID);";
  s.op->newline() << "#else";
  s.op->newline() << "  tsk = find_task_by_pid (pid);";
  s.op->newline() << "#endif /* 2.6.24 */";

  s.op->newline() << "if (tsk) {"; // just in case the thing exited while we weren't watching
  s.op->newline(1) << "if (__access_process_vm_noflush(tsk, sup->sdt_sem_address, &sdt_semaphore, sizeof(sdt_semaphore), 0)) {";
  s.op->newline(1) << "sdt_semaphore --;";
  s.op->newline() << "#ifdef DEBUG_UPROBES";
  s.op->newline() << "_stp_dbug (__FUNCTION__,__LINE__, \"-semaphore %#x @ %#lx\\n\", sdt_semaphore, sup->sdt_sem_address);";
  s.op->newline() << "#endif";
  s.op->newline() << "__access_process_vm_noflush(tsk, sup->sdt_sem_address, &sdt_semaphore, sizeof(sdt_semaphore), 1);";
  s.op->newline(-1) << "}";
  // XXX: need to analyze possibility of race condition
  s.op->newline(-1) << "}";
  s.op->newline() << "rcu_read_unlock();";
  s.op->newline(-1) << "}";

  s.op->newline() << "if (sups->return_p) {";
  s.op->newline(1) << "#ifdef DEBUG_UPROBES";
  s.op->newline() << "_stp_dbug (__FUNCTION__,__LINE__, \"-uretprobe spec %d index %d pid %d addr %p\\n\", sup->spec_index, j, sup->up.pid, (void*) sup->up.vaddr);";
  s.op->newline() << "#endif";
  // NB: PR6829 does not change that we still need to unregister at
  // *this* time -- when the script as a whole exits.
  s.op->newline() << "unregister_uretprobe (& sup->urp);";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "#ifdef DEBUG_UPROBES";
  s.op->newline() << "_stp_dbug (__FUNCTION__,__LINE__, \"-uprobe spec %d index %d pid %d addr %p\\n\", sup->spec_index, j, sup->up.pid, (void*) sup->up.vaddr);";
  s.op->newline() << "#endif";
  s.op->newline() << "unregister_uprobe (& sup->up);";
  s.op->newline(-1) << "}";

  s.op->newline() << "sup->spec_index = -1;";

  // XXX: uprobe missed counts?

  s.op->newline(-1) << "}";

  s.op->newline() << "mutex_destroy (& stap_uprobes_lock);";
}


void
uprobe_derived_probe_group::emit_module_inode_decls (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- inode uprobes ---- */";
  emit_module_maxuprobes (s);
  s.op->newline() << "#include \"uprobes-inode.c\"";

  // Write the probe handler.
  s.op->newline() << "static int enter_inode_uprobe "
                  << "(struct uprobe_consumer *inst, struct pt_regs *regs) {";
  s.op->newline(1) << "struct stapiu_consumer *sup = "
                   << "container_of(inst, struct stapiu_consumer, consumer);";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sup->probe",
                                 "_STP_PROBE_HANDLER_UPROBE");
  s.op->newline() << "c->uregs = regs;";
  s.op->newline() << "c->probe_flags |= _STP_PROBE_STATE_USER_MODE;";
  // XXX: Can't set SET_REG_IP; we don't actually know the relocated address.
  // ...  In some error cases, uprobes itself calls uprobes_get_bkpt_addr().
  s.op->newline() << "(*sup->probe->ph) (c);";
  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";
  s.op->assert_0_indent();

  // Index of all the modules for which we need inodes.
  map<string, unsigned> module_index;
  unsigned module_index_ctr = 0;

  // Discover and declare targets for each unique path.
  s.op->newline() << "static struct stapiu_target "
                  << "stap_inode_uprobe_targets[] = {";
  s.op->indent(1);
  for (unsigned i=0; i<probes.size(); i++)
    {
      uprobe_derived_probe *p = probes[i];
      const string key = make_pbm_key(p);
      if (module_index.find (key) == module_index.end())
        {
          module_index[key] = module_index_ctr++;
          s.op->newline() << "{";
          s.op->line() << " .finder={";
          if (p->pid != 0)
            s.op->line() << " .pid=" << p->pid << ",";

          if (p->section == "") // .statement(addr).absolute  XXX?
            s.op->line() << " .callback=&stapiu_process_found,";
          else if (p->section == ".absolute") // proxy for ET_EXEC -> exec()'d program
            {
              s.op->line() << " .procname=" << lex_cast_qstring(p->module) << ",";
              s.op->line() << " .callback=&stapiu_process_found,";
            }
          else if (p->section != ".absolute") // ET_DYN
            {
              if (p->has_library)
                s.op->line() << " .procname=\"" << p->path << "\", ";
              s.op->line() << " .mmap_callback=&stapiu_mmap_found, ";
              s.op->line() << " .munmap_callback=&stapiu_munmap_found, ";
              s.op->line() << " .callback=&stapiu_process_munmap,";
            }
          s.op->line() << " },";
          s.op->line() << " .filename=" << lex_cast_qstring(p->module) << ",";
          s.op->line() << " },";
        }
    }
  s.op->newline(-1) << "};";
  s.op->assert_0_indent();

  // Declare the actual probes.
  s.op->newline() << "static struct stapiu_consumer "
                  << "stap_inode_uprobe_consumers[] = {";
  s.op->indent(1);
  for (unsigned i=0; i<probes.size(); i++)
    {
      uprobe_derived_probe *p = probes[i];
      unsigned index = module_index[make_pbm_key(p)];
      s.op->newline() << "{";
      s.op->line() << " .consumer={ .handler=enter_inode_uprobe },";
      s.op->line() << " .target=&stap_inode_uprobe_targets[" << index << "],";
      s.op->line() << " .offset=(loff_t)0x" << hex << p->addr << dec << "ULL,";
      if (p->sdt_semaphore_addr)
        s.op->line() << " .sdt_sem_offset=(loff_t)0x"
                     << hex << p->sdt_semaphore_addr << dec << "ULL,";
      s.op->line() << " .probe=" << common_probe_init (p) << ",";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->assert_0_indent();
}


void
uprobe_derived_probe_group::emit_module_inode_init (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- inode uprobes ---- */";
  // Let stapiu_init() handle reporting errors by setting probe_point
  // to NULL.
  s.op->newline() << "probe_point = NULL;";
  s.op->newline() << "rc = stapiu_init ("
                  << "stap_inode_uprobe_targets, "
                  << "ARRAY_SIZE(stap_inode_uprobe_targets), "
                  << "stap_inode_uprobe_consumers, "
                  << "ARRAY_SIZE(stap_inode_uprobe_consumers));";
}


void
uprobe_derived_probe_group::emit_module_inode_exit (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "/* ---- inode uprobes ---- */";
  s.op->newline() << "stapiu_exit ("
                  << "stap_inode_uprobe_targets, "
                  << "ARRAY_SIZE(stap_inode_uprobe_targets), "
                  << "stap_inode_uprobe_consumers, "
                  << "ARRAY_SIZE(stap_inode_uprobe_consumers));";
}


void
uprobe_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (kernel_supports_inode_uprobes (s))
    emit_module_inode_decls (s);
  else
    emit_module_utrace_decls (s);
}


void
uprobe_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (kernel_supports_inode_uprobes (s))
    emit_module_inode_init (s);
  else
    emit_module_utrace_init (s);
}


void
uprobe_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (kernel_supports_inode_uprobes (s))
    emit_module_inode_exit (s);
  else
    emit_module_utrace_exit (s);
}


// ------------------------------------------------------------------------
// Kprobe derived probes
// ------------------------------------------------------------------------

static const string TOK_KPROBE("kprobe");

struct kprobe_derived_probe: public derived_probe
{
  kprobe_derived_probe (probe *base,
			probe_point *location,
			const string& name,
			int64_t stmt_addr,
			bool has_return,
			bool has_statement,
			bool has_maxactive,
			bool has_path,
			bool has_library,
			long maxactive_val,
			const string& path,
			const string& library
			);
  string symbol_name;
  Dwarf_Addr addr;
  bool has_return;
  bool has_statement;
  bool has_maxactive;
  bool has_path;
  bool has_library;
  long maxactive_val;
  string path;
  string library;
  bool access_var;
  void printsig (std::ostream &o) const;
  void join_group (systemtap_session& s);
};

struct kprobe_derived_probe_group: public derived_probe_group
{
private:
  multimap<string,kprobe_derived_probe*> probes_by_module;
  typedef multimap<string,kprobe_derived_probe*>::iterator p_b_m_iterator;

public:
  void enroll (kprobe_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};

kprobe_derived_probe::kprobe_derived_probe (probe *base,
					    probe_point *location,
					    const string& name,
					    int64_t stmt_addr,
					    bool has_return,
					    bool has_statement,
					    bool has_maxactive,
					    bool has_path,
					    bool has_library,
					    long maxactive_val,
					    const string& path,
					    const string& library
					    ):
  derived_probe (base, location, true /* .components soon rewritten */ ),
  symbol_name (name), addr (stmt_addr),
  has_return (has_return), has_statement (has_statement),
  has_maxactive (has_maxactive), has_path (has_path),
  has_library (has_library),
  maxactive_val (maxactive_val),
  path (path), library (library)
{
  this->tok = base->tok;
  this->access_var = false;

#ifndef USHRT_MAX
#define USHRT_MAX 32767
#endif

  // Expansion of $target variables in the probe body produces an error during
  // translate phase, since we're not using debuginfo

  vector<probe_point::component*> comps;
  comps.push_back (new probe_point::component(TOK_KPROBE));

  if (has_statement)
    {
      comps.push_back (new probe_point::component(TOK_STATEMENT,
                                                  new literal_number(addr, true)));
      comps.push_back (new probe_point::component(TOK_ABSOLUTE));
    }
  else
    {
      size_t pos = name.find(':');
      if (pos != string::npos)
        {
          string module = name.substr(0, pos);
          string function = name.substr(pos + 1);
          comps.push_back (new probe_point::component(TOK_MODULE, new literal_string(module)));
          comps.push_back (new probe_point::component(TOK_FUNCTION, new literal_string(function)));
        }
      else
        comps.push_back (new probe_point::component(TOK_FUNCTION, new literal_string(name)));
    }

  if (has_return)
    comps.push_back (new probe_point::component(TOK_RETURN));
  if (has_maxactive)
    comps.push_back (new probe_point::component(TOK_MAXACTIVE, new literal_number(maxactive_val)));

  this->sole_location()->components = comps;
}

void kprobe_derived_probe::printsig (ostream& o) const
{
  sole_location()->print (o);
  o << " /* " << " name = " << symbol_name << "*/";
  printsig_nested (o);
}

void kprobe_derived_probe::join_group (systemtap_session& s)
{

  if (! s.kprobe_derived_probes)
	s.kprobe_derived_probes = new kprobe_derived_probe_group ();
  s.kprobe_derived_probes->enroll (this);

}

void kprobe_derived_probe_group::enroll (kprobe_derived_probe* p)
{
  probes_by_module.insert (make_pair (p->symbol_name, p));
  // probes of same symbol should share single kprobe/kretprobe
}

void
kprobe_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_module.empty()) return;

  s.op->newline() << "/* ---- kprobe-based probes ---- */";

  // Warn of misconfigured kernels
  s.op->newline() << "#if ! defined(CONFIG_KPROBES)";
  s.op->newline() << "#error \"Need CONFIG_KPROBES!\"";
  s.op->newline() << "#endif";
  s.op->newline();

  s.op->newline() << "#ifndef KRETACTIVE";
  s.op->newline() << "#define KRETACTIVE (max(15,6*(int)num_possible_cpus()))";
  s.op->newline() << "#endif";

  // Forward declare the master entry functions
  s.op->newline() << "static int enter_kprobe2_probe (struct kprobe *inst,";
  s.op->line() << " struct pt_regs *regs);";
  s.op->newline() << "static int enter_kretprobe2_probe (struct kretprobe_instance *inst,";
  s.op->line() << " struct pt_regs *regs);";

  // Emit an array of kprobe/kretprobe pointers
  s.op->newline() << "#if defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline() << "static void * stap_unreg_kprobes2[" << probes_by_module.size() << "];";
  s.op->newline() << "#endif";

  // Emit the actual probe list.

  s.op->newline() << "static struct stap_dwarfless_kprobe {";
  s.op->newline(1) << "union { struct kprobe kp; struct kretprobe krp; } u;";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "struct kprobe dummy;";
  s.op->newline() << "#endif";
  s.op->newline(-1) << "} stap_dwarfless_kprobes[" << probes_by_module.size() << "];";
  // NB: bss!

  s.op->newline() << "static struct stap_dwarfless_probe {";
  s.op->newline(1) << "const unsigned return_p:1;";
  s.op->newline() << "const unsigned maxactive_p:1;";
  s.op->newline() << "const unsigned optional_p:1;";
  s.op->newline() << "unsigned registered_p:1;";
  s.op->newline() << "const unsigned short maxactive_val;";

  // Function Names are mostly small and uniform enough to justify putting
  // char[MAX]'s into  the array instead of relocated char*'s.

  size_t symbol_string_name_max = 0;
  size_t symbol_string_name_tot = 0;
  for (p_b_m_iterator it = probes_by_module.begin(); it != probes_by_module.end(); it++)
    {
      kprobe_derived_probe* p = it->second;
#define DOIT(var,expr) do {                             \
        size_t var##_size = (expr) + 1;                 \
        var##_max = max (var##_max, var##_size);        \
        var##_tot += var##_size; } while (0)
      DOIT(symbol_string_name, p->symbol_name.size());
#undef DOIT
    }

#define CALCIT(var)                                                     \
	s.op->newline() << "const char " << #var << "[" << var##_name_max << "] ;";

  CALCIT(symbol_string);
#undef CALCIT

  s.op->newline() << "unsigned long address;";
  s.op->newline() << "struct stap_probe * const probe;";
  s.op->newline(-1) << "} stap_dwarfless_probes[] = {";
  s.op->indent(1);

  for (p_b_m_iterator it = probes_by_module.begin(); it != probes_by_module.end(); it++)
    {
      kprobe_derived_probe* p = it->second;
      s.op->newline() << "{";
      if (p->has_return)
        s.op->line() << " .return_p=1,";

      if (p->has_maxactive)
        {
          s.op->line() << " .maxactive_p=1,";
          assert (p->maxactive_val >= 0 && p->maxactive_val <= USHRT_MAX);
          s.op->line() << " .maxactive_val=" << p->maxactive_val << ",";
        }

      if (p->locations[0]->optional)
        s.op->line() << " .optional_p=1,";

      if (p->has_statement)
        s.op->line() << " .address=(unsigned long)0x" << hex << p->addr << dec << "ULL,";
      else
        s.op->line() << " .symbol_string=\"" << p->symbol_name << "\",";

      s.op->line() << " .probe=" << common_probe_init (p) << ",";
      s.op->line() << " },";
    }

  s.op->newline(-1) << "};";

  // Emit the kprobes callback function
  s.op->newline();
  s.op->newline() << "static int enter_kprobe2_probe (struct kprobe *inst,";
  s.op->line() << " struct pt_regs *regs) {";
  // NB: as of PR5673, the kprobe|kretprobe union struct is in BSS
  s.op->newline(1) << "int kprobe_idx = ((uintptr_t)inst-(uintptr_t)stap_dwarfless_kprobes)/sizeof(struct stap_dwarfless_kprobe);";
  // Check that the index is plausible
  s.op->newline() << "struct stap_dwarfless_probe *sdp = &stap_dwarfless_probes[";
  s.op->line() << "((kprobe_idx >= 0 && kprobe_idx < " << probes_by_module.size() << ")?";
  s.op->line() << "kprobe_idx:0)"; // NB: at least we avoid memory corruption
  // XXX: it would be nice to give a more verbose error though; BUG_ON later?
  s.op->line() << "];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->probe",
				 "_STP_PROBE_HANDLER_KPROBE");
  s.op->newline() << "c->kregs = regs;";

  // Make it look like the IP is set as it wouldn't have been replaced
  // by a breakpoint instruction when calling real probe handler. Reset
  // IP regs on return, so we don't confuse kprobes. PR10458
  s.op->newline() << "{";
  s.op->indent(1);
  s.op->newline() << "unsigned long kprobes_ip = REG_IP(c->kregs);";
  s.op->newline() << "SET_REG_IP(regs, (unsigned long) inst->addr);";
  s.op->newline() << "(*sdp->probe->ph) (c);";
  s.op->newline() << "SET_REG_IP(regs, kprobes_ip);";
  s.op->newline(-1) << "}";

  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  // Same for kretprobes
  s.op->newline();
  s.op->newline() << "static int enter_kretprobe2_probe (struct kretprobe_instance *inst,";
  s.op->line() << " struct pt_regs *regs) {";
  s.op->newline(1) << "struct kretprobe *krp = inst->rp;";

  // NB: as of PR5673, the kprobe|kretprobe union struct is in BSS
  s.op->newline() << "int kprobe_idx = ((uintptr_t)krp-(uintptr_t)stap_dwarfless_kprobes)/sizeof(struct stap_dwarfless_kprobe);";
  // Check that the index is plausible
  s.op->newline() << "struct stap_dwarfless_probe *sdp = &stap_dwarfless_probes[";
  s.op->line() << "((kprobe_idx >= 0 && kprobe_idx < " << probes_by_module.size() << ")?";
  s.op->line() << "kprobe_idx:0)"; // NB: at least we avoid memory corruption
  // XXX: it would be nice to give a more verbose error though; BUG_ON later?
  s.op->line() << "];";

  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->probe",
				 "_STP_PROBE_HANDLER_KRETPROBE");
  s.op->newline() << "c->kregs = regs;";
  s.op->newline() << "c->ips.krp.pi = inst;"; // for assisting runtime's backtrace logic

  // Make it look like the IP is set as it wouldn't have been replaced
  // by a breakpoint instruction when calling real probe handler. Reset
  // IP regs on return, so we don't confuse kprobes. PR10458
  s.op->newline() << "{";
  s.op->indent(1);
  s.op->newline() << "unsigned long kprobes_ip = REG_IP(c->kregs);";
  s.op->newline() << "SET_REG_IP(regs, (unsigned long) inst->rp->kp.addr);";
  s.op->newline() << "(*sdp->probe->ph) (c);";
  s.op->newline() << "SET_REG_IP(regs, kprobes_ip);";
  s.op->newline(-1) << "}";

  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  s.op->newline() << "#ifdef STAPCONF_KALLSYMS_ON_EACH_SYMBOL";
  s.op->newline() << "static int kprobe_resolve(void *data, const char *name,";
  s.op->newline() << "                          struct module *owner,";
  s.op->newline() << "                          unsigned long val) {";
  s.op->newline(1) << "int i;";
  s.op->newline() << "int *p = (int *) data;";
  s.op->newline() << "for (i=0; i<" << probes_by_module.size()
		  << " && *p > 0; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "if (! sdp->address)";
  s.op->newline(1) << "if (strcmp(sdp->symbol_string, name) == 0) {";
  s.op->newline(1) << "sdp->address = val;";
  s.op->newline() << "(*p)--;";
  s.op->newline(-1) << "}";
  s.op->newline(-2) << "}";
  s.op->newline() << "return (p > 0) ? 0 : -1;";
  s.op->newline(-1) << "}";
  s.op->newline() << "#endif";
}


void
kprobe_derived_probe_group::emit_module_init (systemtap_session& s)
{
  s.op->newline() << "#ifdef STAPCONF_KALLSYMS_ON_EACH_SYMBOL";
  s.op->newline() << "{";
  s.op->newline(1) << "int p = 0;";
  s.op->newline() << "for (i = 0; i < " << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "if (! sdp->address)";
  s.op->newline(1) << "p++;";
  s.op->newline(-2) << "}";
  s.op->newline() << "kallsyms_on_each_symbol(kprobe_resolve, &p);";
  s.op->newline(-1) << "}";
  s.op->newline() << "#endif";

  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "struct stap_dwarfless_kprobe *kp = & stap_dwarfless_kprobes[i];";
  s.op->newline() << "void *addr = (void *) sdp->address;";
  s.op->newline() << "const char *symbol_name = addr ? NULL : sdp->symbol_string;";

  s.op->newline() << "#ifdef STAPCONF_KALLSYMS_ON_EACH_SYMBOL";
  s.op->newline() << "if (! addr) {";
  s.op->newline(1) << "sdp->registered_p = 0;";
  s.op->newline() << "if (!sdp->optional_p)";
  s.op->newline(1) << "_stp_warn (\"probe %s registration error (symbol not found)\", probe_point);";
  s.op->newline(-1) << "continue;";
  s.op->newline(-1) << "}";
  s.op->newline() << "#endif";

  s.op->newline() << "probe_point = sdp->probe->pp;"; // for error messages
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline(1) << "kp->u.krp.kp.addr = addr;";
  s.op->newline() << "#ifdef STAPCONF_KPROBE_SYMBOL_NAME";
  s.op->newline() << "kp->u.krp.kp.symbol_name = (char *) symbol_name;";
  s.op->newline() << "#endif";
  s.op->newline() << "if (sdp->maxactive_p) {";
  s.op->newline(1) << "kp->u.krp.maxactive = sdp->maxactive_val;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "kp->u.krp.maxactive = KRETACTIVE;";
  s.op->newline(-1) << "}";
  s.op->newline() << "kp->u.krp.handler = &enter_kretprobe2_probe;";
  // to ensure safeness of bspcache, always use aggr_kprobe on ia64
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "kp->dummy.addr = kp->u.krp.kp.addr;";
  s.op->newline() << "#ifdef STAPCONF_KPROBE_SYMBOL_NAME";
  s.op->newline() << "kp->dummy.symbol_name = kp->u.krp.kp.symbol_name;";
  s.op->newline() << "#endif";
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
  s.op->newline(1) << "kp->u.kp.addr = addr;";
  s.op->newline() << "#ifdef STAPCONF_KPROBE_SYMBOL_NAME";
  s.op->newline() << "kp->u.kp.symbol_name = (char *) symbol_name;";
  s.op->newline() << "#endif";
  s.op->newline() << "kp->u.kp.pre_handler = &enter_kprobe2_probe;";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "kp->dummy.pre_handler = NULL;";
  s.op->newline() << "kp->dummy.addr = kp->u.kp.addr;";
  s.op->newline() << "#ifdef STAPCONF_KPROBE_SYMBOL_NAME";
  s.op->newline() << "kp->dummy.symbol_name = kp->u.kp.symbol_name;";
  s.op->newline() << "#endif";
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
  s.op->newline() << "if (rc) {"; // PR6749: tolerate a failed register_*probe.
  s.op->newline(1) << "sdp->registered_p = 0;";
  s.op->newline() << "if (!sdp->optional_p)";
  s.op->newline(1) << "_stp_warn (\"probe %s (address 0x%lx) registration error (rc %d)\", probe_point, (unsigned long) addr, rc);";
  s.op->newline(-1) << "rc = 0;"; // continue with other probes
  // XXX: shall we increment numskipped?
  s.op->newline(-1) << "}";

  s.op->newline() << "else sdp->registered_p = 1;";
  s.op->newline(-1) << "}"; // for loop
}


void
kprobe_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  //Unregister kprobes by batch interfaces.
  s.op->newline() << "#if defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline() << "j = 0;";
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "struct stap_dwarfless_kprobe *kp = & stap_dwarfless_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "if (!sdp->return_p)";
  s.op->newline(1) << "stap_unreg_kprobes2[j++] = &kp->u.kp;";
  s.op->newline(-2) << "}";
  s.op->newline() << "unregister_kprobes((struct kprobe **)stap_unreg_kprobes2, j);";
  s.op->newline() << "j = 0;";
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "struct stap_dwarfless_kprobe *kp = & stap_dwarfless_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "if (sdp->return_p)";
  s.op->newline(1) << "stap_unreg_kprobes2[j++] = &kp->u.krp;";
  s.op->newline(-2) << "}";
  s.op->newline() << "unregister_kretprobes((struct kretprobe **)stap_unreg_kprobes2, j);";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "j = 0;";
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "struct stap_dwarfless_kprobe *kp = & stap_dwarfless_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "stap_unreg_kprobes2[j++] = &kp->dummy;";
  s.op->newline(-1) << "}";
  s.op->newline() << "unregister_kprobes((struct kprobe **)stap_unreg_kprobes2, j);";
  s.op->newline() << "#endif";
  s.op->newline() << "#endif";

  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "struct stap_dwarfless_kprobe *kp = & stap_dwarfless_kprobes[i];";
  s.op->newline() << "if (! sdp->registered_p) continue;";
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline(1) << "unregister_kretprobe (&kp->u.krp);";
  s.op->newline() << "#endif";
  s.op->newline() << "atomic_add (kp->u.krp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/1 on '%s': %d\\n\", sdp->probe->pp, kp->u.krp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline() << "atomic_add (kp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/2 on '%s': %lu\\n\", sdp->probe->pp, kp->u.krp.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "} else {";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline(1) << "unregister_kprobe (&kp->u.kp);";
  s.op->newline() << "#endif";
  s.op->newline() << "atomic_add (kp->u.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kprobe on '%s': %lu\\n\", sdp->probe->pp, kp->u.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "}";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES) && defined(__ia64__)";
  s.op->newline() << "unregister_kprobe (&kp->dummy);";
  s.op->newline() << "#endif";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";
}

struct kprobe_builder: public derived_probe_builder
{
  kprobe_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results);
};


void
kprobe_builder::build(systemtap_session & sess,
		      probe * base,
		      probe_point * location,
		      literal_map_t const & parameters,
		      vector<derived_probe *> & finished_results)
{
  string function_string_val, module_string_val;
  string path, library, path_tgt, library_tgt;
  int64_t statement_num_val = 0, maxactive_val = 0;
  bool has_function_str, has_module_str, has_statement_num;
  bool has_absolute, has_return, has_maxactive;
  bool has_path, has_library;

  has_function_str = get_param(parameters, TOK_FUNCTION, function_string_val);
  has_module_str = get_param(parameters, TOK_MODULE, module_string_val);
  has_return = has_null_param (parameters, TOK_RETURN);
  has_maxactive = get_param(parameters, TOK_MAXACTIVE, maxactive_val);
  has_statement_num = get_param(parameters, TOK_STATEMENT, statement_num_val);
  has_absolute = has_null_param (parameters, TOK_ABSOLUTE);
  has_path = get_param (parameters, TOK_PROCESS, path);
  has_library = get_param (parameters, TOK_LIBRARY, library);

  if (has_path)
    {
      path = find_executable (path, sess.sysroot, sess.sysenv);
      path_tgt = path_remove_sysroot(sess, path);
    }
  if (has_library)
    {
      library = find_executable (library, sess.sysroot, sess.sysenv,
                                 "LD_LIBRARY_PATH");
      library_tgt = path_remove_sysroot(sess, library);
    }

  if (has_function_str)
    {
      if (has_module_str)
	function_string_val = module_string_val + ":" + function_string_val;

      finished_results.push_back (new kprobe_derived_probe (base,
							    location, function_string_val,
							    0, has_return,
							    has_statement_num,
							    has_maxactive,
							    has_path,
							    has_library,
							    maxactive_val,
							    path_tgt,
							    library_tgt));
    }
  else
    {
      // assert guru mode for absolute probes
      if ( has_statement_num && has_absolute && !base->privileged )
	throw semantic_error (_("absolute statement probe in unprivileged script; need stap -g"), base->tok);

      finished_results.push_back (new kprobe_derived_probe (base,
							    location, "",
							    statement_num_val,
							    has_return,
							    has_statement_num,
							    has_maxactive,
							    has_path,
							    has_library,
							    maxactive_val,
							    path_tgt,
							    library_tgt));
    }
}

// ------------------------------------------------------------------------
//  Hardware breakpoint based probes.
// ------------------------------------------------------------------------

static const string TOK_HWBKPT("data");
static const string TOK_HWBKPT_WRITE("write");
static const string TOK_HWBKPT_RW("rw");
static const string TOK_LENGTH("length");

#define HWBKPT_READ 0
#define HWBKPT_WRITE 1
#define HWBKPT_RW 2
struct hwbkpt_derived_probe: public derived_probe
{
  hwbkpt_derived_probe (probe *base,
                        probe_point *location,
                        uint64_t addr,
			string symname,
			unsigned int len,
			bool has_only_read_access,
			bool has_only_write_access,
			bool has_rw_access
                        );
  Dwarf_Addr hwbkpt_addr;
  string symbol_name;
  unsigned int hwbkpt_access,hwbkpt_len;

  void printsig (std::ostream &o) const;
  void join_group (systemtap_session& s);
};

struct hwbkpt_derived_probe_group: public derived_probe_group
{
private:
  vector<hwbkpt_derived_probe*> hwbkpt_probes;

public:
  void enroll (hwbkpt_derived_probe* probe, systemtap_session& s);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};

hwbkpt_derived_probe::hwbkpt_derived_probe (probe *base,
                                            probe_point *location,
                                            uint64_t addr,
                                            string symname,
                                            unsigned int len,
                                            bool has_only_read_access,
                                            bool has_only_write_access,
                                            bool):
  derived_probe (base, location, true /* .components soon rewritten */ ),
  hwbkpt_addr (addr),
  symbol_name (symname),
  hwbkpt_len (len)
{
  this->tok = base->tok;

  vector<probe_point::component*> comps;
  comps.push_back (new probe_point::component(TOK_KERNEL));

  if (hwbkpt_addr)
    comps.push_back (new probe_point::component (TOK_HWBKPT,
                                                 new literal_number(hwbkpt_addr, true)));
  else if (symbol_name.size())
    comps.push_back (new probe_point::component (TOK_HWBKPT, new literal_string(symbol_name)));

  comps.push_back (new probe_point::component (TOK_LENGTH, new literal_number(hwbkpt_len)));

  if (has_only_read_access)
    this->hwbkpt_access = HWBKPT_READ ;
//TODO add code for comps.push_back for read, since this flag is not for x86

  else
    {
      if (has_only_write_access)
        {
          this->hwbkpt_access = HWBKPT_WRITE ;
          comps.push_back (new probe_point::component(TOK_HWBKPT_WRITE));
        }
      else
        {
          this->hwbkpt_access = HWBKPT_RW ;
          comps.push_back (new probe_point::component(TOK_HWBKPT_RW));
        }
    }

  this->sole_location()->components = comps;
}

void hwbkpt_derived_probe::printsig (ostream& o) const
{
  sole_location()->print (o);
  printsig_nested (o);
}

void hwbkpt_derived_probe::join_group (systemtap_session& s)
{
  if (! s.hwbkpt_derived_probes)
    s.hwbkpt_derived_probes = new hwbkpt_derived_probe_group ();
  s.hwbkpt_derived_probes->enroll (this, s);
}

void hwbkpt_derived_probe_group::enroll (hwbkpt_derived_probe* p, systemtap_session& s)
{
  hwbkpt_probes.push_back (p);

  unsigned max_hwbkpt_probes_by_arch = 0;
  if (s.architecture == "i386" || s.architecture == "x86_64")
    max_hwbkpt_probes_by_arch = 4;
  else if (s.architecture == "s390")
    max_hwbkpt_probes_by_arch = 1;

  if (hwbkpt_probes.size() >= max_hwbkpt_probes_by_arch)
    s.print_warning (_F("Too many hardware breakpoint probes requested for %s (%zu vs. %u)",
                          s.architecture.c_str(), hwbkpt_probes.size(), max_hwbkpt_probes_by_arch));
}

void
hwbkpt_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (hwbkpt_probes.empty()) return;

  s.op->newline() << "/* ---- hwbkpt-based probes ---- */";

  s.op->newline() << "#include <linux/perf_event.h>";
  s.op->newline() << "#include <linux/hw_breakpoint.h>";
  s.op->newline();

  // Forward declare the master entry functions
  s.op->newline() << "static int enter_hwbkpt_probe (struct perf_event *bp,";
  s.op->line() << " int nmi,";
  s.op->line() << " struct perf_sample_data *data,";
  s.op->line() << " struct pt_regs *regs);";

  // Emit the actual probe list.

  s.op->newline() << "static struct perf_event_attr ";
  s.op->newline() << "stap_hwbkpt_probe_array[" << hwbkpt_probes.size() << "];";

  s.op->newline() << "static struct perf_event **";
  s.op->newline() << "stap_hwbkpt_ret_array[" << hwbkpt_probes.size() << "];";
  s.op->newline() << "static struct stap_hwbkpt_probe {";
  s.op->newline() << "int registered_p:1;";
// registered_p =  0 signifies a probe that is unregistered (or failed)
// registered_p =  1 signifies a probe that got registered successfully

  // Symbol Names are mostly small and uniform enough
  // to justify putting const char*.
  s.op->newline() << "const char * const symbol;";

  s.op->newline() << "const unsigned long address;";
  s.op->newline() << "uint8_t atype;";
  s.op->newline() << "unsigned int len;";
  s.op->newline() << "struct stap_probe * const probe;";
  s.op->newline() << "} stap_hwbkpt_probes[] = {";
  s.op->indent(1);

  for (unsigned int it = 0; it < hwbkpt_probes.size(); it++)
    {
      hwbkpt_derived_probe* p = hwbkpt_probes.at(it);
      s.op->newline() << "{";
      if (p->symbol_name.size())
      s.op->line() << " .address=(unsigned long)0x0" << "ULL,";
      else
      s.op->line() << " .address=(unsigned long)0x" << hex << p->hwbkpt_addr << dec << "ULL,";
      switch(p->hwbkpt_access){
      case HWBKPT_READ:
		s.op->line() << " .atype=HW_BREAKPOINT_R ,";
		break;
      case HWBKPT_WRITE:
		s.op->line() << " .atype=HW_BREAKPOINT_W ,";
		break;
      case HWBKPT_RW:
		s.op->line() << " .atype=HW_BREAKPOINT_R|HW_BREAKPOINT_W ,";
		break;
	};
      s.op->line() << " .len=" << p->hwbkpt_len << ",";
      s.op->line() << " .probe=" << common_probe_init (p) << ",";
      s.op->line() << " .symbol=\"" << p->symbol_name << "\",";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";

  // Emit the hwbkpt callback function
  s.op->newline() ;
  s.op->newline() << "static int enter_hwbkpt_probe (struct perf_event *bp,";
  s.op->line() << " int nmi,";
  s.op->line() << " struct perf_sample_data *data,";
  s.op->line() << " struct pt_regs *regs) {";
  s.op->newline(1) << "unsigned int i;";
  s.op->newline() << "if (bp->attr.type != PERF_TYPE_BREAKPOINT) return -1;";
  s.op->newline() << "for (i=0; i<" << hwbkpt_probes.size() << "; i++) {";
  s.op->newline(1) << "struct perf_event_attr *hp = & stap_hwbkpt_probe_array[i];";
  // XXX: why not match stap_hwbkpt_ret_array[i] against bp instead?
  s.op->newline() << "if (bp->attr.bp_addr==hp->bp_addr && bp->attr.bp_type==hp->bp_type && bp->attr.bp_len==hp->bp_len) {";
  s.op->newline(1) << "struct stap_hwbkpt_probe *sdp = &stap_hwbkpt_probes[i];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->probe",
				 "_STP_PROBE_HANDLER_HWBKPT");
  s.op->newline() << "if (user_mode(regs)) {";
  s.op->newline(1)<< "c->probe_flags |= _STP_PROBE_STATE_USER_MODE;";
  s.op->newline() << "c->uregs = regs;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "c->kregs = regs;";
  s.op->newline(-1) << "}";
  s.op->newline() << "(*sdp->probe->ph) (c);";
  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";
}

void
hwbkpt_derived_probe_group::emit_module_init (systemtap_session& s)
{
  s.op->newline() << "for (i=0; i<" << hwbkpt_probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_hwbkpt_probe *sdp = & stap_hwbkpt_probes[i];";
  s.op->newline() << "struct perf_event_attr *hp = & stap_hwbkpt_probe_array[i];";
  s.op->newline() << "void *addr = (void *) sdp->address;";
  s.op->newline() << "const char *hwbkpt_symbol_name = addr ? NULL : sdp->symbol;";
  s.op->newline() << "hw_breakpoint_init(hp);";
  s.op->newline() << "if (addr)";
  s.op->newline(1) << "hp->bp_addr = (unsigned long) addr;";
  s.op->newline(-1) << "else { ";
  s.op->newline(1) << "hp->bp_addr = kallsyms_lookup_name(hwbkpt_symbol_name);";
  s.op->newline() << "if (!hp->bp_addr) { ";
  s.op->newline(1) << "_stp_warn(\"Probe %s registration skipped: invalid symbol %s \",sdp->probe->pp,hwbkpt_symbol_name);";
  s.op->newline() << "continue;";
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
  s.op->newline() << "hp->bp_type = sdp->atype;";

  // On x86 & x86-64, hp->bp_len is not just a number but a macro/enum (!?!).
  if (s.architecture == "i386" || s.architecture == "x86_64" )
    {
      s.op->newline() << "switch(sdp->len) {";
      s.op->newline() << "case 1:";
      s.op->newline(1) << "hp->bp_len = HW_BREAKPOINT_LEN_1;";
      s.op->newline() << "break;";
      s.op->newline(-1) << "case 2:";
      s.op->newline(1) << "hp->bp_len = HW_BREAKPOINT_LEN_2;";
      s.op->newline() << "break;";
      s.op->newline(-1) << "case 3:";
      s.op->newline() << "case 4:";
      s.op->newline(1) << "hp->bp_len = HW_BREAKPOINT_LEN_4;";
      s.op->newline() << "break;";
      s.op->newline(-1) << "case 5:";
      s.op->newline() << "case 6:";
      s.op->newline() << "case 7:";
      s.op->newline() << "case 8:";
      s.op->newline() << "default:"; // XXX: could instead reject
      s.op->newline(1) << "hp->bp_len = HW_BREAKPOINT_LEN_8;";
      s.op->newline() << "break;";
      s.op->newline(-1) << "}";
    }
  else // other architectures presumed straightforward
    s.op->newline() << "hp->bp_len = sdp->len;";

  s.op->newline() << "probe_point = sdp->probe->pp;"; // for error messages
  s.op->newline() << "#ifdef STAPCONF_HW_BREAKPOINT_CONTEXT";
  s.op->newline() << "stap_hwbkpt_ret_array[i] = register_wide_hw_breakpoint(hp, (void *)&enter_hwbkpt_probe, NULL);";
  s.op->newline() << "#else";
  s.op->newline() << "stap_hwbkpt_ret_array[i] = register_wide_hw_breakpoint(hp, (void *)&enter_hwbkpt_probe);";
  s.op->newline() << "#endif";
  s.op->newline() << "rc = 0;";
  s.op->newline() << "if (IS_ERR(stap_hwbkpt_ret_array[i])) {";
  s.op->newline(1) << "rc = PTR_ERR(stap_hwbkpt_ret_array[i]);";
  s.op->newline() << "stap_hwbkpt_ret_array[i] = 0;";
  s.op->newline(-1) << "}";
  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "_stp_warn(\"Hwbkpt probe %s: registration error %d, addr %p, name %s\", probe_point, rc, addr, hwbkpt_symbol_name);";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";
  s.op->newline() << " else sdp->registered_p = 1;";
  s.op->newline(-1) << "}"; // for loop
}

void
hwbkpt_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  //Unregister hwbkpt probes.
  s.op->newline() << "for (i=0; i<" << hwbkpt_probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_hwbkpt_probe *sdp = & stap_hwbkpt_probes[i];";
  s.op->newline() << "if (sdp->registered_p == 0) continue;";
  s.op->newline() << "unregister_wide_hw_breakpoint(stap_hwbkpt_ret_array[i]);";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";
}

struct hwbkpt_builder: public derived_probe_builder
{
  hwbkpt_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results);
};

void
hwbkpt_builder::build(systemtap_session & sess,
		      probe * base,
		      probe_point * location,
		      literal_map_t const & parameters,
		      vector<derived_probe *> & finished_results)
{
  string symbol_str_val;
  int64_t hwbkpt_address, len;
  bool has_addr, has_symbol_str, has_write, has_rw, has_len;

  if (! (sess.kernel_config["CONFIG_PERF_EVENTS"] == string("y")))
      throw semantic_error (_("CONFIG_PERF_EVENTS not available on this kernel"),
                            location->components[0]->tok);
  if (! (sess.kernel_config["CONFIG_HAVE_HW_BREAKPOINT"] == string("y")))
      throw semantic_error (_("CONFIG_HAVE_HW_BREAKPOINT not available on this kernel"),
                            location->components[0]->tok);

  has_addr = get_param (parameters, TOK_HWBKPT, hwbkpt_address);
  has_symbol_str = get_param (parameters, TOK_HWBKPT, symbol_str_val);
  has_len = get_param (parameters, TOK_LENGTH, len);
  has_write = (parameters.find(TOK_HWBKPT_WRITE) != parameters.end());
  has_rw = (parameters.find(TOK_HWBKPT_RW) != parameters.end());

  if (!has_len)
	len = 1;

  if (has_addr)
      finished_results.push_back (new hwbkpt_derived_probe (base,
							    location,
							    hwbkpt_address,
							    "",len,0,
							    has_write,
							    has_rw));
  else if (has_symbol_str)
      finished_results.push_back (new hwbkpt_derived_probe (base,
							    location,
							    0,
							    symbol_str_val,len,0,
							    has_write,
							    has_rw));
  else
    assert (0);
}

// ------------------------------------------------------------------------
// statically inserted kernel-tracepoint derived probes
// ------------------------------------------------------------------------

struct tracepoint_arg
{
  string name, c_type, typecast;
  bool usable, used, isptr;
  Dwarf_Die type_die;
  tracepoint_arg(): usable(false), used(false), isptr(false) {}
};

struct tracepoint_derived_probe: public derived_probe
{
  tracepoint_derived_probe (systemtap_session& s,
                            dwflpp& dw, Dwarf_Die& func_die,
                            const string& tracepoint_name,
                            probe* base_probe, probe_point* location);

  systemtap_session& sess;
  string tracepoint_name, header;
  vector <struct tracepoint_arg> args;

  void build_args(dwflpp& dw, Dwarf_Die& func_die);
  void getargs (std::list<std::string> &arg_set) const;
  void join_group (systemtap_session& s);
  void print_dupe_stamp(ostream& o);
};


struct tracepoint_derived_probe_group: public generic_dpg<tracepoint_derived_probe>
{
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct tracepoint_var_expanding_visitor: public var_expanding_visitor
{
  tracepoint_var_expanding_visitor(dwflpp& dw, const string& probe_name,
                                   vector <struct tracepoint_arg>& args):
    dw (dw), probe_name (probe_name), args (args) {}
  dwflpp& dw;
  const string& probe_name;
  vector <struct tracepoint_arg>& args;

  void visit_target_symbol (target_symbol* e);
  void visit_target_symbol_arg (target_symbol* e);
  void visit_target_symbol_context (target_symbol* e);
};


void
tracepoint_var_expanding_visitor::visit_target_symbol_arg (target_symbol* e)
{
  string argname = e->sym_name();

  // search for a tracepoint parameter matching this name
  tracepoint_arg *arg = NULL;
  for (unsigned i = 0; i < args.size(); ++i)
    if (args[i].usable && args[i].name == argname)
      {
        arg = &args[i];
        arg->used = true;
        break;
      }

  if (arg == NULL)
    {
      stringstream alternatives;
      for (unsigned i = 0; i < args.size(); ++i)
        alternatives << " $" << args[i].name;
      alternatives << " $$name $$parms $$vars";

      // We hope that this value ends up not being referenced after all, so it
      // can be optimized out quietly.
      throw semantic_error(_F("unable to find tracepoint variable '%s' (alternatives: %s)",
                              e->name.c_str(), alternatives.str().c_str()), e->tok);
      // NB: we can have multiple errors, since a target variable
      // may be expanded in several different contexts:
      //     trace ("*") { $foo->bar }
    }

  // make sure we're not dereferencing base types
  if (!arg->isptr)
    e->assert_no_components("tracepoint", true);

  // we can only write to dereferenced fields, and only if guru mode is on
  bool lvalue = is_active_lvalue(e);
  if (lvalue && (!dw.sess.guru_mode || e->components.empty()))
    throw semantic_error(_F("write to tracepoint variable '%s' not permitted; need stap -g", e->name.c_str()), e->tok);

  // XXX: if a struct/union arg is passed by value, then writing to its fields
  // is also meaningless until you dereference past a pointer member.  It's
  // harder to detect and prevent that though...

  if (e->components.empty())
    {
      if (e->addressof)
        throw semantic_error(_("cannot take address of tracepoint variable"), e->tok);

      // Just grab the value from the probe locals
      symbol* sym = new symbol;
      sym->tok = e->tok;
      sym->name = "__tracepoint_arg_" + arg->name;
      provide (sym);
    }
  else
    {
      // make a copy of the original as a bare target symbol for the tracepoint
      // value, which will be passed into the dwarf dereferencing code
      target_symbol* e2 = deep_copy_visitor::deep_copy(e);
      e2->components.clear();

      if (e->components.back().type == target_symbol::comp_pretty_print)
        {
          if (lvalue)
            throw semantic_error(_("cannot write to pretty-printed variable"), e->tok);

          dwarf_pretty_print dpp(dw, &arg->type_die, e2, arg->isptr, false, *e);
          dpp.expand()->visit (this);
          return;
        }

      // Synthesize a function to dereference the dwarf fields,
      // with a pointer parameter that is the base tracepoint variable
      functiondecl *fdecl = new functiondecl;
      fdecl->synthetic = true;
      fdecl->tok = e->tok;
      embeddedcode *ec = new embeddedcode;
      ec->tok = e->tok;

      string fname = (string(lvalue ? "_tracepoint_tvar_set" : "_tracepoint_tvar_get")
                      + "_" + e->sym_name()
                      + "_" + lex_cast(tick++));

      fdecl->name = fname;
      fdecl->body = ec;

      ec->code += EMBEDDED_FETCH_DEREF(false);
      ec->code += dw.literal_stmt_for_pointer (&arg->type_die, e,
                                                  lvalue, fdecl->type);

      // Give the fdecl an argument for the raw tracepoint value
      vardecl *v1 = new vardecl;
      v1->type = pe_long;
      v1->name = "pointer";
      v1->tok = e->tok;
      fdecl->formal_args.push_back(v1);

      // Any non-literal indexes need to be passed in too.
      for (unsigned i = 0; i < e->components.size(); ++i)
        if (e->components[i].type == target_symbol::comp_expression_array_index)
          {
            vardecl *v = new vardecl;
            v->type = pe_long;
            v->name = "index" + lex_cast(i);
            v->tok = e->tok;
            fdecl->formal_args.push_back(v);
          }

      if (lvalue)
        {
          // Modify the fdecl so it carries a pe_long formal
          // argument called "value".

          // FIXME: For the time being we only support setting target
          // variables which have base types; these are 'pe_long' in
          // stap's type vocabulary.  Strings and pointers might be
          // reasonable, some day, but not today.

          vardecl *v2 = new vardecl;
          v2->type = pe_long;
          v2->name = "value";
          v2->tok = e->tok;
          fdecl->formal_args.push_back(v2);
        }
      else
        ec->code += "/* pure */";

      ec->code += "/* unprivileged */";
      ec->code += EMBEDDED_FETCH_DEREF_DONE;

      fdecl->join (dw.sess);

      // Synthesize a functioncall.
      functioncall* n = new functioncall;
      n->tok = e->tok;
      n->function = fname;
      n->args.push_back(require(e2));

      // Any non-literal indexes need to be passed in too.
      for (unsigned i = 0; i < e->components.size(); ++i)
        if (e->components[i].type == target_symbol::comp_expression_array_index)
          n->args.push_back(require(e->components[i].expr_index));

      if (lvalue)
        {
          // Provide the functioncall to our parent, so that it can be
          // used to substitute for the assignment node immediately above
          // us.
          assert(!target_symbol_setter_functioncalls.empty());
          *(target_symbol_setter_functioncalls.top()) = n;
        }

      provide (n);
    }
}


void
tracepoint_var_expanding_visitor::visit_target_symbol_context (target_symbol* e)
{
  if (e->addressof)
    throw semantic_error(_("cannot take address of context variable"), e->tok);

  if (is_active_lvalue (e))
    throw semantic_error(_F("write to tracepoint '%s' not permitted", e->name.c_str()), e->tok);

  if (e->name == "$$name")
    {
      e->assert_no_components("tracepoint");

      // Synthesize an embedded expression.
      embedded_expr *expr = new embedded_expr;
      expr->tok = e->tok;
      expr->code = string("/* string */ /* pure */ ")
	+ string("c->ips.tracepoint_name ? c->ips.tracepoint_name : \"\"");
      provide (expr);
    }
  else if (e->name == "$$vars" || e->name == "$$parms")
    {
      e->assert_no_components("tracepoint", true);

      token* pf_tok = new token(*e->tok);
      pf_tok->content = "sprintf";

      print_format* pf = print_format::create(pf_tok);

      for (unsigned i = 0; i < args.size(); ++i)
        {
          if (!args[i].usable)
            continue;
          if (i > 0)
            pf->raw_components += " ";
          pf->raw_components += args[i].name;
          target_symbol *tsym = new target_symbol;
          tsym->tok = e->tok;
          tsym->name = "$" + args[i].name;
          tsym->components = e->components;

          // every variable should always be accessible!
          tsym->saved_conversion_error = 0;
          expression *texp = require (tsym); // NB: throws nothing ...
          if (tsym->saved_conversion_error) // ... but this is how we know it happened.
            {
              if (dw.sess.verbose>2)
                for (const semantic_error *c = tsym->saved_conversion_error;
                     c != 0; c = c->chain)
                  clog << _("variable location problem: ") << c->what() << endl;
              pf->raw_components += "=?";
              continue;
            }

          if (!e->components.empty() &&
              e->components[0].type == target_symbol::comp_pretty_print)
            pf->raw_components += "=%s";
          else
            pf->raw_components += args[i].isptr ? "=%p" : "=%#x";
          pf->args.push_back(texp);
        }

      pf->components = print_format::string_to_components(pf->raw_components);
      provide (pf);
    }
  else
    assert(0); // shouldn't get here
}

void
tracepoint_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  try
    {
      assert(e->name.size() > 0
	     && ((e->name[0] == '$' && e->target_name == "")
		 || (e->name == "@var" && e->target_name != "")));

      if (e->name == "$$name" || e->name == "$$parms" || e->name == "$$vars")
        visit_target_symbol_context (e);
      else if (e->name == "@var")
	throw semantic_error(_("cannot use @var DWARF variables in tracepoints"), e->tok);
      else
        visit_target_symbol_arg (e);
    }
  catch (const semantic_error &er)
    {
      e->chain (er);
      provide (e);
    }
}



tracepoint_derived_probe::tracepoint_derived_probe (systemtap_session& s,
                                                    dwflpp& dw, Dwarf_Die& func_die,
                                                    const string& tracepoint_name,
                                                    probe* base, probe_point* loc):
  derived_probe (base, loc, true /* .components soon rewritten */),
  sess (s), tracepoint_name (tracepoint_name)
{
  // create synthetic probe point name; preserve condition
  vector<probe_point::component*> comps;
  comps.push_back (new probe_point::component (TOK_KERNEL));
  comps.push_back (new probe_point::component (TOK_TRACE, new literal_string (tracepoint_name)));
  this->sole_location()->components = comps;

  // fill out the available arguments in this tracepoint
  build_args(dw, func_die);

  // determine which header defined this tracepoint
  string decl_file = dwarf_decl_file(&func_die);
  header = decl_file; 

#if 0 /* This convention is not enforced. */
  size_t header_pos = decl_file.rfind("trace/");
  if (header_pos == string::npos)
    throw semantic_error ("cannot parse header location for tracepoint '"
                                  + tracepoint_name + "' in '"
                                  + decl_file + "'");
  header = decl_file.substr(header_pos);
#endif

  // tracepoints from FOO_event_types.h should really be included from FOO.h
  // XXX can dwarf tell us the include hierarchy?  it would be better to
  // ... walk up to see which one was directly included by tracequery.c
  // XXX: see also PR9993.
  size_t header_pos = header.find("_event_types");
  if (header_pos != string::npos)
    header.erase(header_pos, 12);

  // Now expand the local variables in the probe body
  tracepoint_var_expanding_visitor v (dw, name, args);
  v.replace (this->body);
  for (unsigned i = 0; i < args.size(); i++)
    if (args[i].used)
      {
	vardecl* v = new vardecl;
	v->name = "__tracepoint_arg_" + args[i].name;
	v->tok = this->tok;
	v->set_arity(0, this->tok);
	v->type = pe_long;
	v->synthetic = true;
	this->locals.push_back (v);
      }

  if (sess.verbose > 2)
    clog << "tracepoint-based " << name << " tracepoint='" << tracepoint_name << "'" << endl;
}


static bool
resolve_tracepoint_arg_type(tracepoint_arg& arg)
{
  Dwarf_Die type;
  switch (dwarf_tag(&arg.type_die))
    {
    case DW_TAG_typedef:
    case DW_TAG_const_type:
    case DW_TAG_volatile_type:
      // iterate on the referent type
      return (dwarf_attr_die(&arg.type_die, DW_AT_type, &arg.type_die)
              && resolve_tracepoint_arg_type(arg));
    case DW_TAG_base_type:
    case DW_TAG_enumeration_type:
      // base types will simply be treated as script longs
      arg.isptr = false;
      return true;
    case DW_TAG_pointer_type:
      // pointers can be treated as script longs,
      // and if we know their type, they can also be dereferenced
      type = arg.type_die;
      while (dwarf_attr_die(&arg.type_die, DW_AT_type, &arg.type_die))
        {
          // It still might be a non-type, e.g. const void,
          // so we need to strip away all qualifiers.
          int tag = dwarf_tag(&arg.type_die);
          if (tag != DW_TAG_typedef &&
              tag != DW_TAG_const_type &&
              tag != DW_TAG_volatile_type)
            {
              arg.isptr = true;
              break;
            }
        }
      if (!arg.isptr)
        arg.type_die = type;
      arg.typecast = "(intptr_t)";
      return true;
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
      // for structs/unions which are passed by value, we turn it into
      // a pointer that can be dereferenced.
      arg.isptr = true;
      arg.typecast = "(intptr_t)&";
      return true;
    default:
      // should we consider other types too?
      return false;
    }
}


void
tracepoint_derived_probe::build_args(dwflpp&, Dwarf_Die& func_die)
{
  Dwarf_Die arg;
  if (dwarf_child(&func_die, &arg) == 0)
    do
      if (dwarf_tag(&arg) == DW_TAG_formal_parameter)
        {
          // build a tracepoint_arg for this parameter
          tracepoint_arg tparg;
          tparg.name = dwarf_diename(&arg);

          // read the type of this parameter
          if (!dwarf_attr_die (&arg, DW_AT_type, &tparg.type_die)
              || !dwarf_type_name(&tparg.type_die, tparg.c_type))
            throw semantic_error (_F("cannot get type of parameter '%s' of tracepoint '%s'",
                                     tparg.name.c_str(), tracepoint_name.c_str()));

          tparg.usable = resolve_tracepoint_arg_type(tparg);
          args.push_back(tparg);
          if (sess.verbose > 4)
            clog << _F("found parameter for tracepoint '%s': type:'%s' name:'%s' %s",
                       tracepoint_name.c_str(), tparg.c_type.c_str(), tparg.name.c_str(),
                       tparg.usable ? "ok" : "unavailable") << endl;
        }
    while (dwarf_siblingof(&arg, &arg) == 0);
}

void
tracepoint_derived_probe::getargs(std::list<std::string> &arg_set) const
{
  for (unsigned i = 0; i < args.size(); ++i)
    if (args[i].usable)
      arg_set.push_back("$"+args[i].name+":"+args[i].c_type);
}

void
tracepoint_derived_probe::join_group (systemtap_session& s)
{
  if (! s.tracepoint_derived_probes)
    s.tracepoint_derived_probes = new tracepoint_derived_probe_group ();
  s.tracepoint_derived_probes->enroll (this);
}


void
tracepoint_derived_probe::print_dupe_stamp(ostream& o)
{
  for (unsigned i = 0; i < args.size(); i++)
    if (args[i].used)
      o << "__tracepoint_arg_" << args[i].name << endl;
}


static vector<string> tracepoint_extra_decls (systemtap_session& s, const string& header)
{
  vector<string> they_live;
  // PR 9993
  // XXX: may need this to be configurable
  they_live.push_back ("#include <linux/skbuff.h>");

  // PR11649: conditional extra header
  // for kvm tracepoints in 2.6.33ish
  if (s.kernel_config["CONFIG_KVM"] != string("")) {
    they_live.push_back ("#include <linux/kvm_host.h>");
  }

  if (header.find("xfs") != string::npos && s.kernel_config["CONFIG_XFS_FS"] != string("")) {
    they_live.push_back ("#define XFS_BIG_BLKNOS 1");
    if (s.kernel_source_tree != "")
      they_live.push_back ("#include \"fs/xfs/xfs_types.h\""); // in kernel-source tree
    they_live.push_back ("struct xfs_mount;");
    they_live.push_back ("struct xfs_inode;");
    they_live.push_back ("struct xfs_buf;");
    they_live.push_back ("struct xfs_bmbt_irec;");
    they_live.push_back ("struct xfs_trans;");
  }

  if (header.find("nfs") != string::npos && s.kernel_config["CONFIG_NFSD"] != string("")) {
    they_live.push_back ("struct rpc_task;");
  }

  they_live.push_back ("#include <asm/cputime.h>");

  // linux 3.0
  they_live.push_back ("struct cpu_workqueue_struct;");

  if (header.find("ext4") != string::npos && s.kernel_config["CONFIG_EXT4_FS"] != string(""))
    if (s.kernel_source_tree != "")
      they_live.push_back ("#include \"fs/ext4/ext4.h\""); // in kernel-source tree

  if (header.find("ext3") != string::npos && s.kernel_config["CONFIG_EXT3_FS"] != string(""))
    they_live.push_back ("struct ext3_reserve_window_node;");

  return they_live;
}


void
tracepoint_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty())
    return;

  s.op->newline() << "/* ---- tracepoint probes ---- */";
  s.op->newline();


  // We create a MODULE_aux_N.c file for each tracepoint header, to allow them
  // to be separately compiled.  That's because kernel tracepoint headers sometimes
  // conflict.  PR13155.

  map<string,translator_output*> per_header_aux;
  // GC NB: the translator_output* structs are owned/retained by the systemtap_session.

  for (unsigned i = 0; i < probes.size(); ++i)
    {
      tracepoint_derived_probe *p = probes[i];
      string header = p->header;

      // We cache the auxiliary output files on a per-header basis.  We don't
      // need one aux file per tracepoint, only one per tracepoint-header.
      translator_output *tpop = per_header_aux[header];
      if (tpop == 0)
        {
          tpop = s.op_create_auxiliary();
          per_header_aux[header] = tpop;

          // PR9993: Add extra headers to work around undeclared types in individual
          // include/trace/foo.h files
          const vector<string>& extra_decls = tracepoint_extra_decls (s, header);
          for (unsigned z=0; z<extra_decls.size(); z++)
            tpop->newline() << extra_decls[z] << "\n";

          // strip include/ substring, the same way as done in get_tracequery_module()
          size_t root_pos = header.rfind("include/");
          header = ((root_pos != string::npos) ? header.substr(root_pos + 8) : header);

          tpop->newline() << "#include <linux/tracepoint.h>" << endl;
          tpop->newline() << "#include <" << header << ">";

          // Starting in 2.6.35, at the same time NOARGS was added, the callback
          // always has a void* as the first parameter. PR11599
          tpop->newline() << "#ifdef DECLARE_TRACE_NOARGS";
          tpop->newline() << "#define STAP_TP_DATA   , NULL";
          tpop->newline() << "#define STAP_TP_PROTO  void *cb_data"
                          << " __attribute__ ((unused))";
          if (!p->args.empty())
            tpop->line() << ",";
          tpop->newline() << "#else";
          tpop->newline() << "#define STAP_TP_DATA";
          tpop->newline() << "#define STAP_TP_PROTO";
          if (p->args.empty())
            tpop->line() << " void";
          tpop->newline() << "#endif";

          tpop->newline() << "#define intptr_t long";
        }

      // collect the args that are actually in use
      vector<const tracepoint_arg*> used_args;
      for (unsigned j = 0; j < p->args.size(); ++j)
        if (p->args[j].used)
          used_args.push_back(&p->args[j]);

      // forward-declare the generated-side tracepoint callback
      tpop->newline() << "void enter_real_tracepoint_probe_" << i << "(";
      tpop->indent(2);
      if (used_args.empty())
        tpop->line() << "void";
      for (unsigned j = 0; j < used_args.size(); ++j)
        {
          if (j > 0)
            tpop->line() << ", ";
          tpop->line() << "int64_t";
        }
      tpop->line() << ");";
      tpop->indent(-2);

      // define the generated-side tracepoint callback - in the main translator-output
      s.op->newline() << "void enter_real_tracepoint_probe_" << i << "(";
      s.op->indent(2);
      if (used_args.empty())
        s.op->newline() << "void";
      for (unsigned j = 0; j < used_args.size(); ++j)
        {
          if (j > 0)
            s.op->line() << ", ";
          s.op->newline() << "int64_t __tracepoint_arg_" << used_args[j]->name;
        }
      s.op->newline() << ")";
      s.op->newline(-2) << "{";
      s.op->newline(1) << "struct stap_probe * const probe = "
                       << common_probe_init (p) << ";";
      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "probe",
				     "_STP_PROBE_HANDLER_TRACEPOINT");
      s.op->newline() << "c->ips.tracepoint_name = "
                      << lex_cast_qstring (p->tracepoint_name)
                      << ";";
      for (unsigned j = 0; j < used_args.size(); ++j)
        {
          s.op->newline() << "c->probe_locals." << p->name
                          << ".__tracepoint_arg_" << used_args[j]->name
                          << " = __tracepoint_arg_" << used_args[j]->name << ";";
        }
      s.op->newline() << "(*probe->ph) (c);";
      common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
      s.op->newline(-1) << "}";

      // define the real tracepoint callback function
      tpop->newline() << "static void enter_tracepoint_probe_" << i << "(";
      tpop->newline(2) << "STAP_TP_PROTO";
      for (unsigned j = 0; j < p->args.size(); ++j)
        {
          if (j > 0)
            tpop->line() << ", ";
          tpop->newline() << p->args[j].c_type << " __tracepoint_arg_" << p->args[j].name;
        }
      tpop->newline() << ")";
      tpop->newline(-2) << "{";
      tpop->newline(1) << "enter_real_tracepoint_probe_" << i << "(";
      tpop->indent(2);
      for (unsigned j = 0; j < used_args.size(); ++j)
        {
          if (j > 0)
            tpop->line() << ", ";
          tpop->newline() << "(int64_t)" << used_args[j]->typecast
                          << "__tracepoint_arg_" << used_args[j]->name;
        }
      tpop->newline() << ");";
      tpop->newline(-3) << "}";


      // emit normalized registration functions
      tpop->newline() << "int register_tracepoint_probe_" << i << "(void) {";
      tpop->newline(1) << "return register_trace_" << p->tracepoint_name
                       << "(enter_tracepoint_probe_" << i << " STAP_TP_DATA);";
      tpop->newline(-1) << "}";

      // NB: we're not prepared to deal with unreg failures.  However, failures
      // can only occur if the tracepoint doesn't exist (yet?), or if we
      // weren't even registered.  The former should be OKed by the initial
      // registration call, and the latter is safe to ignore.
      tpop->newline() << "void unregister_tracepoint_probe_" << i << "(void) {";
      tpop->newline(1) << "(void) unregister_trace_" << p->tracepoint_name
                       << "(enter_tracepoint_probe_" << i << " STAP_TP_DATA);";
      tpop->newline(-1) << "}";
      tpop->newline();

      // declare normalized registration functions
      s.op->newline() << "int register_tracepoint_probe_" << i << "(void);";
      s.op->newline() << "void unregister_tracepoint_probe_" << i << "(void);";

      tpop->assert_0_indent();
    }

  // emit an array of registration functions for easy init/shutdown
  s.op->newline() << "static struct stap_tracepoint_probe {";
  s.op->newline(1) << "int (*reg)(void);";
  s.op->newline(0) << "void (*unreg)(void);";
  s.op->newline(-1) << "} stap_tracepoint_probes[] = {";
  s.op->indent(1);
  for (unsigned i = 0; i < probes.size(); ++i)
    {
      s.op->newline () << "{";
      s.op->line() << " .reg=&register_tracepoint_probe_" << i << ",";
      s.op->line() << " .unreg=&unregister_tracepoint_probe_" << i;
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();
}


void
tracepoint_derived_probe_group::emit_module_init (systemtap_session &s)
{
  if (probes.size () == 0)
    return;

  s.op->newline() << "/* init tracepoint probes */";
  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "rc = stap_tracepoint_probes[i].reg();";
  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "for (j=i-1; j>=0; j--)"; // partial rollback
  s.op->newline(1) << "stap_tracepoint_probes[j].unreg();";
  s.op->newline(-1) << "break;"; // don't attempt to register any more probes
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";

  // This would be technically proper (on those autoconf-detectable
  // kernels that include this function in tracepoint.h), however we
  // already make several calls to synchronze_sched() during our
  // shutdown processes.

  // s.op->newline() << "if (rc)";
  // s.op->newline(1) << "tracepoint_synchronize_unregister();";
  // s.op->indent(-1);
}


void
tracepoint_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty())
    return;

  s.op->newline() << "/* deregister tracepoint probes */";
  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "stap_tracepoint_probes[i].unreg();";
  s.op->indent(-1);

  // Not necessary: see above.

  // s.op->newline() << "tracepoint_synchronize_unregister();";
}


struct tracepoint_query : public base_query
{
  tracepoint_query(dwflpp & dw, const string & tracepoint,
                   probe * base_probe, probe_point * base_loc,
                   vector<derived_probe *> & results):
    base_query(dw, "*"), tracepoint(tracepoint),
    base_probe(base_probe), base_loc(base_loc),
    results(results) {}

  const string& tracepoint;

  probe * base_probe;
  probe_point * base_loc;
  vector<derived_probe *> & results;
  set<string> probed_names;

  void handle_query_module();
  int handle_query_cu(Dwarf_Die * cudie);
  int handle_query_func(Dwarf_Die * func);
  void query_library (const char *) {}
  void query_plt (const char *entry, size_t addr) {}

  static int tracepoint_query_cu (Dwarf_Die * cudie, void * arg);
  static int tracepoint_query_func (Dwarf_Die * func, base_query * query);
};


void
tracepoint_query::handle_query_module()
{
  // look for the tracepoints in each CU
  dw.iterate_over_cus(tracepoint_query_cu, this, false);
}


int
tracepoint_query::handle_query_cu(Dwarf_Die * cudie)
{
  dw.focus_on_cu (cudie);

  // look at each function to see if it's a tracepoint
  string function = "stapprobe_" + tracepoint;
  return dw.iterate_over_functions (tracepoint_query_func, this, function);
}


int
tracepoint_query::handle_query_func(Dwarf_Die * func)
{
  dw.focus_on_function (func);

  assert(startswith(dw.function_name, "stapprobe_"));
  string tracepoint_instance = dw.function_name.substr(10);

  // check for duplicates -- sometimes tracepoint headers may be indirectly
  // included in more than one of our tracequery modules.
  if (!probed_names.insert(tracepoint_instance).second)
    return DWARF_CB_OK;

  derived_probe *dp = new tracepoint_derived_probe (dw.sess, dw, *func,
                                                    tracepoint_instance,
                                                    base_probe, base_loc);
  results.push_back (dp);
  return DWARF_CB_OK;
}


int
tracepoint_query::tracepoint_query_cu (Dwarf_Die * cudie, void * arg)
{
  tracepoint_query * q = static_cast<tracepoint_query *>(arg);
  if (pending_interrupts) return DWARF_CB_ABORT;
  return q->handle_query_cu(cudie);
}


int
tracepoint_query::tracepoint_query_func (Dwarf_Die * func, base_query * query)
{
  tracepoint_query * q = static_cast<tracepoint_query *>(query);
  if (pending_interrupts) return DWARF_CB_ABORT;
  return q->handle_query_func(func);
}


struct tracepoint_builder: public derived_probe_builder
{
private:
  dwflpp *dw;
  bool init_dw(systemtap_session& s);
  void get_tracequery_modules(systemtap_session& s,
                              const vector<string>& headers,
                              vector<string>& modules);

public:

  tracepoint_builder(): dw(0) {}
  ~tracepoint_builder() { delete dw; }

  void build_no_more (systemtap_session& s)
  {
    if (dw && s.verbose > 3)
      clog << _("tracepoint_builder releasing dwflpp") << endl;
    delete dw;
    dw = NULL;

    delete_session_module_cache (s);
  }

  void build(systemtap_session& s,
             probe *base, probe_point *location,
             literal_map_t const& parameters,
             vector<derived_probe*>& finished_results);
};



// Create (or cache) one or more tracequery .o modules, based upon the
// tracepoint-related header files given.  Return the generated or cached
// modules[].

void
tracepoint_builder::get_tracequery_modules(systemtap_session& s,
                                           const vector<string>& headers,
                                           vector<string>& modules)
{
  if (s.verbose > 2)
    {
      clog << _F("Pass 2: getting a tracepoint query for %zu headers: ", headers.size()) << endl;
      for (size_t i = 0; i < headers.size(); ++i)
        clog << "  " << headers[i] << endl;
    }

  map<string,string> headers_cache_obj;  // header name -> cache/.../tracequery_hash.o file name
  // Map the headers to cache .o names.  Note that this has side-effects of
  // creating the $SYSTEMTAP_DIR/.cache/XX/... directory and the hash-log file,
  // so we prefer not to repeat this.
  vector<string> uncached_headers;
  for (size_t i=0; i<headers.size(); i++)
    headers_cache_obj[headers[i]] = find_tracequery_hash(s, headers[i]);

  // They may be in the cache already.
  if (s.use_cache && !s.poison_cache)
    for (size_t i=0; i<headers.size(); i++)
      {
        // see if the cached module exists
        const string& tracequery_path = headers_cache_obj[headers[i]];
        if (!tracequery_path.empty() && file_exists(tracequery_path))
          {
            if (s.verbose > 2)
              clog << _F("Pass 2: using cached %s", tracequery_path.c_str()) << endl;

            // an empty file is a cached failure
            if (get_file_size(tracequery_path) > 0)
              modules.push_back (tracequery_path);
          }
        else
          uncached_headers.push_back(headers[i]);
      }
  else
    uncached_headers = headers;

  // If we have nothing left to search for, quit
  if (uncached_headers.empty()) return;

  map<string,string> headers_tracequery_src; // header -> C-source code mapping

  // We could query several subsets of headers[] to make this go
  // faster, but let's KISS and do one at a time.
  for (size_t i=0; i<uncached_headers.size(); i++)
    {
      const string& header = uncached_headers[i];

      // create a tracequery source file
      ostringstream osrc;

      // PR9993: Add extra headers to work around undeclared types in individual
      // include/trace/foo.h files
      vector<string> short_decls = tracepoint_extra_decls(s, header);

      // add each requested tracepoint header
      size_t root_pos = header.rfind("include/");
      short_decls.push_back(string("#include <") +
                            ((root_pos != string::npos) ? header.substr(root_pos + 8) : header) +
                            string(">"));

      osrc << "#ifdef CONFIG_TRACEPOINTS" << endl;
      osrc << "#include <linux/tracepoint.h>" << endl;

      // the kernel has changed this naming a few times, previously TPPROTO,
      // TP_PROTO, TPARGS, TP_ARGS, etc.  so let's just dupe the latest.
      osrc << "#ifndef PARAMS" << endl;
      osrc << "#define PARAMS(args...) args" << endl;
      osrc << "#endif" << endl;

      // override DECLARE_TRACE to synthesize probe functions for us
      osrc << "#undef DECLARE_TRACE" << endl;
      osrc << "#define DECLARE_TRACE(name, proto, args) \\" << endl;
      osrc << "  void stapprobe_##name(proto) {}" << endl;

      // 2.6.35 added the NOARGS variant, but it's the same for us
      osrc << "#undef DECLARE_TRACE_NOARGS" << endl;
      osrc << "#define DECLARE_TRACE_NOARGS(name) \\" << endl;
      osrc << "  DECLARE_TRACE(name, void, )" << endl;

      // 2.6.38 added the CONDITION variant, which can also just redirect
      osrc << "#undef DECLARE_TRACE_CONDITION" << endl;
      osrc << "#define DECLARE_TRACE_CONDITION(name, proto, args, cond) \\" << endl;
      osrc << "  DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))" << endl;

      // older tracepoints used DEFINE_TRACE, so redirect that too
      osrc << "#undef DEFINE_TRACE" << endl;
      osrc << "#define DEFINE_TRACE(name, proto, args) \\" << endl;
      osrc << "  DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))" << endl;

      // add the specified decls/#includes
      for (unsigned z=0; z<short_decls.size(); z++)
        osrc << "#undef TRACE_INCLUDE_FILE\n"
             << "#undef TRACE_INCLUDE_PATH\n"
             << short_decls[z] << "\n";

      // finish up the module source
      osrc << "#endif /* CONFIG_TRACEPOINTS */" << endl;

      // save the source file away
      headers_tracequery_src[header] = osrc.str();
    }

  // now build them all together
  map<string,string> tracequery_objs = make_tracequeries(s, headers_tracequery_src);

  // now plop them into the cache
  if (s.use_cache)
    for (size_t i=0; i<uncached_headers.size(); i++)
      {
        const string& header = uncached_headers[i];
        const string& tracequery_obj = tracequery_objs[header];
        const string& tracequery_path = headers_cache_obj[header];
        if (tracequery_obj !="" && file_exists(tracequery_obj))
          {
            copy_file(tracequery_obj, tracequery_path, s.verbose > 2);
            modules.push_back (tracequery_path);
          }
        else
          // cache an empty file for failures
          copy_file("/dev/null", tracequery_path, s.verbose > 2);
      }
}



bool
tracepoint_builder::init_dw(systemtap_session& s)
{
  if (dw != NULL)
    return true;

  vector<string> tracequery_modules;
  vector<string> system_headers;

  glob_t trace_glob;

  // find kernel_source_tree
  if (s.kernel_source_tree == "")
    {
      unsigned found;
      DwflPtr dwfl_ptr = setup_dwfl_kernel ("kernel", &found, s);
      Dwfl *dwfl = dwfl_ptr.get()->dwfl;
      if (found)
        {
          Dwarf_Die *cudie = 0;
          Dwarf_Addr bias;
          while ((cudie = dwfl_nextcu (dwfl, cudie, &bias)) != NULL)
            {
              assert_no_interrupts();
              Dwarf_Attribute attr;
              const char* name = dwarf_formstring (dwarf_attr (cudie, DW_AT_comp_dir, &attr));
              if (name) 
                {
                  if (s.verbose > 2)
                    clog << _F("Located kernel source tree (DW_AT_comp_dir) at '%s'", name) << endl;

                  s.kernel_source_tree = name;
                  break; // skip others; modern Kbuild uses same comp_dir for them all
                }
            }
        }
    }

  // prefixes
  vector<string> glob_prefixes;
  glob_prefixes.push_back (s.kernel_build_tree);
  if (s.kernel_source_tree != "")
    glob_prefixes.push_back (s.kernel_source_tree);

  // suffixes
  vector<string> glob_suffixes;
  glob_suffixes.push_back("include/trace/events/*.h");
  glob_suffixes.push_back("include/trace/*.h");
  glob_suffixes.push_back("arch/x86/kvm/*trace.h");
  glob_suffixes.push_back("fs/xfs/linux-*/xfs_tr*.h");

  // compute cartesian product
  vector<string> globs;
  for (unsigned i=0; i<glob_prefixes.size(); i++)
    for (unsigned j=0; j<glob_suffixes.size(); j++)
      globs.push_back (glob_prefixes[i]+string("/")+glob_suffixes[j]);

  set<string> duped_headers;
  for (unsigned z = 0; z < globs.size(); z++)
    {
      string glob_str = globs[z];
      if (s.verbose > 3)
        clog << _("Checking tracepoint glob ") << glob_str << endl;

      glob(glob_str.c_str(), 0, NULL, &trace_glob);
      for (unsigned i = 0; i < trace_glob.gl_pathc; ++i)
        {
          string header(trace_glob.gl_pathv[i]);

          // filter out a few known "internal-only" headers
          if (endswith(header, "/define_trace.h") ||
              endswith(header, "/ftrace.h")       ||
              endswith(header, "/trace_events.h") ||
              endswith(header, "_event_types.h"))
            continue;

          // skip identical headers from the build and source trees.
          size_t root_pos = header.rfind("include/");
          if (root_pos != string::npos &&
              !duped_headers.insert(header.substr(root_pos + 8)).second)
            continue;

          system_headers.push_back(header);
        }
      globfree(&trace_glob);
    }

  // Build tracequery modules
  get_tracequery_modules(s, system_headers, tracequery_modules);

  // TODO: consider other sources of tracepoint headers too, like from
  // a command-line parameter or some environment or .systemtaprc

  dw = new dwflpp(s, tracequery_modules, true);
  return true;
}

void
tracepoint_builder::build(systemtap_session& s,
                          probe *base, probe_point *location,
                          literal_map_t const& parameters,
                          vector<derived_probe*>& finished_results)
{
  if (!init_dw(s))
    return;

  string tracepoint;
  assert(get_param (parameters, TOK_TRACE, tracepoint));

  tracepoint_query q(*dw, tracepoint, base, location, finished_results);
  dw->iterate_over_modules(&query_module, &q);
}


// ------------------------------------------------------------------------
//  Standard tapset registry.
// ------------------------------------------------------------------------

void
register_standard_tapsets(systemtap_session & s)
{
  register_tapset_been(s);
  register_tapset_itrace(s);
  register_tapset_mark(s);
  register_tapset_procfs(s);
  register_tapset_timers(s);
  register_tapset_netfilter(s);
  register_tapset_utrace(s);

  // dwarf-based kprobe/uprobe parts
  dwarf_derived_probe::register_patterns(s);

  // XXX: user-space starter set
  s.pattern_root->bind_num(TOK_PROCESS)
    ->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)
    ->bind_privilege(pr_all)
    ->bind(new uprobe_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)
    ->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)->bind(TOK_RETURN)
    ->bind_privilege(pr_all)
    ->bind(new uprobe_builder ());

  // kernel tracepoint probes
  s.pattern_root->bind(TOK_KERNEL)->bind_str(TOK_TRACE)
    ->bind(new tracepoint_builder());

  // Kprobe based probe
  s.pattern_root->bind(TOK_KPROBE)->bind_str(TOK_FUNCTION)
     ->bind(new kprobe_builder());
  s.pattern_root->bind(TOK_KPROBE)->bind_str(TOK_MODULE)
     ->bind_str(TOK_FUNCTION)->bind(new kprobe_builder());
  s.pattern_root->bind(TOK_KPROBE)->bind_str(TOK_FUNCTION)->bind(TOK_RETURN)
     ->bind(new kprobe_builder());
  s.pattern_root->bind(TOK_KPROBE)->bind_str(TOK_FUNCTION)->bind(TOK_RETURN)
     ->bind_num(TOK_MAXACTIVE)->bind(new kprobe_builder());
  s.pattern_root->bind(TOK_KPROBE)->bind_str(TOK_MODULE)
     ->bind_str(TOK_FUNCTION)->bind(TOK_RETURN)->bind(new kprobe_builder());
  s.pattern_root->bind(TOK_KPROBE)->bind_str(TOK_MODULE)
     ->bind_str(TOK_FUNCTION)->bind(TOK_RETURN)
     ->bind_num(TOK_MAXACTIVE)->bind(new kprobe_builder());
  s.pattern_root->bind(TOK_KPROBE)->bind_num(TOK_STATEMENT)
      ->bind(TOK_ABSOLUTE)->bind(new kprobe_builder());

  //Hwbkpt based probe
  // NB: we formerly registered the probe point types only if the kernel configuration
  // allowed it.  However, we get better error messages if we allow probes to resolve.
  s.pattern_root->bind(TOK_KERNEL)->bind_num(TOK_HWBKPT)
    ->bind(TOK_HWBKPT_WRITE)->bind(new hwbkpt_builder());
  s.pattern_root->bind(TOK_KERNEL)->bind_str(TOK_HWBKPT)
    ->bind(TOK_HWBKPT_WRITE)->bind(new hwbkpt_builder());
  s.pattern_root->bind(TOK_KERNEL)->bind_num(TOK_HWBKPT)
    ->bind(TOK_HWBKPT_RW)->bind(new hwbkpt_builder());
  s.pattern_root->bind(TOK_KERNEL)->bind_str(TOK_HWBKPT)
    ->bind(TOK_HWBKPT_RW)->bind(new hwbkpt_builder());
  s.pattern_root->bind(TOK_KERNEL)->bind_num(TOK_HWBKPT)
    ->bind_num(TOK_LENGTH)->bind(TOK_HWBKPT_WRITE)->bind(new hwbkpt_builder());
  s.pattern_root->bind(TOK_KERNEL)->bind_num(TOK_HWBKPT)
    ->bind_num(TOK_LENGTH)->bind(TOK_HWBKPT_RW)->bind(new hwbkpt_builder());
  // length supported with address only, not symbol names

  //perf event based probe
  register_tapset_perf(s);
}


vector<derived_probe_group*>
all_session_groups(systemtap_session& s)
{
  vector<derived_probe_group*> g;

#define DOONE(x) \
  if (s. x##_derived_probes) \
    g.push_back ((derived_probe_group*)(s. x##_derived_probes))

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
  DOONE(tracepoint);
  DOONE(kprobe);
  DOONE(hwbkpt);
  DOONE(perf);
  DOONE(hrtimer);
  DOONE(procfs);
  DOONE(netfilter);

  // Another "order is important" item.  We want to make sure we
  // "register" the dummy task_finder probe group after all probe
  // groups that use the task_finder.
  DOONE(utrace);
  DOONE(itrace);
  DOONE(task_finder);
#undef DOONE
  return g;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
