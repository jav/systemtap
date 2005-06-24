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

#ifdef HAVE_ELFUTILS_LIBDWFL_H
extern "C" {
#include <elfutils/libdwfl.h>
}
#endif

#include <fnmatch.h>

using namespace std;

// Members of match_key.

match_key::match_key(string const & n) 
  : name(n), 
    have_parameter(false), 
    parameter_type(tok_junk)
{
}

match_key::match_key(probe_point::component const & c)
  : name(c.functor),
    have_parameter(c.arg != NULL),
    parameter_type(c.arg ? c.arg->tok->type : tok_junk)
{
}

match_key &
match_key::with_number() 
{
  have_parameter = true;
  parameter_type = tok_number;
  return *this;
}

match_key &
match_key::with_string() 
{
  have_parameter = true;
  parameter_type = tok_string;
  return *this;
}

string 
match_key::str() const
{
  if (have_parameter)
    switch (parameter_type)
      {
      case tok_string: return name + "(string)";
      case tok_number: return name + "(number)";
      default: return name + "(...)";
      }
  return name;
}

bool 
match_key::operator<(match_key const & other) const
{
  return ((name < other.name)
	  
	  || (name == name 
	      && have_parameter < other.have_parameter)
	  
	  || (name == name 
	      && have_parameter == other.have_parameter 
	      && parameter_type < other.parameter_type));
}


// Members of match_node.

match_node::match_node()
  : end(NULL)
{}

match_node & 
match_node::bind(match_key const & k) 
{
  map<match_key, match_node *>::const_iterator i = sub.find(k);
  if (i != sub.end())
    return *i->second;
  match_node * n = new match_node();
  sub.insert(make_pair(k, n));
  return *n;
}

void 
match_node::bind(derived_probe_builder * e)
{
  if (end)
    throw semantic_error("already have a pattern ending");
  end = e;
}

match_node & 
match_node::bind(string const & k)
{
  return bind(match_key(k));
}

match_node & 
match_node::bind_str(string const & k)
{
  return bind(match_key(k).with_string());
}

match_node & 
match_node::bind_num(string const & k)
{
  return bind(match_key(k).with_number());
}

derived_probe_builder * 
match_node::find_builder(vector<probe_point::component *> const & components,
			 unsigned pos,
			 vector< pair<string, literal *> > & parameters)
{
  assert(pos <= components.size());
  if (pos == components.size())
    {
      // Probe_point ends here. We match iff we have
      // an "end" entry here. If we don't, it'll be null.
      return end;
    }
  else
    {
      // Probe_point contains a component here. We match iff there's
      // an entry in the sub table, and its value matches the rest
      // of the probe_point.
      match_key k(*components[pos]);
      map<match_key, match_node *>::const_iterator i = sub.find(k);
      if (i == sub.end())
	return NULL;
      else
	{
	  derived_probe_builder * builder = NULL;
	  if (k.have_parameter)
	    {
	      assert(components[pos]->arg);
	      parameters.push_back(make_pair(components[pos]->functor, 
					     components[pos]->arg));
	    }
	  else
	    {
	      // store a "null parameter" for any component we run into, anyways
	      literal_string *empty = NULL;
	      parameters.push_back(make_pair(components[pos]->functor, empty));
	    }
	  builder = i->second->find_builder(components, pos+1, parameters);
	  if (k.have_parameter && !builder)
	    parameters.pop_back();
	  return builder;
	}
    }
}


static void
param_vec_to_map(vector< pair<string, literal *> > const & param_vec, 
		   map<string, literal *> & param_map)
{
  for (vector< pair<string, literal *> >::const_iterator i = param_vec.begin();
       i != param_vec.end(); ++i)
    {
      param_map[i->first] = i->second;
    }
}

// XXX: bind patterns for probe aliases found in AST

struct 
alias_derived_probe
{
  alias_derived_probe(probe_point * expansion) 
    : alias_expansion(expansion) {}
  probe_point * alias_expansion;
  void emit_registrations (translator_output* o, unsigned i) {}
  void emit_deregistrations (translator_output* o, unsigned i) {}
  void emit_probe_entries (translator_output* o, unsigned i) {}
};

// The root of the global pattern-matching tree.
static match_node * root_node;


// The match-and-expand loop.
void
symresolution_info::derive_probes (probe *p, vector<derived_probe*>& dps)
{
  if (!root_node)
    {
      root_node = new match_node();
      register_standard_tapsets(*root_node);
    }

  assert(root_node);

  deque<probe_point *> work(p->locations.begin(), p->locations.end());

  while(!work.empty())
    {
      probe_point *loc = work.front();
      work.pop_front();

      vector< pair<string, literal *> > param_vec;
      map<string, literal *> param_map;

      derived_probe_builder * builder = 
	root_node->find_builder(loc->components, 0, param_vec);

      if (!builder)
	throw semantic_error ("no match for probe point", loc->tok);

      param_vec_to_map(param_vec, param_map);

      derived_probe *derived = builder->build(p, loc, param_map);
      assert(derived);

      // Append to worklist if it's an alias; append to result otherwise.
      alias_derived_probe *as_alias = dynamic_cast<alias_derived_probe *>(derived);
      if (as_alias)
	{
	  work.push_back(as_alias->alias_expansion);
	  delete derived;
	}
      else
	dps.push_back (derived);      
    }
}


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

