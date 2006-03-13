// translation pass
// Copyright (C) 2005, 2006 Red Hat Inc.
// Copyright (C) 2005, 2006 Intel Corporation
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "elaborate.h"
#include "translate.h"
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <cassert>

#define EXTRACTORS_PERMISSIVE 0 /* PR 2142 */


using namespace std;



// little utility function

template <typename T>
static string
stringify(T t)
{
  ostringstream s;
  s << t;
  return s.str ();
}

// return as quoted string, with at least '"' backslash-escaped
template <typename IN> inline string
lex_cast_qstring(IN const & in)
{
  stringstream ss;
  string out, out2;
  if (!(ss << in))
    throw runtime_error("bad lexical cast");
  out = ss.str();
  out2 += '"';
  for (unsigned i=0; i<out.length(); i++)
    {
      if (out[i] == '"') // XXX others?
	out2 += '\\';
      out2 += out[i];
    }
  out2 += '"';
  return out2;
}


struct var;
struct tmpvar;
struct aggvar;
struct mapvar;
struct itervar;

struct c_unparser: public unparser, public visitor
{
  systemtap_session* session;
  translator_output* o;

  derived_probe* current_probe;
  unsigned current_probenum;
  functiondecl* current_function;
  unsigned tmpvar_counter;
  unsigned label_counter;

  c_unparser (systemtap_session* ss):
    session (ss), o (ss->op), current_probe(0), current_function (0),
  tmpvar_counter (0), label_counter (0) {}
  ~c_unparser () {}

  void emit_map_type_instantiations ();
  void emit_common_header ();
  void emit_global (vardecl* v);
  void emit_global_param (vardecl* v);
  void emit_functionsig (functiondecl* v);
  void emit_module_init ();
  void emit_module_exit ();
  void emit_function (functiondecl* v);
  void emit_locks (const varuse_collecting_visitor& v);
  void emit_probe (derived_probe* v, unsigned i);
  void emit_unlocks (const varuse_collecting_visitor& v);

  // for use by stats (pmap) foreach
  set<string> aggregations_active;

  // for use by looping constructs
  vector<string> loop_break_labels;
  vector<string> loop_continue_labels;

  string c_typename (exp_type e);
  string c_varname (const string& e);

  void c_assign (var& lvalue, const string& rvalue, const token* tok);
  void c_assign (const string& lvalue, expression* rvalue, const string& msg);
  void c_assign (const string& lvalue, const string& rvalue, exp_type type,
                 const string& msg, const token* tok);

  void c_declare(exp_type ty, const string &name);
  void c_declare_static(exp_type ty, const string &name);

  void c_strcat (const string& lvalue, const string& rvalue);
  void c_strcat (const string& lvalue, expression* rvalue);

  void c_strcpy (const string& lvalue, const string& rvalue);
  void c_strcpy (const string& lvalue, expression* rvalue);

  bool is_local (vardecl const* r, token const* tok);

  tmpvar gensym(exp_type ty);
  aggvar gensym_aggregate();

  var getvar(vardecl* v, token const* tok = NULL);
  itervar getiter(symbol* s);
  mapvar getmap(vardecl* v, token const* tok = NULL);

  void load_map_indices(arrayindex* e,
			vector<tmpvar> & idx);

  void load_aggregate (expression *e, aggvar & agg, bool pre_agg=false);
  string histogram_index_check(var & vase, tmpvar & idx) const;

  void collect_map_index_types(vector<vardecl* > const & vars,
			       set< pair<vector<exp_type>, exp_type> > & types);

  void visit_statement (statement* s, unsigned actions);

