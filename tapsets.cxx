// tapset resolution
// Copyright (C) 2005-2009 Red Hat Inc.
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
#include <glob.h>
#include <fnmatch.h>
#include <stdio.h>
#include <sys/types.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}


using namespace std;
using namespace __gnu_cxx;



// ------------------------------------------------------------------------
void
common_probe_entryfn_prologue (translator_output* o, string statestr,
                               string new_pp,
                               bool overload_processing)
{
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

  o->newline() << "c = per_cpu_ptr (contexts, smp_processor_id());";
  o->newline() << "if (atomic_inc_return (& c->busy) != 1) {";
  o->newline(1) << "#if !INTERRUPTIBLE";
  o->newline() << "atomic_inc (& skipped_count);";
  o->newline() << "#endif";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "atomic_inc (& skipped_count_reentrant);";
  o->newline() << "#ifdef DEBUG_REENTRANCY";
  o->newline() << "_stp_warn (\"Skipped %s due to %s residency on cpu %u\\n\", "
               << new_pp << ", c->probe_point ?: \"?\", smp_processor_id());";
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
  o->newline() << "c->nesting = 0;";
  o->newline() << "c->regs = 0;";
  o->newline() << "c->unwaddr = 0;";
  o->newline() << "c->probe_point = " << new_pp << ";";
  // reset unwound address cache
  o->newline() << "c->pi = 0;";
  o->newline() << "c->regparm = 0;";
  o->newline() << "c->marker_name = NULL;";
  o->newline() << "c->marker_format = NULL;";

  o->newline() << "#if INTERRUPTIBLE";
  o->newline() << "c->actionremaining = MAXACTION_INTERRUPTIBLE;";
  o->newline() << "#else";
  o->newline() << "c->actionremaining = MAXACTION;";
  o->newline() << "#endif";
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
                               bool overload_processing)
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

  o->newline() << "c->probe_point = 0;"; // vacated
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

  // Check for excessive skip counts.
  o->newline() << "if (unlikely (atomic_read (& skipped_count) > MAXSKIPPED)) {";
  o->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  o->newline() << "_stp_exit ();";
  o->newline(-1) << "}";

  o->newline() << "#if INTERRUPTIBLE";
  o->newline() << "preempt_enable_no_resched ();";
  o->newline() << "#else";
  o->newline() << "local_irq_restore (flags);";
  o->newline() << "#endif";
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
static const string TOK_RETURN("return");
static const string TOK_MAXACTIVE("maxactive");
static const string TOK_STATEMENT("statement");
static const string TOK_ABSOLUTE("absolute");
static const string TOK_PROCESS("process");
static const string TOK_MARK("mark");
static const string TOK_TRACE("trace");
static const string TOK_LABEL("label");

static int query_cu (Dwarf_Die * cudie, void * arg);

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
  void add_symbol(const char *name, bool weak, Dwarf_Addr addr,
                                               Dwarf_Addr *high_addr);
  enum info_status read_symbols(FILE *f, const string& path);
  enum info_status read_from_elf_file(const string& path);
  enum info_status read_from_text_file(const string& path);
  enum info_status get_from_elf();
  void prepare_section_rejection(Dwfl_Module *mod);
  bool reject_section(GElf_Word section);
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
  bool access_vars;

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
  static void register_patterns(systemtap_session& s);
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

  // Extracted parameters.
  bool has_kernel;
  bool has_module;
  bool has_process;
  string module_val; // has_kernel => module_val = "kernel"

  virtual void handle_query_module() = 0;
};


base_query::base_query(dwflpp & dw, literal_map_t const & params):
  sess(dw.sess), dw(dw)
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