struct
be_builder 
  : public derived_probe_builder
{
  bool begin;
  be_builder(bool b) : begin(b) {}
  virtual derived_probe * build(probe * base, 
				probe_point * location,
				map<string, literal *> const & parameters)
  {
    return new be_derived_probe(base, location, begin);
  }
  virtual ~be_builder() {}
};


void 
be_derived_probe::emit_registrations (translator_output* o, unsigned j)
{
  if (begin)
    for (unsigned i=0; i<locations.size(); i++)
      {
        o->newline() << "enter_" << j << "_" << i << " ()";
        o->newline() << "rc = errorcount;";
      }
  else
    o->newline() << "rc = 0;";
}


void 
be_derived_probe::emit_deregistrations (translator_output* o, unsigned j)
{
  if (begin)
    o->newline() << "rc = 0;";
  else
    for (unsigned i=0; i<locations.size(); i++)
      {
        o->newline() << "enter_" << j << "_" << i << " ()";
        o->newline() << "rc = errorcount;";
      }
}


void
be_derived_probe::emit_probe_entries (translator_output* o, unsigned j)
{
  for (unsigned i=0; i<locations.size(); i++)
    {
      probe_point *l = locations[i];
      o->newline() << "/* location " << i << ": " << *l << " */";
      o->newline() << "static void enter_" << j << "_" << i << " ()";
      o->newline() << "{";
      o->newline(1) << "struct context* c = & contexts [0];";
      // XXX: assert #0 is free; need locked search instead
      o->newline() << "if (c->busy) { errorcount ++; return; }";
      o->newline() << "c->busy ++;";
      o->newline() << "c->actioncount = 0;";
      o->newline() << "c->nesting = 0;";
      // NB: locals are initialized by probe function itself
      o->newline() << "probe_" << j << " (c);";
      o->newline() << "c->busy --;";
      o->newline(-1) << "}" << endl;
    }
}


// ------------------------------------------------------------------------
//  Dwarf derived probes.
// ------------------------------------------------------------------------
#ifdef HAVE_ELFUTILS_LIBDWFL_H

// Helper for dealing with selected portions of libdwfl in a more readable
// fashion, and with specific cleanup / checking / logging options.