  void visit_block (block* s);
  void visit_embeddedcode (embeddedcode* s);
  void visit_null_statement (null_statement* s);
  void visit_expr_statement (expr_statement* s);
  void visit_if_statement (if_statement* s);
  void visit_for_loop (for_loop* s);
  void visit_foreach_loop (foreach_loop* s);
  void visit_return_statement (return_statement* s);
  void visit_delete_statement (delete_statement* s);
  void visit_next_statement (next_statement* s);
  void visit_break_statement (break_statement* s);
  void visit_continue_statement (continue_statement* s);
  void visit_literal_string (literal_string* e);
  void visit_literal_number (literal_number* e);
  void visit_binary_expression (binary_expression* e);
  void visit_unary_expression (unary_expression* e);
  void visit_pre_crement (pre_crement* e);
  void visit_post_crement (post_crement* e);
  void visit_logical_or_expr (logical_or_expr* e);
  void visit_logical_and_expr (logical_and_expr* e);
  void visit_array_in (array_in* e);
  void visit_comparison (comparison* e);
  void visit_concatenation (concatenation* e);
  void visit_ternary_expression (ternary_expression* e);
  void visit_assignment (assignment* e);
  void visit_symbol (symbol* e);
  void visit_target_symbol (target_symbol* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
  void visit_print_format (print_format* e);
  void visit_stat_op (stat_op* e);
  void visit_hist_op (hist_op* e);
};

// A shadow visitor, meant to generate temporary variable declarations
// for function or probe bodies.  Member functions should exactly match
// the corresponding c_unparser logic and traversal sequence,
// to ensure interlocking naming and declaration of temp variables.
struct c_tmpcounter: 
  public traversing_visitor
{
  c_unparser* parent;
  c_tmpcounter (c_unparser* p): 
    parent (p) 
  {
    parent->tmpvar_counter = 0;
  }

  void visit_block (block *s);
  void visit_for_loop (for_loop* s);
  void visit_foreach_loop (foreach_loop* s);
  // void visit_return_statement (return_statement* s);
  void visit_delete_statement (delete_statement* s);
  void visit_binary_expression (binary_expression* e);
  // void visit_unary_expression (unary_expression* e);
  void visit_pre_crement (pre_crement* e);
  void visit_post_crement (post_crement* e);
  // void visit_logical_or_expr (logical_or_expr* e);
  // void visit_logical_and_expr (logical_and_expr* e);
  void visit_array_in (array_in* e);
  // void visit_comparison (comparison* e);
  void visit_concatenation (concatenation* e);
  // void visit_ternary_expression (ternary_expression* e);
  void visit_assignment (assignment* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
  void visit_print_format (print_format* e);
  void visit_stat_op (stat_op* e);
};

struct c_unparser_assignment: 
  public throwing_visitor
{
  c_unparser* parent;
  string op;
  expression* rvalue;
  bool post; // true == value saved before modify operator
  c_unparser_assignment (c_unparser* p, const string& o, expression* e):
    throwing_visitor ("invalid lvalue type"),
    parent (p), op (o), rvalue (e), post (false) {}
  c_unparser_assignment (c_unparser* p, const string& o, bool pp):
    throwing_visitor ("invalid lvalue type"),
    parent (p), op (o), rvalue (0), post (pp) {}

  void prepare_rvalue (string const & op, 
		       tmpvar const & rval,
		       token const*  tok);

  void c_assignop(tmpvar & res, 
		  var const & lvar, 
		  tmpvar const & tmp,
		  token const*  tok);

  // only symbols and arrayindex nodes are possible lvalues
  void visit_symbol (symbol* e);
  void visit_arrayindex (arrayindex* e);
};


struct c_tmpcounter_assignment: 
  public traversing_visitor
// leave throwing for illegal lvalues to the c_unparser_assignment instance
{
  c_tmpcounter* parent;
  const string& op;
  expression* rvalue;
  c_tmpcounter_assignment (c_tmpcounter* p, const string& o, expression* e):
    parent (p), op (o), rvalue (e) {}

  // only symbols and arrayindex nodes are possible lvalues
  void visit_symbol (symbol* e);
  void visit_arrayindex (arrayindex* e);
};


ostream & operator<<(ostream & o, var const & v);


/*
  Some clarification on the runtime structures involved in statistics:
  
  The basic type for collecting statistics in the runtime is struct
  stat_data. This contains the count, min, max, sum, and possibly
  histogram fields.
  
  There are two places struct stat_data shows up.
  
  1. If you declare a statistic variable of any sort, you want to make
  a struct _Stat. A struct _Stat* is also called a Stat. Struct _Stat
  contains a per-CPU array of struct stat_data values, as well as a
  struct stat_data which it aggregates into. Writes into a Struct
  _Stat go into the per-CPU struct stat. Reads involve write-locking
  the struct _Stat, aggregating into its aggregate struct stat_data,
  unlocking, read-locking the struct _Stat, then reading values out of
  the aggregate and unlocking.

  2. If you declare a statistic-valued map, you want to make a
  pmap. This is a per-CPU array of maps, each of which holds struct
  stat_data values, as well as an aggregate *map*. Writes into a pmap
  go into the per-CPU map. Reads involve write-locking the pmap,
  aggregating into its aggregate map, unlocking, read-locking the
  pmap, then reading values out of its aggregate (which is a normal
  map) and unlocking.

  Because, at the moment, the runtime does not support the concept of
  a statistic which collects multiple histogram types, we may need to
  instantiate one pmap or struct _Stat for each histogram variation
  the user wants to track.  
 */

class var
{

protected:
  bool local;
  exp_type ty;
  statistic_decl sd;
  string name;

public:

  var(bool local, exp_type ty, statistic_decl const & sd, string const & name)
    : local(local), ty(ty), sd(sd), name(name)
  {}

  var(bool local, exp_type ty, string const & name)
    : local(local), ty(ty), name(name)
  {}

  virtual ~var() {}

  bool is_local() const
  {
    return local;
  }

  statistic_decl const & sdecl() const
  {
    return sd;
  }

  void assert_hist_compatible(hist_op const & hop)
  {
    // Semantic checks in elaborate should have caught this if it was
    // false. This is just a double-check.
    switch (sd.type)
      {
      case statistic_decl::linear:
	assert(hop.htype == hist_linear);
	assert(hop.params.size() == 3);
	assert(hop.params[0] == sd.linear_low);
	assert(hop.params[1] == sd.linear_high);
	assert(hop.params[2] == sd.linear_step);
	break;
      case statistic_decl::logarithmic:
	assert(hop.htype == hist_log);
	assert(hop.params.size() == 1);
	assert(hop.params[0] == sd.logarithmic_buckets);
	break;
      case statistic_decl::none:
	assert(false);
      }
  }

  exp_type type() const
  {
    return ty;
  }

  string qname() const
  {
    if (local)
      return "l->" + name;
    else
      return "global_" + name;
  }

  virtual string hist() const
  {
    assert (ty == pe_stats);
    assert (sd.type != statistic_decl::none);
    return "(&(" + qname() + "->hist))";
  }

  virtual string buckets() const
  {
    assert (ty == pe_stats);
    assert (sd.type != statistic_decl::none);
    return "(" + qname() + "->hist.buckets)";
  }

  string init() const
  {
    switch (type())
      {
      case pe_string:
        if (! local)
          return ""; // module_param
        else
	  return qname() + "[0] = '\\0';";
      case pe_long:
        if (! local)
          return qname() + " = (int64_t) init_" + qname() + ";"; // module_param
        else
          return qname() + " = 0;";
      case pe_stats:
	switch (sd.type)
	  {
	  case statistic_decl::none:
	    return (qname() 
		    + " = _stp_stat_init (HIST_NONE);");
	    break;
	    
	  case statistic_decl::linear:
	    return (qname() 
		    + " = _stp_stat_init (HIST_LINEAR"
		    + ", " + stringify(sd.linear_low) 
		    + ", " + stringify(sd.linear_high) 
		    + ", " + stringify(sd.linear_step)
		    + ");");
	    break;

	  case statistic_decl::logarithmic:
	    return (qname() 
		    + " = _stp_stat_init (HIST_LOG"
		    + ", " + stringify(sd.logarithmic_buckets) 
		    + ");");
	    break;
	  }
      default:
	throw semantic_error("unsupported initializer for " + qname());
      }
  }

  void declare(c_unparser &c) const
  {
    c.c_declare(ty, name);
  }
};

ostream & operator<<(ostream & o, var const & v)
{
  return o << v.qname();
}

struct stmt_expr
{
  c_unparser & c;
  stmt_expr(c_unparser & c) : c(c) 
  {
    c.o->newline() << "({";
    c.o->indent(1);
  }
  ~stmt_expr()
  {
    c.o->newline(-1) << "})";
  }
};


struct tmpvar
  : public var
{
  tmpvar(exp_type ty, 
	 unsigned & counter) 
    : var(true, ty, ("__tmp" + stringify(counter++)))
  {}
};

struct aggvar
  : public var
{
  aggvar(unsigned & counter) 
    : var(true, pe_stats, ("__tmp" + stringify(counter++)))
  {}

  string init() const
  {
    assert (type() == pe_stats);
    return qname() + " = NULL;";
  }

  void declare(c_unparser &c) const
  {
    assert (type() == pe_stats);
    c.o->newline() << "struct stat_data *" << name << ";";
  }
};

struct mapvar
  : public var
{
  vector<exp_type> index_types;
  mapvar (bool local, exp_type ty, 
	  statistic_decl const & sd,
	  string const & name, 
	  vector<exp_type> const & index_types)
    : var (local, ty, sd, name),
      index_types (index_types)
  {}
  
  static string shortname(exp_type e);
  static string key_typename(exp_type e);
  static string value_typename(exp_type e);

  string keysym () const
  {
    string result;
    vector<exp_type> tmp = index_types;
    tmp.push_back (type ());
    for (unsigned i = 0; i < tmp.size(); ++i)
      {
	switch (tmp[i])
	  {
	  case pe_long:
	    result += 'i';
	    break;
	  case pe_string:
	    result += 's';
	    break;
	  case pe_stats:
	    result += 'x';
	    break;
	  default:
	    throw semantic_error("unknown type of map");
	    break;
	  }
      }
    return result;
  }

  string call_prefix (string const & fname, vector<tmpvar> const & indices, bool pre_agg=false) const
  {
    string mtype = (is_parallel() && !pre_agg) ? "pmap" : "map";
    string result = "_stp_" + mtype + "_" + fname + "_" + keysym() + " (";
    result += pre_agg? fetch_existing_aggregate() : qname();
    for (unsigned i = 0; i < indices.size(); ++i)
      {
	if (indices[i].type() != index_types[i])
	  throw semantic_error("index type mismatch");
	result += ", ";
	result += indices[i].qname();
      }

    return result;
  }

  bool is_parallel() const
  {
    return type() == pe_stats;
  }

  string calculate_aggregate() const
  {
    if (!is_parallel())
      throw semantic_error("aggregating non-parallel map type");
    
    return "_stp_pmap_agg (" + qname() + ")";
  }

  string fetch_existing_aggregate() const
  {
    if (!is_parallel())
      throw semantic_error("fetching aggregate of non-parallel map type");
    
    return "_stp_pmap_get_agg(" + qname() + ")";
  }

  string del (vector<tmpvar> const & indices) const
  { 
    if (type() == pe_string)
      return (call_prefix("set", indices) + ", NULL)");
    else if ((type() == pe_long) || (type() == pe_stats))
      return (call_prefix("set", indices) + ", 0)");
    else
      throw semantic_error("setting a value of an unsupported map type");
  }

  string exists (vector<tmpvar> const & indices) const
  {
    return "((uintptr_t)" + call_prefix("get", indices) + ") != (uintptr_t) 0)";
  }

  string get (vector<tmpvar> const & indices, bool pre_agg=false) const
  {
    // see also itervar::get_key
    if (type() == pe_string)
        // impedance matching: NULL -> empty strings
      return ("({ char *v = " + call_prefix("get", indices, pre_agg) + ");"
	      + "if (!v) v = \"\"; v; })");
    else if (type() == pe_long || type() == pe_stats)
      return call_prefix("get", indices, pre_agg) + ")";
    else
      throw semantic_error("getting a value from an unsupported map type");
  }

  string add (vector<tmpvar> const & indices, tmpvar const & val) const
  {
    // impedance matching: empty strings -> NULL
    if (type() == pe_stats)
      return (call_prefix("add", indices) + ", " + val.qname() + ")");
    else
      throw semantic_error("adding a value of an unsupported map type");
  }

  string set (vector<tmpvar> const & indices, tmpvar const & val) const
  {
    // impedance matching: empty strings -> NULL
    if (type() == pe_string)
      return (call_prefix("set", indices) 
	      + ", (" + val.qname() + "[0] ? " + val.qname() + " : NULL))");
    else if (type() == pe_long)
      return (call_prefix("set", indices) + ", " + val.qname() + ")");
    else
      throw semantic_error("setting a value of an unsupported map type");
  }

  string hist() const
  {
    assert (ty == pe_stats);
    assert (sd.type != statistic_decl::none);
    return "(&(" + fetch_existing_aggregate() + "->hist))";
  }

  string buckets() const
  {
    assert (ty == pe_stats);
    assert (sd.type != statistic_decl::none);
    return "(" + fetch_existing_aggregate() + "->hist.buckets)";
  }
		
  string init () const
  {
    string mtype = is_parallel() ? "pmap" : "map";
    string prefix = qname() + " = _stp_" + mtype + "_new_" + keysym() + " (MAXMAPENTRIES" ;

    if (type() == pe_stats)
      {
	switch (sdecl().type)
	  {
	  case statistic_decl::none:
	    return (prefix 
		    + ", HIST_NONE);");
	    break;

	  case statistic_decl::linear:
	    // FIXME: check for "reasonable" values in linear stats
	    return (prefix 
		    + ", HIST_LINEAR" 
		    + ", " + stringify(sdecl().linear_low) 
		    + ", " + stringify(sdecl().linear_high) 
		    + ", " + stringify(sdecl().linear_step)
		    + ");");
	    break;

	  case statistic_decl::logarithmic:
	    if (sdecl().logarithmic_buckets > 64)
	      throw semantic_error("cannot support > 64 logarithmic buckets");	    
	    return (prefix
		    + ", HIST_LOG" 
		    + ", " + stringify(sdecl().logarithmic_buckets) 
		    + ");");
	    break;
	  }
      }

    return (prefix + ");");
  }

  string fini () const
  {
    if (is_parallel())
      return "_stp_pmap_del (" + qname() + ");";
    else
      return "_stp_map_del (" + qname() + ");";
  }
};


class itervar
{
  exp_type referent_ty;
  string name;

public:

  itervar (symbol* e, unsigned & counter)
    : referent_ty(e->referent->type), 
      name("__tmp" + stringify(counter++))
  {
    if (referent_ty == pe_unknown)
      throw semantic_error("iterating over unknown reference type", e->tok);
  }
  
  string declare () const
  {
    return "struct map_node *" + name + ";";
  }
  
  string start (mapvar const & mv) const
  {
    string res;

    if (mv.type() != referent_ty)
      throw semantic_error("inconsistent iterator type in itervar::start()");
    
    if (mv.is_parallel())
      return "_stp_map_start (" + mv.fetch_existing_aggregate() + ")";
    else
      return "_stp_map_start (" + mv.qname() + ")";
  }

  string next (mapvar const & mv) const
  {
    if (mv.type() != referent_ty)
      throw semantic_error("inconsistent iterator type in itervar::next()");

    if (mv.is_parallel())
      return "_stp_map_iter (" + mv.fetch_existing_aggregate() + ", " + qname() + ")";
    else
      return "_stp_map_iter (" + mv.qname() + ", " + qname() + ")";
  }

  string qname () const
  {
    return "l->" + name;
  }
  
  string get_key (exp_type ty, unsigned i) const
  {
    // bug translator/1175: runtime uses base index 1 for the first dimension
    // see also mapval::get
    switch (ty)
      {
      case pe_long:
	return "_stp_key_get_int64 ("+ qname() + ", " + stringify(i+1) + ")";
      case pe_string:
        // impedance matching: NULL -> empty strings
	return "({ char *v = "
          "_stp_key_get_str ("+ qname() + ", " + stringify(i+1) + "); "
          "if (! v) v = \"\"; "
          "v; })";
      default:
	throw semantic_error("illegal key type");
      }
  }
};

ostream & operator<<(ostream & o, itervar const & v)
{
  return o << v.qname();
}

// ------------------------------------------------------------------------


translator_output::translator_output (ostream& f):
  buf(0), o2 (0), o (f), tablevel (0)
{
}


translator_output::translator_output (const string& filename, size_t bufsize):
  buf (new char[bufsize]),
  o2 (new ofstream (filename.c_str ())), 
  o (*o2), 
  tablevel (0)
{
  o2->rdbuf()->pubsetbuf(buf, bufsize);
}


translator_output::~translator_output ()
{
  delete o2;
  delete buf;
}


ostream&
translator_output::newline (int indent)
{
  assert (indent > 0 || tablevel >= (unsigned)-indent);
  tablevel += indent;
  o << "\n";
  for (unsigned i=0; i<tablevel; i++)
    o << "  ";
  return o;
}


void
translator_output::indent (int indent)
{
  assert (indent > 0 || tablevel >= (unsigned)-indent);
  tablevel += indent;
}


ostream&
translator_output::line ()
{
  return o;
}


// ------------------------------------------------------------------------

void
c_unparser::emit_common_header ()
{
  // XXX: tapsets.cxx should be able to add additional definitions

  o->newline() << "typedef char string_t[MAXSTRINGLEN];";
  o->newline();
  o->newline() << "#define STAP_SESSION_STARTING 0";
  o->newline() << "#define STAP_SESSION_RUNNING 1";
  o->newline() << "#define STAP_SESSION_ERROR 2";
  o->newline() << "#define STAP_SESSION_STOPPING 3";
  o->newline() << "#define STAP_SESSION_STOPPED 4";
  o->newline() << "atomic_t session_state = ATOMIC_INIT (STAP_SESSION_STARTING);";
  o->newline() << "atomic_t error_count = ATOMIC_INIT (0);";
  o->newline() << "atomic_t skipped_count = ATOMIC_INIT (0);";
  o->newline();
  o->newline() << "struct context {";
  o->newline(1) << "atomic_t busy;";
  o->newline() << "const char *probe_point;";
  o->newline() << "unsigned actioncount;";
  o->newline() << "unsigned nesting;";
  o->newline() << "const char *last_error;";
  // NB: last_error is used as a health flag within a probe.
  // While it's 0, execution continues
  // When it's "", current function or probe unwinds and returns early
  // When it's "something", probe code unwinds, _stp_error's, sets error state
  // See c_unparser::visit_statement()
  o->newline() << "const char *last_stmt;";
  o->newline() << "struct pt_regs *regs;";
  o->newline() << "union {";
  o->indent(1);
  // XXX: this handles only scalars!

  for (unsigned i=0; i<session->probes.size(); i++)
    {
      derived_probe* dp = session->probes[i];
      o->newline() << "struct probe_" << i << "_locals {";
      o->indent(1);
      for (unsigned j=0; j<dp->locals.size(); j++)
        {
          vardecl* v = dp->locals[j];
	  try 
	    {
	      o->newline() << c_typename (v->type) << " " 
			   << c_varname (v->name) << ";";
	    } catch (const semantic_error& e) {
	      semantic_error e2 (e);
	      if (e2.tok1 == 0) e2.tok1 = v->tok;
	      throw e2;
	    }
        }
      c_tmpcounter ct (this);
      dp->body->visit (& ct);
      o->newline(-1) << "} probe_" << i << ";";
    }

  for (unsigned i=0; i<session->functions.size(); i++)
    {
      functiondecl* fd = session->functions[i];
      o->newline()
        << "struct function_" << c_varname (fd->name) << "_locals {";
      o->indent(1);
      for (unsigned j=0; j<fd->locals.size(); j++)
        {
	  vardecl* v = fd->locals[j];
	  try 
	    {
	      o->newline() << c_typename (v->type) << " " 
			   << c_varname (v->name) << ";";
	    } catch (const semantic_error& e) {
	      semantic_error e2 (e);
	      if (e2.tok1 == 0) e2.tok1 = v->tok;
	      throw e2;
	    }
        }
      for (unsigned j=0; j<fd->formal_args.size(); j++)
        {
          vardecl* v = fd->formal_args[j];
	  try 
	    {
	      o->newline() << c_typename (v->type) << " " 
			   << c_varname (v->name) << ";";
	    } catch (const semantic_error& e) {
	      semantic_error e2 (e);
	      if (e2.tok1 == 0) e2.tok1 = v->tok;
	      throw e2;
	    }
        }
      c_tmpcounter ct (this);
      fd->body->visit (& ct);
      if (fd->type == pe_unknown)
	o->newline() << "/* no return value */";
      else
	{
	  o->newline() << c_typename (fd->type) << " __retvalue;";
	}
      o->newline(-1) << "} function_" << c_varname (fd->name) << ";";
    }
  o->newline(-1) << "} locals [MAXNESTING];";
  o->newline(-1) << "};\n";
  o->newline() << "void *contexts = NULL; /* alloc_percpu */\n";

  emit_map_type_instantiations ();

  if (!session->stat_decls.empty())
    o->newline() << "#include \"stat.c\"\n";
}


void
c_unparser::emit_global_param (vardecl *v)
{
  string vn = c_varname (v->name);

  // NB: systemtap globals can collide with linux macros,
  // e.g. VM_FAULT_MAJOR.  We want the parameter name anyway.  This
  // #undef is spit out at the end of the C file, so that removing the
  // definition won't affect any other embedded-C or generated code.
  // XXX: better not have a global variable named module_param_named etc.!
  o->newline() << "#undef " << vn;

  // Emit module_params for this global, if its type is convenient.
  if (v->arity == 0 && v->type == pe_long)
    {
      o->newline() << "module_param_named (" << vn << ", "
                   << "init_global_" << vn << ", long, 0);";
    }
  else if (v->arity == 0 && v->type == pe_string)
    {
      // NB: no special copying is needed.
      o->newline() << "module_param_string (" << vn << ", "
                   << "global_" << vn
                   << ", MAXSTRINGLEN, 0);";
    }
}


void
c_unparser::emit_global (vardecl *v)
{
  string vn = c_varname (v->name);

  if (v->arity == 0)
    o->newline() << "static __cacheline_aligned "
		 << c_typename (v->type)
		 << " "
		 << "global_" << vn
		 << ";";
  else if (v->type == pe_stats)
    {
      o->newline() << "static __cacheline_aligned PMAP global_" 
		   << vn << ";";
    }
  else
    {
      o->newline() << "static __cacheline_aligned MAP global_" 
		   << vn << ";";
    }
  o->newline() << "static __cacheline_aligned rwlock_t "
               << "global_" << vn << "_lock;";

  // Emit module_param helper variable
  if (v->arity == 0 && v->type == pe_long)
    {
      // XXX: moduleparam.h does not have a 64-bit type, so let's just
      // take a plain long here, and manually copy/widen during
      // initialization.  See var::init().
      o->newline() << "long init_global_" << vn << ";";
    }
}


void
c_unparser::emit_functionsig (functiondecl* v)
{
  o->newline() << "static void function_" << v->name
	       << " (struct context * __restrict__ c);";
}


void
c_unparser::emit_module_init ()
{
  // Emit the per-probe-point registrations into individual functions,
  // to avoid forcing the compiler to work too hard at optimizing such
  // a silly function.  A "don't optimize this function" pragma could
  // come in handy too.
  for (unsigned i=0; i<session->probes.size(); i++)
    {
      o->newline() << "noinline int register_probe_" << i << " (void) {";
      o->indent(1);
      // By default, mark the first location as the site of possible
      // registration failure.  This is helpful since non-dwarf
      // derived_probes tend to have only a single location.
      assert (session->probes[i]->locations.size() > 0);
      o->newline() << "int rc = 0;";
      o->newline() << "const char *probe_point = " <<
        lex_cast_qstring (*session->probes[i]->locations[0]) << ";";
      session->probes[i]->emit_registrations (o, i);

      o->newline() << "if (unlikely (rc)) {";
      // In case it's just a lower-layer (kprobes) error that set rc
      // but not session_state, do that here to prevent any other BEGIN
      // probe from attempting to run.
      o->newline(1) << "atomic_set (&session_state, STAP_SESSION_ERROR);";

      o->newline() << "_stp_error (\"probe " << i << " registration failed"
                   << ", rc=%d, %s\\n\", rc, probe_point);";
      o->newline(-1) << "}";

      o->newline() << "return rc;";
      o->newline(-1) << "}";
      
      o->newline();
      o->newline() << "noinline void unregister_probe_" << i << " (void) {";
      o->indent(1);
      session->probes[i]->emit_deregistrations (o, i);
      o->newline(-1) << "}";
    }

  o->newline();
  o->newline() << "int systemtap_module_init (void) {";
  o->newline(1) << "int rc = 0;";
  o->newline() << "const char *probe_point = \"\";";

  o->newline() << "(void) probe_point;";
  o->newline() << "atomic_set (&session_state, STAP_SESSION_STARTING);";
  // This signals any other probes that may be invoked in the next little
  // while to abort right away.  Currently running probes are allowed to
  // terminate.  These may set STAP_SESSION_ERROR!

  // per-cpu context
  o->newline() << "if (sizeof (struct context) <= 131072)";
  o->newline(1) << "contexts = alloc_percpu (struct context);";
  o->newline(-1) << "if (contexts == NULL) {";
  o->newline(1) << "_stp_error (\"percpu context (size %lu) allocation failed\", sizeof (struct context));";
  o->newline() << "rc = -ENOMEM;";
  o->newline() << "goto out;";
  o->newline(-1) << "}";

  for (unsigned i=0; i<session->globals.size(); i++)
    {
      // XXX: handle failure!
      vardecl* v = session->globals[i];      
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).init();
      else
	o->newline() << getvar (v).init();
      o->newline() << "rwlock_init (& global_" << c_varname (v->name) << "_lock);";
    }

  for (unsigned i=0; i<session->probes.size(); i++)
    {
      o->newline() << "rc = register_probe_" << i << "();";
      o->newline() << "if (rc)";
      o->indent(1);
      // We need to deregister any already probes set up - this is
      // essential for kprobes.
      if (i > 0)
        o->newline() << "goto unregister_" << (i-1) << ";";
      else
        o->newline() << "goto out;";
      o->indent(-1);
    }

  // BEGIN probes would have all been run by now.  One of them may
  // have triggered a STAP_SESSION_ERROR (which would incidentally
  // block later BEGIN ones).  If so, let that indication stay, and
  // otherwise act like probe insertion was a success.
  o->newline() << "if (atomic_read (&session_state) == STAP_SESSION_STARTING)";
  o->newline(1) << "atomic_set (&session_state, STAP_SESSION_RUNNING);";
  o->newline(-1) << "goto out;";

  // Recovery code for partially successful registration (rc != 0)
  // XXX: Do we need to delay here to ensure any triggered probes have
  // terminated?  Probably not much, as they should all test for
  // SESSION_STARTING state right at the top and return.  ("begin"
  // probes don't count, as they return synchronously.)
  o->newline();
  for (int i=session->probes.size()-2; i >= 0; i--) // NB: -2
    {
      o->newline(-1) << "unregister_" << i << ":";
      o->newline(1) << "unregister_probe_" << i << "();";
      // NB: This may be an END probe.  It will refuse to run
      // if the session_state was ERRORed.
    }  
  o->newline();

  // If any registrations failed, we will need to deregister the globals,
  // as this is our only chance.
  for (unsigned i=0; i<session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];      
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).fini();
    }

  o->newline(-1) << "out:";
  o->newline(1) << "return rc;";
  o->newline(-1) << "}\n";
}


