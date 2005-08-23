// tapset resolution
// Copyright (C) 2005 Red Hat Inc.
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

#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstdarg>

extern "C" {
#include <fcntl.h>
#include <elfutils/libdwfl.h>
#include <elfutils/libdw.h>
#include <dwarf.h>
#include <elf.h>
#include <obstack.h>
#include "loc2c.h"

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}

#include <fnmatch.h>

using namespace std;

// ------------------------------------------------------------------------
// begin/end probes are run right during registration / deregistration
// ------------------------------------------------------------------------

struct be_derived_probe: public derived_probe
{
  bool begin;
  be_derived_probe (probe* p, bool b): derived_probe (p), begin (b) {}
  be_derived_probe (probe* p, probe_point* l, bool b):
    derived_probe (p, l), begin (b) {}

  void emit_registrations (translator_output* o, unsigned i);
  void emit_deregistrations (translator_output* o, unsigned i);
  void emit_probe_entries (translator_output* o, unsigned i);
};


struct be_builder: public derived_probe_builder
{
  bool begin;
  be_builder(bool b) : begin(b) {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<probe *> & results_to_expand_further,
		     vector<derived_probe *> & finished_results)
  {
    finished_results.push_back(new be_derived_probe(base, location, begin));
  }
};


void
be_derived_probe::emit_registrations (translator_output* o, unsigned j)
{
  if (begin)
    for (unsigned i=0; i<locations.size(); i++)
      o->newline() << "enter_" << j << "_" << i << " ();";
}


void
be_derived_probe::emit_deregistrations (translator_output* o, unsigned j)
{
  if (!begin)
    for (unsigned i=0; i<locations.size(); i++)
      o->newline() << "enter_" << j << "_" << i << " ();";
}


void
be_derived_probe::emit_probe_entries (translator_output* o, unsigned j)
{
  for (unsigned i=0; i<locations.size(); i++)
    {
      probe_point *l = locations[i];
      o->newline() << "/* location " << i << ": " << *l << " */";
      o->newline() << "static void enter_" << j << "_" << i << " (void);";
      o->newline() << "void enter_" << j << "_" << i << " () {";

      // While begin/end probes are executed single-threaded, we
      // still code defensively and use a per-cpu context.
      o->newline(1) << "struct context* c = & contexts [smp_processor_id()];";

      // A precondition for running a probe handler is that we're in STARTING
      // or STOPPING state (not ERROR), and that no one else is already using
      // this context.
      o->newline() << "if (atomic_read (&session_state) != ";
      if (begin) o->line() << "STAP_SESSION_STARTING)";
      else o->line() << "STAP_SESSION_STOPPING)";
      o->newline(1) << "return;";
      o->newline(-1) << "if (c->busy) {";
      o->newline(1) << "printk (KERN_ERR \"probe reentrancy\");";
      o->newline() << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      o->newline() << "return;";
      o->newline(-1) << "}";
      o->newline();

      o->newline() << "c->busy ++;";
      o->newline() << "mb ();"; // for smp
      o->newline() << "c->last_error = 0;";
      o->newline() << "c->nesting = 0;";
      o->newline() << "c->regs = 0;";
      o->newline() << "c->actioncount = 0;";

      // NB: locals are initialized by probe function itself
      o->newline() << "probe_" << j << " (c);";

      o->newline() << "if (c->last_error) {";
      o->newline(1) << "if (c->last_error[0]) _stp_error (\"%s near %s\", c->last_error, c->last_stmt);";
      o->newline() << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      o->newline(-1) << "}";

      o->newline() << "c->busy --;";
      o->newline() << "mb ();";
      o->newline(-1) << "}" << endl;
    }
}


// ------------------------------------------------------------------------
//  Dwarf derived probes.
// ------------------------------------------------------------------------

static string TOK_PROCESS("process");
static string TOK_KERNEL("kernel");
static string TOK_MODULE("module");

static string TOK_FUNCTION("function");
static string TOK_RETURN("return");
static string TOK_CALLEES("callees");

static string TOK_STATEMENT("statement");
static string TOK_LABEL("label");
static string TOK_RELATIVE("relative");


// XXX: should standardize to these functions throughout translator

template <typename OUT, typename IN> inline OUT
lex_cast(IN const & in)
{
  stringstream ss;
  OUT out;
  if (!(ss << in && ss >> out))
    throw runtime_error("bad lexical cast");
  return out;
}

template <typename OUT, typename IN> inline OUT
lex_cast_hex(IN const & in)
{
  stringstream ss;
  OUT out;
  if (!(ss << hex << showbase << in && ss >> out))
    throw runtime_error("bad lexical cast");
  return out;
}

struct 
func_info
{
  string name;
  Dwarf_Die die;
  Dwarf_Addr prologue_end;
};