struct
dwflpp
{

  Dwfl * dwfl;

  // These are "current" values we focus on.
  Dwfl_Module * module;
  Dwarf * module_dwarf;
  Dwarf_Addr module_bias;
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
    if (verbose)
      clog << "WARNING: no name found for " << type << endl;
    return string("default_anonymous_" ) + type;
  }

  void focus_on_module(Dwfl_Module * m)
  {
    assert(m);
    module = m;
    module_dwarf = dwfl_module_getdwarf(module, &module_bias);
    module_name = default_name(dwfl_module_info(module, NULL, 
						NULL, NULL,
						NULL, NULL,
						NULL, NULL),
			       "module");
    if (verbose)
      clog << "focused on module " << module_name << endl;
  }

  void focus_on_cu(Dwarf_Die * c)
  {
    assert(c);
    cu = c;
    cu_name = default_name(dwarf_diename(c), "cu");
    if (verbose)
      clog << "focused on CU " << cu_name 
	   << ", in module " << module_name << endl;
  }

  void focus_on_function(Dwarf_Func * f)
  {
    assert(f);
    function = f;
    function_name = default_name(dwarf_func_name(function), 
				 "function");
    if (verbose)
      clog << "focused on function " << function_name 
	   << ", in CU " << cu_name 
	   << ", module " << module_name << endl;
  }

  void focus_on_module_containing_global_address(Dwarf_Addr a)
  {
    assert(dwfl);
    if (verbose)
      clog << "focusing on module containing global addr " << a << endl;
    focus_on_module(dwfl_addrmodule(dwfl, a));
  }

  void focus_on_cu_containing_module_address(Dwarf_Addr a)
  {
    assert(dwfl);
    assert(module);
    Dwarf_Addr bias;
    if (verbose)
      clog << "focusing on cu containing module addr " << a << endl;
    focus_on_cu(dwfl_module_addrdie(module, a, &bias));
    assert(bias == module_bias);
  }

  void focus_on_cu_containing_global_address(Dwarf_Addr a)
  {
    assert(dwfl);
    if (verbose)
      clog << "focusing on cu containing global addr " << a << endl;
    focus_on_module_containing_global_address(a);
    assert(a > module_bias);
    a = global_address_to_module(a);
    focus_on_cu_containing_module_address(a);
  }

  Dwarf_Addr module_address_to_global(Dwarf_Addr a)
  {
    assert(module);
    if (verbose)
      clog << "module addr " << a 
	   << " + bias " << module_bias 
	   << " -> global addr " << a + module_bias << endl;
    return a + module_bias;
  }

  Dwarf_Addr global_address_to_module(Dwarf_Addr a)
  {
    assert(module);
    if (verbose)
      clog << "global addr " << a 
	   << " - bias " << module_bias 
	   << " -> module addr " << a - module_bias << endl;
    return a - module_bias;
  }


  bool module_name_matches(string pattern)
  {
    assert(module);
    bool t = (fnmatch(pattern.c_str(), module_name.c_str(), 0) == 0);
    if (verbose)
      clog << "pattern '" << pattern << "' "
	   << (t ? "matches " : "does not match ") 
	   << "module '" << module_name << "'" << endl;
    return t;
  }

  bool function_name_matches(string pattern)
  {
    assert(function);
    bool t = (fnmatch(pattern.c_str(), function_name.c_str(), 0) == 0);
    if (verbose)
      clog << "pattern '" << pattern << "' "
	   << (t ? "matches " : "does not match ") 
	   << "function '" << function_name << "'" << endl;
    return t;
  }

  bool cu_name_matches(string pattern)
  {
    assert(cu);
    bool t = (fnmatch(pattern.c_str(), cu_name.c_str(), 0) == 0);
    if (verbose)
      clog << "pattern '" << pattern << "' "
	   << (t ? "matches " : "does not match ") 
	   << "CU '" << cu_name << "'" << endl;
    return t;
  }

  void dwflpp_assert(int rc)
  {
    if (rc != 0)
      throw semantic_error(string("dwfl failure: ") + dwfl_errmsg(rc));
  }

  dwflpp()
    :
    dwfl(NULL),
    module(NULL),
    module_dwarf(NULL),
    module_bias(0),
    cu(NULL),
    function(NULL)
  {}
  
  void setup(bool kernel)
  {
    static const Dwfl_Callbacks proc_callbacks =
      {
	dwfl_linux_proc_find_elf,
	dwfl_standard_find_debuginfo,
	NULL,
	NULL
      };
    
    static const Dwfl_Callbacks kernel_callbacks =
      {
	dwfl_linux_kernel_find_elf,
	dwfl_standard_find_debuginfo,
	dwfl_linux_kernel_module_section_address,
	NULL
      };

    if (kernel)
      {
	dwfl = dwfl_begin(&kernel_callbacks);
	if (!dwfl)
	  throw semantic_error("cannot open dwfl");
	dwfl_report_begin(dwfl);
	dwflpp_assert(dwfl_linux_kernel_report_kernel(dwfl));
	dwflpp_assert(dwfl_linux_kernel_report_modules(dwfl));
      }
    else
      {
	dwfl = dwfl_begin(&proc_callbacks);
	dwfl_report_begin(dwfl);
	if (!dwfl)
	  throw semantic_error("cannot open dwfl");
	// XXX: Find pids or processes, do userspace stuff.
      }

    dwflpp_assert(dwfl_report_end(dwfl, NULL, NULL));
  }

  void iterate_over_modules(int (* callback)(Dwfl_Module *, void **,
					     const char *, Dwarf_Addr,
					     Dwarf *, Dwarf_Addr, void *),
			    void * data)
  {
    if (verbose)
      clog << "iterating over modules" << endl;
    ptrdiff_t off = 0;
    do
      {
	off = dwfl_getdwarf(dwfl, callback, data, off);
      }
    while (off > 0);
    if (verbose)
      clog << "finished iterating over modules" << endl;
    dwflpp_assert(off);
  }

  void iterate_over_cus (int (*callback)(Dwarf_Die * die, void * arg), 
			 void * data)
  {
    if (!module_dwarf)
      {
	cerr << "WARNING: no dwarf info found for module " << module_name << endl;
	return;
      }

    if (verbose)
      clog << "iterating over CUs in module " << module_name << endl;

    Dwarf *dw = module_dwarf;
    Dwarf_Off off = 0;
    size_t cuhl;
    Dwarf_Off noff;
    while (dwarf_nextcu(dw, off, &noff, &cuhl, NULL, NULL, NULL) == 0)
      {		  
	Dwarf_Die die_mem;
	Dwarf_Die *die;
	die = dwarf_offdie(dw, off + cuhl, &die_mem);
	if (callback(die, data) != DWARF_CB_OK)
	  break;
	off = noff;
      }
  }

  void iterate_over_functions(int (* callback)(Dwarf_Func * func, void * arg),
			      void * data)
  {
    assert(module);
    assert(cu);
    if (verbose)
      clog << "iterating over functions in CU " << cu_name << endl;
    dwarf_getfuncs(cu, callback, data, 0);
  }

  bool function_entrypc(Dwarf_Addr * addr)
  {
    return (dwarf_func_entrypc(function, addr) == 0);
  }

  bool function_includes_global_addr(Dwarf_Addr addr)
  {
    assert(module_dwarf);
    assert(cu);
    assert(function);
    Dwarf_Addr lo, hi;
    if (dwarf_func_lowpc(function, &lo) != 0)
      {
	if (verbose)
	  clog << "WARNING: cannot find low PC value for function " << function_name << endl;
	return false;
      }
    
    if (dwarf_func_highpc(function, &hi) != 0)
    {
      if (verbose)
	clog << "WARNING: cannot find high PC value for function " << function_name << endl;
      return false;
    }
    
    bool t = lo <= addr && addr <= hi;
    if (verbose)
      clog << "function " << function_name << " = [" << lo << "," << hi << "] "
	   << (t ? "contains " : "does not contain ") 
	   << " global addr " << addr << endl;
    return t;
  }

  bool function_includes_module_addr(Dwarf_Addr addr)
  {
    return function_includes_global_addr(module_address_to_global(addr));
  }

  Dwarf_Addr global_addr_of_line_in_cu(int line)
  {
    Dwarf_Lines * lines;
    size_t nlines;
    Dwarf_Addr addr;
    Dwarf_Line * linep;

    assert(module);
    assert(cu);
    dwflpp_assert(dwarf_getsrclines(cu, &lines, &nlines));
    linep = dwarf_onesrcline(lines, line);
    dwflpp_assert(dwarf_lineaddr(linep, &addr));
    if (verbose)
      clog << "line " << line 
	   << " of cu " << cu_name 
	   << " has module address " << addr 
	   << " in " << module_name << endl;
    return module_address_to_global(addr);
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

enum
probe_type 
  { 
    probe_address,
    probe_function_return
  };

struct 
probe_spec
{
  probe_spec(Dwarf_Addr a, probe_type ty) 
    : address(a), type(ty)
  {}
  Dwarf_Addr address;
  probe_type type;
  bool operator<(probe_spec const & other) const
  {
    return ((address < other.address) ||
	    ((address == other.address) && (type < other.type)));
  }
};

struct dwarf_builder;
struct dwarf_derived_probe : public derived_probe
{
  dwarf_derived_probe (probe* p, probe_point* l, 
		       map<string, literal *> const & params);
  
  static string TOK_PROCESS;
  static string TOK_KERNEL;
  static string TOK_MODULE;
  static string TOK_FUNCTION;
  static string TOK_STATEMENT;
  static string TOK_CALLEES;
  static string TOK_RETURN;
  static string TOK_RELATIVE;
  static string TOK_LABEL;

  // Pattern registration helpers.
  static void register_relative_variants(match_node & root, 
					 dwarf_builder * dw);
  static void register_statement_variants(match_node & root, 
					  dwarf_builder * dw);
  static void register_callee_variants(match_node & root, 
				       dwarf_builder * dw);
  static void register_function_and_statement_variants(match_node & root, 
						       dwarf_builder * dw);
  static void register_patterns(match_node & root);

  // Parameter extractors.
  static bool has_null_param(map<string, literal *> const & params, 
			     string const & k);
  static bool get_string_param(map<string, literal *> const & params, 
			       string const & k, string & v);
  static bool get_number_param(map<string, literal *> const & params, 
			       string const & k, long & v);

  // The results of all our hard work go in these vectors.
  set<probe_spec> kernel_probes;
  map<string, set<probe_spec>*> module_probes;

  void add_kernel_probe(probe_spec const & p);
  void add_module_probe(string const & module, probe_spec const & p);

  // Helper struct to thread through the dwfl callbacks.
  struct 
  dwarf_query
  {
    dwarf_query(dwarf_derived_probe & probe,
		dwflpp & d,
		map<string, literal *> const & params);

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
    long statement_num_val; 
    long function_num_val; 

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


    dwarf_derived_probe & probe;
    dwflpp & dw;
  };
  
  virtual void emit_registrations (translator_output* o, unsigned i);
  virtual void emit_deregistrations (translator_output* o, unsigned i);
  virtual void emit_probe_entries (translator_output* o, unsigned i);
  virtual ~dwarf_derived_probe() {}
};

struct
dwarf_builder 
  : public derived_probe_builder
{
  dwarf_builder() {}
  virtual derived_probe * build(probe * base, 
				probe_point * location,
				map<string, literal *> const & parameters)
  {
    return new dwarf_derived_probe(base, location, parameters);
  }
  virtual ~dwarf_builder() {}
};

bool 
dwarf_derived_probe::has_null_param(map<string, literal *> const & params, 
				    string const & k)
{
  map<string, literal *>::const_iterator i = params.find(k);
  if (i != params.end() && i->second == NULL)
    return true;
  return false;
}

bool 
dwarf_derived_probe::get_string_param(map<string, literal *> const & params, 
				      string const & k, string & v)
{
  map<string, literal *>::const_iterator i = params.find(k);
  if (i == params.end())
    return false;
  literal_string * ls = dynamic_cast<literal_string *>(i->second);
  if (!ls)
    return false;
  v = ls->value;
  return true;
}

bool 
dwarf_derived_probe::get_number_param(map<string, literal *> const & params, 
				      string const & k, long & v)
{
  map<string, literal *>::const_iterator i = params.find(k);
  if (i == params.end())
    return false;
  if (i->second == NULL)
    return false;
  literal_number * ln = dynamic_cast<literal_number *>(i->second);
  if (!ln)
    return false;
  v = ln->value;
  return true;
}

void 
dwarf_derived_probe::add_kernel_probe(probe_spec const & p)
{
  kernel_probes.insert(p);
}

void 
dwarf_derived_probe::add_module_probe(string const & module, 
				      probe_spec const & p)
{
  set<probe_spec>* specs;

  map<string, set<probe_spec>*>::const_iterator i 
    = module_probes.find(module);

  if (i == module_probes.end())
    {
      specs = new set<probe_spec>();
      module_probes.insert(make_pair(module, specs));
    }
  else
    {
      specs = i->second;
    }
  specs->insert(p);
}


dwarf_derived_probe::dwarf_query::dwarf_query(dwarf_derived_probe & probe,
					      dwflpp & d,
					      map<string, literal *> const & params)
  : probe(probe), 
    dw(d)
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


template <typename OUT, typename IN> inline OUT 
lex_cast(IN const & in)
{
  stringstream ss;
  OUT out;
  if (!(ss << in && ss >> out))
    throw runtime_error("bad lexical cast");
  return out;
}

function_spec_type
dwarf_derived_probe::dwarf_query::parse_function_spec(string & spec)
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
      if (verbose)
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
      if (verbose)
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
      if (verbose)
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
    throw semantic_error("malformed specification '" + spec + "'", probe.tok);
}