void
c_unparser::emit_module_exit ()
{
  o->newline() << "void systemtap_module_exit (void) {";
  // rc?
  o->newline(1) << "int holdon;";

  // If we aborted startup, then everything has been cleaned up already, and
  // module_exit shouldn't even have been called.  But since it might be, let's
  // beat a hasty retreat to avoid double uninitialization.
  o->newline() << "if (atomic_read (&session_state) == STAP_SESSION_STARTING)";
  o->newline(1) << "return;";
  o->indent(-1);
  
  o->newline() << "if (atomic_read (&session_state) == STAP_SESSION_RUNNING)";
  // NB: only other valid state value is ERROR, in which case we don't 
  o->newline(1) << "atomic_set (&session_state, STAP_SESSION_STOPPING);";
  o->indent(-1);
  // This signals any other probes that may be invoked in the next little
  // while to abort right away.  Currently running probes are allowed to
  // terminate.  These may set STAP_SESSION_ERROR!

  // NB: systemtap_module_exit is assumed to be called from ordinary
  // user context, say during module unload.  Among other things, this
  // means we can sleep a while.
  o->newline() << "do {";
  o->newline(1) << "int i;";
  o->newline() << "holdon = 0;";
  o->newline() << "for (i=0; i < NR_CPUS; i++)";
  o->newline(1) << "if (cpu_possible (i) && " 
                << "atomic_read (& ((struct context *)per_cpu_ptr(contexts, i))->busy)) "
                << "holdon = 1;";
  // o->newline(-1) << "if (holdon) msleep (5);";
  o->newline(-1) << "} while (holdon);";
  o->newline(-1);
  // XXX: might like to have an escape hatch, in case some probe is
  // genuinely stuck somehow

  for (int i=session->probes.size()-1; i>=0; i--)
    o->newline() << "unregister_probe_" << i << "();"; // NB: runs "end" probes

  for (unsigned i=0; i<session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];      
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).fini();
    }

  o->newline() << "free_percpu (contexts);";

  // print final error/reentrancy counts if non-zero
  o->newline() << "if (atomic_read (& skipped_count) || "
               << "atomic_read (& error_count))";
  o->newline(1) << "_stp_warn (\"Number of errors: %d, "
                << "skipped probes: %d\\n\", "
                << "(int) atomic_read (& error_count), "
                << "(int) atomic_read (& skipped_count));";
  o->indent(-1);

  o->newline(-1) << "}\n";
}


void
c_unparser::emit_function (functiondecl* v)
{
  o->newline() << "void function_" << c_varname (v->name)
            << " (struct context* __restrict__ c) {";
  o->indent(1);
  this->current_probe = 0;
  this->current_probenum = 0;
  this->current_function = v;
  this->tmpvar_counter = 0;

  o->newline()
    << "struct function_" << c_varname (v->name) << "_locals * "
    << " __restrict__ l =";
  o->newline(1)
    << "& c->locals[c->nesting].function_" << c_varname (v->name)
    << ";";
  o->newline(-1) << "(void) l;"; // make sure "l" is marked used
  o->newline() << "#define CONTEXT c";
  o->newline() << "#define THIS l";
  o->newline() << "if (0) goto out;"; // make sure out: is marked used

  // initialize locals
  // XXX: optimization: use memset instead
  for (unsigned i=0; i<v->locals.size(); i++)
    {
      if (v->locals[i]->index_types.size() > 0) // array?
	throw semantic_error ("array locals not supported", v->locals[i]->tok);

      o->newline() << getvar (v->locals[i]).init();
    }

  // initialize return value, if any
  if (v->type != pe_unknown)
    {
      var retvalue = var(true, v->type, "__retvalue");
      o->newline() << retvalue.init();
    }

  v->body->visit (this);

  this->current_function = 0;

  o->newline(-1) << "out:";
  o->newline(1) << ";";

  o->newline() << "#undef CONTEXT";
  o->newline() << "#undef THIS";
  o->newline(-1) << "}\n";
}


void
c_unparser::emit_probe (derived_probe* v, unsigned i)
{
  this->current_function = 0;
  this->current_probe = v;
  this->current_probenum = i;
  this->tmpvar_counter = 0;

  // o->newline() << "static void probe_" << i << " (struct context *c);";
  o->newline() << "void probe_" << i << " (struct context * __restrict__ c) {";
  o->indent(1);

  // initialize frame pointer
  o->newline() << "struct probe_" << i << "_locals * __restrict__ l =";
  o->newline(1) << "& c->locals[c->nesting].probe_" << i << ";";
  o->newline(-1) << "(void) l;"; // make sure "l" is marked used

  // emit all read/write locks for global variables
  varuse_collecting_visitor vut;
  v->body->visit (& vut);
  emit_locks (vut);

  // initialize locals
  for (unsigned j=0; j<v->locals.size(); j++)
    {
      if (v->locals[j]->index_types.size() > 0) // array?
	throw semantic_error ("array locals not supported", v->tok);
      else if (v->locals[j]->type == pe_long)
	o->newline() << "l->" << c_varname (v->locals[j]->name)
		     << " = 0;";
      else if (v->locals[j]->type == pe_string)
	o->newline() << "l->" << c_varname (v->locals[j]->name)
		     << "[0] = '\\0';";
      else
	throw semantic_error ("unsupported local variable type",
			      v->locals[j]->tok);
    }
  
  v->body->visit (this);

  o->newline(-1) << "out:";
  // NB: no need to uninitialize locals, except if arrays can somedays be local
  o->newline(1) << "_stp_print_flush();";

  emit_unlocks (vut);

  o->newline(-1) << "}\n";
  
  this->current_probe = 0;
  this->current_probenum = 0; // not essential

  v->emit_probe_entries (o, i);
}


void 
c_unparser::emit_locks(const varuse_collecting_visitor& vut)
{
  o->newline() << "{";
  o->newline(1) << "unsigned numtrylock = 0;";
  o->newline() << "(void) numtrylock;";

  string last_locked_var;
  for (unsigned i = 0; i < session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];
      bool read_p = vut.read.find(v) != vut.read.end();
      bool write_p = vut.written.find(v) != vut.written.end();
      if (!read_p && !write_p) continue;

      if (v->type == pe_stats) // read and write locks are flipped
        // Specifically, a "<<<" to a stats object is considered a
        // "shared-lock" operation, since it's implicitly done
        // per-cpu.  But a "@op(x)" extraction is an "exclusive-lock"
        // one, as is a (sorted or unsorted) foreach, so those cases
        // are excluded by the w & !r condition below.
        {
          if (write_p && !read_p) { read_p = true; write_p = false; }
          else if (read_p && !write_p) { read_p = false; write_p = true; }
        }

      string lockcall = 
        string (write_p ? "write" : "read") +
        "_trylock (& global_" + v->name + "_lock)";

      o->newline() << "while (! " << lockcall
                   << "&& (++numtrylock < MAXTRYLOCK))";
      o->newline(1) << "ndelay (TRYLOCKDELAY);";
      o->newline(-1) << "if (unlikely (numtrylock >= MAXTRYLOCK)) {";
      o->newline(1) << "atomic_inc (& skipped_count);";
      // The following works even if i==0.  Note that using
      // globals[i-1]->name is wrong since that global may not have
      // been lockworthy by this probe.
      o->newline() << "goto unlock_" << last_locked_var << ";";
      o->newline(-1) << "}";

      last_locked_var = v->name;
    }

  o->newline() << "if (0) goto unlock_;";

  o->newline(-1) << "}";
}


void 
c_unparser::emit_unlocks(const varuse_collecting_visitor& vut)
{
  unsigned numvars = 0;

  if (session->verbose>1)
    clog << "Probe #" << current_probenum << " locks ";

  for (int i = session->globals.size()-1; i>=0; i--) // in reverse order!
    {
      vardecl* v = session->globals[i];
      bool read_p = vut.read.find(v) != vut.read.end();
      bool write_p = vut.written.find(v) != vut.written.end();
      if (!read_p && !write_p) continue;

      numvars ++;
      o->newline(-1) << "unlock_" << v->name << ":";
      o->indent(1);

      // Duplicate lock flipping logic from above
      if (v->type == pe_stats)
        {
          if (write_p && !read_p) { read_p = true; write_p = false; }
          else if (read_p && !write_p) { read_p = false; write_p = true; }
        }

      if (session->verbose>1)
        clog << v->name << "[" << (read_p ? "r" : "")
             << (write_p ? "w" : "")  << "] ";

      if (write_p) // emit write lock
        o->newline() << "write_unlock (& global_" << v->name << "_lock);";
      else // (read_p && !write_p) : emit read lock
        o->newline() << "read_unlock (& global_" << v->name << "_lock);";

      // fall through to next variable; thus the reverse ordering
    }
  
  // emit plain "unlock" label, used if the very first lock failed.
  o->newline(-1) << "unlock_: ;";
  o->indent(1);

  if (numvars) // is there a chance that any lock attempt failed?
    {
      o->newline() << "if (atomic_read (& skipped_count) > MAXSKIPPED) {";
      // XXX: In this known non-reentrant context, we could print a more
      // informative error.
      o->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      o->newline() << "_stp_exit();";
      o->newline(-1) << "}";

      if (session->verbose>1)
        clog << endl;
    }
  else if (session->verbose>1)
    clog << "nothing" << endl;
}


void 
c_unparser::collect_map_index_types(vector<vardecl *> const & vars,
				    set< pair<vector<exp_type>, exp_type> > & types)
{
  for (unsigned i = 0; i < vars.size(); ++i)
    {
      vardecl *v = vars[i];
      if (v->arity > 0)
	{
	  types.insert(make_pair(v->index_types, v->type));
	}
    }
}