struct
inline_instance_info
{
  string name;
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

struct
dwflpp
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
  Dwarf_Func * function;

  string module_name;
  string cu_name;
  string function_name;


  string const default_name(char const * in,
			    char const * type)
  {
    if (in)
      return in;
    if (false && sess.verbose)
      clog << "WARNING: no name found for " << type << endl;
    return string("");
  }


  void get_module_dwarf()
  {
    if (!module_dwarf)
      module_dwarf = dwfl_module_getdwarf(module, &module_bias);

    if (module_dwarf == NULL && sess.verbose)
      clog << "WARNING: dwfl_module_getdwarf() : "
	   << dwfl_errmsg (dwfl_errno ()) << endl;
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


  void focus_on_function(Dwarf_Func * f)
  {
    assert(f);
    assert(module);
    assert(cu);

    function = f;
    function_name = default_name(dwarf_func_name(function),
				 "function");
  }


  void focus_on_module_containing_global_address(Dwarf_Addr a)
  {
    assert(dwfl);
    cu = NULL;
    if (false && sess.verbose)
      clog << "focusing on module containing global addr " << a << endl;
    focus_on_module(dwfl_addrmodule(dwfl, a));
  }


  void query_cu_containing_global_address(Dwarf_Addr a, void *arg)
  {
    Dwarf_Addr bias;
    assert(dwfl);
    get_module_dwarf();
    if (false && sess.verbose)
      clog << "focusing on cu containing global addr " << a << endl;
    query_cu (dwfl_module_addrdie(module, a, &bias), arg);
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

    if (false && sess.verbose)
      clog << "module addr " << hex << a
	   << " + module start " << module_start
	   << " -> global addr " << (a + module_start) << dec << endl;
    return a + module_start;
  }


  Dwarf_Addr global_address_to_module(Dwarf_Addr a)
  {
    assert(module);
    get_module_dwarf();
    if (false && sess.verbose)
      clog << "global addr " << a
	   << " - module start " << hex << module_start
	   << " -> module addr " << (a - module_start) << dec << endl;
    return a - module_bias;
  }


  bool module_name_matches(string pattern)
  {
    assert(module);
    bool t = (fnmatch(pattern.c_str(), module_name.c_str(), 0) == 0);
    if (t && sess.verbose)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "module '" << module_name << "'" << endl;
    return t;
  }


  bool function_name_matches(string pattern)
  {
    assert(function);
    bool t = (fnmatch(pattern.c_str(), function_name.c_str(), 0) == 0);
    if (t && sess.verbose)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "function '" << function_name << "'" << endl;
    return t;
  }


  bool cu_name_matches(string pattern)
  {
    assert(cu);
    bool t = (fnmatch(pattern.c_str(), cu_name.c_str(), 0) == 0);
    if (t && sess.verbose)
      clog << "pattern '" << pattern << "' "
	   << "matches "
	   << "CU '" << cu_name << "'" << endl;
    return t;
  }


  void dwfl_assert(string desc, int rc) // NB: "rc == 0" means OK in this case
  {
    string msg = "libdwfl failure (" + desc + "): ";
    if (rc < 0) msg += dwfl_errmsg (rc);
    else if (rc > 0) msg += strerror (rc);
    if (rc != 0)
      throw semantic_error (msg);
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
    static char* debuginfo_path = "-:.debug:/usr/lib/debug";

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
	dwfl_linux_kernel_module_section_address,
        & debuginfo_path
      };

    if (kernel)
      {
	dwfl = dwfl_begin (&kernel_callbacks);
	if (!dwfl)
	  throw semantic_error ("cannot open dwfl");
	dwfl_report_begin (dwfl);
        // XXX: if we have only kernel.* probe points, we shouldn't waste time
        // looking for module debug-info (and vice versa).
	dwfl_assert ("dwfl_linux_kernel_report_kernel", 
		     dwfl_linux_kernel_report_kernel (dwfl));
	dwfl_assert ("dwfl_linux_kernel_report_modules", 
		     dwfl_linux_kernel_report_modules (dwfl));
      }
    else
      {
	dwfl = dwfl_begin (&proc_callbacks);
	dwfl_report_begin (dwfl);
	if (!dwfl)
	  throw semantic_error ("cannot open dwfl");
	// XXX: Find pids or processes, do userspace stuff.
      }

    dwfl_assert ("dwfl_report_end", dwfl_report_end(dwfl, NULL, NULL));
  }

  void iterate_over_modules(int (* callback)(Dwfl_Module *, void **,
					     const char *, Dwarf_Addr,
					     void *),
			    void * data)
  {
    ptrdiff_t off = 0;
    do
      {
	off = dwfl_getmodules (dwfl, callback, data, off);
      }
    while (off > 0);
    dwfl_assert("dwfl_getmodules", off);
  }


  void iterate_over_cus (int (*callback)(Dwarf_Die * die, void * arg),
			 void * data)
  {
    get_module_dwarf();

    if (!module_dwarf)
      {
	cerr << "WARNING: no dwarf info found for module " << module_name << endl;
	return;
      }

    Dwarf *dw = module_dwarf;
    Dwarf_Off off = 0;
    size_t cuhl;
    Dwarf_Off noff;
    while (dwarf_nextcu (dw, off, &noff, &cuhl, NULL, NULL, NULL) == 0)
      {
	Dwarf_Die die_mem;
	Dwarf_Die *die;
	die = dwarf_offdie (dw, off + cuhl, &die_mem);
	if (callback (die, data) != DWARF_CB_OK)
	  break;
	off = noff;
      }
  }


  bool func_is_inline()
  {
    assert (function);
    return dwarf_func_inline (function) != 0;
  }

  void iterate_over_inline_instances (int (* callback)(Dwarf_Die * die, void * arg),
				      void * data)
  {
    assert (function);
    assert (func_is_inline ());
    dwarf_assert ("dwarf_func_inline_instances", 
		  dwarf_func_inline_instances (function, callback, data));
  }


  void iterate_over_functions (int (* callback)(Dwarf_Func * func, void * arg),
			       void * data)
  {
    assert (module);
    assert (cu);
    dwarf_getfuncs (cu, callback, data, 0);
  }


  void iterate_over_srcfile_lines (char const * srcfile,
				   int lineno, 
				   void (* callback) (Dwarf_Line * line, void * arg),				   
				   void *data)
  {
    Dwarf_Line **srcsp;
    size_t nsrcs;

    get_module_dwarf();

    dwarf_assert ("dwarf_getsrc_file",
		  dwarf_getsrc_file (module_dwarf, 
				     srcfile, lineno, 0,
				     &srcsp, &nsrcs));
    
    for (size_t i = 0; i < nsrcs; ++i)
      {
	callback (srcsp[i], data);
      }
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
	    if (sess.verbose)
	      clog << "selected source file '" << fname << "'" << endl;
	  }
      }
    }
  }  

  void resolve_prologue_endings (map<Dwarf_Addr, func_info> & funcs)
  {
    assert(module);
    assert(cu);

    size_t nlines;
    Dwarf_Lines *lines;
    Dwarf_Addr previous_addr;
    bool choose_next_line = false;

    dwarf_assert ("dwarf_getsrclines", 
		  dwarf_getsrclines(cu, &lines, &nlines));    

    for (size_t i = 0; i < nlines; ++i)
      {
	Dwarf_Addr addr;
	Dwarf_Line * line_rec = dwarf_onesrcline(lines, i);
	dwarf_lineaddr (line_rec, &addr);
	
	if (choose_next_line)
	  {
	    map<Dwarf_Addr, func_info>::iterator i = funcs.find (previous_addr);
	    assert (i != funcs.end());
	    i->second.prologue_end = addr;
	    choose_next_line = false;
	  }
	
	else 
	  {
	    map<Dwarf_Addr, func_info>::const_iterator i = funcs.find (addr);
	    if (i != funcs.end())
	      choose_next_line = true;
	  }
	previous_addr = addr;
      }
  }


  bool function_entrypc (Dwarf_Addr * addr)
  {
    assert (function);
    return (dwarf_func_entrypc (function, addr) == 0);
  }


  bool die_entrypc (Dwarf_Die * die, Dwarf_Addr * addr)
  {
    Dwarf_Attribute attr_mem;
    Dwarf_Attribute *attr = dwarf_attr (die, DW_AT_entry_pc, &attr_mem);
    if (attr != NULL)
      return (dwarf_formaddr (attr, addr) == 0);

    return ( dwarf_lowpc (die, addr) == 0);
  }


  char const * function_srcfile ()
  {
    assert (function);
    return dwarf_func_file (function);
  }

  void function_die (Dwarf_Die *d)
  {
    assert (function);
    dwarf_func_die (function, d);
  }

  bool die_has_pc (Dwarf_Die * die, Dwarf_Addr pc)
  {
    int res = dwarf_haspc (die, pc);
    if (res == -1)
      dwarf_assert ("dwarf_haspc", res);
    return res == 1;
  }


  static void loc2c_error (void *arg, const char *fmt, ...)
  {
    char *msg = NULL;
    va_list ap;
    va_start (ap, fmt);
    vasprintf (&msg, fmt, ap);
    va_end (ap);
    throw semantic_error (msg);
  }


  static void loc2c_emit_address (void *arg, struct obstack *pool,
				  Dwarf_Addr address)
  {
    dwflpp *dwfl = (dwflpp *) arg;
    obstack_printf (pool, "%#" PRIx64 "UL /* hard-coded %s address */",
		    address, dwfl_module_info (dwfl->module, NULL, NULL, NULL,
					       NULL, NULL, NULL, NULL));
  }

  string literal_stmt_for_local(Dwarf_Addr pc,
				string const & local,
				vector<pair<target_symbol::component_type,
				std::string> > const & components)
  {
    assert (cu);

    Dwarf_Die *scopes;
    Dwarf_Die vardie;

    int nscopes = dwarf_getscopes (cu, pc, &scopes);
    if (nscopes == 0)
      {
	throw semantic_error ("unable to find any scopes containing "
			      + lex_cast_hex<string>(pc)
			      + " while searching for local '" + local + "'");
      }

    int declaring_scope = dwarf_getscopevar (scopes, nscopes,
					     local.c_str(),
					     0, NULL, 0, 0,
					     &vardie);
    if (declaring_scope < 0)
      {
	throw semantic_error ("unable to find local '" + local + "'"
			      + " near pc " + lex_cast_hex<string>(pc));
      }

    Dwarf_Attribute fb_attr_mem, *fb_attr = NULL;
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
					      &fb_attr_mem);
	    break;
	  }
      }

    if (sess.verbose)
      clog << "finding location for local '" << local
	   << "' near address " << hex << pc
	   << ", module bias " << module_bias << dec
	   << endl;

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
    struct location *head = c_translate_location (&pool, &loc2c_error, this,
						  &loc2c_emit_address,
						  1, module_bias,
						  &attr_mem, pc,
						  &tail, fb_attr);

    if (dwarf_attr_integrate (&vardie, DW_AT_type, &attr_mem) == NULL)
      throw semantic_error("failed to retrieve type "
			   "attribute for local '" + local + "'");

    Dwarf_Die die_mem, *die = &vardie;
    unsigned i = 0;
    while (i < components.size())
      {
	die = dwarf_formref_die (&attr_mem, &die_mem);
	const int typetag = dwarf_tag (die);
	switch (typetag)
	  {
	  case DW_TAG_typedef:
	    /* Just iterate on the referent type.  */
	    break;

	  case DW_TAG_pointer_type:
	    if (components[i].first == target_symbol::comp_literal_array_index)
	      goto subscript;

	    c_translate_pointer (&pool, 1, module_bias, die, &tail);
	    break;

	  case DW_TAG_array_type:
	    if (components[i].first == target_symbol::comp_literal_array_index)
	      {
	      subscript:
		c_translate_array (&pool, 1, module_bias, die, &tail,
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
	    switch (dwarf_child (die, &die_mem))
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
	      if (dwarf_siblingof (die, &die_mem) != 0)
		throw semantic_error ("field name " + components[i].second + " not found");

	    if (dwarf_attr_integrate (die, DW_AT_data_member_location,
				      &attr_mem) == NULL)
	      {
		/* Union members don't usually have a location,
		   but just use the containing union's location.  */
		if (typetag != DW_TAG_union_type)
		  throw semantic_error ("no location for field "
					+ components[i].second
					+ " :" + string(dwarf_errmsg (-1)));
	      }
	    else
	      c_translate_location (&pool, NULL, NULL, NULL, 1,
				    module_bias, &attr_mem, pc,
				    &tail, NULL);
	    ++i;
	    break;

	  case DW_TAG_base_type:
	    throw semantic_error ("field "
				  + components[i].second
				  + " vs base type "
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
	if (dwarf_attr_integrate (die, DW_AT_type, &attr_mem) == NULL)
	  throw semantic_error ("cannot get type of field: " + string(dwarf_errmsg (-1)));
      }

    /* Fetch the type DIE corresponding to the final location to be accessed.
       It must be a base type or a typedef for one.  */

    Dwarf_Die typedie_mem;
    Dwarf_Die *typedie;
    int typetag;
    while (1)
      {
	typedie = dwarf_formref_die (&attr_mem, &typedie_mem);
	if (typedie == NULL)
	  throw semantic_error ("cannot get type of field: " + string(dwarf_errmsg (-1)));
	typetag = dwarf_tag (typedie);
	if (typetag != DW_TAG_typedef)
	  break;
	if (dwarf_attr_integrate (typedie, DW_AT_type, &attr_mem) == NULL)
	  throw semantic_error ("cannot get type of field: " + string(dwarf_errmsg (-1)));
      }

    if (typetag != DW_TAG_base_type)
      throw semantic_error ("target location not a base type");

    c_translate_fetch (&pool, 1, module_bias, die, typedie, &tail,
		       "THIS->__retvalue");

    size_t bufsz = 1024;
    char *buf = static_cast<char*>(malloc(bufsz));
    assert(buf);

    FILE *memstream = open_memstream (&buf, &bufsz);
    assert(memstream);

    bool deref = c_emit_location (memstream, head, 1);
    fprintf(memstream, "  goto out;\n");

    // dummy use of deref_fault label, to disable warning if deref() not used
    fprintf(memstream, "if (0) goto deref_fault;\n");

    // XXX: deref flag not reliable; emit fault label unconditionally
    if (deref) ;
    fprintf(memstream,
            "deref_fault:\n"
            "  c->last_error = \"pointer dereference fault\";\n"
            "  goto out;\n");

    fclose (memstream);
    string result(buf);
    free (buf);
    return result;
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

struct dwarf_derived_probe : public derived_probe
{
  dwarf_derived_probe (dwarf_query & q,
		       Dwarf_Addr addr);

  string module_name;
  string function_name;
  bool has_statement;
  Dwarf_Addr addr;
  Dwarf_Addr module_bias;
  bool has_return;

  // Pattern registration helpers.
  static void register_relative_variants(match_node * root,
					 dwarf_builder * dw);
  static void register_statement_variants(match_node * root,
					  dwarf_builder * dw);
  static void register_function_variants(match_node * root,
					  dwarf_builder * dw);
  static void register_function_and_statement_variants(match_node * root,
						       dwarf_builder * dw);
  static void register_patterns(match_node * root);

  virtual void emit_registrations (translator_output * o, unsigned i);
  virtual void emit_deregistrations (translator_output * o, unsigned i);
  virtual void emit_probe_entries (translator_output * o, unsigned i);
};

// Helper struct to thread through the dwfl callbacks.
struct
dwarf_query
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

  string pt_regs_member_for_regnum(uint8_t dwarf_regnum);

  vector<derived_probe *> & results;

  bool has_kernel;
  bool has_process;
  bool has_module;
  string process_val;
  string module_val;
  string function_val;

  bool has_function_str;
  bool has_statement_str;
  bool has_function_num;
  bool has_statement_num;
  string statement_str_val;
  string function_str_val;
  Dwarf_Addr statement_num_val;
  Dwarf_Addr function_num_val;

  bool has_callees;
  long callee_val;

  bool has_return;

  bool has_label;
  string label_val;

  bool has_relative;
  long relative_val;

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
  dwarf_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<probe *> & results_to_expand_further,
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
  has_module = get_string_param(params, TOK_MODULE, module_val);
  has_process = get_string_param(params, TOK_PROCESS, process_val);

  has_function_str = get_string_param(params, TOK_FUNCTION, function_str_val);
  has_function_num = get_number_param(params, TOK_FUNCTION, function_num_val);

  has_statement_str = get_string_param(params, TOK_STATEMENT, statement_str_val);
  has_statement_num = get_number_param(params, TOK_STATEMENT, statement_num_val);

  callee_val = 1;
  has_callees = (has_null_param(params, TOK_CALLEES) ||
		 get_number_param(params, TOK_CALLEES, callee_val));

  has_return = has_null_param(params, TOK_RETURN);

  has_label = get_string_param(params, TOK_LABEL, label_val);
  has_relative = get_number_param(params, TOK_RELATIVE, relative_val);

  if (has_function_str)
    spec_type = parse_function_spec(function_str_val);
  else if (has_statement_str)
    spec_type = parse_function_spec(statement_str_val);
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
      if (sess.verbose)
	clog << "parsed '" << spec
	     << "' -> func '" << function
	     << "'" << endl;
      return function_alone;
    }

  if (i++ == e)
    goto bad;

  while (i != e && *i != ':')
    file += *i++;

  if (i == e)
    {
      if (sess.verbose)
	clog << "parsed '" << spec
	     << "' -> func '"<< function
	     << "', file '" << file
	     << "'" << endl;
      return function_and_file;
    }

  if (i++ == e)
    goto bad;

  try
    {
      line = lex_cast<int>(string(i, e));
      if (sess.verbose)
	clog << "parsed '" << spec
	     << "' -> func '"<< function
	     << "', file '" << file
	     << "', line " << line << endl;
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


static void
query_statement (Dwarf_Addr stmt_addr, dwarf_query * q)
{
  try
    {
      // XXX: implement
      if (q->has_relative)
        throw semantic_error("incomplete: do not know how to interpret .relative",
                             q->base_probe->tok);

      q->results.push_back(new dwarf_derived_probe(*q, stmt_addr));
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
    }
}

static void
query_inline_instance_info (Dwarf_Addr entrypc,
			    inline_instance_info const & ii,
			    dwarf_query * q)
{
  if (q->has_return)
    {
      throw semantic_error ("cannot probe .return of inline function '" + ii.name + "'");
    }
  else
    {
      if (q->sess.verbose)
	clog << "querying entrypc " 
	     << hex << entrypc << dec 
	     << " of instance of inline '" << ii.name << "'" << endl;
      query_statement (entrypc, q);
    }
}

static void
query_func_info (Dwarf_Addr entrypc,
		 func_info const & fi,
		 dwarf_query * q)
{
  if (q->has_return)
    {
      // NB. dwarf_derived_probe::emit_registrations will emit a
      // kretprobe based on the entrypc in this case.
      if (q->sess.verbose)
	clog << "querying entrypc of function '" << fi.name << "' for return probe" << endl;
      query_statement (entrypc, q);
    }
  else
    {
      if (q->sess.verbose)
	clog << "querying prologue-end of function '" << fi.name << "'" << endl;
      query_statement (fi.prologue_end, q);
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
	  if (q->sess.verbose)
	    clog << "function DIE lands on srcfile" << endl;
	  query_func_info (i->first, i->second, q);
	}
    }  

  for (map<Dwarf_Addr, inline_instance_info>::iterator i = q->filtered_inlines.begin();
       i != q->filtered_inlines.end(); ++i)
    {
      if (q->dw.die_has_pc (&(i->second.die), addr))
	{
	  if (q->sess.verbose)
	    clog << "inline instance DIE lands on srcfile" << endl;
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

      bool record_this_inline = false;

      if (q->sess.verbose)
	clog << "examining inline instance of " << q->dw.function_name << endl;

      if (q->has_function_str || q->has_statement_str)
	record_this_inline = true;
      else if (q->has_function_num)
	{
	  Dwarf_Addr query_addr = q->function_num_val;
      
	  if (q->has_module)
	    query_addr = q->dw.module_address_to_global(query_addr);
      
	  if (q->dw.die_has_pc (die, query_addr))
	    record_this_inline = true;      
	}

      if (record_this_inline)
	{
	  if (q->sess.verbose)
	    clog << "selected inline instance of " << q->dw.function_name << endl;

	  Dwarf_Addr entrypc;
	  if (q->dw.die_entrypc (die, &entrypc))
	    {
	      inline_instance_info inl;
	      inl.die = *die;
	      inl.name = q->dw.function_name;
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
query_dwarf_func (Dwarf_Func * func, void * arg)
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);
  assert (!q->has_statement_num);

  try
    {
      // XXX: implement
      if (q->has_callees)
        throw semantic_error ("incomplete: do not know how to interpret .callees",
			      q->base_probe->tok);
      
      if (q->has_label)
        throw semantic_error ("incomplete: do not know how to interpret .label",
			      q->base_probe->tok);

      q->dw.focus_on_function (func);

      if (q->dw.func_is_inline () 
	  && (((q->has_statement_str || q->has_function_str)
	       && q->dw.function_name_matches(q->function))
	      || q->has_function_num))
	{
	  if (q->sess.verbose)
	    clog << "checking instances of inline " << q->dw.function_name << endl;
	  q->dw.iterate_over_inline_instances (query_dwarf_inline_instance, arg);
	}
      else
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
	  
	      if (q->has_module)
		query_addr = q->dw.module_address_to_global(query_addr);

	      Dwarf_Die d;
	      q->dw.function_die (&d);
	  
	      if (q->dw.die_has_pc (&d, query_addr))
		record_this_function = true;
	    }

	  if (record_this_function)
	    {
	      if (q->sess.verbose)
		clog << "selected function " << q->dw.function_name << endl;

	      Dwarf_Addr entrypc;
	      if (q->dw.function_entrypc (&entrypc))
		{
		  func_info func;
		  q->dw.function_die (&func.die);
		  func.name = q->dw.function_name;
		  q->filtered_functions[entrypc] = func;
		}
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

      if (false && q->sess.verbose)
        clog << "focused on CU '" << q->dw.cu_name
             << "', in module '" << q->dw.module_name << "'" << endl;

      if (q->has_statement_str || q->has_function_str || q->has_function_num)
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
	  q->dw.resolve_prologue_endings (q->filtered_functions);

	  if ((q->has_statement_str || q->has_function_str)
	      && (q->spec_type == function_file_and_line))
	    {
	      // If we have a pattern string with target *line*, we
	      // have to look at lines in all the matched srcfiles. 
	      for (set<char const *>::const_iterator i = q->filtered_srcfiles.begin();
		   i != q->filtered_srcfiles.end(); ++i)
		q->dw.iterate_over_srcfile_lines (*i, q->line, query_srcfile_line, q);
	    }
	  else
	    {
	      // Otherwise, simply probe all resolved functions...
	      for (map<Dwarf_Addr, func_info>::const_iterator i = q->filtered_functions.begin();
		   i != q->filtered_functions.end(); ++i)
		query_func_info (i->first, i->second, q);

	      // And all inline instances.
	      for (map<Dwarf_Addr, inline_instance_info>::const_iterator i 
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
	  if (q->has_module)
	    query_addr = q->dw.module_address_to_global(query_addr);

	  query_statement (query_addr, q);
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
query_module (Dwfl_Module *mod __attribute__ ((unused)),
	      void **userdata __attribute__ ((unused)),
	      const char *name, Dwarf_Addr base,
	      void *arg __attribute__ ((unused)))
{
  dwarf_query * q = static_cast<dwarf_query *>(arg);

  try
    {
      q->dw.focus_on_module(mod);

      // If we have enough information in the pattern to skip a module and
      // the module does not match that information, return early.

      if (q->has_kernel && !q->dw.module_name_matches(TOK_KERNEL))
        return DWARF_CB_OK;

      if (q->has_module && !q->dw.module_name_matches(q->module_val))
        return DWARF_CB_OK;

    if (q->sess.verbose)
      clog << "focused on module '" << q->dw.module_name
	   << "' = [" << hex << q->dw.module_start
	   << "-" << q->dw.module_end
	   << ", bias " << q->dw.module_bias << "]" << dec << endl;

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

	  // NB: We should not have kernel.* here; global addresses
	  // should have bypassed query_module in dwarf_builder::build
	  // and gone directly to query_cu.

          assert (!q->has_kernel);
          assert (q->has_module);
	  q->dw.query_cu_containing_module_address(addr, q);
        }
      else
        {
          // Otherwise if we have a function("foo") or statement("foo")
          // specifier, we have to scan over all the CUs looking for
          // the function(s) in question

          assert(q->has_function_str || q->has_statement_str);
          q->dw.iterate_over_cus(&query_cu, q);

	  // If we just processed the module "kernel", and the user asked for
	  // the kernel pattern, there's no need to iterate over any further
	  // modules
	  
	  if (q->has_kernel && q->dw.module_name_matches(TOK_KERNEL))
	    return DWARF_CB_ABORT;
        }

      return DWARF_CB_OK;
    }
  catch (const semantic_error& e)
    {
      q->sess.print_error (e);
      return DWARF_CB_ABORT;
    }
}

struct
var_expanding_copy_visitor
  : public deep_copy_visitor
{
  static unsigned tick;

  dwarf_query & q;
  Dwarf_Addr addr;

  var_expanding_copy_visitor(dwarf_query & q, Dwarf_Addr a)
    : q(q), addr(a)
  {}
  void visit_target_symbol (target_symbol* e);
};


unsigned var_expanding_copy_visitor::tick = 0;


void
var_expanding_copy_visitor::visit_target_symbol (target_symbol *e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (is_active_lvalue(e))
    {
      throw semantic_error("read-only special variable "
			   + e->base_name + " used as lvalue", e->tok);
    }

  string fname = "get_" + e->base_name.substr(1) + "_" + lex_cast<string>(tick++);

  // synthesize a function
  functiondecl *fdecl = new functiondecl;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;
  ec->code = q.dw.literal_stmt_for_local(addr,
					 e->base_name.substr(1),
					 e->components);
  fdecl->name = fname;
  fdecl->body = ec;
  fdecl->type = pe_long;
  q.sess.functions.push_back(fdecl);

  // synthesize a call
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = NULL;
  provide <functioncall*> (this, n);
}


dwarf_derived_probe::dwarf_derived_probe (dwarf_query & q,
					  Dwarf_Addr addr)
  : derived_probe (NULL),
    module_name(q.dw.module_name),
    function_name(q.dw.function_name),
    has_statement(q.has_statement_str || q.has_statement_num),
    addr(addr),
    module_bias(q.dw.module_bias),
    has_return (q.has_return)
{
  // Lock the kernel module in memory.
  if (module_name != TOK_KERNEL)
    {
      // XXX: There is a race window here, between the time that libdw
      // opened up this same file for its relocation duties, and now.
      int fd = q.sess.module_fds[module_name];
      if (fd == 0)
        {
          string sys_module = "/sys/module/" + module_name + "/sections/.text";
          fd = open (sys_module.c_str(), O_RDONLY);
          if (fd < 0)
            throw semantic_error ("error opening module refcount-bumping file.");
          q.sess.module_fds[module_name] = fd;
        }
    }

  // first synthesize an "expanded" location
  vector<probe_point::component*> comps;
  comps.push_back
    (module_name == TOK_KERNEL
     ? new probe_point::component(TOK_KERNEL)
     : new probe_point::component(TOK_MODULE, new literal_string(module_name)));

  string fn_or_stmt;
  if (q.has_function_str || q.has_function_num)
    fn_or_stmt = "function";
  else
    fn_or_stmt = "statement";

  if (q.has_function_str || q.has_statement_str)
      {
        string retro_name;;
        if (! function_name.empty())
          retro_name = function_name + "@" + q.dw.cu_name; // XXX: add line number
        else if (q.has_function_str)
          retro_name = q.function_str_val;
        else // has_statement_str
          retro_name = q.statement_str_val;
        // XXX: actually the statement_str case is not yet adequately
        // handled in the search code

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
    }

  if (has_return)
    comps.push_back
      (new probe_point::component(TOK_RETURN));

  assert(q.base_probe->locations.size() > 0);
  locations.push_back(new probe_point(comps, q.base_probe->locations[0]->tok));

  // Now make a local-variable-expanded copy of the probe body
  var_expanding_copy_visitor v (q, addr);
  require <block*> (&v, &(this->body), q.base_probe->body);
  this->tok = q.base_probe->tok;
}

void
dwarf_derived_probe::register_relative_variants(match_node * root,
						dwarf_builder * dw)
{
  // Here we match 2 forms:
  //
  // .
  // .relative(NN)

  root->bind(dw);
  root->bind_num(TOK_RELATIVE)->bind(dw);
}

void
dwarf_derived_probe::register_statement_variants(match_node * root,
						 dwarf_builder * dw)
{
  // Here we match 3 forms:
  //
  // .
  // .return
  // .label("foo")

  register_relative_variants(root, dw);
  register_relative_variants(root->bind_str(TOK_LABEL), dw);
}

void
dwarf_derived_probe::register_function_variants(match_node * root,
					      dwarf_builder * dw)
{
  // Here we match 4 forms:
  //
  // .
  // .return
  // .callees
  // .callees(N)
  //
  // The last form permits N-level callee resolving without any
  // recursive .callees.callees.callees... pattern-matching on our part.

  root->bind(dw);
  root->bind(TOK_RETURN)->bind(dw);
  root->bind(TOK_CALLEES)->bind(dw);
  root->bind_num(TOK_CALLEES)->bind(dw);
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

  // Here we match 3 forms:
  //
  // .kernel
  // .module("foo")
  // .process("foo")

  register_function_and_statement_variants(root->bind(TOK_KERNEL), dw);
  // XXX: may need to disable these for 2005-08 release
  register_function_and_statement_variants(root->bind_str(TOK_MODULE), dw);
  register_function_and_statement_variants(root->bind_str(TOK_PROCESS), dw);
}

static string
probe_entry_function_name(unsigned probenum)
{
  return "dwarf_kprobe_" + lex_cast<string>(probenum) + "_enter";
}

static string
probe_entry_struct_kprobe_name(unsigned probenum)
{
  return "dwarf_kprobe_" + lex_cast<string>(probenum);
}

void
dwarf_derived_probe::emit_registrations (translator_output* o, unsigned probenum)
{
  if (! (module_name.empty() || module_name == "kernel"))
    {
      // XXX: lock module_name in memory
    }

  if (has_return)
    {
      o->newline() << probe_entry_struct_kprobe_name(probenum)
                   << ".kp.addr = (void *) 0x" << hex << addr << ";" << dec;
      o->newline() << "rc = register_kretprobe (&"
                   << probe_entry_struct_kprobe_name(probenum)
                   << ");";
    }
  else
    {
      o->newline() << probe_entry_struct_kprobe_name(probenum)
                   << ".addr = (void *) 0x" << hex << addr << ";" << dec;
      o->newline() << "rc = register_kprobe (&"
                   << probe_entry_struct_kprobe_name(probenum)
                   << ");";
    }
}

void
dwarf_derived_probe::emit_deregistrations (translator_output* o, unsigned probenum)
{
  if (has_return)
    o->newline() << "unregister_kretprobe (& "
                 << probe_entry_struct_kprobe_name(probenum)
                 << ");";
  else
    o->newline() << "unregister_kprobe (& "
                 << probe_entry_struct_kprobe_name(probenum)
                 << ");";

  if (! (module_name.empty() || module_name == "kernel"))
    {
      // XXX: unlock module_name
    }
}

void
dwarf_derived_probe::emit_probe_entries (translator_output* o, unsigned probenum)
{

  // Construct a single entry function, and a struct kprobe pointing into
  // the entry function. The entry function will call the probe function.
  o->newline();
  o->newline() << "static int ";
  o->newline() << probe_entry_function_name(probenum) << " (";
  if (has_return)
    o->line() << "struct kretprobe_instance *_ignored";
  else
    o->line() << "struct kprobe *_ignored";
  o->line() << ", struct pt_regs *regs) {";
  o->newline(1) << "struct context *c = & contexts [smp_processor_id()];";
  o->newline();

  // A precondition for running a probe handler is that we're in RUNNING
  // state (not ERROR), and that no one else is already using this context.
  o->newline() << "if (atomic_read (&session_state) != STAP_SESSION_RUNNING)";
  o->newline(1) << "return 0;";
  o->newline(-1) << "if (c->busy) {";
  o->newline(1) << "printk (KERN_ERR \"probe reentrancy\");";
  o->newline() << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  o->newline() << "return 0;";
  o->newline(-1) << "}";
  o->newline();

  o->newline() << "c->busy ++;";
  o->newline() << "mb ();"; // for smp
  o->newline() << "c->last_error = 0;";
  o->newline() << "c->nesting = 0;";
  o->newline() << "c->regs = regs;";
  o->newline() << "c->actioncount = 0;";

  // NB: locals are initialized by probe function itself
  o->newline() << "probe_" << probenum << " (c);";

  o->newline() << "if (c->last_error) {";
  o->newline(1) << "if (c->last_error[0]) _stp_error (\"%s near %s\", c->last_error, c->last_stmt);";
  o->newline() << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  o->newline(-1) << "}";

  o->newline() << "c->busy --;";
  o->newline() << "mb ();";

  o->newline() << "return 0;";
  o->newline(-1) << "}" << endl;

  o->newline();
  if (has_return)
    {
      o->newline() << "static struct kretprobe "
                   << probe_entry_struct_kprobe_name(probenum)
                   << "= {";
      o->newline(1) << ".kp.addr = 0," ;
      o->newline() << ".handler = &" << probe_entry_function_name(probenum);
      o->newline(-1) << "};";
    }
  else
    {
      o->newline() << "static struct kprobe "
                   << probe_entry_struct_kprobe_name(probenum)
                   << "= {";
      o->newline(1) << ".addr       = 0," ;
      o->newline() << ".pre_handler = &" << probe_entry_function_name(probenum);
      o->newline(-1) << "};";
    }
  o->newline();
}


void
dwarf_builder::build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<probe *> & results_to_expand_further,
		     vector<derived_probe *> & finished_results)
{

  dwflpp dw(sess);
  dwarf_query q(sess, base, location, dw, parameters, finished_results);

  dw.setup(q.has_kernel || q.has_module);

  if (q.has_kernel && (q.has_function_num || q.has_statement_num))
    {
      // If we have kernel.function(0xbeef), or
      // kernel.statement(0xbeef) the address is global (relative to
      // the kernel) and we can seek directly to the module and cudie
      // in question.
      Dwarf_Addr a = (q.has_function_num
		      ? q.function_num_val
		      : q.statement_num_val);
      dw.focus_on_module_containing_global_address(a);
      dw.query_cu_containing_global_address(a, &q);
    }
  else
    {
      // Otherwise we have module("foo"), kernel.statement("foo"), or
      // kernel.function("foo"); in these cases we need to scan all
      // the modules.
      assert((q.has_kernel && q.has_function_str) ||
	     (q.has_kernel && q.has_statement_str) ||
	     (q.has_module));
      dw.iterate_over_modules(&query_module, &q);
    }
}



// ------------------------------------------------------------------------
// timer derived probes
// ------------------------------------------------------------------------


struct timer_derived_probe: public derived_probe
{
  int64_t interval, randomize;

  timer_derived_probe (probe* p, probe_point* l, int64_t i, int64_t r);

  virtual void emit_registrations (translator_output * o, unsigned i);
  virtual void emit_deregistrations (translator_output * o, unsigned i);
  virtual void emit_probe_entries (translator_output * o, unsigned i);
};


timer_derived_probe::timer_derived_probe (probe* p, probe_point* l, int64_t i, int64_t r):
  derived_probe (p, l), interval (i), randomize (r)
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
timer_derived_probe::emit_registrations (translator_output* o, unsigned j)
{
  o->newline() << "init_timer (& timer_" << j << ");";
  o->newline() << "timer_" << j << ".expires = jiffies + " << interval << ";";
  o->newline() << "timer_" << j << ".function = & enter_" << j << ";";
  o->newline() << "add_timer (& timer_" << j << ");";
}


void
timer_derived_probe::emit_deregistrations (translator_output* o, unsigned j)
{
  o->newline() << "del_timer_sync (& timer_" << j << ");";
}


void
timer_derived_probe::emit_probe_entries (translator_output* o, unsigned j)
{
  o->newline() << "static struct timer_list timer_" << j << ";";

  o->newline() << "void enter_" << j << " (unsigned long val) {";
  o->newline(1) << "struct context* c = & contexts [smp_processor_id()];";
  
  o->newline() << "(void) val;";

  // A precondition for running a probe handler is that we're in
  // RUNNING state (not ERROR), and that no one else is already using
  // this context.
  o->newline() << "if (atomic_read (&session_state) != STAP_SESSION_RUNNING)";
  o->newline(1) << "return;";

  o->newline(-1) << "if (c->busy) {";
  o->newline(1) << "printk (KERN_ERR \"probe reentrancy\");";
  o->newline() << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  o->newline() << "return;";
  o->newline(-1) << "}";
  o->newline();

  o->newline() << "mod_timer (& timer_" << j << ", "
               << "jiffies + " << interval;
  if (randomize)
    o->line() << " + _stp_random_pm(" << randomize << ")";
  o->line() << ");";
  
  o->newline() << "c->busy ++;";
  o->newline() << "mb ();"; // for smp
  o->newline() << "c->last_error = 0;";
  o->newline() << "c->nesting = 0;";
  o->newline() << "if (! in_interrupt())";
  o->newline(1) << "c->regs = 0;";
  o->newline(-1) << "else";
  o->newline(1) << "c->regs = task_pt_regs (current);";
  o->indent(-1);
  o->newline() << "c->actioncount = 0;";
  
  // NB: locals are initialized by probe function itself
  o->newline() << "probe_" << j << " (c);";
  
  o->newline() << "if (c->last_error) {";
  o->newline(1) << "if (c->last_error[0]) _stp_error (\"%s near %s\", c->last_error, c->last_stmt);";
  o->newline() << "atomic_set (& session_state, STAP_SESSION_ERROR);";
  o->newline(-1) << "}";
  
  o->newline() << "c->busy --;";
  o->newline() << "mb ();";
  o->newline(-1) << "}" << endl;
}


struct timer_builder: public derived_probe_builder
{
  timer_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<probe *> &,
		     vector<derived_probe *> & finished_results)
  {
    int64_t jn, rn;
    bool jn_p, rn_p;

    jn_p = get_param (parameters, "jiffies", jn);
    rn_p = get_param (parameters, "randomize", rn);
    
    finished_results.push_back(new timer_derived_probe(base, location,
                                                       jn, rn_p ? rn : 0));
  }
};



// ------------------------------------------------------------------------
//  Standard tapset registry.
// ------------------------------------------------------------------------

void
register_standard_tapsets(systemtap_session & s)
{
  // Rudimentary binders for begin and end targets
  s.pattern_root->bind("begin")->bind(new be_builder(true));
  s.pattern_root->bind("end")->bind(new be_builder(false));
  s.pattern_root->bind("timer")->bind_num("jiffies")->bind(new timer_builder());
  s.pattern_root->bind("timer")->bind_num("jiffies")->bind_num("randomize")->bind(new timer_builder());

  // kernel/module parts
  dwarf_derived_probe::register_patterns(s.pattern_root);
}