static void
query_statement(Dwarf_Addr stmt_addr, dwarf_derived_probe::dwarf_query *q)
{
  // XXX: implement 
  if (q->has_relative)
    throw semantic_error("incomplete: do not know how to interpret .relative", q->probe.tok);

  probe_type ty = (((q->has_function_str || q->has_function_num) && q->has_return) 
		   ? probe_function_return 
		   : probe_address);

  if (q->has_module)
    q->probe.add_module_probe(q->dw.module_name, 
			      probe_spec(q->dw.global_address_to_module(stmt_addr), ty));
  else
    q->probe.add_kernel_probe(probe_spec(stmt_addr, ty));
}

static int
query_function(Dwarf_Func * func, void * arg)
{
  
  dwarf_derived_probe::dwarf_query *q = 
    static_cast<dwarf_derived_probe::dwarf_query *>(arg);

  // XXX: implement 
  if (q->has_callees)
    throw semantic_error("incomplete: do not know how to interpret .callees", q->probe.tok);

  if (q->has_label)
    throw semantic_error("incomplete: do not know how to interpret .label", q->probe.tok);

  q->dw.focus_on_function(func);

  // XXX: We assume addr is a global address here. Is it?
  Dwarf_Addr addr;
  if (!q->dw.function_entrypc(&addr))
    {
      if (verbose)
	clog << "WARNING: cannot find entry PC for function " 
	     << q->dw.function_name << endl;
      return DWARF_CB_OK;
    }

  if ((q->has_statement_str || q->has_function_str) 
      && q->dw.function_name_matches(q->function))
    {
      // If this function's name matches a function or statement
      // pattern, we use its entry pc, but we do not abort iteration
      // since there might be other functions matching the pattern.
      query_statement(addr, q);
    }
  else if (q->has_kernel 
	   && q->has_function_num
	   && q->dw.function_includes_global_addr(q->function_num_val))
    {
      // If this function's address range matches a kernel-relative
      // function address, we use its entry pc and break out of the
      // iteration, since there can only be one such function.
      query_statement(addr, q);
      return DWARF_CB_ABORT;
    }
  else if (q->has_module
	   && q->has_function_num
	   && q->dw.function_includes_module_addr(q->function_num_val))
    {
      // If this function's address range matches a module-relative
      // function address, we use its entry pc and break out of the
      // iteration, since there can only be one such function.
      query_statement(addr, q);
      return DWARF_CB_ABORT;
    }

  return DWARF_CB_OK;
}