string
mapvar::value_typename(exp_type e)
{
  switch (e)
    {
    case pe_long:
      return "INT64";
      break;
    case pe_string:
      return "STRING";
      break;
    case pe_stats:
      return "STAT";
      break;
    default:
      throw semantic_error("array type is neither string nor long");
      break;
    }	      
}

string
mapvar::key_typename(exp_type e)
{
  switch (e)
    {
    case pe_long:
      return "INT64";
      break;
    case pe_string:
      return "STRING";
      break;
    default:
      throw semantic_error("array key is neither string nor long");
      break;
    }	      
}

string
mapvar::shortname(exp_type e)
{
  switch (e)
    {
    case pe_long:
      return "i";
      break;
    case pe_string:
      return "s";
      break;
    default:
      throw semantic_error("array type is neither string nor long");
      break;
    }	      
}


void
c_unparser::emit_map_type_instantiations ()
{
  set< pair<vector<exp_type>, exp_type> > types;
  
  collect_map_index_types(session->globals, types);

  for (unsigned i = 0; i < session->probes.size(); ++i)
    collect_map_index_types(session->probes[i]->locals, types);

  for (unsigned i = 0; i < session->functions.size(); ++i)
    collect_map_index_types(session->functions[i]->locals, types);

  if (!types.empty())
    o->newline() << "#include \"alloc.c\"";

  for (set< pair<vector<exp_type>, exp_type> >::const_iterator i = types.begin();
       i != types.end(); ++i)
    {
      o->newline() << "#define VALUE_TYPE " << mapvar::value_typename(i->second);
      for (unsigned j = 0; j < i->first.size(); ++j)
	{
	  string ktype = mapvar::key_typename(i->first.at(j));
	  o->newline() << "#define KEY" << (j+1) << "_TYPE " << ktype;
	}
      if (i->second == pe_stats)
	o->newline() << "#include \"pmap-gen.c\"";
      else
	o->newline() << "#include \"map-gen.c\"";
      o->newline() << "#undef VALUE_TYPE";
      for (unsigned j = 0; j < i->first.size(); ++j)
	{
	  o->newline() << "#undef KEY" << (j+1) << "_TYPE";
	}      

      /* FIXME
       * For pmaps, we also need to include map-gen.c, because we might be accessing
       * the aggregated map.  The better way to handle this is for pmap-gen.c to make
       * this include, but that's impossible with the way they are set up now.
       */
      if (i->second == pe_stats)
	{
	  o->newline() << "#define VALUE_TYPE " << mapvar::value_typename(i->second);
	  for (unsigned j = 0; j < i->first.size(); ++j)
	    {
	      string ktype = mapvar::key_typename(i->first.at(j));
	      o->newline() << "#define KEY" << (j+1) << "_TYPE " << ktype;
	    }
	  o->newline() << "#include \"map-gen.c\"";
	  o->newline() << "#undef VALUE_TYPE";
	  for (unsigned j = 0; j < i->first.size(); ++j)
	    {
	      o->newline() << "#undef KEY" << (j+1) << "_TYPE";
	    }      
	}
    }

  if (!types.empty())
    o->newline() << "#include \"map.c\"";

};


string
c_unparser::c_typename (exp_type e)
{
  switch (e)
    {
    case pe_long: return string("int64_t");
    case pe_string: return string("string_t"); 
    case pe_stats: return string("Stat");
    case pe_unknown: 
    default:
      throw semantic_error ("cannot expand unknown type");
    }
}


string
c_unparser::c_varname (const string& e)
{
  // XXX: safeify, uniquefy, given name
  return e;
}

void 
c_unparser::c_assign (var& lvalue, const string& rvalue, const token *tok)
{  
  switch (lvalue.type())
    {
    case pe_string:
      c_strcpy(lvalue.qname(), rvalue);
      break;
    case pe_long:
      o->newline() << lvalue << " = " << rvalue << ";";
      break;
    default:
      throw semantic_error ("unknown lvalue type in assignment", tok);
    }
}

void
c_unparser::c_assign (const string& lvalue, expression* rvalue,
		      const string& msg)
{
  if (rvalue->type == pe_long)
    {
      o->newline() << lvalue << " = ";
      rvalue->visit (this);
      o->line() << ";";
    }
  else if (rvalue->type == pe_string)
    {
      c_strcpy (lvalue, rvalue);
    }
  else
    {
      string fullmsg = msg + " type unsupported";
      throw semantic_error (fullmsg, rvalue->tok);
    }
}


void
c_unparser::c_assign (const string& lvalue, const string& rvalue,
		      exp_type type, const string& msg, const token* tok)
{
  if (type == pe_long)
    {
      o->newline() << lvalue << " = " << rvalue << ";";
    }
  else if (type == pe_string)
    {
      c_strcpy (lvalue, rvalue);
    }
  else
    {
      string fullmsg = msg + " type unsupported";
      throw semantic_error (fullmsg, tok);
    }
}


void 
c_unparser_assignment::c_assignop(tmpvar & res, 
				  var const & lval, 
				  tmpvar const & rval,
				  token const * tok)
{
  // This is common code used by scalar and array-element assignments.
  // It assumes an operator-and-assignment (defined by the 'pre' and
  // 'op' fields of c_unparser_assignment) is taking place between the
  // following set of variables:
  //
  // _res: the result of evaluating the expression, a temporary
  // lval: the lvalue of the expression, which may be damaged
  // rval: the rvalue of the expression, which is a temporary

  // we'd like to work with a local tmpvar so we can overwrite it in 
  // some optimized cases

  translator_output* o = parent->o;

  if (res.type() == pe_string)
    {
      if (post)
	throw semantic_error ("post assignment on strings not supported", 
			      tok);
      if (op == "=")
	{
	  parent->c_strcpy (lval.qname(), rval.qname());
	  // no need for second copy
	  res = rval;
	}
      else if (op == ".=")
	{
	  // shortcut two-step construction of concatenated string in
	  // empty res, then copy to a: instead concatenate to a
	  // directly, then copy back to res
	  parent->c_strcat (lval.qname(), rval.qname());
	  parent->c_strcpy (res.qname(), lval.qname());
	}
      else
	throw semantic_error ("string assignment operator " +
			      op + " unsupported", tok);
    }
  else if (op == "<<<")
    {
      assert(lval.type() == pe_stats);
      assert(rval.type() == pe_long);
      assert(res.type() == pe_long);
      o->newline() << res << " = " << rval << ";";
      o->newline() << "_stp_stat_add (" << lval << ", " << res << ");";
    }
  else if (res.type() == pe_long)
    {
      // a lot of operators come through this "gate":
      // - vanilla assignment "="
      // - stats aggregation "<<<"
      // - modify-accumulate "+=" and many friends
      // - pre/post-crement "++"/"--"
      // - "/" and "%" operators, but these need special handling in kernel

      // compute the modify portion of a modify-accumulate
      string macop;
      unsigned oplen = op.size();
      if (op == "=")
	macop = "*error*"; // special shortcuts below
      else if (oplen > 1 && op[oplen-1] == '=') // for +=, %=, <<=, etc...
	macop = op.substr(0, oplen-1);
      else if (op == "++")
	macop = "+";
      else if (op == "--")
	macop = "-";
      else
	// internal error
	throw semantic_error ("unknown macop for assignment", tok);

      if (post)
	{
          if (macop == "/" || macop == "%" || op == "=")
            throw semantic_error ("invalid post-mode operator", tok);

	  o->newline() << res << " = " << lval << ";";
          o->newline() << lval << " = " << res << " " << macop << " " << rval << ";";
	}
      else
	{
          if (op == "=") // shortcut simple assignment
            {
              o->newline() << lval << " = " << rval << ";";
              res = rval;
            }
          else
            {
              if (macop == "/")
                o->newline() << res << " = _stp_div64 (&c->last_error, "
                             << lval << ", " << rval << ");";
              else if (macop == "%")
                o->newline() << res << " = _stp_mod64 (&c->last_error, "
                             << lval << ", " << rval << ");";
              else
                o->newline() << res << " = " << lval << " " << macop << " " << rval << ";";

              o->newline() << lval << " = " << res << ";";
            }
	}
    }
    else
      throw semantic_error ("assignment type not yet implemented", tok);
}


void 
c_unparser::c_declare(exp_type ty, const string &name) 
{
  o->newline() << c_typename (ty) << " " << c_varname (name) << ";";
}


void 
c_unparser::c_declare_static(exp_type ty, const string &name) 
{
  o->newline() << "static " << c_typename (ty) << " " << c_varname (name) << ";";
}


void 
c_unparser::c_strcpy (const string& lvalue, const string& rvalue) 
{
  o->newline() << "strlcpy (" 
		   << lvalue << ", " 
		   << rvalue << ", MAXSTRINGLEN);";
}


void 
c_unparser::c_strcpy (const string& lvalue, expression* rvalue) 
{
  o->newline() << "strlcpy (" << lvalue << ", ";
  rvalue->visit (this);
  o->line() << ", MAXSTRINGLEN);";
}


void 
c_unparser::c_strcat (const string& lvalue, const string& rvalue) 
{
  o->newline() << "strlcat (" 
	       << lvalue << ", " 
	       << rvalue << ", MAXSTRINGLEN);";
}


void 
c_unparser::c_strcat (const string& lvalue, expression* rvalue) 
{
  o->newline() << "strlcat (" << lvalue << ", ";
  rvalue->visit (this);
  o->line() << ", MAXSTRINGLEN);";
}


bool
c_unparser::is_local(vardecl const *r, token const *tok)
{  
  if (current_probe)
    {
      for (unsigned i=0; i<current_probe->locals.size(); i++)
	{
	  if (current_probe->locals[i] == r)
	    return true;
	}
    }
  else if (current_function)
    {
      for (unsigned i=0; i<current_function->locals.size(); i++)
	{
	  if (current_function->locals[i] == r)
	    return true;
	}

      for (unsigned i=0; i<current_function->formal_args.size(); i++)
	{
	  if (current_function->formal_args[i] == r)
	    return true;
	}
    }

  for (unsigned i=0; i<session->globals.size(); i++)
    {
      if (session->globals[i] == r)
	return false;
    }
  
  if (tok)
    throw semantic_error ("unresolved symbol", tok);
  else
    throw semantic_error ("unresolved symbol: " + r->name);
}


tmpvar 
c_unparser::gensym(exp_type ty) 
{ 
  return tmpvar (ty, tmpvar_counter); 
}

aggvar 
c_unparser::gensym_aggregate() 
{ 
  return aggvar (tmpvar_counter); 
}


var 
c_unparser::getvar(vardecl *v, token const *tok) 
{ 
  bool loc = is_local (v, tok);
  if (loc)    
    return var (loc, v->type, v->name);
  else
    {
      statistic_decl sd;
      std::map<std::string, statistic_decl>::const_iterator i;
      i = session->stat_decls.find(v->name);
      if (i != session->stat_decls.end())
	sd = i->second;
      return var (loc, v->type, sd, v->name);
    }
}


mapvar 
c_unparser::getmap(vardecl *v, token const *tok) 
{   
  if (v->arity < 1)
    throw semantic_error("attempt to use scalar where map expected", tok);
  statistic_decl sd;
  std::map<std::string, statistic_decl>::const_iterator i;
  i = session->stat_decls.find(v->name);
  if (i != session->stat_decls.end())
    sd = i->second;
  return mapvar (is_local (v, tok), v->type, sd, v->name, v->index_types);
}


itervar 
c_unparser::getiter(symbol *s)
{ 
  return itervar (s, tmpvar_counter);
}



// An artificial common "header" for each statement.  This is where
// activity counts limits and error state early exits are enforced.
void
c_unparser::visit_statement (statement *s, unsigned actions)
{
  // For some constructs, it is important to avoid an error branch
  // right to the bottom of the probe/function.  The foreach() locking
  // construct is one example.  Instead, if we are nested within a
  // loop, we branch merely to its "break" label.  The next statement
  // will branch one level higher, and so on, until we can go straight
  // "out".
  string outlabel = "out";
  unsigned loops = loop_break_labels.size();
  if (loops > 0)
    outlabel = loop_break_labels[loops-1];

  o->newline() << "if (unlikely (c->last_error)) goto " << outlabel << ";";
  assert (s->tok);
  o->newline() << "c->last_stmt = " << lex_cast_qstring(*s->tok) << ";";
  if (actions > 0)
    {
      o->newline() << "c->actioncount += " << actions << ";";
      // XXX: This check is inserted too frequently.
      o->newline() << "if (unlikely (c->actioncount > MAXACTION)) {";
      o->newline(1) << "c->last_error = \"MAXACTION exceeded\";";
      o->newline() << "goto " << outlabel << ";";
      o->newline(-1) << "}";
    }
}


void
c_unparser::visit_block (block *s)
{
  o->newline() << "{";
  o->indent (1);
  visit_statement (s, 0);

  for (unsigned i=0; i<s->statements.size(); i++)
    {
      try
        {
          s->statements[i]->visit (this);
	  o->newline();
        }
      catch (const semantic_error& e)
        {
          session->print_error (e);
        }
    }
  o->newline(-1) << "}";
}


void
c_unparser::visit_embeddedcode (embeddedcode *s)
{
  visit_statement (s, 1);
  o->newline() << "{";
  o->newline(1) << s->code;
  o->newline(-1) << "}";
}


void
c_unparser::visit_null_statement (null_statement *s)
{
  visit_statement (s, 0);
  o->newline() << "/* null */;";
}


void
c_unparser::visit_expr_statement (expr_statement *s)
{
  visit_statement (s, 1);
  o->newline() << "(void) ";
  s->value->visit (this);
  o->line() << ";";
}