base_query::base_query(dwflpp & dw, const string & module_val)
  : sess(dw.sess), dw(dw), module_val(module_val)
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
  dwarf_query(systemtap_session & sess,
	      probe * base_probe,
	      probe_point * base_loc,
	      dwflpp & dw,
	      literal_map_t const & params,
	      vector<derived_probe *> & results);

  vector<derived_probe *> & results;
  probe * base_probe;
  probe_point * base_loc;

  virtual void handle_query_module();
  void query_module_dwarf();
  void query_module_symtab();

  void add_probe_point(string const & funcname,
		       char const * filename,
		       int line,
		       Dwarf_Die *scope_die,
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

  bool has_mark;

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


struct dwarf_builder: public derived_probe_builder
{
  map <string,dwflpp*> kern_dw; /* NB: key string could be a wildcard */
  map <string,dwflpp*> user_dw;
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
  void dwarf_build_no_more (bool verbose)
  {
    for (map<string,dwflpp*>::iterator udi = kern_dw.begin();
         udi != kern_dw.end();
         udi ++)
      {
        if (verbose)
          clog << "dwarf_builder releasing kernel dwflpp " << udi->first << endl;
        delete udi->second;
      }
    kern_dw.erase (kern_dw.begin(), kern_dw.end());

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

  set<string> probes_handled;
  struct probe_table
  {
  public:
    enum probe_types
      {
	uprobe_type = 0x31425250, // "PRB1"
	kprobe_type = 0x32425250, // "PRB2"
	utrace_type = 0x33425250, // "PRB3"
      } probe_type;
    __uint64_t probe_arg;
    string & mark_name;
    string probe_name;
    probe_table(string & mark_name, systemtap_session & sess, dwflpp * dw);
    bool is_uprobe() {return probe_type == uprobe_type;};
    bool is_utrace() {return probe_type == utrace_type;};
    bool get_next_probe();
    void convert_probe(probe *new_base);
    void convert_location(probe *new_base, probe_point *new_location);

  private:
    bool have_probes;
    systemtap_session & sess;
    dwflpp* dw;
    Elf_Data *pdata;
    size_t probe_scn_offset;
    size_t probe_scn_addr;
  };
};

dwarf_builder::probe_table::probe_table(string& mark_name, systemtap_session & sess, dwflpp* dw):
  mark_name(mark_name), sess(sess), dw(dw)
{
  Elf* elf;
  GElf_Shdr shdr_mem;
  GElf_Shdr *shdr = NULL;
  Dwarf_Addr bias;
  size_t shstrndx;

  // Explicitly look in the main elf file first.
  elf = dwfl_module_getelf (dw->module, &bias);
  Elf_Scn *probe_scn = NULL;

  dwfl_assert ("getshdrstrndx", elf_getshdrstrndx (elf, &shstrndx));

  have_probes = false;
  
  // Is there a .probes section?
  while ((probe_scn = elf_nextscn (elf, probe_scn)))
    {
      shdr = gelf_getshdr (probe_scn, &shdr_mem);
      assert (shdr != NULL);

      if (strcmp (elf_strptr (elf, shstrndx, shdr->sh_name), ".probes") == 0)
	{
	  have_probes = true;
	  break;
	}
    }

  // Older versions put .probes section in the debuginfo dwarf file,
  // so check if it actually exists, if not take a look in the debuginfo file
  if (! have_probes || (have_probes && shdr->sh_type == SHT_NOBITS))
    {
      elf = dwarf_getelf (dwfl_module_getdwarf (dw->module, &bias));
      if (! elf)
	return;
      dwfl_assert ("getshdrstrndx", elf_getshdrstrndx (elf, &shstrndx));
      probe_scn = NULL;
      while ((probe_scn = elf_nextscn (elf, probe_scn)))
	{
	  shdr = gelf_getshdr (probe_scn, &shdr_mem);
	  if (strcmp (elf_strptr (elf, shstrndx, shdr->sh_name),
		      ".probes") == 0)
	    have_probes = true;
	    break;
	}
    }

  if (!have_probes)
    return;

  pdata = elf_getdata_rawchunk (elf, shdr->sh_offset, shdr->sh_size, ELF_T_BYTE);
  probe_scn_offset = 0;
  probe_scn_addr = shdr->sh_addr;
  assert (pdata != NULL);
  if (sess.verbose > 4)
    clog << "got .probes elf scn_addr@0x" << probe_scn_addr << dec
	 << ", size: " << pdata->d_size << endl;
}

bool
dwarf_builder::probe_table::get_next_probe()
{
  if (! have_probes)
    return false;
    
  // Extract probe info from the .probes section
  if (sess.verbose > 3)
    clog << "probe_type == use_uprobe, use statement addr" << endl;
 
  while (probe_scn_offset < pdata->d_size)
    {
      struct probe_entry
      {
	__uint64_t name;
	__uint64_t arg;
      }  *pbe;
      __uint32_t *type = (__uint32_t*) ((char*)pdata->d_buf + probe_scn_offset);
      probe_type = (enum probe_types)*type;
      if (probe_type != uprobe_type && probe_type != kprobe_type
	  && probe_type != utrace_type)
	{
	  // Unless this is a mangled .probes section, this happens
	  // because the name of the probe comes first, followed by
	  // the sentinel.
	  if (sess.verbose > 5)
	    clog << "got unknown probe_type: 0x" << hex << probe_type
		 << dec << endl;
	  probe_scn_offset += sizeof(__uint32_t);
	  continue;
	}
      probe_scn_offset += sizeof(__uint32_t);
      probe_scn_offset += probe_scn_offset % sizeof(__uint64_t);
      pbe = (struct probe_entry*) ((char*)pdata->d_buf + probe_scn_offset);
      probe_name = (char*)((char*)pdata->d_buf + pbe->name - (char*)probe_scn_addr);
      probe_arg = pbe->arg;
      if (sess.verbose > 4)
	clog << "saw .probes " << probe_name
	     << "@0x" << hex << probe_arg << dec << endl;
    
      probe_scn_offset += sizeof (struct probe_entry);
      if ((mark_name == probe_name)
	  || (dw->name_has_wildcard (mark_name)
	      && dw->function_name_matches_pattern (probe_name, mark_name)))
	{
	  if (sess.verbose > 3)
	    clog << "found probe_name" << probe_name << " at 0x"
		 << hex << probe_arg << dec << endl;
	  return true;
	}
      else
	continue;
    }
  return false;
}


void
dwarf_builder::probe_table::convert_probe (probe *new_base)
{
  block *b = ((block*)(new_base->body));

  functioncall *fc = new functioncall;
  fc->function = (probe_type == utrace_type) ? "_utrace_syscall_arg" : "ulong_arg";
  fc->tok = new_base->body->tok;
  literal_number* num = new literal_number((probe_type == utrace_type) ? 0 : 1);
  num->tok = new_base->body->tok;
  fc->args.push_back(num);

  // Generate: if (arg1 != mark("label")) next;
  functioncall *fcus = new functioncall;
  fcus->function = "user_string";
  fcus->type = pe_string;
  fcus->tok = new_base->body->tok;
  fcus->args.push_back(fc);

  if_statement *is = new if_statement;
  is->thenblock = new next_statement;
  is->elseblock = NULL;
  is->tok = new_base->body->tok;
  comparison *be = new comparison;
  be->op = "!=";
  be->tok = new_base->body->tok;
  be->left = fcus;
  be->right = new literal_string(mark_name);
  is->condition = be;
  b->statements.insert(b->statements.begin(),(statement*) is);

  if (probe_type == utrace_type)
    {
      // Generate: if ($syscall != 0xbead) next;
      if_statement *issc = new if_statement;
      issc->thenblock = new next_statement;
      issc->elseblock = NULL;
      issc->tok = new_base->body->tok;
      comparison *besc = new comparison;
      besc->op = "!=";
      besc->tok = new_base->body->tok;
      functioncall* n = new functioncall;
      n->tok = new_base->body->tok;
      n->function = "_utrace_syscall_nr";
      n->referent = 0;
      besc->left = n;
      literal_number* fake_syscall = new literal_number(0xbead);
      fake_syscall->tok = new_base->body->tok;
      besc->right = fake_syscall;
      issc->condition = besc;
      b->statements.insert(b->statements.begin(),(statement*) issc);
    }
  
  if (probe_type == kprobe_type)
    {
      // Generate: if (arg2 != kprobe_type) next;
      if_statement *istid = new if_statement;
      istid->thenblock = new next_statement;
      istid->elseblock = NULL;
      istid->tok = new_base->body->tok;
      comparison *betid = new comparison;
      betid->op = "!=";
      betid->tok = new_base->body->tok;

      functioncall *arg2 = new functioncall;
      arg2->function = "ulong_arg";
      arg2->tok = new_base->body->tok;
      literal_number* num = new literal_number(2);
      num->tok = new_base->body->tok;
      arg2->args.push_back(num);

      betid->left = arg2;
      literal_number* littid = new literal_number(kprobe_type);
      littid->tok = new_base->body->tok;
      betid->right = littid;
      istid->condition = betid;
      b->statements.insert(b->statements.begin(),(statement*) istid);
    }
  
#ifdef __i386__
  if (probe_type == kprobe_type)
    {
      functioncall *rp = new functioncall;
      rp->tok = new_base->body->tok;
      rp->function = "regparm";
      rp->tok = new_base->body->tok;
      literal_number* littid = new literal_number(0);
      littid->tok = new_base->body->tok;
      rp->args.push_back(littid);
      expr_statement* es = new expr_statement;
      es->tok = new_base->body->tok;
      es->value = rp;
      b->statements.insert(b->statements.begin(),(statement*) es);
    }
#endif
}


void
dwarf_builder::probe_table::convert_location (probe *new_base,
					      probe_point *new_location)
{
  if (probe_type == kprobe_type)
    {
      new_location->components[0]->functor = "kernel";
      new_location->components[0]->arg = NULL;
      new_location->components[1]->functor = "function";
      new_location->components[1]->arg = new literal_string("*getegid*");
      new_base->locations.push_back(new_location);
    }
  else if (probe_type == utrace_type)
    {
      // process("executable").syscall
      new_location->components[1]->functor = "syscall";
      new_location->components[1]->arg = NULL;
      new_base->locations.push_back(new_location);
    }
}


dwarf_query::dwarf_query(systemtap_session & sess,
			 probe * base_probe,
			 probe_point * base_loc,
			 dwflpp & dw,
			 literal_map_t const & params,
			 vector<derived_probe *> & results)
  : base_query(dw, params), results(results),
    base_probe(base_probe), base_loc(base_loc)
{
  // Reduce the query to more reasonable semantic values (booleans,
  // extracted strings, numbers, etc).
  has_function_str = get_string_param(params, TOK_FUNCTION, function_str_val);
  has_function_num = get_number_param(params, TOK_FUNCTION, function_num_val);

  has_statement_str = get_string_param(params, TOK_STATEMENT, statement_str_val);
  has_statement_num = get_number_param(params, TOK_STATEMENT, statement_num_val);

  has_label = get_string_param(params, TOK_LABEL, label_val);

  has_call = has_null_param(params, TOK_CALL);
  has_inline = has_null_param(params, TOK_INLINE);
  has_return = has_null_param(params, TOK_RETURN);
  has_maxactive = get_number_param(params, TOK_MAXACTIVE, maxactive_val);
  has_absolute = has_null_param(params, TOK_ABSOLUTE);
  has_mark = false;

  if (has_function_str)
    spec_type = parse_function_spec(function_str_val);
  else if (has_statement_str)
    spec_type = parse_function_spec(statement_str_val);

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
add_label_name(dwarf_query *q, const char *name)
{
  // this is a kludge to let the listing mode show labels to the user
  if (q->sess.listing_mode)
    q->results.back()->locations[0]->components.push_back
      (new probe_point::component(TOK_LABEL, new literal_string (name)));
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

      // Translate to an actual symbol address.
      addr = dw.literal_addr_to_sym_addr (addr);

      Dwarf_Die* cudie = dw.query_cu_containing_address(addr);
      if (cudie) // address could be wildly out of range
        query_cu(cudie, this);
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
  bool report = dbinfo_reqt == dbr_need_dwarf || !sess.consult_symtab;
  dw.get_module_dwarf(false, report);

  // prebuild the symbol table to resolve aliases
  dw.mod_info->get_symtab(this);

  if (dw.mod_info->dwarf_status == info_present)
    query_module_dwarf();

  // Consult the symbol table if we haven't found all we're looking for.
  // asm functions can show up in the symbol table but not in dwarf.
  if (sess.consult_symtab && !query_done)
    query_module_symtab();
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


void
dwarf_query::add_probe_point(const string& funcname,
			     const char* filename,
			     int line,
			     Dwarf_Die* scope_die,
			     Dwarf_Addr addr)
{
  string reloc_section; // base section for relocation purposes
  Dwarf_Addr reloc_addr; // relocated
  string blacklist_section; // linking section for blacklist purposes
  const string& module = dw.module_name; // "kernel" or other

  assert (! has_absolute); // already handled in dwarf_builder::build()

  reloc_addr = dw.relocate_address(addr, reloc_section, blacklist_section);

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

  bool bad = dw.blacklisted_p (funcname, filename, line, module,
                               blacklist_section, addr, has_return);
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
      q->add_probe_point(func, file ? file : "",
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
      q->dw.iterate_over_labels (&i->die, q->label_val.c_str(), q->function.c_str(),
                                 q, query_statement);
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
query_dwarf_func (Dwarf_Die * func, base_query * bq)
{
  dwarf_query * q = static_cast<dwarf_query *>(bq);

  try
    {
      q->dw.focus_on_function (func);

      if (q->dw.func_is_inline ()
          && (! q->has_call) && (! q->has_return)
	  && (q->has_statement_str || q->has_function_str))
	{
	  if (q->sess.verbose>3)
	    clog << "checking instances of inline " << q->dw.function_name
                 << "\n";
	  q->dw.iterate_over_inline_instances (query_dwarf_inline_instance, q);

          if (q->dw.function_name_final_match (q->function))
            return DWARF_CB_ABORT;
	}
      else if (!q->dw.func_is_inline () && (! q->has_inline))
	{
	  bool record_this_function = false;

	  if (q->has_statement_str || q->has_function_str)
	    {
	      record_this_function = true;
	    }
	  else if (q->has_function_num || q->has_statement_num)
	    {
              Dwarf_Addr query_addr =
                (q->has_function_num ? q->function_num_val :
		 q->has_statement_num ? q->statement_num_val :
		 (assert(0) , 0));
	      Dwarf_Die d;
	      q->dw.function_die (&d);

	      // Translate literal address to symbol address, then
	      // compensate for dw bias.
	      query_addr = q->dw.literal_addr_to_sym_addr(query_addr);
	      query_addr -= q->dw.module_bias;

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

		  // Translate literal address to symbol address, then
		  // compensate for dw bias (will be used for query dw funcs).
		  func.entrypc = q->dw.literal_addr_to_sym_addr(func.entrypc);
		  func.entrypc -= q->dw.module_bias;

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
          // is at the start of an assembly instruction.  Mark probes are in the
	  // middle of a macro and thus not strictly at a statement beginning.
	  // Guru mode may override this check.
          if (q->has_statement_num && ! q->has_mark)
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
                      << " does not match the beginning of a statement";
                  if (address_line)
                    msg << " (try 0x" << hex << lineaddr << ")";
                  if (! q->sess.guru_mode)
                    throw semantic_error(msg.str());
                  else if (! q->sess.suppress_warnings)
                   q->sess.print_warning(msg.str());
                }
            }
	  // Pick up [entrypc, name, DIE] tuples for all the functions
	  // matching the query, and fill in the prologue endings of them
	  // all in a single pass.
	  int rc = q->dw.iterate_over_functions (query_dwarf_func, q,
                                                 q->function,
                                                 q->has_statement_num);
	  if (rc != DWARF_CB_OK)
	    q->query_done = true;

          if ((q->sess.prologue_searching || q->has_process) // PR 6871
              && !q->has_statement_str && !q->has_statement_num) // PR 2608
            if (! q->filtered_functions.empty())
              q->dw.resolve_prologue_endings (q->filtered_functions);

	  if (q->has_label)
	    {
	      if (q->line[0] == 0)		// No line number specified
                q->dw.iterate_over_labels (q->dw.cu, q->label_val.c_str(), q->function.c_str(),
                                           q, query_statement);
	      else
		for (set<char const *>::const_iterator i = q->filtered_srcfiles.begin();
		     i != q->filtered_srcfiles.end(); ++i)
		  q->dw.iterate_over_srcfile_lines (*i, q->line, q->has_statement_str,
						    q->line_type, query_srcfile_label, q);
	    }
	  else if ((q->has_statement_str || q->has_function_str)
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
      expect_machine = "ppc";
      if (! q->has_process) break; // 32-bit kernel/module
      /* FALLSTHROUGH */
    case EM_PPC64:
      expect_machine2 = "ppc64";
      break;
    case EM_S390: expect_machine = "s390x"; break;
    case EM_IA_64: expect_machine = "ia64"; break;
    case EM_ARM: expect_machine = "armv*"; break;
      // XXX: fill in some more of these
    default: expect_machine = "?"; break;
    }

  if (! debug_filename) debug_filename = main_filename;
  if (! debug_filename) debug_filename = name;

  if (fnmatch (expect_machine.c_str(), sess_machine.c_str(), 0) != 0 &&
      fnmatch (expect_machine2.c_str(), sess_machine.c_str(), 0) != 0)
    {
      stringstream msg;
      msg << "ELF machine " << expect_machine << "|" << expect_machine2
          << " (code " << elf_machine
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
         << " ELF machine " << expect_machine << "|" << expect_machine2
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


struct dwarf_var_expanding_visitor: public var_expanding_visitor
{
  dwarf_query & q;
  Dwarf_Die *scope_die;
  Dwarf_Addr addr;
  block *add_block;
  probe *add_probe;
  std::map<std::string, symbol *> return_ts_map;
  bool visited;

  dwarf_var_expanding_visitor(dwarf_query & q, Dwarf_Die *sd, Dwarf_Addr a):
    q(q), scope_die(sd), addr(a), add_block(NULL), add_probe(NULL), visited(false) {}
  void visit_target_symbol (target_symbol* e);
  void visit_cast_op (cast_op* e);
};


unsigned var_expanding_visitor::tick = 0;

void
var_expanding_visitor::visit_assignment (assignment* e)
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
  new_left = require (e->left);
  target_symbol_setter_functioncalls.pop ();
  new_right = require (e->right);

  if (fcall != NULL)
    {
      // Our left child is informing us that it was a target variable
      // and it has been replaced with a set_target_foo() function
      // call; we are going to provide that function call -- with the
      // right child spliced in as sole argument -- in place of
      // ourselves, in the var expansion we're in the middle of making.

      // FIXME: for the time being, we only support plan $foo = bar,
      // not += or any other op= variant. This is fixable, but a bit
      // ugly.
      if (e->op != "=")
	throw semantic_error ("Operator-assign expressions on target "
			     "variables not implemented", e->tok);

      assert (new_left == fcall);
      fcall->args.push_back (new_right);
      provide (fcall);
    }
  else
    {
      e->left = new_left;
      e->right = new_right;
      provide (e);
    }
}


void
dwarf_var_expanding_visitor::visit_target_symbol (target_symbol *e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');
  visited = true;

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
	  provide (i->second);
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
      provide (tmpsym);

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

      if (e->addressof)
        throw semantic_error("cannot take address of context variable", e->tok);

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
          expression *texp = tsym;
          texp = require (texp); // NB: throws nothing ...
          if (tsym->saved_conversion_error) // ... but this is how we know it happened.
            {

            }
          else
            {
              pf->raw_components += "return";
              pf->raw_components += "=%#x ";
              pf->args.push_back(texp);
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
                if (! diename) continue;

                tsym->tok = e->tok;
                tsym->base_name = "$";
                tsym->base_name += diename;

                // Ignore any variable that isn't accessible.
                tsym->saved_conversion_error = 0;
                expression *texp = tsym;
                texp = require (texp); // NB: throws nothing ...
                if (tsym->saved_conversion_error) // ... but this is how we know it happened.
                  {
                    if (q.sess.verbose>2)
                      {
                        for (semantic_error *c = tsym->saved_conversion_error;
                             c != 0;
                             c = c->chain) {
                          clog << "variable location problem: " << c->what() << endl;
                        }
                      }

                    pf->raw_components += diename;
                    pf->raw_components += "=? ";
                  }
                else
                  {
                    pf->raw_components += diename;
                    pf->raw_components += "=%#x ";
                    pf->args.push_back(texp);
                  }
              }
            while (dwarf_siblingof (&result, &result) == 0);
        }

      pf->components = print_format::string_to_components(pf->raw_components);
      provide (pf);

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
						   e,
						   lvalue,
						   fdecl->type);
	}
      else
        {
	  ec->code = q.dw.literal_stmt_for_local (scope_die,
						  addr,
						  e->base_name.substr(1),
						  e,
						  lvalue,
						  fdecl->type);
	}

      if (! lvalue)
        ec->code += "/* pure */";
    }
  catch (const semantic_error& er)
    {
      if (!q.sess.skip_badvars)
	{
	  // We suppress this error message, and pass the unresolved
	  // target_symbol to the next pass.  We hope that this value ends
	  // up not being referenced after all, so it can be optimized out
	  // quietly.
	  provide (e);
	  semantic_error* saveme = new semantic_error (er); // copy it
	  saveme->tok1 = e->tok; // XXX: token not passed to q.dw code generation routines
	  // NB: we can have multiple errors, since a $target variable
	  // may be expanded in several different contexts:
	  //     function ("*") { $var }
	  saveme->chain = e->saved_conversion_error;
	  e->saved_conversion_error = saveme;
	}
      else 
	{
	  // Upon user request for ignoring context, the symbol is replaced 
	  // with a literal 0 and a warning message displayed
	  literal_number* ln_zero = new literal_number (0);
	  ln_zero->tok = e->tok;
	  provide (ln_zero);
          if (!q.sess.suppress_warnings)
            q.sess.print_warning ("Bad $context variable being substituted with literal 0",
                                  e->tok);
	}
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
  q.sess.functions[fdecl->name]=fdecl;

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

  provide (n);
}


void
dwarf_var_expanding_visitor::visit_cast_op (cast_op *e)
{
  // Fill in our current module context if needed
  if (e->module.empty())
    e->module = q.dw.module_name;

  var_expanding_visitor::visit_cast_op(e);
}


struct dwarf_cast_query : public base_query
{
  cast_op& e;
  const bool lvalue;

  exp_type& pe_type;
  string& code;

  dwarf_cast_query(dwflpp& dw, const string& module, cast_op& e,
                   bool lvalue, exp_type& pe_type, string& code):
    base_query(dw, module), e(e), lvalue(lvalue),
    pe_type(pe_type), code(code) {}

  void handle_query_module();
  int handle_query_cu(Dwarf_Die * cudie);

  static int cast_query_cu (Dwarf_Die * cudie, void * arg);
};


void
dwarf_cast_query::handle_query_module()
{
  if (!code.empty())
    return;

  // look for the type in each CU
  dw.iterate_over_cus(cast_query_cu, this);
}


int
dwarf_cast_query::handle_query_cu(Dwarf_Die * cudie)
{
  if (!code.empty())
    return DWARF_CB_ABORT;

  dw.focus_on_cu (cudie);
  Dwarf_Die* type_die = dw.declaration_resolve(e.type.c_str());
  if (type_die)
    {
      try
        {
          code = dw.literal_stmt_for_pointer (type_die, &e,
                                              lvalue, pe_type);
        }
      catch (const semantic_error& er)
        {
          // XXX might be better to try again in another CU
	  // NB: we can have multiple errors, since a @cast
	  // may be expanded in several different contexts:
	  //     function ("*") { @cast(...) }
	  semantic_error* new_er = new semantic_error(er);
	  new_er->chain = e.saved_conversion_error;
	  e.saved_conversion_error = new_er;
        }
      return DWARF_CB_ABORT;
    }
  return DWARF_CB_OK;
}


int
dwarf_cast_query::cast_query_cu (Dwarf_Die * cudie, void * arg)
{
  dwarf_cast_query * q = static_cast<dwarf_cast_query *>(arg);
  if (pending_interrupts) return DWARF_CB_ABORT;
  return q->handle_query_cu(cudie);
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


void dwarf_cast_expanding_visitor::filter_special_modules(string& module)
{
  // look for "<path/to/header>" or "kernel<path/to/header>"
  // for those cases, build a module including that header
  if (module[module.size() - 1] == '>' &&
      (module[0] == '<' || module.compare(0, 7, "kernel<") == 0))
    {
      string cached_module;
      if (s.use_cache)
        {
          // see if the cached module exists
          cached_module = find_typequery_hash(s, module);
          if (!cached_module.empty())
            {
              int fd = open(cached_module.c_str(), O_RDONLY);
              if (fd != -1)
                {
                  if (s.verbose > 2)
                    clog << "Pass 2: using cached " << cached_module << endl;
                  module = cached_module;
                  close(fd);
                  return;
                }
            }
        }

      // no cached module, time to make it
      if (make_typequery(s, module) == 0)
        {
          if (s.use_cache)
            {
              // try to save typequery in the cache
              if (s.verbose > 2)
                clog << "Copying " << module
                  << " to " << cached_module << endl;
              if (copy_file(module.c_str(),
                            cached_module.c_str()) != 0)
                cerr << "Copy failed (\"" << module << "\" to \""
                  << cached_module << "\"): " << strerror(errno) << endl;
            }
        }
    }
}


void dwarf_cast_expanding_visitor::visit_cast_op (cast_op* e)
{
  bool lvalue = is_active_lvalue(e);
  if (lvalue && !s.guru_mode)
    throw semantic_error("write to typecast value not permitted", e->tok);

  if (e->module.empty())
    e->module = "kernel"; // "*" may also be reasonable to search all kernel modules

  string code;
  exp_type type = pe_long;

  // split the module string by ':' for alternatives
  vector<string> modules;
  tokenize(e->module, modules, ":");
  for (unsigned i = 0; code.empty() && i < modules.size(); ++i)
    {
      string& module = modules[i];
      filter_special_modules(module);

      // NB: This uses '/' to distinguish between kernel modules and userspace,
      // which means that userspace modules won't get any PATH searching.
      dwflpp* dw;
      try
	{
	  if (module.find('/') == string::npos)
	    {
	      // kernel or kernel module target
	      dw = db.get_kern_dw(s, module);
	    }
	  else
	    {
	      module = find_executable (module); // canonicalize it
	      dw = db.get_user_dw(s, module);
	    }
	}
      catch (const semantic_error& er)
	{
	  /* ignore and go to the next module */
	  continue;
	}

      dwarf_cast_query q (*dw, module, *e, lvalue, type, code);
      dw->iterate_over_modules(&query_module, &q);
    }

  if (code.empty())
    {
      // We pass the unresolved cast_op to the next pass, and hope
      // that this value ends up not being referenced after all, so
      // it can be optimized out quietly.
      provide (e);
      return;
    }

  string fname = (string(lvalue ? "_dwarf_tvar_set" : "_dwarf_tvar_get")
		  + "_" + e->base_name.substr(1)
		  + "_" + lex_cast<string>(tick++));

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  fdecl->type = type;
  fdecl->name = fname;

  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;
  ec->code = code;
  fdecl->body = ec;

  // Give the fdecl an argument for the pointer we're trying to cast
  vardecl *v1 = new vardecl;
  v1->type = pe_long;
  v1->name = "pointer";
  v1->tok = e->tok;
  fdecl->formal_args.push_back(v1);

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
      v2->tok = e->tok;
      fdecl->formal_args.push_back(v2);
    }
  else
    ec->code += "/* pure */";

  s.functions[fdecl->name] = fdecl;

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0;  // NB: must not resolve yet, to ensure inclusion in session
  n->args.push_back(e->operand);

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
  this->access_vars = false;

  // XXX: hack for strange g++/gcc's
#ifndef USHRT_MAX
#define USHRT_MAX 32767
#endif

  // Range limit maxactive() value
  if (q.has_maxactive && (q.maxactive_val < 0 || q.maxactive_val > USHRT_MAX))
    throw semantic_error ("maxactive value out of range [0,"
                          + lex_cast<string>(USHRT_MAX) + "]",
                          q.base_loc->tok);

  // Expand target variables in the probe body
  if (!null_die(scope_die))
    {
      dwarf_var_expanding_visitor v (q, scope_die, dwfl_addr);
      this->body = v.require (this->body);
      this->access_vars = v.visited;

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
          {
            retro_name += ("@" + string (filename));
            if (line > 0)
              retro_name += (":" + lex_cast<string> (line));
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
dwarf_derived_probe::register_patterns(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  dwarf_builder *dw = new dwarf_builder();

  update_visitor *filter = new dwarf_cast_expanding_visitor(s, *dw);
  s.code_filters.push_back(filter);

  register_function_and_statement_variants(root->bind(TOK_KERNEL), dw);
  register_function_and_statement_variants(root->bind_str(TOK_MODULE), dw);
  root->bind(TOK_KERNEL)->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)->bind(dw);
  root->bind(TOK_KERNEL)->bind_str(TOK_FUNCTION)->bind_str(TOK_LABEL)->bind(dw);
  root->bind_str(TOK_PROCESS)->bind_str(TOK_FUNCTION)->bind_str(TOK_LABEL)->bind(dw);

  register_function_and_statement_variants(root->bind_str(TOK_PROCESS), dw);
  root->bind_str(TOK_PROCESS)->bind_str(TOK_MARK)->bind(dw);
  root->bind_str(TOK_PROCESS)->bind_num(TOK_MARK)->bind(dw);
}

void
dwarf_derived_probe::emit_probe_local_init(translator_output * o)
{
  if (access_vars)
    {
      // if accessing $variables, emit bsp cache setup for speeding up
      o->newline() << "bspcache(c->unwaddr, c->regs);";
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
  s.op->newline() << "#define KRETACTIVE (max(15,6*NR_CPUS))";
  s.op->newline() << "#endif";

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
  s.op->newline() << "static struct stap_dwarf_kprobe {";
  s.op->newline(1) << "union { struct kprobe kp; struct kretprobe krp; } u;";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "struct kprobe dummy;";
  s.op->newline() << "#endif";
  s.op->newline(-1) << "} stap_dwarf_kprobes[" << probes_by_module.size() << "];";
  // NB: bss!

  s.op->newline() << "static struct stap_dwarf_probe {";
  s.op->newline(1) << "const unsigned return_p:1;";
  s.op->newline() << "const unsigned maxactive_p:1;";
  s.op->newline() << "const unsigned optional_p:1;";
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
#undef CALCIT

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
      if (p->locations[0]->optional)
        s.op->line() << " .optional_p=1,";
      s.op->line() << " .address=(unsigned long)0x" << hex << p->addr << dec << "ULL,";
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
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->pp");
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

  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->pp");
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
  s.op->newline(1) << "kp->u.krp.maxactive = KRETACTIVE;";
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
  s.op->newline() << "if (rc) {"; // PR6749: tolerate a failed register_*probe.
  s.op->newline(1) << "sdp->registered_p = 0;";
  s.op->newline() << "if (!sdp->optional_p)";
  s.op->newline(1) << "_stp_warn (\"probe %s (address 0x%lx) registration error (rc %d)\", probe_point, relocated_addr, rc);";
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
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/1 on '%s': %d\\n\", sdp->pp, kp->u.krp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline() << "atomic_add (kp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/2 on '%s': %d\\n\", sdp->pp, kp->u.krp.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "} else {";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline(1) << "unregister_kprobe (&kp->u.kp);";
  s.op->newline() << "#endif";
  s.op->newline() << "atomic_add (kp->u.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kprobe on '%s': %d\\n\", sdp->pp, kp->u.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "}";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES) && defined(__ia64__)";
  s.op->newline() << "unregister_kprobe (&kp->dummy);";
  s.op->newline() << "#endif";
  s.op->newline() << "sdp->registered_p = 0;";
  s.op->newline(-1) << "}";
}

struct sdt_var_expanding_visitor: public var_expanding_visitor
{
  sdt_var_expanding_visitor(string & process_name, string & probe_name,
			    int arg_count, bool have_reg_args, bool utrace_probe):
    process_name (process_name), probe_name (probe_name),
    have_reg_args (have_reg_args), utrace_probe (utrace_probe),
    arg_count (arg_count)
  {
    assert(!have_reg_args || (arg_count >= 0 && arg_count <= 10));
  }
  string & process_name;
  string & probe_name;
  bool have_reg_args;
  bool utrace_probe;
  int arg_count;

  void visit_target_symbol (target_symbol* e);
};

void
sdt_var_expanding_visitor::visit_target_symbol (target_symbol *e)
{
  if (e->base_name == "$$name")
    {
      if (e->addressof)
        throw semantic_error("cannot take address of sdt variable", e->tok);

      literal_string *myname = new literal_string (probe_name);
      myname->tok = e->tok;
      provide(myname);
      return;
    }
  else if (e->base_name.find("$arg") == string::npos || ! have_reg_args)
    {
      provide(e);
      return;
    }

  int argno = lex_cast<int>(e->base_name.substr(4));
  if (argno < 1 || argno > arg_count)
    throw semantic_error ("invalid argument number", e->tok);

  bool lvalue = is_active_lvalue(e);
  functioncall *fc = new functioncall;

  // First two args are hidden: 1. pointer to probe name 2. task id
  if (arg_count < 2)
    {
      fc->function = utrace_probe ? "_utrace_syscall_arg" : "ulong_arg";
      fc->type = pe_long;
      fc->tok = e->tok;
      literal_number* num = new literal_number(argno + (utrace_probe ? 1 : 2));
      num->tok = e->tok;
      fc->args.push_back(num);
    }
  else				// args passed as a struct
    {
      fc->function = "user_long";
      fc->tok = e->tok;
      binary_expression *be = new binary_expression;
      be->tok = e->tok;
      functioncall *get_arg1 = new functioncall;
      get_arg1->function = utrace_probe ? "_utrace_syscall_arg" : "pointer_arg";
      get_arg1->tok = e->tok;
      literal_number* num = new literal_number((utrace_probe ? 2 : 3));
      num->tok = e->tok;
      get_arg1->args.push_back(num);

      be->left = get_arg1;
      be->op = "+";
      literal_number* inc = new literal_number((argno - 1) * 8);
      be->right = inc;
      fc->args.push_back(be);
    }
  
  if (lvalue)
    *(target_symbol_setter_functioncalls.top()) = fc;

  if (e->components.empty())
    {
      if (e->addressof)
        throw semantic_error("cannot take address of sdt variable", e->tok);

      provide(fc);
      return;
    }
  cast_op *cast = new cast_op;
  cast->base_name = "@cast";
  cast->tok = e->tok;
  cast->operand = fc;
  cast->components = e->components;
  cast->type = probe_name + "_arg" + lex_cast<string>(argno);
  cast->module = process_name;

  provide(cast);
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

  string module_name;
  if (has_null_param (parameters, TOK_KERNEL))
    {
      dw = get_kern_dw(sess, "kernel");
    }
  else if (get_param (parameters, TOK_MODULE, module_name))
    {
      dw = get_kern_dw(sess, module_name);
    }
  else if (get_param (parameters, TOK_PROCESS, module_name))
    {
      module_name = find_executable (module_name); // canonicalize it

      // user-space target; we use one dwflpp instance per module name
      // (= program or shared library)
      dw = get_user_dw(sess, module_name);
    }

  if (sess.verbose > 3)
    clog << "dwarf_builder::build for " << module_name << endl;

  string mark_name;
  if (get_param(parameters, TOK_MARK, mark_name))
    {
      probe_table probe_table(mark_name, sess, dw);
      if (! probe_table.get_next_probe())
	return;
      
      if (sess.verbose > 3)
	clog << "TOK_MARK: " << probe_table.mark_name << endl;

      if (probe_table.is_uprobe())
	{
	  do
	    {
	      probe *new_base = new probe;
	      *new_base = *base;
	      
	      new_base->body = deep_copy_visitor::deep_copy(base->body);
	      probe_point *new_location = new probe_point;
	      *new_location = *location;
	      new_base->locations.clear();
	      
	      const token* sv_tok = new_location->components[1]->arg->tok;
	      new_location->components[1]->functor = TOK_STATEMENT;
	      new_location->components[1]->arg = new literal_number((long)probe_table.probe_arg);
	      new_location->components[1]->arg->tok = sv_tok;
	      ((literal_map_t&)parameters)[TOK_STATEMENT] = new_location->components[1]->arg;
	      new_base->locations.push_back(new_location);

	      if (sess.verbose > 3)
		clog << "probe_type == use_uprobe, use statement addr: 0x"
		     << hex << probe_table.probe_arg << dec << endl;
	    
	      // Now expand the local variables in the probe body
	      sdt_var_expanding_visitor svv (module_name, probe_table.probe_name,
					     probe_table.probe_arg, false, false);
	      new_base->body = svv.require (new_base->body);

	      dwarf_query q(sess, new_base, new_location, *dw, parameters, finished_results);
	      q.has_mark = true;
	      dw->iterate_over_modules(&query_module, &q);
	      if (sess.listing_mode)
		{
		  finished_results.back()->locations[0]->components[1]->functor = TOK_MARK;
		  finished_results.back()->locations[0]->components[1]->arg = new literal_string (probe_table.probe_name.c_str());
		}
	    }
	  while (probe_table.get_next_probe());
	  return;
	}

      else if (probe_table.probe_type == probe_table.kprobe_type
	       || probe_table.probe_type == probe_table.utrace_type)
	{
	  do
	    {
	      set<string>::iterator pb;
	      pb = probes_handled.find(probe_table.probe_name);
	      if (pb == probes_handled.end())
		probes_handled.insert (probe_table.probe_name);
	      else
		return;

	      probe *new_base = new probe;
	      *new_base = *base;
	      
	      new_base->body = deep_copy_visitor::deep_copy(base->body);
	      probe_point *new_location = new probe_point;
	      *new_location = *location;
	      new_base->locations.clear();
	      
	      probe_table.convert_probe(new_base);

	      // Expand the local variables in the probe body
	      sdt_var_expanding_visitor svv (module_name, probe_table.probe_name,
					     probe_table.probe_arg, true,
					     probe_table.is_utrace());
	      new_base->body = svv.require (new_base->body);
	      probe_table.convert_location(new_base, new_location);
	      derive_probes(sess, new_base, finished_results);
	      if (sess.listing_mode)
		{
		  finished_results.back()->locations[0]->components[0]->functor = TOK_FUNCTION;
		  finished_results.back()->locations[0]->components[0]->arg = new literal_string (module_name);
		  finished_results.back()->locations[0]->components[1]->functor = TOK_MARK;
		  finished_results.back()->locations[0]->components[1]->arg = new literal_string (probe_table.probe_name.c_str());
		}
	    }
	  while (probe_table.get_next_probe());
	  return;
	}

      else
	{
	  location->components[1]->functor = TOK_FUNCTION;
	  location->components[1]->arg = new literal_string("*");
	  ((literal_map_t&)parameters)[TOK_FUNCTION] = location->components[1]->arg;
	  location->components.push_back(new probe_point::component(TOK_LABEL));
	  location->components[2]->arg = new literal_string("_stapprobe1_" + probe_table.mark_name);
	  ((literal_map_t&)parameters).erase(TOK_MARK);
	  ((literal_map_t&)parameters).insert(pair<string,literal*>(TOK_LABEL, location->components[2]->arg));

	  if (sess.verbose > 3)
	    clog << "probe_type == use_uprobe_no_dwarf, use label name: "
		 << "_stapprobe1_" << probe_table.mark_name << endl;
	}

      dw->module = 0;
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

  dw->iterate_over_modules(&query_module, &q);
}

symbol_table::~symbol_table()
{
  for (iterator_t i = map_by_addr.begin(); i != map_by_addr.end(); ++i)
    delete i->second;
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

  if (map_by_addr.size() < 1)
    {
      cerr << "Symbol table error: "
           << path << " contains no function symbols." << endl;
      return info_absent;
    }
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
      if (name && GELF_ST_TYPE(sym.st_info) == STT_FUNC &&
                                                   !reject_section(section))
        add_symbol(name, (GELF_ST_BIND(sym.st_info) == STB_WEAK),
                                              sym.st_value, &high_addr);
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
            new_funcs[it->second->name] = it->second->die;
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

  // Expand target variables in the probe body
  if (!null_die(scope_die))
    {
      dwarf_var_expanding_visitor v (q, scope_die, dwfl_addr); // XXX: user-space deref's!
      this->body = v.require (this->body);

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
          {
            retro_name += ("@" + string (filename));
            if (line > 0)
              retro_name += (":" + lex_cast<string> (line));
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
  enable_task_finder(s);

  // Ask buildrun.cxx to build extra module if needed, and
  // signal staprun to load that module
  s.need_uprobes = true;
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

  // If uprobes isn't in the kernel, pull it in from the runtime.
  s.op->newline() << "#if defined(CONFIG_UPROBES) || defined(CONFIG_UPROBES_MODULE)";
  s.op->newline() << "#include <linux/uprobes.h>";
  s.op->newline() << "#else";
  s.op->newline() << "#include \"uprobes/uprobes.h\"";
  s.op->newline() << "#endif";
  s.op->newline() << "#ifndef UPROBES_API_VERSION";
  s.op->newline() << "#define UPROBES_API_VERSION 1";
  s.op->newline() << "#endif";

  s.op->newline() << "#ifndef MULTIPLE_UPROBES";
  s.op->newline() << "#define MULTIPLE_UPROBES 256"; // maximum possible armed uprobes per process() probe point
						     // or apprx. max number of processes mapping a shared library
  s.op->newline() << "#endif";
  s.op->newline() << "#ifndef MAXUPROBES";
  s.op->newline() << "#define MAXUPROBES (MULTIPLE_UPROBES * " << probes.size() << ")";
  s.op->newline() << "#endif";

  // In .bss, the shared pool of uprobe/uretprobe structs.  These are
  // too big to embed in the initialized .data stap_uprobe_spec array.
  s.op->newline() << "static struct stap_uprobe {";
  s.op->newline(1) << "union { struct uprobe up; struct uretprobe urp; };";
  s.op->newline() << "int spec_index;"; // index into stap_uprobe_specs; <0 == free && unregistered
  s.op->newline(-1) << "} stap_uprobes [MAXUPROBES];";
  s.op->newline() << "DEFINE_MUTEX(stap_uprobes_lock);"; // protects against concurrent registration/unregistration

  // Emit vma callbacks.
  s.op->newline() << "#ifdef STP_NEED_VMA_TRACKER";
  s.op->newline() << "static struct stap_task_finder_target stap_uprobe_vmcbs[] = {";
  s.op->indent(1);
  for (unsigned i = 0; i < probes.size(); i++)
    {
      uprobe_derived_probe* p = probes[i];
      if (p->pid != 0)
	emit_vma_callback_probe_decl (s, "", p->pid);
      else
	emit_vma_callback_probe_decl (s, p->module, (int64_t)0);
    }
  s.op->newline(-1) << "};";
  s.op->newline() << "#endif";

  s.op->newline() << "static struct stap_uprobe_spec {";
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
      s.op->line() << " .address=(unsigned long)0x" << hex << p->address << dec << "ULL,";
      s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
      s.op->line() << " .ph=&" << p->name << ",";
      if (p->return_p) s.op->line() << " .return_p=1,";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";

  s.op->newline() << "static void enter_uprobe_probe (struct uprobe *inst, struct pt_regs *regs) {";
  s.op->newline(1) << "struct stap_uprobe *sup = container_of(inst, struct stap_uprobe, up);";
  s.op->newline() << "struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sups->pp");
  s.op->newline() << "if (sup->spec_index < 0 ||"
                  << "sup->spec_index >= " << probes.size() << ") return;"; // XXX: should not happen
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "(*sups->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";

  s.op->newline() << "static void enter_uretprobe_probe (struct uretprobe_instance *inst, struct pt_regs *regs) {";
  s.op->newline(1) << "struct stap_uprobe *sup = container_of(inst->rp, struct stap_uprobe, urp);";
  s.op->newline() << "struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sups->pp");
  s.op->newline() << "if (sup->spec_index < 0 ||"
                  << "sup->spec_index >= " << probes.size() << ") return;"; // XXX: should not happen
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
  // free/busy flag) value with the outer protective stap_probes_lock
  // spinlock, to protect it against concurrent registration /
  // unregistration.

  s.op->newline();
  s.op->newline() << "static int stap_uprobe_change (struct task_struct *tsk, int register_p, unsigned long relocation, struct stap_uprobe_spec *sups) {";
  s.op->newline(1) << "int spec_index = (sups - stap_uprobe_specs);";
  s.op->newline() << "int handled_p = 0;";
  s.op->newline() << "int slotted_p = 0;";
  s.op->newline() << "int rc = 0;";
  s.op->newline() << "int i;";

  s.op->newline() << "mutex_lock (& stap_uprobes_lock);";
  s.op->newline() << "for (i=0; i<MAXUPROBES; i++) {"; // XXX: slow linear search
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[i];";

  // register new uprobe
  s.op->newline() << "if (register_p && sup->spec_index < 0) {";
  s.op->newline(1) << "#if (UPROBES_API_VERSION < 2)";
  // See PR6829 comment.
  s.op->newline() << "if (sup->spec_index == -1 && sup->up.kdata != NULL) continue;";
  s.op->newline() << "else if (sup->spec_index == -2 && sup->urp.u.kdata != NULL) continue;";
  s.op->newline() << "#endif";
  s.op->newline() << "sup->spec_index = spec_index;";
  s.op->newline() << "slotted_p = 1;";
  s.op->newline() << "break;";
  s.op->newline(-1) << "} else if (!register_p && "
                    << "sup->spec_index == spec_index && " // a u[ret]probe set up for this probe point
                    << "((sups->return_p && sup->urp.u.pid == tsk->tgid && sup->urp.u.vaddr == relocation + sups->address) ||" // dying uretprobe
                    << "(!sups->return_p && sup->up.pid == tsk->tgid && sup->up.vaddr == relocation + sups->address))) {"; // dying uprobe
  s.op->newline(1) << "slotted_p = 1;";
  s.op->newline() << "break;"; // exit to-free slot search
  s.op->newline(-1) << "}";

  s.op->newline(-1) << "}";
  s.op->newline() << "mutex_unlock (& stap_uprobes_lock);";

  s.op->newline() << "#ifdef DEBUG_UPROBES";
  s.op->newline() << "printk (KERN_INFO \"%cuprobe spec %d idx %d process %s[%d] reloc %p pp '%s'\\n\", ";
  s.op->line() << "(register_p ? '+' : '-'), spec_index, (slotted_p ? i : -1), tsk->comm, tsk->tgid, (void*) relocation, sups->pp);";
  s.op->newline() << "#endif";

  // Here, slotted_p implies that `i' points to the single
  // stap_uprobes[] element that has been slotted in for registration
  // or unregistration processing.  !slotted_p implies that the table
  // was full (registration; MAXUPROBES) or that no matching entry was
  // found (unregistration; should not happen).

  s.op->newline() << "if (register_p && slotted_p) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[i];";
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
  s.op->newline(1) << "printk (KERN_WARNING \"uprobe failed %s[%d] '%s' addr %p rc %d\\n\", tsk->comm, tsk->tgid, sups->pp, (void*)(relocation + sups->address), rc);";
  // NB: we need to release this slot, so we need to borrow the mutex temporarily.
  s.op->newline() << "mutex_lock (& stap_uprobes_lock);";
  s.op->newline() << "sup->spec_index = -1;";
  s.op->newline() << "mutex_unlock (& stap_uprobes_lock);";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "handled_p = 1;"; // success
  s.op->newline(-1) << "}";

  s.op->newline(-1) << "} else if (!register_p && slotted_p) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[i];";
  s.op->newline() << "int unregistered_flag;";
  // PR6829, PR9940:
  // Here we're unregistering for one of two reasons:
  // 1. the process image is going away (or gone) due to exit or exec; or
  // 2. the vma containing the probepoint has been unmapped.
  // In case 1, it's sort of a nop, because uprobes will notice the event
  // and dispose of the probes eventually, if it hasn't already.  But by
  // calling unmap_u[ret]probe() ourselves, we free up sup right away.
  //
  // In both cases, we must use unmap_u[ret]probe instead of
  // unregister_u[ret]probe, so uprobes knows not to try to restore the
  // original opcode.
  s.op->newline() << "#if (UPROBES_API_VERSION >= 2)";
  s.op->newline() << "if (sups->return_p)";
  s.op->newline(1) << "unmap_uretprobe (& sup->urp);";
  s.op->newline(-1) << "else";
  s.op->newline(1) << "unmap_uprobe (& sup->up);";
  s.op->newline(-1) << "unregistered_flag = -1;";
  s.op->newline() << "#else";
  // Uprobes lacks unmap_u[ret]probe.  Before reusing sup, we must wait
  // until uprobes turns loose of the u[ret]probe on its own, as indicated
  // by uprobe.kdata = NULL.
  s.op->newline() << "unregistered_flag = (sups->return_p ? -2 : -1);";
  s.op->newline() << "#endif";
  s.op->newline() << "mutex_lock (& stap_uprobes_lock);";
  s.op->newline() << "sup->spec_index = unregistered_flag;";
  s.op->newline() << "mutex_unlock (& stap_uprobes_lock);";
  s.op->newline() << "handled_p = 1;";
  s.op->newline(-1) << "}"; // if slotted_p

  // NB: handled_p implies slotted_p

  s.op->newline() << "if (! handled_p) {";
  s.op->newline(1) << "#ifdef STP_TIMING";
  s.op->newline() << "atomic_inc (register_p ? & skipped_count_uprobe_reg : & skipped_count_uprobe_unreg);";
  s.op->newline() << "#endif";
  // NB: duplicates common_entryfn_epilogue, but then this is not a probe entry fn epilogue.
  s.op->newline() << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
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

  // The task_finder_mmap_callback we use for ET_DYN targets.
  s.op->newline();
  s.op->newline() << "static int stap_uprobe_mmap_found (struct stap_task_finder_target *tgt, struct task_struct *tsk, char *path, unsigned long addr, unsigned long length, unsigned long offset, unsigned long vm_flags) {";
  s.op->newline(1) << "struct stap_uprobe_spec *sups = container_of(tgt, struct stap_uprobe_spec, finder);";
  // 1 - shared libraries' executable segments load from offset 0 - ld.so convention
  s.op->newline() << "if (offset != 0) return 0;";
  // 2 - the shared library we're interested in
  s.op->newline() << "if (path == NULL || strcmp (path, sups->pathname)) return 0;";
  // 3 - probe address within the mapping limits; test should not fail
  s.op->newline() << "if (sups->address >= addr && sups->address < (addr + length)) return 0;";
  // 4 - mapping should be executable
  s.op->newline() << "if (!(vm_flags & VM_EXEC)) return 0;";

  s.op->newline() << "#ifdef DEBUG_TASK_FINDER_VMA";
  s.op->newline() << "printk (KERN_INFO \"vmchange pid %d path %s addr %p length %lu offset %p\\n\", tsk->tgid, path, (void *) addr, length, (void*) offset);";
  s.op->newline() << "printk (KERN_INFO \"sups %p pp %s path %s address %p\\n\", sups, sups->pp, sups->pathname ?: \"\", (void*) sups->address);";
  s.op->newline() << "#endif";

  s.op->newline(0) << "return stap_uprobe_change (tsk, 1, addr, sups);";
  s.op->newline(-1) << "}";
  s.op->assert_0_indent();


  s.op->newline();
}


void
uprobe_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;
  s.op->newline() << "#ifdef STP_NEED_VMA_TRACKER";
  s.op->newline() << "_stp_sym_init();";
  s.op->newline() << "/* ---- uprobe vma callbacks ---- */";
  s.op->newline() << "for (i=0; i<ARRAY_SIZE(stap_uprobe_vmcbs); i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_task_finder_target *r = &stap_uprobe_vmcbs[i];";
  s.op->newline() << "rc = stap_register_task_finder_target(r);";
  s.op->newline(-1) << "}";
  s.op->newline() << "#endif";

  s.op->newline() << "/* ---- user probes ---- */";

  s.op->newline() << "for (j=0; j<MAXUPROBES; j++) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[j];";
  s.op->newline() << "sup->spec_index = -1;"; // free slot
  // NB: we assume the rest of the struct (specificaly, sup->up) is
  // initialized to zero.  This is so that we can use
  // sup->up->kdata = NULL for "really free!"  PR 6829.
  s.op->newline(-1) << "}";
  s.op->newline() << "mutex_init (& stap_uprobes_lock);";

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_uprobe_spec *sups = & stap_uprobe_specs[i];";
  s.op->newline() << "probe_point = sups->pp;"; // for error messages
  s.op->newline() << "if (sups->finder.pathname) sups->finder.callback = & stap_uprobe_process_found;";
  s.op->newline() << "else if (sups->pathname) sups->finder.mmap_callback = & stap_uprobe_mmap_found;";
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
  // __stp_task_finder_cleanup() via stap_stop_task_finder().
  //
  // This function blocks until all callbacks are completed, so there
  // is supposed to be no possibility of any registration-related code starting
  // to run in parallel with our shutdown here.  So we don't need to protect the
  // stap_uprobes[] array with the mutex.

  s.op->newline() << "for (j=0; j<MAXUPROBES; j++) {";
  s.op->newline(1) << "struct stap_uprobe *sup = & stap_uprobes[j];";
  s.op->newline() << "struct stap_uprobe_spec *sups = &stap_uprobe_specs [sup->spec_index];";
  s.op->newline() << "if (sup->spec_index < 0) continue;"; // free slot

  s.op->newline() << "if (sups->return_p) {";
  s.op->newline(1) << "#ifdef DEBUG_UPROBES";
  s.op->newline() << "printk (KERN_INFO \"-uretprobe spec %d index %d pid %d addr %p\\n\", sup->spec_index, j, sup->up.pid, (void*) sup->up.vaddr);";
  s.op->newline() << "#endif";
  // NB: PR6829 does not change that we still need to unregister at
  // *this* time -- when the script as a whole exits.
  s.op->newline() << "unregister_uretprobe (& sup->urp);";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "#ifdef DEBUG_UPROBES";
  s.op->newline() << "printk (KERN_INFO \"-uprobe spec %d index %d pid %d addr %p\\n\", sup->spec_index, j, sup->urp.u.pid, (void*) sup->urp.u.vaddr);";
  s.op->newline() << "#endif";
  s.op->newline() << "unregister_uprobe (& sup->up);";
  s.op->newline(-1) << "}";

  s.op->newline() << "sup->spec_index = -1;";

  // XXX: uprobe missed counts?

  s.op->newline(-1) << "}";

  s.op->newline() << "mutex_destroy (& stap_uprobes_lock);";
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
			long maxactive_val
			);
  string symbol_name;
  Dwarf_Addr addr;
  bool has_return;
  bool has_statement;
  bool has_maxactive;
  long maxactive_val;
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
					    long maxactive_val
					    ):
  derived_probe (base, location),
  symbol_name (name), addr (stmt_addr),
  has_return (has_return), has_statement (has_statement),
  has_maxactive (has_maxactive), maxactive_val (maxactive_val)
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
      comps.push_back (new probe_point::component(TOK_STATEMENT, new literal_number(addr)));
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
  s.op->newline() << "#define KRETACTIVE (max(15,6*NR_CPUS))";
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

  size_t pp_name_max = 0, symbol_string_name_max = 0;
  size_t pp_name_tot = 0, symbol_string_name_tot = 0;
  for (p_b_m_iterator it = probes_by_module.begin(); it != probes_by_module.end(); it++)
    {
      kprobe_derived_probe* p = it->second;
#define DOIT(var,expr) do {                             \
        size_t var##_size = (expr) + 1;                 \
        var##_max = max (var##_max, var##_size);        \
        var##_tot += var##_size; } while (0)
      DOIT(pp_name, lex_cast_qstring(*p->sole_location()).size());
      DOIT(symbol_string_name, p->symbol_name.size());
#undef DOIT
    }

#define CALCIT(var)                                                     \
	s.op->newline() << "const char " << #var << "[" << var##_name_max << "] ;";

  CALCIT(pp);
  CALCIT(symbol_string);
#undef CALCIT

  s.op->newline() << "const unsigned long address;";
  s.op->newline() << "void (* const ph) (struct context*);";
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

      s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
      s.op->line() << " .ph=&" << p->name;
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
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->pp");
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "(*sdp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
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

  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "sdp->pp");
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "c->pi = inst;"; // for assisting runtime's backtrace logic
  s.op->newline() << "(*sdp->ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";
}


void
kprobe_derived_probe_group::emit_module_init (systemtap_session& s)
{
  s.op->newline() << "for (i=0; i<" << probes_by_module.size() << "; i++) {";
  s.op->newline(1) << "struct stap_dwarfless_probe *sdp = & stap_dwarfless_probes[i];";
  s.op->newline() << "struct stap_dwarfless_kprobe *kp = & stap_dwarfless_kprobes[i];";
  s.op->newline() << "void *addr = (void *) sdp->address;";
  s.op->newline() << "const char *symbol_name = addr ? NULL : sdp->symbol_string;";
  s.op->newline() << "probe_point = sdp->pp;"; // for error messages
  s.op->newline() << "if (sdp->return_p) {";
  s.op->newline(1) << "kp->u.krp.kp.addr = addr;";
  s.op->newline() << "kp->u.krp.kp.symbol_name = (char *) symbol_name;";
  s.op->newline() << "if (sdp->maxactive_p) {";
  s.op->newline(1) << "kp->u.krp.maxactive = sdp->maxactive_val;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "kp->u.krp.maxactive = KRETACTIVE;";
  s.op->newline(-1) << "}";
  s.op->newline() << "kp->u.krp.handler = &enter_kretprobe2_probe;";
  // to ensure safeness of bspcache, always use aggr_kprobe on ia64
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "kp->dummy.addr = kp->u.krp.kp.addr;";
  s.op->newline() << "kp->dummy.symbol_name = kp->u.krp.kp.symbol_name;";
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
  s.op->newline() << "kp->u.kp.symbol_name = (char *) symbol_name;";
  s.op->newline() << "kp->u.kp.pre_handler = &enter_kprobe2_probe;";
  s.op->newline() << "#ifdef __ia64__";
  s.op->newline() << "kp->dummy.pre_handler = NULL;";
  s.op->newline() << "kp->dummy.addr = kp->u.kp.addr;";
  s.op->newline() << "kp->dummy.symbol_name = kp->u.kp.symbol_name;";
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
  s.op->newline(1) << "_stp_warn (\"probe %s (address 0x%lx) registration error (rc %d)\", probe_point, addr, rc);";
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
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/1 on '%s': %d\\n\", sdp->pp, kp->u.krp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline() << "atomic_add (kp->u.krp.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.krp.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kretprobe/2 on '%s': %d\\n\", sdp->pp, kp->u.krp.kp.nmissed);";
  s.op->newline(-1) << "#endif";
  s.op->newline(-1) << "} else {";
  s.op->newline() << "#if !defined(STAPCONF_UNREGISTER_KPROBES)";
  s.op->newline(1) << "unregister_kprobe (&kp->u.kp);";
  s.op->newline() << "#endif";
  s.op->newline() << "atomic_add (kp->u.kp.nmissed, & skipped_count);";
  s.op->newline() << "#ifdef STP_TIMING";
  s.op->newline() << "if (kp->u.kp.nmissed)";
  s.op->newline(1) << "_stp_warn (\"Skipped due to missed kprobe on '%s': %d\\n\", sdp->pp, kp->u.kp.nmissed);";
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
  int64_t statement_num_val = 0, maxactive_val = 0;
  bool has_function_str, has_module_str, has_statement_num;
  bool has_absolute, has_return, has_maxactive;

  has_function_str = get_param(parameters, TOK_FUNCTION, function_string_val);
  has_module_str = get_param(parameters, TOK_MODULE, module_string_val);
  has_return = has_null_param (parameters, TOK_RETURN);
  has_maxactive = get_param(parameters, TOK_MAXACTIVE, maxactive_val);
  has_statement_num = get_param(parameters, TOK_STATEMENT, statement_num_val);
  has_absolute = has_null_param (parameters, TOK_ABSOLUTE);

  if (has_function_str)
    {
      if (has_module_str)
	function_string_val = module_string_val + ":" + function_string_val;

      finished_results.push_back (new kprobe_derived_probe (base,
							    location, function_string_val,
							    0, has_return,
							    has_statement_num,
							    has_maxactive,
							    maxactive_val));
    }
  else
    {
      // assert guru mode for absolute probes
      if ( has_statement_num && has_absolute && !base->privileged )
	throw semantic_error ("absolute statement probe in unprivileged script", base->tok);

      finished_results.push_back (new kprobe_derived_probe (base,
							    location, "",
							    statement_num_val,
							    has_return,
							    has_statement_num,
							    has_maxactive,
							    maxactive_val));
    }
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
  void printargs (std::ostream &o) const;
  void join_group (systemtap_session& s);
  void print_dupe_stamp(ostream& o);
  void emit_probe_context_vars (translator_output* o);
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
  string argname = e->base_name.substr(1);

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
      semantic_error* saveme =
        new semantic_error("unable to find tracepoint variable '" + e->base_name
                           + "' (alternatives:" + alternatives.str () + ")", e->tok);
      // NB: we can have multiple errors, since a target variable
      // may be expanded in several different contexts:
      //     trace ("*") { $foo->bar }
      saveme->chain = e->saved_conversion_error;
      e->saved_conversion_error = saveme;
      provide (e);
      return;
    }

  // make sure we're not dereferencing base types
  if (!e->components.empty() && !arg->isptr)
    switch (e->components[0].first)
      {
      case target_symbol::comp_literal_array_index:
        throw semantic_error("tracepoint variable '" + e->base_name
                             + "' may not be used as array", e->tok);
      case target_symbol::comp_struct_member:
        throw semantic_error("tracepoint variable '" + e->base_name
                             + "' may not be used as a structure", e->tok);
      default:
        throw semantic_error("invalid use of tracepoint variable '"
                             + e->base_name + "'", e->tok);
      }

  // we can only write to dereferenced fields, and only if guru mode is on
  bool lvalue = is_active_lvalue(e);
  if (lvalue && (!dw.sess.guru_mode || e->components.empty()))
    throw semantic_error("write to tracepoint variable '" + e->base_name
                         + "' not permitted", e->tok);
  // XXX: if a struct/union arg is passed by value, then writing to its fields
  // is also meaningless until you dereference past a pointer member.  It's
  // harder to detect and prevent that though...

  if (e->components.empty())
    {
      if (e->addressof)
        throw semantic_error("cannot take address of tracepoint variable", e->tok);

      // Just grab the value from the probe locals
      e->probe_context_var = "__tracepoint_arg_" + arg->name;
      e->type = pe_long;
      provide (e);
    }
  else
    {
      // Synthesize a function to dereference the dwarf fields,
      // with a pointer parameter that is the base tracepoint variable
      functiondecl *fdecl = new functiondecl;
      fdecl->tok = e->tok;
      embeddedcode *ec = new embeddedcode;
      ec->tok = e->tok;

      string fname = (string(lvalue ? "_tracepoint_tvar_set" : "_tracepoint_tvar_get")
                      + "_" + e->base_name.substr(1)
                      + "_" + lex_cast<string>(tick++));

      fdecl->name = fname;
      fdecl->body = ec;

      try
        {
          ec->code = dw.literal_stmt_for_pointer (&arg->type_die, e,
                                                  lvalue, fdecl->type);
        }
      catch (const semantic_error& er)
        {
          // We suppress this error message, and pass the unresolved
          // variable to the next pass.  We hope that this value ends
          // up not being referenced after all, so it can be optimized out
          // quietly.
          semantic_error* saveme = new semantic_error (er); // copy it
          saveme->tok1 = e->tok; // XXX: token not passed to dw code generation routines
          // NB: we can have multiple errors, since a target variable
          // may be expanded in several different contexts:
          //     trace ("*") { $foo->bar }
          saveme->chain = e->saved_conversion_error;
          e->saved_conversion_error = saveme;
          provide (e);
          return;
        }

      // Give the fdecl an argument for the raw tracepoint value
      vardecl *v1 = new vardecl;
      v1->type = pe_long;
      v1->name = "pointer";
      v1->tok = e->tok;
      fdecl->formal_args.push_back(v1);

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

      dw.sess.functions[fdecl->name] = fdecl;

      // Synthesize a functioncall.
      functioncall* n = new functioncall;
      n->tok = e->tok;
      n->function = fname;
      n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session

      // make the original a bare target symbol for the tracepoint value,
      // which will be passed into the dwarf dereferencing code
      e->components.clear();
      n->args.push_back(require(e));

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
    throw semantic_error("cannot take address of context variable", e->tok);

  if (is_active_lvalue (e))
    throw semantic_error("write to tracepoint '" + e->base_name + "' not permitted", e->tok);

  if (!e->components.empty())
    switch (e->components[0].first)
      {
      case target_symbol::comp_literal_array_index:
        throw semantic_error("tracepoint '" + e->base_name + "' may not be used as array",
                             e->tok);
      case target_symbol::comp_struct_member:
        throw semantic_error("tracepoint '" + e->base_name + "' may not be used as a structure",
                             e->tok);
      default:
        throw semantic_error("invalid tracepoint '" + e->base_name + "' use", e->tok);
      }

  if (e->base_name == "$$name")
    {
      // Synthesize a functioncall.
      functioncall* n = new functioncall;
      n->tok = e->tok;
      n->function = "_mark_name_get";
      n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session
      provide (n);
    }
  else if (e->base_name == "$$vars" || e->base_name == "$$parms")
    {
      print_format* pf = new print_format;

      // Convert $$vars to sprintf of a list of vars which we recursively evaluate
      // NB: we synthesize a new token here rather than reusing
      // e->tok, because print_format::print likes to use
      // its tok->content.
      token* pf_tok = new token(*e->tok);
      pf_tok->content = "sprintf";

      pf->tok = pf_tok;
      pf->print_to_stream = false;
      pf->print_with_format = true;
      pf->print_with_delim = false;
      pf->print_with_newline = false;
      pf->print_char = false;

      for (unsigned i = 0; i < args.size(); ++i)
        {
          if (!args[i].usable)
            continue;
          if (i > 0)
            pf->raw_components += " ";
          pf->raw_components += args[i].name;
          target_symbol *tsym = new target_symbol;
          tsym->tok = e->tok;
          tsym->base_name = "$" + args[i].name;

          // every variable should always be accessible!
          tsym->saved_conversion_error = 0;
          expression *texp = require (tsym); // NB: throws nothing ...
          assert (!tsym->saved_conversion_error); // ... but this is how we know it happened.

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
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (e->base_name == "$$name" ||
      e->base_name == "$$parms" ||
      e->base_name == "$$vars")
    visit_target_symbol_context (e);
  else
    visit_target_symbol_arg (e);
}



tracepoint_derived_probe::tracepoint_derived_probe (systemtap_session& s,
                                                    dwflpp& dw, Dwarf_Die& func_die,
                                                    const string& tracepoint_name,
                                                    probe* base, probe_point* loc):
  derived_probe (base, new probe_point(*loc) /* .components soon rewritten */),
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
  size_t header_pos = decl_file.rfind("trace/");
  if (header_pos == string::npos)
    throw semantic_error ("cannot parse header location for tracepoint '"
                                  + tracepoint_name + "' in '"
                                  + decl_file + "'");
  header = decl_file.substr(header_pos);

  // tracepoints from FOO_event_types.h should really be included from FOO.h
  // XXX can dwarf tell us the include hierarchy?  it would be better to
  // ... walk up to see which one was directly included by tracequery.c
  // XXX: see also PR9993.
  header_pos = header.find("_event_types");
  if (header_pos != string::npos)
    header.erase(header_pos, 12);

  // Now expand the local variables in the probe body
  tracepoint_var_expanding_visitor v (dw, name, args);
  this->body = v.require (this->body);

  if (sess.verbose > 2)
    clog << "tracepoint-based " << name << " tracepoint='" << tracepoint_name
	 << "'" << endl;
}


static bool
dwarf_type_name(Dwarf_Die& type_die, string& c_type)
{
  // if we've gotten down to a basic type, then we're done
  bool done = true;
  switch (dwarf_tag(&type_die))
    {
    case DW_TAG_structure_type:
      c_type.append("struct ");
      break;
    case DW_TAG_union_type:
      c_type.append("union ");
      break;
    case DW_TAG_typedef:
    case DW_TAG_base_type:
      break;
    default:
      done = false;
      break;
    }
  if (done)
    {
      c_type.append(dwarf_diename_integrate(&type_die));
      return true;
    }

  // otherwise, this die is a type modifier.

  // recurse into the referent type
  // if it can't be named, just call it "void"
  Dwarf_Attribute subtype_attr;
  Dwarf_Die subtype_die;
  if (!dwarf_attr_integrate(&type_die, DW_AT_type, &subtype_attr)
      || !dwarf_formref_die(&subtype_attr, &subtype_die)
      || !dwarf_type_name(subtype_die, c_type))
    c_type = "void";

  const char *suffix = NULL;
  switch (dwarf_tag(&type_die))
    {
    case DW_TAG_pointer_type:
      suffix = "*";
      break;
    case DW_TAG_array_type:
      suffix = "[]";
      break;
    case DW_TAG_const_type:
      suffix = " const";
      break;
    case DW_TAG_volatile_type:
      suffix = " volatile";
      break;
    default:
      return false;
    }
  c_type.append(suffix);

  // XXX HACK!  The va_list isn't usable as found in the debuginfo...
  if (c_type == "struct __va_list_tag*")
    c_type = "va_list";

  return true;
}


static bool
resolve_tracepoint_arg_type(tracepoint_arg& arg)
{
  Dwarf_Attribute type_attr;
  switch (dwarf_tag(&arg.type_die))
    {
    case DW_TAG_typedef:
    case DW_TAG_const_type:
    case DW_TAG_volatile_type:
      // iterate on the referent type
      return (dwarf_attr_integrate(&arg.type_die, DW_AT_type, &type_attr)
              && dwarf_formref_die(&type_attr, &arg.type_die)
              && resolve_tracepoint_arg_type(arg));
    case DW_TAG_base_type:
      // base types will simply be treated as script longs
      arg.isptr = false;
      return true;
    case DW_TAG_pointer_type:
      // pointers can be treated as script longs,
      // and if we know their type, they can also be dereferenced
      if (dwarf_attr_integrate(&arg.type_die, DW_AT_type, &type_attr)
          && dwarf_formref_die(&type_attr, &arg.type_die))
        arg.isptr = true;
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
tracepoint_derived_probe::build_args(dwflpp& dw, Dwarf_Die& func_die)
{
  Dwarf_Die arg;
  if (dwarf_child(&func_die, &arg) == 0)
    do
      if (dwarf_tag(&arg) == DW_TAG_formal_parameter)
        {
          // build a tracepoint_arg for this parameter
          tracepoint_arg tparg;
          tparg.name = dwarf_diename_integrate(&arg);

          // read the type of this parameter
          Dwarf_Attribute type_attr;
          if (!dwarf_attr_integrate (&arg, DW_AT_type, &type_attr)
              || !dwarf_formref_die (&type_attr, &tparg.type_die)
              || !dwarf_type_name(tparg.type_die, tparg.c_type))
            throw semantic_error ("cannot get type of tracepoint '"
                                  + tracepoint_name + "' parameter '"
                                  + tparg.name + "'");

          tparg.usable = resolve_tracepoint_arg_type(tparg);
          args.push_back(tparg);
          if (sess.verbose > 4)
            clog << "found parameter for tracepoint '" << tracepoint_name
                 << "': type:'" << tparg.c_type
                 << "' name:'" << tparg.name << "'" << endl;
        }
    while (dwarf_siblingof(&arg, &arg) == 0);
}

void
tracepoint_derived_probe::printargs(std::ostream &o) const
{
  for (unsigned i = 0; i < args.size(); ++i)
    if (args[i].usable)
      o << " $" << args[i].name << ":" << args[i].c_type;
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


void
tracepoint_derived_probe::emit_probe_context_vars (translator_output* o)
{
  for (unsigned i = 0; i < args.size(); i++)
    if (args[i].used)
      o->newline() << "int64_t __tracepoint_arg_" << args[i].name << ";";
}


static vector<string> tracepoint_extra_headers ()
{
  vector<string> they_live;
  // PR 9993
  // XXX: may need this to be configurable
  they_live.push_back ("linux/skbuff.h");
  return they_live;
}


void
tracepoint_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty())
    return;

  s.op->newline() << "/* ---- tracepoint probes ---- */";
  s.op->newline();

  // PR9993: Add extra headers to work around undeclared types in individual
  // include/trace/foo.h files
  const vector<string>& extra_headers = tracepoint_extra_headers ();
  for (unsigned z=0; z<extra_headers.size(); z++)
    s.op->newline() << "#include <" << extra_headers[z] << ">\n";

  for (unsigned i = 0; i < probes.size(); ++i)
    {
      tracepoint_derived_probe *p = probes[i];

      // emit a separate entry function for each probe, since tracepoints
      // don't provide any sort of context pointer.
      s.op->newline() << "#include <" << p->header << ">";
      s.op->newline() << "static void enter_tracepoint_probe_" << i << "(";
      if (p->args.size() == 0)
        s.op->line() << "void";
      for (unsigned j = 0; j < p->args.size(); ++j)
        {
          if (j > 0)
            s.op->line() << ", ";
          s.op->line() << p->args[j].c_type << " __tracepoint_arg_" << p->args[j].name;
        }
      s.op->line() << ") {";
      s.op->indent(1);
      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", 
                                     lex_cast_qstring (*p->sole_location()));
      s.op->newline() << "c->marker_name = "
                      << lex_cast_qstring (p->tracepoint_name)
                      << ";";
      for (unsigned j = 0; j < p->args.size(); ++j)
        if (p->args[j].used)
          {
            s.op->newline() << "c->locals[0]." << p->name << ".__tracepoint_arg_"
                            << p->args[j].name << " = (int64_t)";
            s.op->line() << p->args[j].typecast;
            s.op->line() << "__tracepoint_arg_" << p->args[j].name << ";";
          }
      s.op->newline() << p->name << " (c);";
      common_probe_entryfn_epilogue (s.op);
      s.op->newline(-1) << "}";

      // emit normalized registration functions
      s.op->newline() << "static int register_tracepoint_probe_" << i << "(void) {";
      s.op->newline(1) << "return register_trace_" << p->tracepoint_name
                       << "(enter_tracepoint_probe_" << i << ");";
      s.op->newline(-1) << "}";

      // NB: we're not prepared to deal with unreg failures.  However, failures
      // can only occur if the tracepoint doesn't exist (yet?), or if we
      // weren't even registered.  The former should be OKed by the initial
      // registration call, and the latter is safe to ignore.
      s.op->newline() << "static void unregister_tracepoint_probe_" << i << "(void) {";
      s.op->newline(1) << "(void) unregister_trace_" << p->tracepoint_name
                       << "(enter_tracepoint_probe_" << i << ");";
      s.op->newline(-1) << "}";
      s.op->newline();
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

  static int tracepoint_query_cu (Dwarf_Die * cudie, void * arg);
  static int tracepoint_query_func (Dwarf_Die * func, base_query * query);
};


void
tracepoint_query::handle_query_module()
{
  // look for the tracepoints in each CU
  dw.iterate_over_cus(tracepoint_query_cu, this);
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

  assert(dw.function_name.compare(0, 10, "stapprobe_") == 0);
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
  string get_tracequery_module(systemtap_session& s, const string& header);

public:

  tracepoint_builder(): dw(0) {}
  ~tracepoint_builder() { delete dw; }

  void build_no_more (systemtap_session& s)
  {
    if (dw && s.verbose > 3)
      clog << "tracepoint_builder releasing dwflpp" << endl;
    delete dw;
    dw = NULL;
  }

  void build(systemtap_session& s,
             probe *base, probe_point *location,
             literal_map_t const& parameters,
             vector<derived_probe*>& finished_results);
};


string
tracepoint_builder::get_tracequery_module(systemtap_session& s,
                                          const string& header)
{
  string tracequery_path;
  if (s.use_cache)
    {
      // see if the cached module exists
      tracequery_path = find_tracequery_hash(s, header);
      if (!tracequery_path.empty())
        {
          int fd = open(tracequery_path.c_str(), O_RDONLY);
          if (fd != -1)
            {
              if (s.verbose > 2)
                clog << "Pass 2: using cached " << tracequery_path << endl;
              close(fd);
              return tracequery_path;
            }
        }
    }

  // no cached module, time to make it

  size_t root_pos = header.rfind("/include/");
  string short_header = (root_pos != string::npos) ?
    header.substr(root_pos + 9) : header;

  string tracequery_ko;
  int rc = make_tracequery(s, tracequery_ko, short_header,
                           tracepoint_extra_headers());
  if (rc != 0)
    return "";

  if (s.use_cache)
    {
      // try to save tracequery in the cache
      if (s.verbose > 2)
        clog << "Copying " << tracequery_ko
             << " to " << tracequery_path << endl;
      if (copy_file(tracequery_ko.c_str(),
                    tracequery_path.c_str()) != 0)
        cerr << "Copy failed (\"" << tracequery_ko << "\" to \""
             << tracequery_path << "\"): " << strerror(errno) << endl;
    }
  return tracequery_ko;
}


bool
tracepoint_builder::init_dw(systemtap_session& s)
{
  if (dw != NULL)
    return true;

  vector<string> tracequery_modules;

  glob_t trace_glob;
  string globs[] = {
      "/include/trace/*.h",
      "/include/trace/events/*.h",
      "/source/include/trace/*.h",
      "/source/include/trace/events/*.h",
  };
  for (unsigned z = 0; z < sizeof(globs) / sizeof(globs[0]); z++)
    {
      string glob_str(s.kernel_build_tree + globs[z]);
      glob(glob_str.c_str(), 0, NULL, &trace_glob);
      for (unsigned i = 0; i < trace_glob.gl_pathc; ++i)
        {
          string header(trace_glob.gl_pathv[i]);

          // filter out a few known "internal-only" headers
          if (header.find("/ftrace.h") != string::npos)
            continue;
          if (header.find("/trace_events.h") != string::npos)
            continue;
          if (header.find("_event_types.h") != string::npos)
            continue;

          string tracequery_path = get_tracequery_module(s, header);
          if (!tracequery_path.empty())
            tracequery_modules.push_back(tracequery_path);
        }
      globfree(&trace_glob);
    }

  // TODO: consider other sources of tracepoint headers too, like from
  // a command-line parameter or some environment or .systemtaprc

  dw = new dwflpp(s, tracequery_modules);
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
  register_tapset_perfmon(s);
  register_tapset_procfs(s);
  register_tapset_timers(s);
  register_tapset_utrace(s);


  // dwarf-based kprobe/uprobe parts
  dwarf_derived_probe::register_patterns(s);

  // XXX: user-space starter set
  s.pattern_root->bind_num(TOK_PROCESS)
    ->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)
    ->bind(new uprobe_builder ());
  s.pattern_root->bind_num(TOK_PROCESS)
    ->bind_num(TOK_STATEMENT)->bind(TOK_ABSOLUTE)->bind(TOK_RETURN)
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

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