static int
query_cu (Dwarf_Die * cudie, void * arg)
{
  dwarf_derived_probe::dwarf_query *q = 
    static_cast<dwarf_derived_probe::dwarf_query *>(arg);
  
  q->dw.focus_on_cu(cudie);

  // If we have enough information in the pattern to skip a CU
  // and the CU does not match that information, return early.
  if ((q->has_statement_str || q->has_function_str)
      && (q->spec_type == function_file_and_line ||
	  q->spec_type == function_and_file)
      && (!q->dw.cu_name_matches(q->file)))
    return DWARF_CB_OK;

  if (q->has_statement_str 
      && (q->spec_type == function_file_and_line) 
      && q->dw.cu_name_matches(q->file))
    {
      // If we have a complete file:line statement
      // functor (not function functor) landing on
      // this CU, we can look up a specific address
      // for the statement, and skip scanning
      // the remaining functions within the CU.
      query_statement(q->dw.global_addr_of_line_in_cu(q->line), q);
    }
  else
    {
      // Otherwise we need to scan all the functions in this CU.
      q->dw.iterate_over_functions(&query_function, q);
    }
  return DWARF_CB_OK;
}

static int
query_module (Dwfl_Module *mod __attribute__ ((unused)),
	      void **userdata __attribute__ ((unused)),
	      const char *name, Dwarf_Addr base,
	      Dwarf *dw, Dwarf_Addr bias,
	      void *arg __attribute__ ((unused)))
{

  dwarf_derived_probe::dwarf_query *q = 
    static_cast<dwarf_derived_probe::dwarf_query *>(arg);

  q->dw.focus_on_module(mod);

  // If we have enough information in the pattern to skip a module and
  // the module does not match that information, return early.
  if (!q->dw.module_name_matches(q->module_val))
    return DWARF_CB_OK;

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
	addr = q->function_num_val;
      
      q->dw.focus_on_cu_containing_module_address(addr);
      q->dw.iterate_over_functions(&query_function, q);
    }  
  else 
    {
      // Otherwise if we have a function("foo") or statement("foo")
      // specifier, we have to scan over all the CUs looking for
      // the function in question
      assert(q->has_function_str || q->has_statement_str);
      q->dw.iterate_over_cus(&query_cu, q);
    }

  return DWARF_CB_OK;
}