void
c_unparser::visit_if_statement (if_statement *s)
{
  visit_statement (s, 1);
  o->newline() << "if (";
  o->indent (1);
  s->condition->visit (this);
  o->indent (-1);
  o->line() << ") {";
  o->indent (1);
  s->thenblock->visit (this);
  o->newline(-1) << "}";
  if (s->elseblock)
    {
      o->newline() << "else {";
      o->indent (1);
      s->elseblock->visit (this);
      o->newline(-1) << "}";
    }
}


void
c_tmpcounter::visit_block (block *s)
{
  // Key insight: individual statements of a block can reuse
  // temporary variable slots, since temporaries don't survive
  // statement boundaries.  So we use gcc's anonymous union/struct
  // facility to explicitly overlay the temporaries.
  parent->o->newline() << "union {";
  parent->o->indent(1);
  for (unsigned i=0; i<s->statements.size(); i++)
    {
      parent->o->newline() << "struct {";
      parent->o->indent(1);
      s->statements[i]->visit (this);
      parent->o->newline(-1) << "};";
    }
  parent->o->newline(-1) << "};";
}

void
c_tmpcounter::visit_for_loop (for_loop *s)
{
  if (s->init) s->init->visit (this);
  s->cond->visit (this);
  s->block->visit (this);
  if (s->incr) s->incr->visit (this);
}


void
c_unparser::visit_for_loop (for_loop *s)
{
  visit_statement (s, 1);

  string ctr = stringify (label_counter++);
  string toplabel = "top_" + ctr;
  string contlabel = "continue_" + ctr;
  string breaklabel = "break_" + ctr;

  // initialization
  if (s->init) s->init->visit (this);

  // condition
  o->newline(-1) << toplabel << ":";
  o->newline(1) << "if (! (";
  if (s->cond->type != pe_long)
    throw semantic_error ("expected numeric type", s->cond->tok);
  s->cond->visit (this);
  o->line() << ")) goto " << breaklabel << ";";

  // body
  loop_break_labels.push_back (breaklabel);
  loop_continue_labels.push_back (contlabel);
  s->block->visit (this);
  loop_break_labels.pop_back ();
  loop_continue_labels.pop_back ();

  // iteration
  o->newline(-1) << contlabel << ":";
  o->indent(1);
  if (s->incr) s->incr->visit (this);
  o->newline() << "goto " << toplabel << ";";

  // exit
  o->newline(-1) << breaklabel << ":";
  o->newline(1) << "; /* dummy statement */";
}


struct arrayindex_downcaster
  : public traversing_visitor
{
  arrayindex *& arr;
  
  arrayindex_downcaster (arrayindex *& arr)
    : arr(arr) 
  {}

  void visit_arrayindex (arrayindex* e)
  {
    arr = e;
  }
};


static bool
expression_is_arrayindex (expression *e, 
			  arrayindex *& hist)
{
  arrayindex *h = NULL;
  arrayindex_downcaster d(h);
  e->visit (&d);
  if (static_cast<void*>(h) == static_cast<void*>(e))
    {
      hist = h;
      return true;
    }
  return false;
}


void
c_tmpcounter::visit_foreach_loop (foreach_loop *s)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (s->base, array, hist);

  if (array)
    {
      itervar iv = parent->getiter (array);
      parent->o->newline() << iv.declare();
    }
  else
   { 
     // See commentary in c_tmpcounter::visit_arrayindex for
     // discussion of tmpvars required to look into @hist_op(...)
     // expressions.

     // First make sure we have exactly one pe_long variable to use as
     // our bucket index.
     
     if (s->indexes.size() != 1 || s->indexes[0]->referent->type != pe_long)
       throw semantic_error("Invalid indexing of histogram", s->tok);
      
      // Then declare what we need to form the aggregate we're
      // iterating over, and all the tmpvars needed by our call to
      // load_aggregate().

      aggvar agg = parent->gensym_aggregate ();
      agg.declare(*(this->parent));

      symbol *sym = get_symbol_within_expression (hist->stat);
      var v = parent->getvar(sym->referent, sym->tok);
      if (sym->referent->arity != 0)
	{
	  arrayindex *arr = NULL;
	  if (!expression_is_arrayindex (hist->stat, arr))
	    throw semantic_error("expected arrayindex expression in iterated hist_op", s->tok);

	  for (unsigned i=0; i<sym->referent->index_types.size(); i++)
	    {	      
	      tmpvar ix = parent->gensym (sym->referent->index_types[i]);
	      ix.declare (*parent);
	      arr->indexes[i]->visit(this);
	    }
	}
    }

  s->block->visit (this);
}

void
c_unparser::visit_foreach_loop (foreach_loop *s)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (s->base, array, hist);

  if (array)
    {
      visit_statement (s, 1);
      
      mapvar mv = getmap (array->referent, s->tok);
      itervar iv = getiter (array);
      vector<var> keys;
      
      string ctr = stringify (label_counter++);
      string toplabel = "top_" + ctr;
      string contlabel = "continue_" + ctr;
      string breaklabel = "break_" + ctr;
      
      // NB: structure parallels for_loop
      
      // initialization

      // aggregate array if required
      if (mv.is_parallel())
	{
	  o->newline() << "if (unlikely(NULL == " << mv.calculate_aggregate() << "))";
	  o->newline(1) << "c->last_error = \"aggregation overflow in " << mv << "\";";
	  o->indent(-1);

	  // sort array if desired
	  if (s->sort_direction) {
	    o->newline() << "else"; // only sort if aggregation was ok
	    o->newline(1) << "_stp_map_sort (" << mv.fetch_existing_aggregate() << ", "
			  << s->sort_column << ", " << - s->sort_direction << ");";
	    o->indent(-1);
	  }
	}
      else
	{      
	  // sort array if desired
	  if (s->sort_direction)
	    {
	      o->newline() << "_stp_map_sort (" << mv.qname() << ", "
			   << s->sort_column << ", " << - s->sort_direction << ");";
	    }
	}

      // NB: sort direction sense is opposite in runtime, thus the negation
      
      if (mv.is_parallel())
	aggregations_active.insert(mv.qname());
      o->newline() << iv << " = " << iv.start (mv) << ";";
      
      // condition
      o->newline(-1) << toplabel << ":";
      o->newline(1) << "if (! (" << iv << ")) goto " << breaklabel << ";";
      
      // body
      loop_break_labels.push_back (breaklabel);
      loop_continue_labels.push_back (contlabel);
      o->newline() << "{";
      o->indent (1);
      for (unsigned i = 0; i < s->indexes.size(); ++i)
	{
	  // copy the iter values into the specified locals
	  var v = getvar (s->indexes[i]->referent);
	  c_assign (v, iv.get_key (v.type(), i), s->tok);
	}
      s->block->visit (this);
      o->newline(-1) << "}";
      loop_break_labels.pop_back ();
      loop_continue_labels.pop_back ();
      
      // iteration
      o->newline(-1) << contlabel << ":";
      o->newline(1) << iv << " = " << iv.next (mv) << ";";
      o->newline() << "goto " << toplabel << ";";
      
      // exit
      o->newline(-1) << breaklabel << ":";
      o->newline(1) << "; /* dummy statement */";

      if (mv.is_parallel())
	aggregations_active.erase(mv.qname());
    }
  else
    {
      // Iterating over buckets in a histogram.
      assert(s->indexes.size() == 1);
      assert(s->indexes[0]->referent->type == pe_long);
      var bucketvar = getvar (s->indexes[0]->referent);

      aggvar agg = gensym_aggregate ();
      load_aggregate(hist->stat, agg);

      symbol *sym = get_symbol_within_expression (hist->stat);
      var v = getvar(sym->referent, sym->tok);
      v.assert_hist_compatible(*hist);

      // XXX: break / continue don't work here yet
      o->newline() << "for (" << bucketvar << " = 0; " 
		   << bucketvar << " < " << v.buckets() << "; "
		   << bucketvar << "++) { ";
      o->newline(1);
      s->block->visit (this);
      o->newline(-1) << "}";
    }
}


void
c_unparser::visit_return_statement (return_statement* s)
{
  visit_statement (s, 1);

  if (current_function == 0)
    throw semantic_error ("cannot 'return' from probe", s->tok);

  if (s->value->type != current_function->type)
    throw semantic_error ("return type mismatch", current_function->tok,
                         "vs", s->tok);

  c_assign ("l->__retvalue", s->value, "return value");
  o->newline() << "c->last_error = \"\";";
  // NB: last_error needs to get reset to NULL in the caller
  // probe/function
}


void
c_unparser::visit_next_statement (next_statement* s)
{
  visit_statement (s, 1);

  if (current_probe == 0)
    throw semantic_error ("cannot 'next' from function", s->tok);

  o->newline() << "c->last_error = \"\";";
}


struct delete_statement_operand_tmp_visitor:
  public traversing_visitor
{
  c_tmpcounter *parent;
  delete_statement_operand_tmp_visitor (c_tmpcounter *p):
    parent (p)
  {}
  //void visit_symbol (symbol* e);
  void visit_arrayindex (arrayindex* e);
};


struct delete_statement_operand_visitor:
  public throwing_visitor
{
  c_unparser *parent;
  delete_statement_operand_visitor (c_unparser *p):
    throwing_visitor ("invalid operand of delete expression"),
    parent (p)
  {}
  void visit_symbol (symbol* e);
  void visit_arrayindex (arrayindex* e);
};

void 
delete_statement_operand_visitor::visit_symbol (symbol* e)
{
  if (e->referent->arity > 0)
    {
      mapvar mvar = parent->getmap(e->referent, e->tok);  
      /* NB: Memory deallocation/allocation operations
       are not generally safe.
      parent->o->newline() << mvar.fini ();
      parent->o->newline() << mvar.init ();  
      */
      if (mvar.is_parallel())
	parent->o->newline() << "_stp_pmap_clear (" << mvar.qname() << ");";
      else
	parent->o->newline() << "_stp_map_clear (" << mvar.qname() << ");";
    }
  else
    {
      var v = parent->getvar(e->referent, e->tok);  
      switch (e->type)
	{
	case pe_stats:
	  parent->o->newline() << "_stp_stat_clear (" << v.qname() << ");";
	  break;
	case pe_long:
	  parent->o->newline() << v.qname() << " = 0;";
	  break;
	case pe_string:
	  parent->o->newline() << v.qname() << "[0] = '\\0';";
	  break;
	case pe_unknown:
	default:
	  throw semantic_error("Cannot delete unknown expression type", e->tok);
	}
    }
}

void 
delete_statement_operand_tmp_visitor::visit_arrayindex (arrayindex* e)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      vardecl* r = array->referent;

      // One temporary per index dimension.
      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  tmpvar ix = parent->parent->gensym (r->index_types[i]);
	  ix.declare (*(parent->parent));
	  e->indexes[i]->visit(parent);
	}
    }
  else
    {
      throw semantic_error("cannot delete histogram bucket entries\n", e->tok);
    }
}

void 
delete_statement_operand_visitor::visit_arrayindex (arrayindex* e)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      vector<tmpvar> idx;
      parent->load_map_indices (e, idx);
      
      {
	mapvar mvar = parent->getmap (array->referent, e->tok);
	parent->o->newline() << mvar.del (idx) << ";";
      }
    }
  else
    {
      throw semantic_error("cannot delete histogram bucket entries\n", e->tok);
    }
}


void
c_tmpcounter::visit_delete_statement (delete_statement* s)
{
  delete_statement_operand_tmp_visitor dv (this);
  s->value->visit (&dv);
}


void
c_unparser::visit_delete_statement (delete_statement* s)
{
  visit_statement (s, 1);
  delete_statement_operand_visitor dv (this);
  s->value->visit (&dv);
}


void
c_unparser::visit_break_statement (break_statement* s)
{
  visit_statement (s, 1);
  if (loop_break_labels.size() == 0)
    throw semantic_error ("cannot 'break' outside loop", s->tok);

  string label = loop_break_labels[loop_break_labels.size()-1];
  o->newline() << "goto " << label << ";";
}


void
c_unparser::visit_continue_statement (continue_statement* s)
{
  visit_statement (s, 1);
  if (loop_continue_labels.size() == 0)
    throw semantic_error ("cannot 'continue' outside loop", s->tok);

  string label = loop_continue_labels[loop_continue_labels.size()-1];
  o->newline() << "goto " << label << ";";
}



void
c_unparser::visit_literal_string (literal_string* e)
{
  const string& v = e->value;
  o->line() << '"';
  for (unsigned i=0; i<v.size(); i++)
    if (v[i] == '"') // or other escapeworthy characters?
      o->line() << '\\' << '"';
    else
      o->line() << v[i];
  o->line() << '"';
}


void
c_unparser::visit_literal_number (literal_number* e)
{
  // This looks ugly, but tries to be warning-free on 32- and 64-bit
  // hosts.
  // NB: this needs to be signed!
  o->line() << "((int64_t)" << e->value << "LL)";
}


void
c_tmpcounter::visit_binary_expression (binary_expression* e)
{
  if (e->op == "/" || e->op == "%")
    {
      tmpvar left = parent->gensym (pe_long);
      tmpvar right = parent->gensym (pe_long);
      left.declare (*parent);
      right.declare (*parent);
    }

  e->left->visit (this);
  e->right->visit (this);
}


void
c_unparser::visit_binary_expression (binary_expression* e)
{
  if (e->type != pe_long ||
      e->left->type != pe_long ||
      e->right->type != pe_long)
    throw semantic_error ("expected numeric types", e->tok);
  
  if (e->op == "+" ||
      e->op == "-" ||
      e->op == "*" ||
      e->op == "&" ||
      e->op == "|" ||
      e->op == "^" ||
      e->op == "<<" ||
      e->op == ">>")
    {
      o->line() << "((";
      e->left->visit (this);
      o->line() << ") " << e->op << " (";
      e->right->visit (this);
      o->line() << "))";
    }
  else if (e->op == "/" ||
           e->op == "%")
    {
      // % and / need a division-by-zero check; and thus two temporaries
      // for proper evaluation order
      tmpvar left = gensym (pe_long);
      tmpvar right = gensym (pe_long);

      o->line() << "({";
      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
      o->newline(1) << left << " = ";
      e->left->visit (this);
      o->line() << ";";

      o->newline() << right << " = ";
      e->right->visit (this);
      o->line() << ";";

      o->newline() << ((e->op == "/") ? "_stp_div64" : "_stp_mod64")
                   << " (&c->last_error, " << left << ", " << right << ");";

      o->newline(-1) << "})";
    }
  else
    throw semantic_error ("operator not yet implemented", e->tok); 
}


void
c_unparser::visit_unary_expression (unary_expression* e)
{
  if (e->type != pe_long ||
      e->operand->type != pe_long)
    throw semantic_error ("expected numeric types", e->tok);

  o->line() << "(" << e->op << " (";
  e->operand->visit (this);
  o->line() << "))";
}

void
c_unparser::visit_logical_or_expr (logical_or_expr* e)
{
  if (e->type != pe_long ||
      e->left->type != pe_long ||
      e->right->type != pe_long)
    throw semantic_error ("expected numeric types", e->tok);

  o->line() << "((";
  e->left->visit (this);
  o->line() << ") " << e->op << " (";
  e->right->visit (this);
  o->line() << "))";
}


void
c_unparser::visit_logical_and_expr (logical_and_expr* e)
{
  if (e->type != pe_long ||
      e->left->type != pe_long ||
      e->right->type != pe_long)
    throw semantic_error ("expected numeric types", e->tok);

  o->line() << "((";
  e->left->visit (this);
  o->line() << ") " << e->op << " (";
  e->right->visit (this);
  o->line() << "))";
}


void 
c_tmpcounter::visit_array_in (array_in* e)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->operand->base, array, hist);
  
  if (array)
    {
      vardecl* r = array->referent;

      // One temporary per index dimension.
      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  tmpvar ix = parent->gensym (r->index_types[i]);
	  ix.declare (*parent);
	  e->operand->indexes[i]->visit(this);
	}
      
      // A boolean result.
      tmpvar res = parent->gensym (e->type);
      res.declare (*parent);
    }
  else
    {
      // By definition:
      //
      // 'foo in @hist_op(...)'  is true iff
      // '@hist_op(...)[foo]'    is nonzero
      //
      // so we just delegate to the latter call, since int64_t is also
      // our boolean type.
      e->operand->visit(this);
    }
}


void
c_unparser::visit_array_in (array_in* e)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->operand->base, array, hist);
  
  if (array)
    {
      stmt_expr block(*this);  
      
      vector<tmpvar> idx;
      load_map_indices (e->operand, idx);
      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
      
      tmpvar res = gensym (pe_long);
      mapvar mvar = getmap (array->referent, e->tok);
      c_assign (res, mvar.exists(idx), e->tok);

      o->newline() << res << ";";
    }
  else
    {
      // By definition:
      //
      // 'foo in @hist_op(...)'  is true iff
      // '@hist_op(...)[foo]'    is nonzero
      //
      // so we just delegate to the latter call, since int64_t is also
      // our boolean type.
      e->operand->visit(this);
    }
}


void
c_unparser::visit_comparison (comparison* e)
{
  o->line() << "(";

  if (e->left->type == pe_string)
    {
      if (e->left->type != pe_string ||
          e->right->type != pe_string)
        throw semantic_error ("expected string types", e->tok);

      o->line() << "strncmp (";
      e->left->visit (this);
      o->line() << ", ";
      e->right->visit (this);
      o->line() << ", MAXSTRINGLEN";
      o->line() << ") " << e->op << " 0";
    }
  else if (e->left->type == pe_long)
    {
      if (e->left->type != pe_long ||
          e->right->type != pe_long)
        throw semantic_error ("expected numeric types", e->tok);

      o->line() << "((";
      e->left->visit (this);
      o->line() << ") " << e->op << " (";
      e->right->visit (this);
      o->line() << "))";
    }
  else
    throw semantic_error ("unexpected type", e->left->tok);

  o->line() << ")";
}


void
c_tmpcounter::visit_concatenation (concatenation* e)
{
  tmpvar t = parent->gensym (e->type);
  t.declare (*parent);
  e->left->visit (this);
  e->right->visit (this);
}


void
c_unparser::visit_concatenation (concatenation* e)
{
  if (e->op != ".")
    throw semantic_error ("unexpected concatenation operator", e->tok);

  if (e->type != pe_string ||
      e->left->type != pe_string ||
      e->right->type != pe_string)
    throw semantic_error ("expected string types", e->tok);

  tmpvar t = gensym (e->type);
  
  o->line() << "({ ";
  o->indent(1);
  o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
  c_assign (t.qname(), e->left, "assignment");
  c_strcat (t.qname(), e->right);
  o->newline() << t << ";";
  o->newline(-1) << "})";
}


void
c_unparser::visit_ternary_expression (ternary_expression* e)
{
  if (e->cond->type != pe_long)
    throw semantic_error ("expected numeric condition", e->cond->tok);

  if (e->truevalue->type != e->falsevalue->type ||
      e->type != e->truevalue->type ||
      (e->truevalue->type != pe_long && e->truevalue->type != pe_string))
    throw semantic_error ("expected matching types", e->tok);

  o->line() << "((";
  e->cond->visit (this);
  o->line() << ") ? (";
  e->truevalue->visit (this);
  o->line() << ") : (";
  e->falsevalue->visit (this);
  o->line() << "))";
}


void
c_tmpcounter::visit_assignment (assignment *e)
{
  c_tmpcounter_assignment tav (this, e->op, e->right);
  e->left->visit (& tav);
}


void
c_unparser::visit_assignment (assignment* e)
{
  if (e->op == "<<<")
    {
      if (e->type != pe_long)
	throw semantic_error ("non-number <<< expression", e->tok);

      if (e->left->type != pe_stats)
	throw semantic_error ("non-stats left operand to <<< expression", e->left->tok);

      if (e->right->type != pe_long)
	throw semantic_error ("non-number right operand to <<< expression", e->right->tok);
	
    }
  else
    {
      if (e->type != e->left->type)
	throw semantic_error ("type mismatch", e->tok,
			      "vs", e->left->tok);
      if (e->right->type != e->left->type)
	throw semantic_error ("type mismatch", e->right->tok,
			      "vs", e->left->tok);
    }

  c_unparser_assignment tav (this, e->op, e->right);
  e->left->visit (& tav);
}


void
c_tmpcounter::visit_pre_crement (pre_crement* e)
{
  c_tmpcounter_assignment tav (this, e->op, 0);
  e->operand->visit (& tav);
}


void
c_unparser::visit_pre_crement (pre_crement* e)
{
  if (e->type != pe_long ||
      e->type != e->operand->type)
    throw semantic_error ("expected numeric type", e->tok);

  c_unparser_assignment tav (this, e->op, false);
  e->operand->visit (& tav);
}


void
c_tmpcounter::visit_post_crement (post_crement* e)
{
  c_tmpcounter_assignment tav (this, e->op, 0);
  e->operand->visit (& tav);
}


void
c_unparser::visit_post_crement (post_crement* e)
{
  if (e->type != pe_long ||
      e->type != e->operand->type)
    throw semantic_error ("expected numeric type", e->tok);

  c_unparser_assignment tav (this, e->op, true);
  e->operand->visit (& tav);
}


void
c_unparser::visit_symbol (symbol* e)
{
  vardecl* r = e->referent;

  if (r->index_types.size() != 0)
    throw semantic_error ("invalid reference to array", e->tok);

  var v = getvar(r, e->tok);
  o->line() << v;
}


// Assignment expansion is tricky.
//
// Because assignments are nestable expressions, we have
// to emit C constructs that are nestable expressions too.
// We have to evaluate the given expressions the proper number of times,
// including array indices.
// We have to lock the lvalue (if global) against concurrent modification,
// especially with modify-assignment operations (+=, ++).
// We have to check the rvalue (for division-by-zero checks).

// In the normal "pre=false" case, for (A op B) emit:
// ({ tmp = B; check(B); lock(A); res = A op tmp; A = res; unlock(A); res; })
// In the "pre=true" case, emit instead:
// ({ tmp = B; check(B); lock(A); res = A; A = res op tmp; unlock(A); res; })
//
// (op is the plain operator portion of a combined calculate/assignment:
// "+" for "+=", and so on.  It is in the "macop" variable below.)
//
// For array assignments, additional temporaries are used for each
// index, which are expanded before the "tmp=B" expression, in order
// to consistently order evaluation of lhs before rhs.
//

void
c_tmpcounter_assignment::visit_symbol (symbol *e)
{
  exp_type ty = rvalue ? rvalue->type : e->type;
  tmpvar tmp = parent->parent->gensym (ty);
  tmpvar res = parent->parent->gensym (ty);

  tmp.declare (*(parent->parent));

  if (op != "=") // simple assignment is shortcut both for strings and numbers
    res.declare (*(parent->parent));

  if (rvalue)
    rvalue->visit (parent);
}


void
c_unparser_assignment::prepare_rvalue (string const & op, 
				       tmpvar const & rval,
				       token const * tok)
{
  if (rvalue)
    parent->c_assign (rval.qname(), rvalue, "assignment");
  else
    {
      if (op == "++" || op == "--")
        parent->o->newline() << rval << " = 1;";
      else
        throw semantic_error ("need rvalue for assignment", tok);
    }
  // OPT: literal rvalues could be used without a tmp* copy
}

void
c_unparser_assignment::visit_symbol (symbol *e)
{
  stmt_expr block(*parent);

  if (e->referent->index_types.size() != 0)
    throw semantic_error ("unexpected reference to array", e->tok);

  parent->o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
  exp_type ty = rvalue ? rvalue->type : e->type;
  tmpvar rval = parent->gensym (ty);
  tmpvar res = parent->gensym (ty);

  prepare_rvalue (op, rval, e->tok);

  var lvar = parent->getvar (e->referent, e->tok);
  c_assignop (res, lvar, rval, e->tok);     

  parent->o->newline() << res << ";";
}


void 
c_unparser::visit_target_symbol (target_symbol* e)
{
  throw semantic_error("cannot translate general target-symbol expression");
}


void
c_unparser::load_map_indices(arrayindex *e,
			     vector<tmpvar> & idx)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      idx.clear();
      
      vardecl* r = array->referent;
      
      if (r->index_types.size() == 0 ||
	  r->index_types.size() != e->indexes.size())
	throw semantic_error ("invalid array reference", e->tok);
      
      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  if (r->index_types[i] != e->indexes[i]->type)
	    throw semantic_error ("array index type mismatch", e->indexes[i]->tok);
	  
	  tmpvar ix = gensym (r->index_types[i]);
	  o->newline() << "c->last_stmt = "
		       << lex_cast_qstring(*e->indexes[i]->tok) << ";";
	  c_assign (ix.qname(), e->indexes[i], "array index copy");
	  idx.push_back (ix);
	}
    }
  else
    {
      assert (e->indexes.size() == 1);
      assert (e->indexes[0]->type == pe_long);
      tmpvar ix = gensym (pe_long);
      o->newline() << "c->last_stmt = "
		   << lex_cast_qstring(*e->indexes[0]->tok) << ";";
      c_assign (ix.qname(), e->indexes[0], "array index copy");
      idx.push_back(ix);
    }  
}


void 
c_unparser::load_aggregate (expression *e, aggvar & agg, bool pre_agg)
{
  symbol *sym = get_symbol_within_expression (e);
  
  if (sym->referent->type != pe_stats)
    throw semantic_error ("unexpected aggregate of non-statistic", sym->tok);
  
  var v = getvar(sym->referent, e->tok);

  if (sym->referent->arity == 0)
    {
      o->newline() << "c->last_stmt = " << lex_cast_qstring(*sym->tok) << ";";
      o->newline() << agg << " = _stp_stat_get (" << v << ", 0);";	  
    }
  else
    {
      arrayindex *arr = NULL;
      if (!expression_is_arrayindex (e, arr))
	throw semantic_error("unexpected aggregate of non-arrayindex", e->tok);
      
      vector<tmpvar> idx;
      load_map_indices (arr, idx);
      mapvar mvar = getmap (sym->referent, sym->tok);
      o->newline() << "c->last_stmt = " << lex_cast_qstring(*sym->tok) << ";";
      o->newline() << agg << " = " << mvar.get(idx, pre_agg) << ";";
    }
}


string 
c_unparser::histogram_index_check(var & base, tmpvar & idx) const
{
  return "((" + idx.qname() + " >= 0)"
    + " && (" + idx.qname() + " < " + base.buckets() + "))"; 
}