dwarf_derived_probe::dwarf_derived_probe (probe* p, probe_point* l, 
					  map<string, literal *> const & params)
  : derived_probe (p, l)
{
  dwflpp dw;
  dwarf_query q(*this, dw, params);

  dw.setup(q.has_kernel || q.has_module);

  if (q.has_kernel && q.has_statement_num)
    {
      // If we have kernel.statement(0xbeef), the address is global
      // (relative to the kernel) and we can seek directly to the
      // statement in question.
      query_statement(q.statement_num_val, &q);
    }
  else if (q.has_kernel && q.has_function_num)
    {
      // If we have kernel.function(0xbeef), the address is global
      // (relative to the kernel) and we can seek directly to the
      // cudie in question.
      dw.focus_on_cu_containing_global_address(q.function_num_val);
      dw.iterate_over_functions(&query_function, &q);
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

string dwarf_derived_probe::TOK_PROCESS("process");
string dwarf_derived_probe::TOK_KERNEL("kernel");
string dwarf_derived_probe::TOK_MODULE("module");

string dwarf_derived_probe::TOK_FUNCTION("function");
string dwarf_derived_probe::TOK_RETURN("return");
string dwarf_derived_probe::TOK_CALLEES("callees");

string dwarf_derived_probe::TOK_STATEMENT("statement");
string dwarf_derived_probe::TOK_LABEL("label");
string dwarf_derived_probe::TOK_RELATIVE("relative");


void 
dwarf_derived_probe::register_relative_variants(match_node & root,
						dwarf_builder * dw)
{
  // Here we match 2 forms:
  //
  // .
  // .relative(NN)

  root.bind(dw);
  root.bind_num(TOK_RELATIVE).bind(dw);
}

void 
dwarf_derived_probe::register_statement_variants(match_node & root,
						 dwarf_builder * dw)
{
  // Here we match 3 forms:
  //
  // .
  // .return
  // .label("foo")
  
  register_relative_variants(root, dw);
  register_relative_variants(root.bind(TOK_RETURN), dw);
  register_relative_variants(root.bind_str(TOK_LABEL), dw);
}

void 
dwarf_derived_probe::register_callee_variants(match_node & root,						
					      dwarf_builder * dw)
{
  // Here we match 3 forms:
  //
  // .
  // .callees
  // .callees(N)
  //
  // The last form permits N-level callee resolving without any
  // recursive .callees.callees.callees... pattern-matching on our part.

  register_statement_variants(root, dw);
  register_statement_variants(root.bind(TOK_CALLEES), dw);
  register_statement_variants(root.bind_num(TOK_CALLEES), dw);
}

void 
dwarf_derived_probe::register_function_and_statement_variants(match_node & root,
							      dwarf_builder * dw)
{
  // Here we match 4 forms:
  //
  // .function("foo")
  // .function(0xdeadbeef)
  // .statement("foo")
  // .statement(0xdeadbeef)

  register_callee_variants(root.bind_str(TOK_FUNCTION), dw);  
  register_callee_variants(root.bind_num(TOK_FUNCTION), dw);
  register_statement_variants(root.bind_str(TOK_STATEMENT), dw);
  register_statement_variants(root.bind_num(TOK_STATEMENT), dw);
}

void
dwarf_derived_probe::register_patterns(match_node & root)
{
  dwarf_builder *dw = new dwarf_builder();

  // Here we match 3 forms:
  //
  // .kernel
  // .module("foo")
  // .process("foo")

  register_function_and_statement_variants(root.bind(TOK_KERNEL), dw);
  register_function_and_statement_variants(root.bind_str(TOK_MODULE), dw);
  register_function_and_statement_variants(root.bind_str(TOK_PROCESS), dw);
}

static string 
probe_entry_function_name(unsigned probenum)
{
  return "dwarf_kprobe_" + lex_cast<string>(probenum) + "_enter";
}

static string 
probe_entry_struct_kprobe_name(unsigned probenum, 
			       unsigned entrynum)
{
  return "dwarf_kprobe_" + lex_cast<string>(probenum) 
    + "_entry_" + lex_cast<string>(entrynum);
}

static string 
end_of_block(unsigned probenum)
{
  return "block_end_" + lex_cast<string>(probenum);
}

typedef unsigned module_index;
typedef unsigned probe_index;
typedef unsigned entry_index;

static void 
foreach_dwarf_probe_entry(dwarf_derived_probe const & p,
			  translator_output *o,
			  probe_index probenum,
			  void (*kernel_entry_cb)(translator_output *,
						  probe_index, 
						  entry_index,
						  probe_spec const &),
			  void (*module_cb)(translator_output *,
					    probe_index, 
					    module_index,
					    string const &),
			  void (*module_entry_cb)(translator_output *,
						  probe_index,
						  string const &,
						  module_index,
						  entry_index,
						  probe_spec const &))
{
  // Just a helper function for an ugly iteration task.
  
  entry_index entrynum = 0;
  
  for (set<probe_spec>::const_iterator i = p.kernel_probes.begin();
       i != p.kernel_probes.end(); ++i, ++entrynum)
    {
      if (kernel_entry_cb)
	kernel_entry_cb(o, probenum, entrynum, *i);
    }

  module_index modnum = 0;

  for (map<string, set<probe_spec>*>::const_iterator i = p.module_probes.begin();
       i != p.module_probes.end(); ++i, ++modnum)
    {
      string modname = i->first;
      set<probe_spec>* probes = i->second;
      if (module_cb)
	module_cb(o, probenum, modnum, modname);
      for (set<probe_spec>::const_iterator j = probes->begin(); 
	   j != probes->end(); ++j, ++entrynum)
	{
	  if (module_entry_cb)
	    module_entry_cb(o, probenum, modname, modnum, entrynum, *j);
	}
    }
}


static void 
declare_dwarf_kernel_entry(translator_output *o,
			   probe_index probenum, 
			   entry_index entrynum,
			   probe_spec const & probe)
{  
  o->newline() << "/* probe for "
	       << (probe.type == probe_function_return ? "return from " : "")
	       << "function at 0x" << hex << probe.address << " in kernel */";  
  o->newline() << "static struct kprobe " 
	       << probe_entry_struct_kprobe_name(probenum, entrynum);
  o->newline() << "{";
  o->indent(1);
  o->newline() << ".addr        = 0x" << hex << probe.address << ",";
  o->newline() << ".pre_handler = &" << probe_entry_function_name(probenum) << ",";
  o->indent(-1);
  o->newline() << "}";
}

static void 
register_dwarf_kernel_entry(translator_output *o,
			    probe_index probenum, 
			    entry_index entrynum,
			    probe_spec const & probe)
{  
  o->newline() << "register_probe (&" 
	       << probe_entry_struct_kprobe_name(probenum, entrynum)
	       << ");";
}

static void 
deregister_dwarf_kernel_entry(translator_output *o,
			      probe_index probenum, 
			      entry_index entrynum,
			      probe_spec const & probe)
{  
  o->newline() << "deregister_probe (&" 
	       << probe_entry_struct_kprobe_name(probenum, entrynum)
	       << ");";
}

static void 
register_dwarf_module(translator_output *o,
		      probe_index probenum, 
		      module_index modnum,
		      string const & modname)
{
  o->newline() << "mod = get_module(\"" << modname << "\");";
  o->newline() << "if (!mod)";
  o->newline() << "{";
  o->indent(1);
  o->newline() << "rc = 1;";
  o->newline() << "goto " << end_of_block(probenum) << ";";
  o->indent(-1);
  o->newline() << "}";
    
}

static void 
declare_dwarf_module_entry(translator_output *o,
			   probe_index probenum, 
			   string const & modname,
			   module_index modnum,
			   entry_index entrynum,
			   probe_spec const & probe)
{
  o->newline();
  o->newline() << "/* probe for "
	       << (probe.type == probe_function_return ? "return from " : "")
	       << "function at 0x" << hex << probe.address 
	       << " in module " << modname << " */";
  
  o->newline() << "static struct kprobe ";
  o->newline() << probe_entry_struct_kprobe_name(probenum, entrynum);
  o->newline() << "{";
  o->indent(1);
  o->newline() << "/* .addr is calculated at init-time */"; 
  o->newline() << ".addr        = 0,";
  o->newline() << ".pre_handler = &" << probe_entry_function_name(probenum) << ",";
  o->indent(-1);
  o->newline() << "}";
  o->newline();
}

static void 
register_dwarf_module_entry(translator_output *o,
			    probe_index probenum,
			    string const & modname, 
			    module_index modnum,
			    entry_index entrynum,
			    probe_spec const & probe)
{
  o->newline();
  o->newline() << probe_entry_struct_kprobe_name(probenum, entrynum) 
	       << ".addr = mod->module_core + 0x" << hex << probe.address << ";";
  o->newline() << "register_probe (&" 
	       << probe_entry_struct_kprobe_name(probenum, entrynum)
	       << ");";  
}

static void 
deregister_dwarf_module_entry(translator_output *o,
			      probe_index probenum, 
			      string const & modname,
			      module_index modnum,
			      entry_index entrynum,
			      probe_spec const & probe)
{
  o->newline() << "deregister_probe (&" 
	       << probe_entry_struct_kprobe_name(probenum, entrynum)
	       << ");";  
}
		    

void 
dwarf_derived_probe::emit_registrations (translator_output* o, unsigned probenum)
{
  o->newline() << "{";
  o->indent(1);
  o->newline() << "struct module *mod = NULL;";
  foreach_dwarf_probe_entry(*this, o, probenum,
			    &register_dwarf_kernel_entry,
			    &register_dwarf_module,
			    &register_dwarf_module_entry);
  o->newline();
  o->newline() << end_of_block(probenum) << ":";
  o->newline();
  o->indent(-1);
  o->newline() << "}";
}

void 
dwarf_derived_probe::emit_deregistrations (translator_output* o, unsigned probenum)
{
  o->newline();
  foreach_dwarf_probe_entry(*this, o, probenum,
			    &deregister_dwarf_kernel_entry,
			    NULL,
			    &deregister_dwarf_module_entry);
  o->newline();
}

void 
dwarf_derived_probe::emit_probe_entries (translator_output* o, unsigned probenum)
{
  // We should have expanded each location in the initial probe to a
  // separate derived_probe instance; each derived_probe should only
  // have one location.
  assert(locations.size() == 1);

  // Construct a single entry function, and a struct kprobe for each
  // address this derived probe will match, all of which point into
  // the entry function.

  // First the entry function
  o->newline() << "/* probe " << probenum << " entry function */";
  o->newline() << "static void ";
  o->newline() << probe_entry_function_name(probenum) << " ()";
  o->newline() << "{";
  o->newline(1) << "struct context* c = & contexts [0];";
  // XXX: assert #0 is free; need locked search instead
  o->newline() << "if (c->busy) { errorcount ++; return; }";
  o->newline() << "c->busy ++;";
  o->newline() << "c->actioncount = 0;";
  o->newline() << "c->nesting = 0;";
  // NB: locals are initialized by probe function itself
  o->newline() << "probe_" << probenum << " (c);";
  o->newline() << "c->busy --;";
  o->newline(-1) << "}" << endl;

  foreach_dwarf_probe_entry(*this, o, probenum,
			    &declare_dwarf_kernel_entry,
			    NULL,
			    &declare_dwarf_module_entry);

}

#endif /* HAVE_ELFUTILS_LIBDWFL_H */


// ------------------------------------------------------------------------
//  Standard tapset registry.
// ------------------------------------------------------------------------

void 
register_standard_tapsets(match_node & root)
{
  // Rudimentary binders for begin and end targets
  root.bind("begin").bind(new be_builder(true));
  root.bind("end").bind(new be_builder(false));

#ifdef HAVE_ELFUTILS_LIBDWFL_H
  dwarf_derived_probe::register_patterns(root);
#endif /* HAVE_ELFUTILS_LIBDWFL_H */
}