void
c_tmpcounter::visit_arrayindex (arrayindex *e)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      vardecl* r = array->referent;
      
      // One temporary per index dimension.
      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  tmpvar ix = parent->gensym (r->index_types[i]);
	  ix.declare (*parent);
	  e->indexes[i]->visit(this);
	}
      
      // The index-expression result.
      tmpvar res = parent->gensym (e->type);
      res.declare (*parent);
    }
  else
    {

      assert(hist);

      // Note: this is a slightly tricker-than-it-looks allocation of
      // temporaries. The reason is that we're in the branch handling
      // histogram-indexing, and the histogram might be build over an
      // indexable entity itself. For example if we have:
      // 
      //  global foo
      //  ...
      //  foo[getpid(), geteuid()] <<< 1
      //  ...
      //  print @log_hist(foo[pid, euid])[bucket]
      //  
      // We are looking at the @log_hist(...)[bucket] expression, so
      // allocating one tmpvar for calculating bucket (the "index" of
      // this arrayindex expression), and one tmpvar for storing the
      // result in, just as normal.
      //      
      // But we are *also* going to call load_aggregate on foo, which
      // will itself require tmpvars for each of its indices. Since
      // this is not handled by delving into the subexpression (it
      // would be if hist were first-class in the type system, but
      // it's not) we we allocate all the tmpvars used in such a
      // subexpression up here: first our own aggvar, then our index
      // (bucket) tmpvar, then all the index tmpvars of our
      // pe_stat-valued subexpression, then our result.

      
      // First all the stuff related to indexing into the histogram

      if (e->indexes.size() != 1)
	throw semantic_error("Invalid indexing of histogram", e->tok);
      tmpvar ix = parent->gensym (pe_long);
      ix.declare (*parent);      
      e->indexes[0]->visit(this);
      tmpvar res = parent->gensym (pe_long);
      res.declare (*parent);
      
      // Then the aggregate, and all the tmpvars needed by our call to
      // load_aggregate().

      aggvar agg = parent->gensym_aggregate ();
      agg.declare(*(this->parent));

      symbol *sym = get_symbol_within_expression (hist->stat);
      var v = parent->getvar(sym->referent, sym->tok);
      if (sym->referent->arity != 0)
	{
	  arrayindex *arr = NULL;
	  if (!expression_is_arrayindex (hist->stat, arr))
	    throw semantic_error("expected arrayindex expression in indexed hist_op", e->tok);

	  for (unsigned i=0; i<sym->referent->index_types.size(); i++)
	    {	      
	      tmpvar ix = parent->gensym (sym->referent->index_types[i]);
	      ix.declare (*parent);
	      arr->indexes[i]->visit(this);
	    }
	}
    }
}


void
c_unparser::visit_arrayindex (arrayindex* e)
{  
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      // Visiting an statistic-valued array in a non-lvalue context is prohibited.
      if (array->referent->type == pe_stats)
	throw semantic_error ("statistic-valued array in rvalue context", e->tok);

      stmt_expr block(*this);  

      // NB: Do not adjust the order of the next few lines; the tmpvar
      // allocation order must remain the same between
      // c_unparser::visit_arrayindex and c_tmpcounter::visit_arrayindex
      
      vector<tmpvar> idx;
      load_map_indices (e, idx);
      tmpvar res = gensym (e->type);
  
      // NB: because these expressions are nestable, emit this construct
      // thusly:
      // ({ tmp0=(idx0); ... tmpN=(idxN);
      //    lock (array);
      //    res = fetch (array, idx0...N);
      //    unlock (array);
      //    res; })
      //
      // we store all indices in temporary variables to avoid nasty
      // reentrancy issues that pop up with nested expressions:
      // e.g. a[a[c]=5] could deadlock
  
      mapvar mvar = getmap (array->referent, e->tok);
      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
      c_assign (res, mvar.get(idx), e->tok);

      o->newline() << res << ";";
    }
  else
    {
      // See commentary in c_tmpcounter::visit_arrayindex

      assert(hist);
      stmt_expr block(*this);  

      // NB: Do not adjust the order of the next few lines; the tmpvar
      // allocation order must remain the same between
      // c_unparser::visit_arrayindex and c_tmpcounter::visit_arrayindex
      
      vector<tmpvar> idx;
      load_map_indices (e, idx);
      tmpvar res = gensym (e->type);
      
      aggvar agg = gensym_aggregate ();

      // These should have faulted during elaboration if not true.
      assert(idx.size() == 1);
      assert(idx[0].type() == pe_long);	

      symbol *sym = get_symbol_within_expression (hist->stat);

      var *v;
      if (sym->referent->arity < 1)
	v = new var(getvar(sym->referent, e->tok));
      else
	v = new mapvar(getmap(sym->referent, e->tok));

      v->assert_hist_compatible(*hist);

      if (aggregations_active.count(v->qname()))
	load_aggregate(hist->stat, agg, true);
      else 
        load_aggregate(hist->stat, agg, false);

      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";

      // PR 2142: NULL check for aggregate struct pointer
      o->newline() << "if (unlikely (" << agg.qname() << " == NULL))";
      o->indent(1);
#if EXTRACTORS_PERMISSIVE /* Accept all @extractors, return 0.  */
      c_assign(res, "0", e->tok);
#else /* Accept @count only, trap on others.  */
      o->newline() << "c->last_error = \"aggregate element not found\";";
#endif
      o->newline(-1) << "else {";
      o->indent(1);

      o->newline() << "if (" << histogram_index_check(*v, idx[0]) << ")";
      o->newline() << "{";
      o->newline(1)  << res << " = " << agg << "->histogram[" << idx[0] << "];";
      o->newline(-1) << "}";
      o->newline() << "else";
      o->newline() << "{";
      o->newline(1)  << "c->last_error = \"histogram index out of range\";";
      o->newline()   << res << " = 0;";
      o->newline(-1) << "}";
      
      delete v;

      o->newline(-1) << "}";
      o->newline() << res << ";";
    }
}


void
c_tmpcounter_assignment::visit_arrayindex (arrayindex *e)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {

      vardecl* r = array->referent;

      // One temporary per index dimension.
      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  tmpvar ix = parent->parent->gensym (r->index_types[i]);
	  ix.declare (*(parent->parent));
	  e->indexes[i]->visit(parent);
	}
 
      // The expression rval, lval, and result.
      exp_type ty = rvalue ? rvalue->type : e->type;
      tmpvar rval = parent->parent->gensym (ty);
      rval.declare (*(parent->parent));

      tmpvar lval = parent->parent->gensym (ty);
      lval.declare (*(parent->parent));

      tmpvar res = parent->parent->gensym (ty);
      res.declare (*(parent->parent));

      if (rvalue)
	rvalue->visit (parent);
    }
  else
    {
      throw semantic_error("cannot assign to histogram buckets", e->tok);
    }
}

void
c_unparser_assignment::visit_arrayindex (arrayindex *e)
{
  symbol *array;  
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {

      stmt_expr block(*parent);  

      translator_output *o = parent->o;

      if (array->referent->index_types.size() == 0)
	throw semantic_error ("unexpected reference to scalar", e->tok);

      // nb: Do not adjust the order of the next few lines; the tmpvar
      // allocation order must remain the same between
      // c_unparser_assignment::visit_arrayindex and
      // c_tmpcounter_assignment::visit_arrayindex
  
      vector<tmpvar> idx;
      parent->load_map_indices (e, idx);
      exp_type ty = rvalue ? rvalue->type : e->type;
      tmpvar rvar = parent->gensym (ty);
      tmpvar lvar = parent->gensym (ty);
      tmpvar res = parent->gensym (ty);
  
      // NB: because these expressions are nestable, emit this construct
      // thusly:
      // ({ tmp0=(idx0); ... tmpN=(idxN); rvar=(rhs); lvar; res;
      //    lock (array);
      //    lvar = get (array,idx0...N); // if necessary
      //    assignop (res, lvar, rvar);
      //    set (array, idx0...N, lvar);
      //    unlock (array);
      //    res; })
      //
      // we store all indices in temporary variables to avoid nasty
      // reentrancy issues that pop up with nested expressions:
      // e.g. ++a[a[c]=5] could deadlock
      //
      //
      // There is an exception to the above form: if we're doign a <<< assigment to 
      // a statistic-valued map, there's a special form we follow:
      //
      // ({ tmp0=(idx0); ... tmpN=(idxN); rvar=(rhs);
      //    *no need to* lock (array);
      //    _stp_map_add_stat (array, idx0...N, rvar);
      //    *no need to* unlock (array);
      //    rvar; })
      //
      // To simplify variable-allocation rules, we assign rvar to lvar and
      // res in this block as well, even though they are technically
      // superfluous.

      prepare_rvalue (op, rvar, e->tok);

      if (op == "<<<")
	{
	  assert (e->type == pe_stats);
	  assert (rvalue->type == pe_long);

	  mapvar mvar = parent->getmap (array->referent, e->tok);
	  o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
	  o->newline() << mvar.add (idx, rvar) << ";";
          res = rvar;
	  // no need for these dummy assignments
	  // o->newline() << lvar << " = " << rvar << ";";
	  // o->newline() << res << " = " << rvar << ";";
	}
      else
	{
	  mapvar mvar = parent->getmap (array->referent, e->tok);
	  o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
	  if (op != "=") // don't bother fetch slot if we will just overwrite it
	    parent->c_assign (lvar, mvar.get(idx), e->tok);
	  c_assignop (res, lvar, rvar, e->tok); 
	  o->newline() << mvar.set (idx, lvar) << ";";
	}

      o->newline() << res << ";";
    } 
  else
    {
      throw semantic_error("cannot assign to histogram buckets", e->tok);
    }
}


void
c_tmpcounter::visit_functioncall (functioncall *e)
{
  functiondecl* r = e->referent;
  // one temporary per argument
  for (unsigned i=0; i<r->formal_args.size(); i++)
    {
      tmpvar t = parent->gensym (r->formal_args[i]->type);
      t.declare (*parent);
      e->args[i]->visit (this);
    }
}


void
c_unparser::visit_functioncall (functioncall* e)
{
  functiondecl* r = e->referent;

  if (r->formal_args.size() != e->args.size())
    throw semantic_error ("invalid length argument list", e->tok);

  stmt_expr block(*this);  

  // NB: we store all actual arguments in temporary variables,
  // to avoid colliding sharing of context variables with
  // nested function calls: f(f(f(1)))

  // compute actual arguments
  vector<tmpvar> tmp;

  for (unsigned i=0; i<e->args.size(); i++)
    {
      tmpvar t = gensym(e->args[i]->type);
      tmp.push_back(t);

      if (r->formal_args[i]->type != e->args[i]->type)
	throw semantic_error ("function argument type mismatch",
			      e->args[i]->tok, "vs", r->formal_args[i]->tok);

      o->newline() << "c->last_stmt = "
		   << lex_cast_qstring(*e->args[i]->tok) << ";";
      c_assign (t.qname(), e->args[i], "function actual argument evaluation");
    }

  o->newline();
  o->newline() << "if (unlikely (c->nesting+2 >= MAXNESTING)) {";
  o->newline(1) << "c->last_error = \"MAXNESTING exceeded\";";
  o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
  o->newline(-1) << "} else if (likely (! c->last_error)) {";
  o->indent(1);

  // copy in actual arguments
  for (unsigned i=0; i<e->args.size(); i++)
    {
      if (r->formal_args[i]->type != e->args[i]->type)
	throw semantic_error ("function argument type mismatch",
			      e->args[i]->tok, "vs", r->formal_args[i]->tok);

      c_assign ("c->locals[c->nesting+1].function_" +
		c_varname (r->name) + "." +
                c_varname (r->formal_args[i]->name),
                tmp[i].qname(),
                e->args[i]->type,
                "function actual argument copy",
                e->args[i]->tok);
    }

  // call function
  o->newline() << "c->nesting ++;";
  o->newline() << "function_" << c_varname (r->name) << " (c);";
  o->newline() << "c->nesting --;";

  // reset last_error to NULL if it was set to "" by return()
  o->newline() << "if (c->last_error && ! c->last_error[0])";
  o->newline(1) << "c->last_error = 0;";
  o->indent(-1);

  o->newline(-1) << "}";

  // return result from retvalue slot
  
  if (r->type == pe_unknown)
    // If we passed typechecking, then nothing will use this return value
    o->newline() << "(void) 0;";
  else
    o->newline() << "c->locals[c->nesting+1]"
                 << ".function_" << c_varname (r->name)
                 << ".__retvalue;";
}

void
c_tmpcounter::visit_print_format (print_format* e)
{
  if (e->hist)
    {
      symbol *sym = get_symbol_within_expression (e->hist->stat);
      var v = parent->getvar(sym->referent, sym->tok);
      aggvar agg = parent->gensym_aggregate ();

      agg.declare(*(this->parent));

      if (sym->referent->arity != 0)
	{
	  // One temporary per index dimension.
	  for (unsigned i=0; i<sym->referent->index_types.size(); i++)
	    {
	      arrayindex *arr = NULL;
	      if (!expression_is_arrayindex (e->hist->stat, arr))
		throw semantic_error("expected arrayindex expression in printed hist_op", e->tok);
	      
	      tmpvar ix = parent->gensym (sym->referent->index_types[i]);
	      ix.declare (*parent);
	      arr->indexes[i]->visit(this);
	    }
	}
    }
  else
    {
      // One temporary per argument
      for (unsigned i=0; i < e->args.size(); i++)
	{
	  tmpvar t = parent->gensym (e->args[i]->type);
	  if (e->args[i]->type == pe_unknown)
	    {
	      throw semantic_error("unknown type of arg to print operator", 
				   e->args[i]->tok);
	    }

	  t.declare (*parent);
	  e->args[i]->visit (this);
	}

      // And the result
      exp_type ty = e->print_to_stream ? pe_long : pe_string;
      tmpvar res = parent->gensym (ty);      
      res.declare (*parent);
    }
}


void 
c_unparser::visit_print_format (print_format* e)
{
  // Print formats can contain a general argument list *or* a special
  // type of argument which gets its own processing: a single,
  // non-format-string'ed, histogram-type stat_op expression.

  if (e->hist)
    {
      stmt_expr block(*this);  
      symbol *sym = get_symbol_within_expression (e->hist->stat);
      aggvar agg = gensym_aggregate ();

      var *v;
      if (sym->referent->arity < 1)
        v = new var(getvar(sym->referent, e->tok));
      else
        v = new mapvar(getmap(sym->referent, e->tok));

      v->assert_hist_compatible(*e->hist);

      {
	if (aggregations_active.count(v->qname()))
	  load_aggregate(e->hist->stat, agg, true);
	else 
          load_aggregate(e->hist->stat, agg, false);
	o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";

        // PR 2142: NULL check for aggregate struct pointer
        o->newline() << "if (unlikely (" << agg.qname() << " == NULL))";
        o->indent(1);
#if EXTRACTORS_PERMISSIVE
        o->newline() << ";"; // ignore print(@hist(foo[bad_index]))
#else /* Accept @count only, trap on others.  */
        o->newline() << "c->last_error = \"aggregate element not found\";";
#endif
        o->newline(-1) << "else {";
        o->indent(1);

	o->newline() << "_stp_stat_print_histogram (" << v->hist() << ", " << agg.qname() << ");";

        o->newline(-1) << "}";
      }

      delete v;
    }
  else
    {
      stmt_expr block(*this);  

      // Compute actual arguments
      vector<tmpvar> tmp;
      
      for (unsigned i=0; i<e->args.size(); i++)
	{
	  tmpvar t = gensym(e->args[i]->type);
	  tmp.push_back(t);

	  o->newline() << "c->last_stmt = "
		       << lex_cast_qstring(*e->args[i]->tok) << ";";
	  c_assign (t.qname(), e->args[i], "print format actual argument evaluation");	  
	}

      std::vector<print_format::format_component> components;
      
      if (e->print_with_format)
	{
	  components = e->components;
	}
      else
	{
	  // Synthesize a print-format string if the user didn't
	  // provide one; the synthetic string simply contains one
	  // directive for each argument.
	  for (unsigned i = 0; i < e->args.size(); ++i)
	    {
	      print_format::format_component curr;
	      curr.clear();
	      switch (e->args[i]->type)
		{
		case pe_unknown:
		  throw semantic_error("cannot print unknown expression type", e->args[i]->tok);
		case pe_stats:
		  throw semantic_error("cannot print a raw stats object", e->args[i]->tok);
		case pe_long:
		  curr.type = print_format::conv_signed_decimal;
		  break;
		case pe_string:
		  curr.type = print_format::conv_string;
		  break;
		}
	      components.push_back (curr);
	    }
	}


      // Allocate the result
      exp_type ty = e->print_to_stream ? pe_long : pe_string;
      tmpvar res = gensym (ty);      

      // Make the [s]printf call
      if (e->print_to_stream)
	{
	  o->newline() << res.qname() << " = 0;";
	  o->newline() << "_stp_printf (";
	}
      else
	o->newline() << "snprintf (" << res.qname() << ", MAXSTRINGLEN, ";

      o->line() << lex_cast_qstring(print_format::components_to_string(components));

      for (unsigned i = 0; i < tmp.size(); ++i)
	{
	  // We must cast our pe_long type (which is int64_t) to "long
	  // long" here, because the format string type we are using
	  // is %ll. We use this format string because, at the back
	  // end of vsnprintf, linux actually implements it using the
	  // "long long" type as well, not a particular 32 or 64 bit
	  // width, and it *also* fails to provide any inttype.h-like
	  // macro machinery to figure out how many bits exist in a
	  // long long. Using %ll and always casting the argument is
	  // the most portable target-sensitive solution.
	  if (tmp[i].type() == pe_long)
	    o->line() << ", ((long long)(" << tmp[i].qname() << "))";
	  else
	    o->line() << ", " << tmp[i].qname();
	}
      o->line() << ");";
      o->newline() << res.qname() << ";";
    }
}


void 
c_tmpcounter::visit_stat_op (stat_op* e)
{
  symbol *sym = get_symbol_within_expression (e->stat);
  var v = parent->getvar(sym->referent, e->tok);
  aggvar agg = parent->gensym_aggregate ();
  tmpvar res = parent->gensym (pe_long);

  agg.declare(*(this->parent));
  res.declare(*(this->parent));

  if (sym->referent->arity != 0)
    {
      // One temporary per index dimension.
      for (unsigned i=0; i<sym->referent->index_types.size(); i++)
	{
	  // Sorry about this, but with no dynamic_cast<> and no
	  // constructor patterns, this is how things work.
	  arrayindex *arr = NULL;
	  if (!expression_is_arrayindex (e->stat, arr))
	    throw semantic_error("expected arrayindex expression in stat_op of array", e->tok);

	  tmpvar ix = parent->gensym (sym->referent->index_types[i]);
	  ix.declare (*parent);
	  arr->indexes[i]->visit(this);
	}
    }
}

void 
c_unparser::visit_stat_op (stat_op* e)
{
  // Stat ops can be *applied* to two types of expression:
  //
  //  1. An arrayindex expression on a pe_stats-valued array. 
  //
  //  2. A symbol of type pe_stats. 

  // FIXME: classify the expression the stat_op is being applied to,
  // call appropriate stp_get_stat() / stp_pmap_get_stat() helper,
  // then reach into resultant struct stat_data.

  // FIXME: also note that summarizing anything is expensive, and we
  // really ought to pass a timeout handler into the summary routine,
  // check its response, possibly exit if it ran out of cycles.
  
  {
    stmt_expr block(*this);
    symbol *sym = get_symbol_within_expression (e->stat);
    aggvar agg = gensym_aggregate ();
    tmpvar res = gensym (pe_long);    
    var v = getvar(sym->referent, e->tok);
    {
      if (aggregations_active.count(v.qname()))
	load_aggregate(e->stat, agg, true);
      else
        load_aggregate(e->stat, agg, false);

      // PR 2142: NULL check for aggregate struct pointer
      o->newline() << "if (unlikely (" << agg.qname() << " == NULL))";
      o->indent(1);
#if EXTRACTORS_PERMISSIVE /* Accept all @extractors, return 0.  */
      c_assign(res, "0", e->tok);
#else /* Accept @count only, trap on others.  */
      if (e->ctype == sc_count)
        c_assign(res, "0", e->tok);
      else
        o->newline() << "c->last_error = \"aggregate element not found\";";
#endif
      o->newline(-1) << "else {";
      o->indent(1);

      switch (e->ctype)
	{
	case sc_average:
	  // impedance matching: Have to compupte average ourselves
	  c_assign(res, ("_stp_div64(&c->last_error, " + agg.qname() + "->sum, " + agg.qname() + "->count)"),
		   e->tok);
	  break;

	case sc_count:
	  c_assign(res, agg.qname() + "->count", e->tok);
	  break;

	case sc_sum:
	  c_assign(res, agg.qname() + "->sum", e->tok);
	  break;

	case sc_min:
	  c_assign(res, agg.qname() + "->min", e->tok);
	  break;

	case sc_max:
	  c_assign(res, agg.qname() + "->max", e->tok);
	  break;
	}
      o->newline(-1) << "}";
    }    
    o->newline() << res << ";";
  }
}


void 
c_unparser::visit_hist_op (hist_op* e)
{
  // Hist ops can only occur in a limited set of circumstances:
  //
  //  1. Inside an arrayindex expression, as the base referent. See
  //     c_unparser::visit_arrayindex for handling of this case.
  //
  //  2. Inside a foreach statement, as the base referent. See
  //     c_unparser::visit_foreach_loop for handling this case.
  //
  //  3. Inside a print_format expression, as the sole argument. See
  //     c_unparser::visit_print_format for handling this case.
  //
  // Note that none of these cases involves the c_unparser ever
  // visiting this node. We should not get here.

  assert(false);
}


int
emit_symbol_data (systemtap_session& s)
{
  int rc = 0;

  // Instead of processing elf symbol tables, for now we just snatch
  // /proc/kallsyms and convert it to our use.  We need it sorted by
  // address (so we can binary search) , and filtered (to show text
  // symbols only), a task that we defer to grep(1) and sort(1).  It
  // may be useful to cache the symbols.sorted file, perhaps indexed
  // by md5sum(/proc/modules), but let's not until this simple method
  // proves too costly.  LC_ALL=C is already set to avoid the
  // excessive penalty of i18n code in some glibc/coreutils versions.

  string sorted_kallsyms = s.tmpdir + "/symbols.sorted";
  string sortcmd = "grep \" [tT] \" /proc/kallsyms | ";
 
  sortcmd += "sort ";
#if __LP64__
  sortcmd += "-k 1,16 ";
#else
  sortcmd += "-k 1,8 ";
#endif
  sortcmd += "-s -o " + sorted_kallsyms;

  if (s.verbose>1) clog << "Running " << sortcmd << endl;
  rc = system(sortcmd.c_str());
  if (rc == 0)
    {
      ifstream kallsyms (sorted_kallsyms.c_str());
      char kallsyms_outbuf [4096];
      ofstream kallsyms_out ((s.tmpdir + "/stap-symbols.h").c_str());
      kallsyms_out.rdbuf()->pubsetbuf (kallsyms_outbuf,
                                       sizeof(kallsyms_outbuf));
      
      s.op->newline() << "\n\n#include \"stap-symbols.h\"";

      unsigned i=0;
      kallsyms_out << "struct stap_symbol stap_symbols [] = {";
      string lastaddr;
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
	  
	  // NB: kallsyms includes some duplicate addresses
	  if ((type == "t" || type == "T") && lastaddr != addr)
	    {
	      kallsyms_out << "  { 0x" << addr << ", "
                           << "\"" << sym << "\", "
                           << "\"" << module << "\" },"
                           << "\n";
	      lastaddr = addr;
	      i ++;
	    }
	}
      kallsyms_out << "};\n";
      kallsyms_out << "unsigned stap_num_symbols = " << i << ";\n";
    }

  return rc;
}


int
translate_pass (systemtap_session& s)
{
  int rc = 0;

  s.op = new translator_output (s.translated_source);
  c_unparser cup (& s);
  s.up = & cup;

  try
    {
      // This is at the very top of the file.
      
      // XXX: the runtime uses #ifdef TEST_MODE to infer systemtap usage.
      s.op->line() << "#define TEST_MODE 0\n";

      s.op->newline() << "#ifndef MAXNESTING";
      s.op->newline() << "#define MAXNESTING 10";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXSTRINGLEN";
      s.op->newline() << "#define MAXSTRINGLEN 128";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXACTION";
      s.op->newline() << "#define MAXACTION 1000";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXTRYLOCK";
      s.op->newline() << "#define MAXTRYLOCK MAXACTION";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef TRYLOCKDELAY";
      s.op->newline() << "#define TRYLOCKDELAY 100";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXMAPENTRIES";
      s.op->newline() << "#define MAXMAPENTRIES 2048";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXERRORS";
      s.op->newline() << "#define MAXERRORS 0";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXSKIPPED";
      s.op->newline() << "#define MAXSKIPPED 100";
      s.op->newline() << "#endif";

      // impedance mismatch
      s.op->newline() << "#define STP_STRING_SIZE MAXSTRINGLEN";
      s.op->newline() << "#define STP_NUM_STRINGS 1";

      if (s.bulk_mode)
	s.op->newline() << "#define STP_RELAYFS";

      s.op->newline() << "#include \"runtime.h\"";
      s.op->newline() << "#include \"current.c\"";
      s.op->newline() << "#include \"stack.c\"";
      s.op->newline() << "#include \"regs.c\"";
      s.op->newline() << "#include <linux/string.h>";
      s.op->newline() << "#include <linux/timer.h>";
      s.op->newline() << "#include <linux/delay.h>";
      s.op->newline() << "#include <linux/profile.h>";
      s.op->newline() << "#include \"loc2c-runtime.h\" ";
      
      // XXX: old 2.6 kernel hack
      s.op->newline() << "#ifndef read_trylock";
      s.op->newline() << "#define read_trylock(x) ({ read_lock(x); 1; })";
      s.op->newline() << "#endif";

      s.up->emit_common_header ();

      for (unsigned i=0; i<s.embeds.size(); i++)
        {
          s.op->newline() << s.embeds[i]->code << "\n";
        }

      for (unsigned i=0; i<s.globals.size(); i++)
        {
          s.op->newline();
          s.up->emit_global (s.globals[i]);
        }

      for (unsigned i=0; i<s.functions.size(); i++)
	{
	  s.op->newline();
	  s.up->emit_functionsig (s.functions[i]);
	}

      for (unsigned i=0; i<s.functions.size(); i++)
	{
	  s.op->newline();
	  s.up->emit_function (s.functions[i]);
	}

      for (unsigned i=0; i<s.probes.size(); i++)
        {
          s.op->newline();
          s.up->emit_probe (s.probes[i], i);
        }

      s.op->newline();
      s.up->emit_module_init ();
      s.op->newline();
      s.up->emit_module_exit ();

      s.op->newline();

      // XXX impedance mismatch
      s.op->newline() << "int probe_start () {";
      s.op->newline(1) << "return systemtap_module_init () ? -1 : 0;";
      s.op->newline(-1) << "}";
      s.op->newline();
      s.op->newline() << "void probe_exit () {";
      s.op->newline(1) << "systemtap_module_exit ();";
      s.op->newline(-1) << "}";

      for (unsigned i=0; i<s.globals.size(); i++)
        {
          s.op->newline();
          s.up->emit_global_param (s.globals[i]);
        }

      s.op->newline() << "MODULE_DESCRIPTION(\"systemtap probe\");";
      s.op->newline() << "MODULE_LICENSE(\"GPL\");"; // XXX
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
    }

  rc |= emit_symbol_data (s);
  s.op->line() << "\n";

  delete s.op;
  s.op = 0;
  s.up = 0;

  return rc + s.num_errors;
}
