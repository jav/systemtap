// translation pass
// Copyright (C) 2005-2009 Red Hat Inc.
// Copyright (C) 2005-2008 Intel Corporation.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "elaborate.h"
#include "translate.h"
#include "session.h"
#include "tapsets.h"
#include "util.h"
#include "dwarf_wrappers.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <cassert>
#include <cstring>

extern "C" {
#include <elfutils/libdwfl.h>
#ifdef HAVE_ELFUTILS_VERSION_H
#include <elfutils/version.h>
#endif
}

using namespace std;

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
  functiondecl* current_function;
  unsigned tmpvar_counter;
  unsigned label_counter;
  unsigned action_counter;
  bool probe_or_function_needs_deref_fault_handler;

  varuse_collecting_visitor vcv_needs_global_locks;

  map<string, string> probe_contents;

  c_unparser (systemtap_session* ss):
    session (ss), o (ss->op), current_probe(0), current_function (0),
  tmpvar_counter (0), label_counter (0) {}
  ~c_unparser () {}

  void emit_map_type_instantiations ();
  void emit_common_header ();
  void emit_global (vardecl* v);
  void emit_global_init (vardecl* v);
  void emit_global_param (vardecl* v);
  void emit_functionsig (functiondecl* v);
  void emit_module_init ();
  void emit_module_exit ();
  void emit_function (functiondecl* v);
  void emit_locks (const varuse_collecting_visitor& v);
  void emit_probe (derived_probe* v);
  void emit_unlocks (const varuse_collecting_visitor& v);

  // for use by stats (pmap) foreach
  set<string> aggregations_active;

  // for use by looping constructs
  vector<string> loop_break_labels;
  vector<string> loop_continue_labels;

  string c_typename (exp_type e);
  string c_varname (const string& e);
  string c_expression (expression* e);

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

  void record_actions (unsigned actions, bool update=false);

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
  void visit_cast_op (cast_op* e);
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

  void load_map_indices(arrayindex* e);

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
		       tmpvar & rval,
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
  bool post; // true == value saved before modify operator
  c_tmpcounter_assignment (c_tmpcounter* p, const string& o, expression* e, bool pp = false):
    parent (p), op (o), rvalue (e), post (pp) {}

  void prepare_rvalue (tmpvar & rval);

  void c_assignop(tmpvar & res);

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
	assert(hop.params.size() == 0);
	break;
      case statistic_decl::none:
	assert(false);
      }
  }

  exp_type type() const
  {
    return ty;
  }

  string value() const
  {
    if (local)
      return "l->" + name;
    else
      return "global.s_" + name;
  }

  virtual string hist() const
  {
    assert (ty == pe_stats);
    assert (sd.type != statistic_decl::none);
    return "(&(" + value() + "->hist))";
  }

  virtual string buckets() const
  {
    assert (ty == pe_stats);
    assert (sd.type != statistic_decl::none);
    return "(" + value() + "->hist.buckets)";
  }

  string init() const
  {
    switch (type())
      {
      case pe_string:
        if (! local)
          return ""; // module_param
        else
	  return value() + "[0] = '\\0';";
      case pe_long:
        if (! local)
          return ""; // module_param
        else
          return value() + " = 0;";
      case pe_stats:
        {
          // See also mapvar::init().

          string prefix = value() + " = _stp_stat_init (";
          // Check for errors during allocation.
          string suffix = "if (" + value () + " == NULL) rc = -ENOMEM;";

          switch (sd.type)
            {
            case statistic_decl::none:
              prefix += "HIST_NONE";
              break;

            case statistic_decl::linear:
              prefix += string("HIST_LINEAR")
                + ", " + stringify(sd.linear_low)
                + ", " + stringify(sd.linear_high)
                + ", " + stringify(sd.linear_step);
              break;

            case statistic_decl::logarithmic:
              prefix += string("HIST_LOG");
              break;

            default:
              throw semantic_error("unsupported stats type for " + value());
            }

          prefix = prefix + "); ";
          return string (prefix + suffix);
        }

      default:
	throw semantic_error("unsupported initializer for " + value());
      }
  }

  string fini () const
  {
    switch (type())
      {
      case pe_string:
      case pe_long:
	return ""; // no action required
      case pe_stats:
	return "_stp_stat_del (" + value () + ");";
      default:
	throw semantic_error("unsupported deallocator for " + value());
      }
  }

  void declare(c_unparser &c) const
  {
    c.c_declare(ty, name);
  }
};

ostream & operator<<(ostream & o, var const & v)
{
  return o << v.value();
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
protected:
  bool overridden;
  string override_value;

public:
  tmpvar(exp_type ty,
	 unsigned & counter)
    : var(true, ty, ("__tmp" + stringify(counter++))), overridden(false)
  {}

  tmpvar(const var& source)
    : var(source), overridden(false)
  {}

  void override(const string &value)
  {
    overridden = true;
    override_value = value;
  }

  string value() const
  {
    if (overridden)
      return override_value;
    else
      return var::value();
  }
};

ostream & operator<<(ostream & o, tmpvar const & v)
{
  return o << v.value();
}

struct aggvar
  : public var
{
  aggvar(unsigned & counter)
    : var(true, pe_stats, ("__tmp" + stringify(counter++)))
  {}

  string init() const
  {
    assert (type() == pe_stats);
    return value() + " = NULL;";
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
  int maxsize;
  mapvar (bool local, exp_type ty,
	  statistic_decl const & sd,
	  string const & name,
	  vector<exp_type> const & index_types,
	  int maxsize)
    : var (local, ty, sd, name),
      index_types (index_types),
      maxsize (maxsize)
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
    result += pre_agg? fetch_existing_aggregate() : value();
    for (unsigned i = 0; i < indices.size(); ++i)
      {
	if (indices[i].type() != index_types[i])
	  throw semantic_error("index type mismatch");
	result += ", ";
	result += indices[i].value();
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

    return "_stp_pmap_agg (" + value() + ")";
  }

  string fetch_existing_aggregate() const
  {
    if (!is_parallel())
      throw semantic_error("fetching aggregate of non-parallel map type");

    return "_stp_pmap_get_agg(" + value() + ")";
  }

  string del (vector<tmpvar> const & indices) const
  {
    return (call_prefix("del", indices) + ")");
  }

  string exists (vector<tmpvar> const & indices) const
  {
    if (type() == pe_long || type() == pe_string)
      return (call_prefix("exists", indices) + ")");
    else if (type() == pe_stats)
      return ("((uintptr_t)" + call_prefix("get", indices)
	      + ") != (uintptr_t) 0)");
    else
      throw semantic_error("checking existence of an unsupported map type");
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
    string res = "{ int rc = ";

    // impedance matching: empty strings -> NULL
    if (type() == pe_stats)
      res += (call_prefix("add", indices) + ", " + val.value() + ")");
    else
      throw semantic_error("adding a value of an unsupported map type");

    res += "; if (unlikely(rc)) { c->last_error = \"Array overflow, check " +
      stringify(maxsize > 0 ?
	  "size limit (" + stringify(maxsize) + ")" : "MAXMAPENTRIES")
      + "\"; goto out; }}";

    return res;
  }

  string set (vector<tmpvar> const & indices, tmpvar const & val) const
  {
    string res = "{ int rc = ";

    // impedance matching: empty strings -> NULL
    if (type() == pe_string)
      res += (call_prefix("set", indices)
	      + ", (" + val.value() + "[0] ? " + val.value() + " : NULL))");
    else if (type() == pe_long)
      res += (call_prefix("set", indices) + ", " + val.value() + ")");
    else
      throw semantic_error("setting a value of an unsupported map type");

    res += "; if (unlikely(rc)) { c->last_error = \"Array overflow, check " +
      stringify(maxsize > 0 ?
	  "size limit (" + stringify(maxsize) + ")" : "MAXMAPENTRIES")
      + "\"; goto out; }}";

    return res;
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
    string prefix = value() + " = _stp_" + mtype + "_new_" + keysym() + " (" +
      (maxsize > 0 ? stringify(maxsize) : "MAXMAPENTRIES") ;

    // See also var::init().

    // Check for errors during allocation.
    string suffix = "if (" + value () + " == NULL) rc = -ENOMEM;";

    if (type() == pe_stats)
      {
	switch (sdecl().type)
	  {
	  case statistic_decl::none:
	    prefix = prefix + ", HIST_NONE";
	    break;

	  case statistic_decl::linear:
	    // FIXME: check for "reasonable" values in linear stats
	    prefix = prefix + ", HIST_LINEAR"
	      + ", " + stringify(sdecl().linear_low)
	      + ", " + stringify(sdecl().linear_high)
	      + ", " + stringify(sdecl().linear_step);
	    break;

	  case statistic_decl::logarithmic:
	    prefix = prefix + ", HIST_LOG";
	    break;
	  }
      }

    prefix = prefix + "); ";
    return (prefix + suffix);
  }

  string fini () const
  {
    // NB: fini() is safe to call even for globals that have not
    // successfully initialized (that is to say, on NULL pointers),
    // because the runtime specifically tolerates that in its _del
    // functions.

    if (is_parallel())
      return "_stp_pmap_del (" + value() + ");";
    else
      return "_stp_map_del (" + value() + ");";
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
      return "_stp_map_start (" + mv.value() + ")";
  }

  string next (mapvar const & mv) const
  {
    if (mv.type() != referent_ty)
      throw semantic_error("inconsistent iterator type in itervar::next()");

    if (mv.is_parallel())
      return "_stp_map_iter (" + mv.fetch_existing_aggregate() + ", " + value() + ")";
    else
      return "_stp_map_iter (" + mv.value() + ", " + value() + ")";
  }

  string value () const
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
	return "_stp_key_get_int64 ("+ value() + ", " + stringify(i+1) + ")";
      case pe_string:
        // impedance matching: NULL -> empty strings
	return "({ char *v = "
          "_stp_key_get_str ("+ value() + ", " + stringify(i+1) + "); "
          "if (! v) v = \"\"; "
          "v; })";
      default:
	throw semantic_error("illegal key type");
      }
  }
};

ostream & operator<<(ostream & o, itervar const & v)
{
  return o << v.value();
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
  delete [] buf;
}


ostream&
translator_output::newline (int indent)
{
  if (!  (indent > 0 || tablevel >= (unsigned)-indent)) o.flush ();
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
  if (!  (indent > 0 || tablevel >= (unsigned)-indent)) o.flush ();
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
  o->newline();
  o->newline() << "typedef char string_t[MAXSTRINGLEN];";
  o->newline();
  o->newline() << "#define STAP_SESSION_STARTING 0";
  o->newline() << "#define STAP_SESSION_RUNNING 1";
  o->newline() << "#define STAP_SESSION_ERROR 2";
  o->newline() << "#define STAP_SESSION_STOPPING 3";
  o->newline() << "#define STAP_SESSION_STOPPED 4";
  o->newline() << "static atomic_t session_state = ATOMIC_INIT (STAP_SESSION_STARTING);";
  o->newline() << "static atomic_t error_count = ATOMIC_INIT (0);";
  o->newline() << "static atomic_t skipped_count = ATOMIC_INIT (0);";
  o->newline() << "static atomic_t skipped_count_lowstack = ATOMIC_INIT (0);";
  o->newline() << "static atomic_t skipped_count_reentrant = ATOMIC_INIT (0);";
  o->newline() << "static atomic_t skipped_count_uprobe_reg = ATOMIC_INIT (0);";
  o->newline() << "static atomic_t skipped_count_uprobe_unreg = ATOMIC_INIT (0);";
  o->newline();
  o->newline() << "struct context {";
  o->newline(1) << "atomic_t busy;";
  o->newline() << "const char *probe_point;";
  o->newline() << "int actionremaining;";
  o->newline() << "unsigned nesting;";
  o->newline() << "string_t error_buffer;";
  o->newline() << "const char *last_error;";
  // NB: last_error is used as a health flag within a probe.
  // While it's 0, execution continues
  // When it's "something", probe code unwinds, _stp_error's, sets error state
  o->newline() << "const char *last_stmt;";
  o->newline() << "struct pt_regs *regs;";
  o->newline() << "unsigned long *unwaddr;";
  // unwaddr is caching unwound address in each probe handler on ia64.
  o->newline() << "struct kretprobe_instance *pi;";
  o->newline() << "int regparm;";
  o->newline() << "va_list *mark_va_list;";
  o->newline() << "const char * marker_name;";
  o->newline() << "const char * marker_format;";
  o->newline() << "void *data;";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "Stat *statp;";
  o->newline() << "#endif";
  o->newline() << "#ifdef STP_OVERLOAD";
  o->newline() << "cycles_t cycles_base;";
  o->newline() << "cycles_t cycles_sum;";
  o->newline() << "#endif";
  o->newline() << "union {";
  o->indent(1);

  // To elide context variables for probe handler functions that
  // themselves are about to get duplicate-eliminated, we XXX
  // duplicate the parse-tree-hash method from ::emit_probe().
  map<string, string> tmp_probe_contents;
  // The reason we don't use c_unparser::probe_contents itself
  // for this is that we don't want to muck up the data for
  // that later routine.

  for (unsigned i=0; i<session->probes.size(); i++)
    {
      derived_probe* dp = session->probes[i];

      // NB: see c_unparser::emit_probe() for original copy of duplicate-hashing logic.
      ostringstream oss;
      oss << "c->statp = & time_" << dp->basest()->name << ";" << endl;  // -t anti-dupe
      oss << "# needs_global_locks: " << dp->needs_global_locks () << endl;
      dp->print_dupe_stamp (oss);
      dp->body->print(oss);
      // NB: dependent probe conditions *could* be listed here, but don't need to be.
      // That's because they're only dependent on the probe body, which is already
      // "hashed" in above.


      if (tmp_probe_contents.count(oss.str()) == 0) // unique
        {
          tmp_probe_contents[oss.str()] = dp->name; // save it

          // XXX: probe locals need not be recursion-nested, only function locals

          o->newline() << "struct " << dp->name << "_locals {";
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

          // NB: This part is finicky.  The logic here must
          // match up with
          c_tmpcounter ct (this);
          dp->emit_probe_context_vars (o);
          dp->body->visit (& ct);

          o->newline(-1) << "} " << dp->name << ";";
        }
    }

  for (map<string,functiondecl*>::iterator it = session->functions.begin(); it != session->functions.end(); it++)
    {
      functiondecl* fd = it->second;
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
  o->newline() << "static void *contexts = NULL; /* alloc_percpu */\n";

  emit_map_type_instantiations ();

  if (!session->stat_decls.empty())
    o->newline() << "#include \"stat.c\"\n";

  o->newline();
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
                   << "global.s_" << vn << ", int64_t, 0);";
    }
  else if (v->arity == 0 && v->type == pe_string)
    {
      // NB: no special copying is needed.
      o->newline() << "module_param_string (" << vn << ", "
                   << "global.s_" << vn
                   << ", MAXSTRINGLEN, 0);";
    }
}


void
c_unparser::emit_global (vardecl *v)
{
  string vn = c_varname (v->name);

  if (v->arity == 0)
    o->newline() << c_typename (v->type) << " s_" << vn << ";";
  else if (v->type == pe_stats)
    o->newline() << "PMAP s_" << vn << ";";
  else
    o->newline() << "MAP s_" << vn << ";";
  o->newline() << "rwlock_t s_" << vn << "_lock;";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "atomic_t s_" << vn << "_lock_skip_count;";
  o->newline() << "#endif\n";
}


void
c_unparser::emit_global_init (vardecl *v)
{
  string vn = c_varname (v->name);

  if (v->arity == 0) // can only statically initialize some scalars
    {
      if (v->init)
	{
	  o->newline() << ".s_" << vn << " = ";
	  v->init->visit(this);
          o->line() << ",";
	}
    }
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << ".s_" << vn << "_lock_skip_count = ATOMIC_INIT(0),";
  o->newline() << "#endif";
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
  vector<derived_probe_group*> g = all_session_groups (*session);
  for (unsigned i=0; i<g.size(); i++)
    g[i]->emit_module_decls (*session);

  o->newline();
  o->newline() << "static int systemtap_module_init (void) {";
  o->newline(1) << "int rc = 0;";
  o->newline() << "int i=0, j=0;"; // for derived_probe_group use
  o->newline() << "const char *probe_point = \"\";";

  // Compare actual and targeted kernel releases/machines.  Sometimes
  // one may install the incorrect debuginfo or -devel RPM, and try to
  // run a probe compiled for a different version.  Catch this early,
  // just in case modversions didn't.
  o->newline() << "{";
  o->newline(1) << "const char* release = UTS_RELEASE;";

  // NB: This UTS_RELEASE compile-time macro directly checks only that
  // the compile-time kbuild tree matches the compile-time debuginfo/etc.
  // It does not check the run time kernel value.  However, this is
  // probably OK since the kbuild modversions system aims to prevent
  // mismatches between kbuild and runtime versions at module-loading time.

  // o->newline() << "const char* machine = UTS_MACHINE;";
  // NB: We could compare UTS_MACHINE too, but on x86 it lies
  // (UTS_MACHINE=i386, but uname -m is i686).  Sheesh.

  o->newline() << "if (strcmp (release, "
               << lex_cast_qstring (session->kernel_release) << ")) {";
  o->newline(1) << "_stp_error (\"module release mismatch (%s vs %s)\", "
                << "release, "
                << lex_cast_qstring (session->kernel_release)
                << ");";
  o->newline() << "rc = -EINVAL;";
  o->newline(-1) << "}";

  // perform buildid-based checking if able
  o->newline() << "if (_stp_module_check()) rc = -EINVAL;";

  o->newline(-1) << "}";
  o->newline() << "if (rc) goto out;";

  // initialize gettimeofday (if needed)
  o->newline() << "#ifdef STAP_NEED_GETTIMEOFDAY";
  o->newline() << "rc = _stp_init_time();";  // Kick off the Big Bang.
  o->newline() << "if (rc) {";
  o->newline(1) << "_stp_error (\"couldn't initialize gettimeofday\");";
  o->newline() << "goto out;";
  o->newline(-1) << "}";
  o->newline() << "#endif";

  o->newline() << "(void) probe_point;";
  o->newline() << "(void) i;";
  o->newline() << "(void) j;";
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
      vardecl* v = session->globals[i];
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).init();
      else
	o->newline() << getvar (v).init();
      // NB: in case of failure of allocation, "rc" will be set to non-zero.
      // Allocation can in general continue.

      o->newline() << "if (rc) {";
      o->newline(1) << "_stp_error (\"global variable " << v->name << " allocation failed\");";
      o->newline() << "goto out;";
      o->newline(-1) << "}";

      o->newline() << "rwlock_init (& global.s_" << c_varname (v->name) << "_lock);";
    }

  // initialize each Stat used for timing information
  o->newline() << "#ifdef STP_TIMING";
  set<string> basest_names;
  for (unsigned i=0; i<session->probes.size(); i++)
    {
      string nm = session->probes[i]->basest()->name;
      if (basest_names.find(nm) == basest_names.end())
        {
          o->newline() << "time_" << nm << " = _stp_stat_init (HIST_NONE);";
          // NB: we don't check for null return here, but instead at
          // passage to probe handlers and at final printing.
          basest_names.insert (nm);
        }
    }
  o->newline() << "#endif";

  // Print a message to the kernel log about this module.  This is
  // intended to help debug problems with systemtap modules.

  o->newline() << "_stp_print_kernel_info("
	       << "\"" << VERSION
	       << "/" << dwfl_version (NULL) << "\""
	       << ", (num_online_cpus() * sizeof(struct context))"
	       << ", " << session->probes.size()
	       << ");";

  // Run all probe registrations.  This actually runs begin probes.

  for (unsigned i=0; i<g.size(); i++)
    {
      g[i]->emit_module_init (*session);
      // NB: this gives O(N**2) amount of code, but luckily there
      // are only seven or eight derived_probe_groups, so it's ok.
      o->newline() << "if (rc) {";
      o->newline(1) << "_stp_error (\"probe %s registration error (rc %d)\", probe_point, rc);";
      // NB: we need to be in the error state so timers can shutdown cleanly,
      // and so end probes don't run.  OTOH, error probes can run.
      o->newline() << "atomic_set (&session_state, STAP_SESSION_ERROR);";
      if (i>0)
        for (int j=i-1; j>=0; j--)
          g[j]->emit_module_exit (*session);
      o->newline() << "goto out;";
      o->newline(-1) << "}";
    }

  // All registrations were successful.  Consider the system started.
  o->newline() << "if (atomic_read (&session_state) == STAP_SESSION_STARTING)";
  // NB: only other valid state value is ERROR, in which case we don't
  o->newline(1) << "atomic_set (&session_state, STAP_SESSION_RUNNING);";
  o->newline(-1) << "return 0;";

  // Error handling path; by now all partially registered probe groups
  // have been unregistered.
  o->newline(-1) << "out:";
  o->indent(1);

  // If any registrations failed, we will need to deregister the globals,
  // as this is our only chance.
  for (unsigned i=0; i<session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).fini();
      else
	o->newline() << getvar (v).fini();
    }

  // For any partially registered/unregistered kernel facilities.
  o->newline() << "#ifdef STAPCONF_SYNCHRONIZE_SCHED";
  o->newline() << "synchronize_sched();";
  o->newline() << "#endif";

  // In case gettimeofday was started, it needs to be stopped
  o->newline() << "#ifdef STAP_NEED_GETTIMEOFDAY";
  o->newline() << " _stp_kill_time();";  // An error is no cause to hurry...
  o->newline() << "#endif";

  // Free up the context memory after an error too
  o->newline() << "free_percpu (contexts);";

  o->newline() << "return rc;";
  o->newline(-1) << "}\n";
}


void
c_unparser::emit_module_exit ()
{
  o->newline() << "static void systemtap_module_exit (void) {";
  // rc?
  o->newline(1) << "int holdon;";
  o->newline() << "int i=0, j=0;"; // for derived_probe_group use

  o->newline() << "(void) i;";
  o->newline() << "(void) j;";
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

  // We're processing the derived_probe_group list in reverse
  // order.  This ensures that probes get unregistered in reverse
  // order of the way they were registered.
  vector<derived_probe_group*> g = all_session_groups (*session);
  for (vector<derived_probe_group*>::reverse_iterator i = g.rbegin();
       i != g.rend(); i++)
    (*i)->emit_module_exit (*session); // NB: runs "end" probes

  // But some other probes may have launched too during unregistration.
  // Let's wait a while to make sure they're all done, done, done.

  // cargo cult prologue
  o->newline() << "#ifdef STAPCONF_SYNCHRONIZE_SCHED";
  o->newline() << "synchronize_sched();";
  o->newline() << "#endif";

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
  // NB: we run at least one of these during the shutdown sequence:
  o->newline () << "yield ();"; // aka schedule() and then some
  o->newline(-2) << "} while (holdon);";

  // cargo cult epilogue
  o->newline() << "#ifdef STAPCONF_SYNCHRONIZE_SCHED";
  o->newline() << "synchronize_sched();";
  o->newline() << "#endif";

  // XXX: might like to have an escape hatch, in case some probe is
  // genuinely stuck somehow

  for (unsigned i=0; i<session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).fini();
      else
	o->newline() << getvar (v).fini();
    }

  o->newline() << "free_percpu (contexts);";

  // print probe timing statistics
  {
    o->newline() << "#ifdef STP_TIMING";
    o->newline() << "{";
    o->indent(1);
    set<string> basest_names;
    for (unsigned i=0; i<session->probes.size(); i++)
      {
        probe* p = session->probes[i]->basest();
        string nm = p->name;
        if (basest_names.find(nm) == basest_names.end())
          {
            basest_names.insert (nm);
            // NB: check for null stat object
            o->newline() << "if (likely (time_" << p->name << ")) {";
            o->newline(1) << "const char *probe_point = "
                         << lex_cast_qstring (* p->locations[0])
                         << (p->locations.size() > 1 ? "\"+\"" : "")
                         << (p->locations.size() > 1 ? lex_cast_qstring(p->locations.size()-1) : "")
                         << ";";
            o->newline() << "const char *decl_location = "
                         << lex_cast_qstring (p->tok->location)
                         << ";";
            o->newline() << "struct stat_data *stats = _stp_stat_get (time_"
                         << p->name
                         << ", 0);";
            o->newline() << "if (stats->count) {";
            o->newline(1) << "int64_t avg = _stp_div64 (NULL, stats->sum, stats->count);";
            o->newline() << "_stp_printf (\"probe %s (%s), hits: %lld, cycles: %lldmin/%lldavg/%lldmax\\n\",";
            o->newline() << "probe_point, decl_location, (long long) stats->count, (long long) stats->min, (long long) avg, (long long) stats->max);";
            o->newline(-1) << "}";
	    o->newline() << "_stp_stat_del (time_" << p->name << ");";
            o->newline(-1) << "}";
          }
      }
    o->newline() << "_stp_print_flush();";
    o->newline(-1) << "}";
    o->newline() << "#endif";
  }

  // teardown gettimeofday (if needed)
  o->newline() << "#ifdef STAP_NEED_GETTIMEOFDAY";
  o->newline() << " _stp_kill_time();";  // Go to a beach.  Drink a beer.
  o->newline() << "#endif";

  // print final error/skipped counts if non-zero
  o->newline() << "if (atomic_read (& skipped_count) || "
               << "atomic_read (& error_count) || "
               << "atomic_read (& skipped_count_reentrant)) {"; // PR9967
  o->newline(1) << "_stp_warn (\"Number of errors: %d, "
                << "skipped probes: %d\\n\", "
                << "(int) atomic_read (& error_count), "
                << "(int) atomic_read (& skipped_count));";
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "{";
  o->newline(1) << "int ctr;";
  for (unsigned i=0; i<session->globals.size(); i++)
    {
      string vn = c_varname (session->globals[i]->name);
      o->newline() << "ctr = atomic_read (& global.s_" << vn << "_lock_skip_count);";
      o->newline() << "if (ctr) _stp_warn (\"Skipped due to global '%s' lock timeout: %d\\n\", "
                   << lex_cast_qstring(vn) << ", ctr);";
    }
  o->newline() << "ctr = atomic_read (& skipped_count_lowstack);";
  o->newline() << "if (ctr) _stp_warn (\"Skipped due to low stack: %d\\n\", ctr);";
  o->newline() << "ctr = atomic_read (& skipped_count_reentrant);";
  o->newline() << "if (ctr) _stp_warn (\"Skipped due to reentrancy: %d\\n\", ctr);";
  o->newline() << "ctr = atomic_read (& skipped_count_uprobe_reg);";
  o->newline() << "if (ctr) _stp_warn (\"Skipped due to uprobe register failure: %d\\n\", ctr);";
  o->newline() << "ctr = atomic_read (& skipped_count_uprobe_unreg);";
  o->newline() << "if (ctr) _stp_warn (\"Skipped due to uprobe unregister failure: %d\\n\", ctr);";
  o->newline(-1) << "}";
  o->newline () << "#endif";
  o->newline() << "_stp_print_flush();";
  o->newline(-1) << "}";
  o->newline(-1) << "}\n";
}


void
c_unparser::emit_function (functiondecl* v)
{
  o->newline() << "static void function_" << c_varname (v->name)
            << " (struct context* __restrict__ c) {";
  o->indent(1);
  this->current_probe = 0;
  this->current_function = v;
  this->tmpvar_counter = 0;
  this->action_counter = 0;

  o->newline()
    << "struct function_" << c_varname (v->name) << "_locals * "
    << " __restrict__ l =";
  o->newline(1)
    << "& c->locals[c->nesting+1].function_" << c_varname (v->name) // NB: nesting+1
    << ";";
  o->newline(-1) << "(void) l;"; // make sure "l" is marked used
  o->newline() << "#define CONTEXT c";
  o->newline() << "#define THIS l";
  o->newline() << "if (0) goto out;"; // make sure out: is marked used

  // set this, in case embedded-c code sets last_error but doesn't otherwise identify itself
  o->newline() << "c->last_stmt = " << lex_cast_qstring(*v->tok) << ";";

  // check/increment nesting level
  o->newline() << "if (unlikely (c->nesting+2 >= MAXNESTING)) {";
  o->newline(1) << "c->last_error = \"MAXNESTING exceeded\";";
  o->newline() << "return;";
  o->newline(-1) << "} else {";
  o->newline(1) << "c->nesting ++;";
  o->newline(-1) << "}";

  // initialize locals
  // XXX: optimization: use memset instead
  for (unsigned i=0; i<v->locals.size(); i++)
    {
      if (v->locals[i]->index_types.size() > 0) // array?
	throw semantic_error ("array locals not supported, missing global declaration?",
                              v->locals[i]->tok);

      o->newline() << getvar (v->locals[i]).init();
    }

  // initialize return value, if any
  if (v->type != pe_unknown)
    {
      var retvalue = var(true, v->type, "__retvalue");
      o->newline() << retvalue.init();
    }

  o->newline() << "#define return goto out"; // redirect embedded-C return
  this->probe_or_function_needs_deref_fault_handler = false;
  v->body->visit (this);
  o->newline() << "#undef return";

  this->current_function = 0;

  record_actions(0, true);

  if (this->probe_or_function_needs_deref_fault_handler) {
    // Emit this handler only if the body included a
    // print/printf/etc. using a string or memory buffer!
    o->newline() << "CATCH_DEREF_FAULT ();";
  }

  o->newline(-1) << "out:";
  o->newline(1) << ";";

  // Function prologue: this is why we redirect the "return" above.
  // Decrement nesting level.
  o->newline() << "c->nesting --;";

  o->newline() << "#undef CONTEXT";
  o->newline() << "#undef THIS";
  o->newline(-1) << "}\n";
}


#define DUPMETHOD_CALL 0
#define DUPMETHOD_ALIAS 0
#define DUPMETHOD_RENAME 1

void
c_unparser::emit_probe (derived_probe* v)
{
  this->current_function = 0;
  this->current_probe = v;
  this->tmpvar_counter = 0;
  this->action_counter = 0;

  // If we about to emit a probe that is exactly the same as another
  // probe previously emitted, make the second probe just call the
  // first one.
  //
  // Notice we're using the probe body itself instead of the emitted C
  // probe body to compare probes.  We need to do this because the
  // emitted C probe body has stuff in it like:
  //   c->last_stmt = "identifier 'printf' at foo.stp:<line>:<column>";
  //
  // which would make comparisons impossible.
  //
  // --------------------------------------------------------------------------
  // NB: see also c_unparser:emit_common_header(), which deliberately but sadly
  // duplicates this calculation.
  // --------------------------------------------------------------------------
  //
  ostringstream oss;

  // NB: statp is just for avoiding designation as duplicate.  It need not be C.
  // NB: This code *could* be enclosed in an "if (session->timing)".  That would
  // recognize more duplicate probe handlers, but then the generated code could
  // be very different with or without -t.
  oss << "c->statp = & time_" << v->basest()->name << ";" << endl;

  v->print_dupe_stamp (oss);
  v->body->print(oss);

  // Since the generated C changes based on whether or not the probe
  // needs locks around global variables, this needs to be reflected
  // here.  We don't want to treat as duplicate the handlers of
  // begin/end and normal probes that differ only in need_global_locks.
  oss << "# needs_global_locks: " << v->needs_global_locks () << endl;

  // If an identical probe has already been emitted, just call that
  // one.
  if (probe_contents.count(oss.str()) != 0)
    {
      string dupe = probe_contents[oss.str()];

      // NB: Elision of context variable structs is a separate
      // operation which has already taken place by now.
      if (session->verbose > 1)
        clog << v->name << " elided, duplicates " << dupe << endl;

#if DUPMETHOD_CALL
      // This one emits a direct call to the first copy.
      o->newline();
      o->newline() << "static void " << v->name << " (struct context * __restrict__ c) ";
      o->newline() << "{ " << dupe << " (c); }";
#elif DUPMETHOD_ALIAS
      // This one defines a function alias, arranging gcc to emit
      // several equivalent symbols for the same function body.
      // For some reason, on gcc 4.1, this is twice as slow as
      // the CALL option.
      o->newline();
      o->newline() << "static void " << v->name << " (struct context * __restrict__ c) ";
      o->line() << "__attribute__ ((alias (\"" << dupe << "\")));";
#elif DUPMETHOD_RENAME
      // This one is sneaky.  It emits nothing for duplicate probe
      // handlers.  It instead redirects subsequent references to the
      // probe handler function to the first copy, *by name*.
      v->name = dupe;
#else
#error "Unknown duplicate elimination method"
#endif
    }
  else // This probe is unique.  Remember it and output it.
    {
      this->probe_or_function_needs_deref_fault_handler = false;

      o->newline();
      o->newline() << "#ifdef STP_TIMING";
      o->newline() << "static __cacheline_aligned Stat " << "time_" << v->basest()->name << ";";
      o->newline() << "#endif";
      o->newline();
      o->newline() << "static void " << v->name << " (struct context * __restrict__ c) ";
      o->line () << "{";
      o->indent (1);

      probe_contents[oss.str()] = v->name;

      // initialize frame pointer
      o->newline() << "struct " << v->name << "_locals * __restrict__ l =";
      o->newline(1) << "& c->locals[0]." << v->name << ";";
      o->newline(-1) << "(void) l;"; // make sure "l" is marked used

      o->newline() << "#ifdef STP_TIMING";
      o->newline() << "c->statp = & time_" << v->basest()->name << ";";
      o->newline() << "#endif";

      // emit probe local initialization block
      v->emit_probe_local_init(o);

      // emit all read/write locks for global variables
      varuse_collecting_visitor vut;
      if (v->needs_global_locks ())
        {
	  v->body->visit (& vut);
	  emit_locks (vut);
	}

      // initialize locals
      for (unsigned j=0; j<v->locals.size(); j++)
        {
	  if (v->locals[j]->index_types.size() > 0) // array?
            throw semantic_error ("array locals not supported, missing global declaration?",
                                  v->locals[j]->tok);
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

      v->initialize_probe_context_vars (o);

      v->body->visit (this);

      record_actions(0, true);

      if (this->probe_or_function_needs_deref_fault_handler) {
	// Emit this handler only if the body included a
	// print/printf/etc. using a string or memory buffer!
	o->newline() << "CATCH_DEREF_FAULT ();";
      }

      o->newline(-1) << "out:";
      // NB: no need to uninitialize locals, except if arrays/stats can
      // someday be local

      // XXX: do this flush only if the body included a
      // print/printf/etc. routine!
      o->newline(1) << "_stp_print_flush();";

      if (v->needs_global_locks ())
	emit_unlocks (vut);

      o->newline(-1) << "}\n";
    }


  this->current_probe = 0;
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

      // We don't need to read lock "read-mostly" global variables.  A
      // "read-mostly" global variable is only written to within
      // probes that don't need global variable locking (such as
      // begin/end probes).  If vcv_needs_global_locks doesn't mark
      // the global as written to, then we don't have to lock it
      // here to read it safely.
      if (read_p && !write_p)
        {
	  if (vcv_needs_global_locks.written.find(v)
	      == vcv_needs_global_locks.written.end())
	    continue;
	}

      string lockcall =
        string (write_p ? "write" : "read") +
        "_trylock (& global.s_" + v->name + "_lock)";

      o->newline() << "while (! " << lockcall
                   << "&& (++numtrylock < MAXTRYLOCK))";
      o->newline(1) << "ndelay (TRYLOCKDELAY);";
      o->newline(-1) << "if (unlikely (numtrylock >= MAXTRYLOCK)) {";
      o->newline(1) << "atomic_inc (& skipped_count);";
      o->newline() << "#ifdef STP_TIMING";
      o->newline() << "atomic_inc (& global.s_" << c_varname (v->name) << "_lock_skip_count);";
      o->newline() << "#endif";
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
    clog << current_probe->name << " locks ";

  for (int i = session->globals.size()-1; i>=0; i--) // in reverse order!
    {
      vardecl* v = session->globals[i];
      bool read_p = vut.read.find(v) != vut.read.end();
      bool write_p = vut.written.find(v) != vut.written.end();
      if (!read_p && !write_p) continue;

      // Duplicate lock flipping logic from above
      if (v->type == pe_stats)
        {
          if (write_p && !read_p) { read_p = true; write_p = false; }
          else if (read_p && !write_p) { read_p = false; write_p = true; }
        }

      // Duplicate "read-mostly" global variable logic from above.
      if (read_p && !write_p)
        {
	  if (vcv_needs_global_locks.written.find(v)
	      == vcv_needs_global_locks.written.end())
	    continue;
	}

      numvars ++;
      o->newline(-1) << "unlock_" << v->name << ":";
      o->indent(1);

      if (session->verbose>1)
        clog << v->name << "[" << (read_p ? "r" : "")
             << (write_p ? "w" : "")  << "] ";

      if (write_p) // emit write lock
        o->newline() << "write_unlock (& global.s_" << v->name << "_lock);";
      else // (read_p && !write_p) : emit read lock
        o->newline() << "read_unlock (& global.s_" << v->name << "_lock);";

      // fall through to next variable; thus the reverse ordering
    }

  // emit plain "unlock" label, used if the very first lock failed.
  o->newline(-1) << "unlock_: ;";
  o->indent(1);

  if (numvars) // is there a chance that any lock attempt failed?
    {
      // Formerly, we checked skipped_count > MAXSKIPPED here, and set
      // SYSTEMTAP_SESSION_ERROR if so.  But now, this check is shared
      // via common_probe_entryfn_epilogue().

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
    case pe_string:
      return "STRING";
    case pe_stats:
      return "STAT";
    default:
      throw semantic_error("array type is neither string nor long");
    }
  return "";
}

string
mapvar::key_typename(exp_type e)
{
  switch (e)
    {
    case pe_long:
      return "INT64";
    case pe_string:
      return "STRING";
    default:
      throw semantic_error("array key is neither string nor long");
    }
  return "";
}

string
mapvar::shortname(exp_type e)
{
  switch (e)
    {
    case pe_long:
      return "i";
    case pe_string:
      return "s";
    default:
      throw semantic_error("array type is neither string nor long");
    }
  return "";
}


void
c_unparser::emit_map_type_instantiations ()
{
  set< pair<vector<exp_type>, exp_type> > types;

  collect_map_index_types(session->globals, types);

  for (unsigned i = 0; i < session->probes.size(); ++i)
    collect_map_index_types(session->probes[i]->locals, types);

  for (map<string,functiondecl*>::iterator it = session->functions.begin(); it != session->functions.end(); it++)
    collect_map_index_types(it->second->locals, types);

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


string
c_unparser::c_expression (expression *e)
{
  // We want to evaluate expression 'e' and return its value as a
  // string.  In the case of expressions that are just numeric
  // constants, if we just print the value into a string, it won't
  // have the same value as being visited by c_unparser.  For
  // instance, a numeric constant evaluated using print() would return
  // "5", while c_unparser::visit_literal_number() would
  // return "((int64_t)5LL)".  String constants evaluated using
  // print() would just return the string, while
  // c_unparser::visit_literal_string() would return the string with
  // escaped double quote characters.  So, we need to "visit" the
  // expression.

  // However, we have to be careful of side effects.  Currently this
  // code is only being used for evaluating literal numbers and
  // strings, which currently have no side effects.  Until needed
  // otherwise, limit the use of this function to literal numbers and
  // strings.
  if (e->tok->type != tok_number && e->tok->type != tok_string)
    throw semantic_error("unsupported c_expression token type");

  // Create a fake output stream so we can grab the string output.
  ostringstream oss;
  translator_output tmp_o(oss);

  // Temporarily swap out the real translator_output stream with our
  // fake one.
  translator_output *saved_o = o;
  o = &tmp_o;

  // Visit the expression then restore the original output stream
  e->visit (this);
  o = saved_o;

  return (oss.str());
}


void
c_unparser::c_assign (var& lvalue, const string& rvalue, const token *tok)
{
  switch (lvalue.type())
    {
    case pe_string:
      c_strcpy(lvalue.value(), rvalue);
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
  // res: the result of evaluating the expression, a temporary
  // lval: the lvalue of the expression, which may be damaged
  // rval: the rvalue of the expression, which is a temporary or constant

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
	  parent->c_strcpy (lval.value(), rval.value());
	  // no need for second copy
	  res = rval;
	}
      else if (op == ".=")
	{
	  parent->c_strcat (lval.value(), rval.value());
	  res = lval;
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
      else if (op == "++" || op == "+=")
	macop = "+=";
      else if (op == "--" || op == "-=")
	macop = "-=";
      else if (oplen > 1 && op[oplen-1] == '=') // for *=, <<=, etc...
	macop = op;
      else
	// internal error
	throw semantic_error ("unknown macop for assignment", tok);

      if (post)
	{
          if (macop == "/" || macop == "%" || op == "=")
            throw semantic_error ("invalid post-mode operator", tok);

	  o->newline() << res << " = " << lval << ";";

	  if (macop == "+=" || macop == "-=")
	    o->newline() << lval << " " << macop << " " << rval << ";";
	  else
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
	      if (macop == "/=" || macop == "%=")
		{
		  o->newline() << "if (unlikely(!" << rval << ")) {";
		  o->newline(1) << "c->last_error = \"division by 0\";";
		  o->newline() << "goto out;";
		  o->newline(-1) << "}";
		  o->newline() << lval << " = "
			       << ((macop == "/=") ? "_stp_div64" : "_stp_mod64")
			       << " (NULL, " << lval << ", " << rval << ");";
		}
	      else
		o->newline() << lval << " " << macop << " " << rval << ";";
	      res = lval;
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
  return mapvar (is_local (v, tok), v->type, sd,
      v->name, v->index_types, v->maxsize);
}


itervar
c_unparser::getiter(symbol *s)
{
  return itervar (s, tmpvar_counter);
}


// Queue up some actions to remove from actionremaining.  Set update=true at
// the end of basic blocks to actually update actionremaining and check it
// against MAXACTION.
void
c_unparser::record_actions (unsigned actions, bool update)
{
  action_counter += actions;

  // Update if needed, or after queueing up a few actions, in case of very
  // large code sequences.
  if ((update && action_counter > 0) || action_counter >= 10/*<-arbitrary*/)
    {
      o->newline() << "c->actionremaining -= " << action_counter << ";";
      o->newline() << "if (unlikely (c->actionremaining <= 0)) {";
      o->newline(1) << "c->last_error = \"MAXACTION exceeded\";";
      o->newline() << "goto out;";
      o->newline(-1) << "}";
      action_counter = 0;
    }
}


void
c_unparser::visit_block (block *s)
{
  o->newline() << "{";
  o->indent (1);

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
  o->newline() << "{";
  o->newline(1) << s->code;
  o->newline(-1) << "}";
}


void
c_unparser::visit_null_statement (null_statement *)
{
  o->newline() << "/* null */;";
}


void
c_unparser::visit_expr_statement (expr_statement *s)
{
  o->newline() << "(void) ";
  s->value->visit (this);
  o->line() << ";";
  record_actions(1);
}


void
c_unparser::visit_if_statement (if_statement *s)
{
  record_actions(1, true);
  o->newline() << "if (";
  o->indent (1);
  s->condition->visit (this);
  o->indent (-1);
  o->line() << ") {";
  o->indent (1);
  s->thenblock->visit (this);
  record_actions(0, true);
  o->newline(-1) << "}";
  if (s->elseblock)
    {
      o->newline() << "else {";
      o->indent (1);
      s->elseblock->visit (this);
      record_actions(0, true);
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
      // To avoid lots of empty structs inside the union, remember
      // where we are now.  Then, output the struct start and remember
      // that positon.  If when we get done with the statement we
      // haven't moved, then we don't really need the struct.  To get
      // rid of the struct start we output, we'll seek back to where
      // we were before we output the struct.
      std::ostream::pos_type before_struct_pos = parent->o->tellp();
      parent->o->newline() << "struct {";
      parent->o->indent(1);
      std::ostream::pos_type after_struct_pos = parent->o->tellp();
      s->statements[i]->visit (this);
      parent->o->indent(-1);
      if (after_struct_pos == parent->o->tellp())
	parent->o->seekp(before_struct_pos);
      else
	parent->o->newline() << "};";
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
  string ctr = stringify (label_counter++);
  string toplabel = "top_" + ctr;
  string contlabel = "continue_" + ctr;
  string breaklabel = "break_" + ctr;

  // initialization
  if (s->init) s->init->visit (this);
  record_actions(1, true);

  // condition
  o->newline(-1) << toplabel << ":";

  // Emit an explicit action here to cover the act of iteration.
  // Equivalently, it can stand for the evaluation of the condition
  // expression.
  o->indent(1);
  record_actions(1);

  o->newline() << "if (! (";
  if (s->cond->type != pe_long)
    throw semantic_error ("expected numeric type", s->cond->tok);
  s->cond->visit (this);
  o->line() << ")) goto " << breaklabel << ";";

  // body
  loop_break_labels.push_back (breaklabel);
  loop_continue_labels.push_back (contlabel);
  s->block->visit (this);
  record_actions(0, true);
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

  // Create a temporary for the loop limit counter and the limit
  // expression result.
  if (s->limit)
    {
      tmpvar res_limit = parent->gensym (pe_long);
      res_limit.declare(*parent);

      s->limit->visit (this);

      tmpvar limitv = parent->gensym (pe_long);
      limitv.declare(*parent);
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
      mapvar mv = getmap (array->referent, s->tok);
      itervar iv = getiter (array);
      vector<var> keys;

      string ctr = stringify (label_counter++);
      string toplabel = "top_" + ctr;
      string contlabel = "continue_" + ctr;
      string breaklabel = "break_" + ctr;

      // NB: structure parallels for_loop

      // initialization

      tmpvar *res_limit = NULL;
      if (s->limit)
        {
	  // Evaluate the limit expression once.
	  res_limit = new tmpvar(gensym(pe_long));
	  c_assign (res_limit->value(), s->limit, "foreach limit");
	}

      // aggregate array if required
      if (mv.is_parallel())
	{
	  o->newline() << "if (unlikely(NULL == " << mv.calculate_aggregate() << ")) {";
	  o->newline(1) << "c->last_error = \"aggregation overflow in " << mv << "\";";
	  o->newline() << "goto out;";
	  o->newline(-1) << "}";

	  // sort array if desired
	  if (s->sort_direction)
	    {
	      int sort_column;

	      // If the user wanted us to sort by value, we'll sort by
	      // @count instead for aggregates.  '-5' tells the
	      // runtime to sort by count.
	      if (s->sort_column == 0)
		sort_column = -5;
	      else
		sort_column = s->sort_column;

	      o->newline() << "else"; // only sort if aggregation was ok
	      if (s->limit)
	        {
		  o->newline(1) << "_stp_map_sortn ("
				<< mv.fetch_existing_aggregate() << ", "
				<< *res_limit << ", " << sort_column << ", "
				<< - s->sort_direction << ");";
		}
	      else
	        {
		  o->newline(1) << "_stp_map_sort ("
				<< mv.fetch_existing_aggregate() << ", "
				<< sort_column << ", "
				<< - s->sort_direction << ");";
		}
	      o->indent(-1);
	    }
        }
      else
	{
	  // sort array if desired
	  if (s->sort_direction)
	    {
	      if (s->limit)
	        {
		  o->newline() << "_stp_map_sortn (" << mv.value() << ", "
			       << *res_limit << ", " << s->sort_column << ", "
			       << - s->sort_direction << ");";
		}
	      else
	        {
		  o->newline() << "_stp_map_sort (" << mv.value() << ", "
			       << s->sort_column << ", "
			       << - s->sort_direction << ");";
		}
	    }
	}

      // NB: sort direction sense is opposite in runtime, thus the negation

      if (mv.is_parallel())
	aggregations_active.insert(mv.value());
      o->newline() << iv << " = " << iv.start (mv) << ";";

      tmpvar *limitv = NULL;
      if (s->limit)
      {
	  // Create the loop limit variable here and initialize it.
	  limitv = new tmpvar(gensym (pe_long));
	  o->newline() << *limitv << " = 0LL;";
      }

      record_actions(1, true);

      // condition
      o->newline(-1) << toplabel << ":";

      // Emit an explicit action here to cover the act of iteration.
      // Equivalently, it can stand for the evaluation of the
      // condition expression.
      o->indent(1);
      record_actions(1);

      o->newline() << "if (! (" << iv << ")) goto " << breaklabel << ";";

      // body
      loop_break_labels.push_back (breaklabel);
      loop_continue_labels.push_back (contlabel);
      o->newline() << "{";
      o->indent (1);

      if (s->limit)
      {
	  // If we've been through LIMIT loop iterations, quit.
	  o->newline() << "if (" << *limitv << "++ >= " << *res_limit
		       << ") goto " << breaklabel << ";";

	  // We're done with limitv and res_limit.
	  delete limitv;
	  delete res_limit;
      }

      for (unsigned i = 0; i < s->indexes.size(); ++i)
	{
	  // copy the iter values into the specified locals
	  var v = getvar (s->indexes[i]->referent);
	  c_assign (v, iv.get_key (v.type(), i), s->tok);
	}
      s->block->visit (this);
      record_actions(0, true);
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
	aggregations_active.erase(mv.value());
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

      tmpvar *res_limit = NULL;
      tmpvar *limitv = NULL;
      if (s->limit)
        {
	  // Evaluate the limit expression once.
	  res_limit = new tmpvar(gensym(pe_long));
	  c_assign (res_limit->value(), s->limit, "foreach limit");

	  // Create the loop limit variable here and initialize it.
	  limitv = new tmpvar(gensym (pe_long));
	  o->newline() << *limitv << " = 0LL;";
	}

      // XXX: break / continue don't work here yet
      record_actions(1, true);
      o->newline() << "for (" << bucketvar << " = 0; "
		   << bucketvar << " < " << v.buckets() << "; "
		   << bucketvar << "++) { ";
      o->newline(1);

      if (s->limit)
      {
	  // If we've been through LIMIT loop iterations, quit.
	  o->newline() << "if (" << *limitv << "++ >= " << *res_limit
		       << ") break;";

	  // We're done with limitv and res_limit.
	  delete limitv;
	  delete res_limit;
      }

      s->block->visit (this);
      record_actions(1, true);
      o->newline(-1) << "}";
    }
}


void
c_unparser::visit_return_statement (return_statement* s)
{
  if (current_function == 0)
    throw semantic_error ("cannot 'return' from probe", s->tok);

  if (s->value->type != current_function->type)
    throw semantic_error ("return type mismatch", current_function->tok,
                         "vs", s->tok);

  c_assign ("l->__retvalue", s->value, "return value");
  record_actions(1, true);
  o->newline() << "goto out;";
}


void
c_unparser::visit_next_statement (next_statement* s)
{
  if (current_probe == 0)
    throw semantic_error ("cannot 'next' from function", s->tok);

  record_actions(1, true);
  o->newline() << "goto out;";
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
  assert (e->referent != 0);
  if (e->referent->arity > 0)
    {
      mapvar mvar = parent->getmap(e->referent, e->tok);
      /* NB: Memory deallocation/allocation operations
       are not generally safe.
      parent->o->newline() << mvar.fini ();
      parent->o->newline() << mvar.init ();
      */
      if (mvar.is_parallel())
	parent->o->newline() << "_stp_pmap_clear (" << mvar.value() << ");";
      else
	parent->o->newline() << "_stp_map_clear (" << mvar.value() << ");";
    }
  else
    {
      var v = parent->getvar(e->referent, e->tok);
      switch (e->type)
	{
	case pe_stats:
	  parent->o->newline() << "_stp_stat_clear (" << v.value() << ");";
	  break;
	case pe_long:
	  parent->o->newline() << v.value() << " = 0;";
	  break;
	case pe_string:
	  parent->o->newline() << v.value() << "[0] = '\\0';";
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
      assert (array->referent != 0);
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
  delete_statement_operand_visitor dv (this);
  s->value->visit (&dv);
  record_actions(1);
}


void
c_unparser::visit_break_statement (break_statement* s)
{
  if (loop_break_labels.size() == 0)
    throw semantic_error ("cannot 'break' outside loop", s->tok);

  record_actions(1, true);
  string label = loop_break_labels[loop_break_labels.size()-1];
  o->newline() << "goto " << label << ";";
}


void
c_unparser::visit_continue_statement (continue_statement* s)
{
  if (loop_continue_labels.size() == 0)
    throw semantic_error ("cannot 'continue' outside loop", s->tok);

  record_actions(1, true);
  string label = loop_continue_labels[loop_continue_labels.size()-1];
  o->newline() << "goto " << label << ";";
}



void
c_unparser::visit_literal_string (literal_string* e)
{
  const string& v = e->value;
  o->line() << '"';
  for (unsigned i=0; i<v.size(); i++)
    // NB: The backslash character is specifically passed through as is.
    // This is because our parser treats "\" as an ordinary character, not
    // an escape sequence, leaving it to the C compiler (and this function)
    // to treat it as such.  If we were to escape it, there would be no way
    // of generating C-level escapes from script code.
    // See also print_format::components_to_string and lex_cast_qstring
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
  if (e->value == -9223372036854775807LL-1) // PR 5023
    o->line() << "((int64_t)" << (unsigned long long) e->value << "ULL)";
  else
    o->line() << "((int64_t)" << e->value << "LL)";
}


void
c_tmpcounter::visit_binary_expression (binary_expression* e)
{
  if (e->op == "/" || e->op == "%")
    {
      tmpvar left = parent->gensym (pe_long);
      tmpvar right = parent->gensym (pe_long);
      if (e->left->tok->type != tok_number)
        left.declare (*parent);
      if (e->right->tok->type != tok_number)
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
      e->op == "^")
    {
      o->line() << "((";
      e->left->visit (this);
      o->line() << ") " << e->op << " (";
      e->right->visit (this);
      o->line() << "))";
    }
  else if (e->op == ">>" ||
           e->op == "<<")
    {
      o->line() << "((";
      e->left->visit (this);
      o->line() << ") " << e->op << "max(min(";
      e->right->visit (this);
      o->line() << ", (int64_t)64LL), (int64_t)0LL))"; // between 0 and 64
    }
  else if (e->op == "/" ||
           e->op == "%")
    {
      // % and / need a division-by-zero check; and thus two temporaries
      // for proper evaluation order
      tmpvar left = gensym (pe_long);
      tmpvar right = gensym (pe_long);

      o->line() << "({";
      o->indent(1);

      if (e->left->tok->type == tok_number)
	left.override(c_expression(e->left));
      else
        {
	  o->newline() << left << " = ";
	  e->left->visit (this);
	  o->line() << ";";
	}

      if (e->right->tok->type == tok_number)
	right.override(c_expression(e->right));
      else
        {
	  o->newline() << right << " = ";
	  e->right->visit (this);
	  o->line() << ";";
	}

      o->newline() << "if (unlikely(!" << right << ")) {";
      o->newline(1) << "c->last_error = \"division by 0\";";
      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
      o->newline() << "goto out;";
      o->newline(-1) << "}";
      o->newline() << ((e->op == "/") ? "_stp_div64" : "_stp_mod64")
		   << " (NULL, " << left << ", " << right << ");";

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

  if (e->op == "-")
    {
      // NB: Subtraction is special, since negative literals in the
      // script language show up as unary negations over positive
      // literals here.  This makes it "exciting" for emitting pure
      // C since: - 0x8000_0000_0000_0000 ==> - (- 9223372036854775808)
      // This would constitute a signed overflow, which gcc warns on
      // unless -ftrapv/-J are in CFLAGS - which they're not.

      o->line() << "(int64_t)(0 " << e->op << " (uint64_t)(";
      e->operand->visit (this);
      o->line() << "))";
    }
  else
    {
      o->line() << "(" << e->op << " (";
      e->operand->visit (this);
      o->line() << "))";
    }
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
      assert (array->referent != 0);
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
      // o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";

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
      if (e->right->type != pe_string)
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
      if (e->right->type != pe_long)
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
  // o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
  c_assign (t.value(), e->left, "assignment");
  c_strcat (t.value(), e->right);
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
  c_tmpcounter_assignment tav (this, e->op, 0, true);
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
  assert (e->referent != 0);
  vardecl* r = e->referent;

  if (r->index_types.size() != 0)
    throw semantic_error ("invalid reference to array", e->tok);

  var v = getvar(r, e->tok);
  o->line() << v;
}


void
c_tmpcounter_assignment::prepare_rvalue (tmpvar & rval)
{
  if (rvalue)
    {
      // literal number and strings don't need any temporaries declared
      if (rvalue->tok->type != tok_number && rvalue->tok->type != tok_string)
	rval.declare (*(parent->parent));

      rvalue->visit (parent);
    }
}

void
c_tmpcounter_assignment::c_assignop(tmpvar & res)
{
  if (res.type() == pe_string)
    {
      // string assignment doesn't need any temporaries declared
    }
  else if (op == "<<<")
    res.declare (*(parent->parent));
  else if (res.type() == pe_long)
    {
      // Only the 'post' operators ('x++') need a temporary declared.
      if (post)
	res.declare (*(parent->parent));
    }
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
  tmpvar rval = parent->parent->gensym (ty);
  tmpvar res = parent->parent->gensym (ty);

  prepare_rvalue(rval);

  c_assignop (res);
}


void
c_unparser_assignment::prepare_rvalue (string const & op,
				       tmpvar & rval,
				       token const * tok)
{
  if (rvalue)
    {
      if (rvalue->tok->type == tok_number || rvalue->tok->type == tok_string)
	// Instead of assigning the numeric or string constant to a
	// temporary, then assigning the temporary to the final, let's
	// just override the temporary with the constant.
	rval.override(parent->c_expression(rvalue));
      else
	parent->c_assign (rval.value(), rvalue, "assignment");
    }
  else
    {
      if (op == "++" || op == "--")
	// Here is part of the conversion proccess of turning "x++" to
	// "x += 1".
        rval.override("1");
      else
        throw semantic_error ("need rvalue for assignment", tok);
    }
}

void
c_unparser_assignment::visit_symbol (symbol *e)
{
  stmt_expr block(*parent);

  assert (e->referent != 0);
  if (e->referent->index_types.size() != 0)
    throw semantic_error ("unexpected reference to array", e->tok);

  // parent->o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
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
  if (!e->probe_context_var.empty())
    o->line() << "l->" << e->probe_context_var;
  else
    throw semantic_error("cannot translate general cast expression", e->tok);
}


void
c_unparser::visit_cast_op (cast_op* e)
{
  throw semantic_error("cannot translate general cast expression", e->tok);
}


void
c_tmpcounter::load_map_indices(arrayindex *e)
{
  symbol *array;
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      assert (array->referent != 0);
      vardecl* r = array->referent;

      // One temporary per index dimension, except in the case of
      // number or string constants.
      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  tmpvar ix = parent->gensym (r->index_types[i]);
	  if (e->indexes[i]->tok->type == tok_number
	      || e->indexes[i]->tok->type == tok_string)
	    {
	      // Do nothing
	    }
	  else
	    ix.declare (*parent);
	  e->indexes[i]->visit(this);
	}
    }
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

      assert (array->referent != 0);
      vardecl* r = array->referent;

      if (r->index_types.size() == 0 ||
	  r->index_types.size() != e->indexes.size())
	throw semantic_error ("invalid array reference", e->tok);

      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  if (r->index_types[i] != e->indexes[i]->type)
	    throw semantic_error ("array index type mismatch", e->indexes[i]->tok);

	  tmpvar ix = gensym (r->index_types[i]);
	  if (e->indexes[i]->tok->type == tok_number
	      || e->indexes[i]->tok->type == tok_string)
	    // Instead of assigning the numeric or string constant to a
	    // temporary, then using the temporary, let's just
	    // override the temporary with the constant.
	    ix.override(c_expression(e->indexes[i]));
	  else
	    {
	      // o->newline() << "c->last_stmt = "
              // << lex_cast_qstring(*e->indexes[i]->tok) << ";";
	      c_assign (ix.value(), e->indexes[i], "array index copy");
	    }
	  idx.push_back (ix);
	}
    }
  else
    {
      assert (e->indexes.size() == 1);
      assert (e->indexes[0]->type == pe_long);
      tmpvar ix = gensym (pe_long);
      // o->newline() << "c->last_stmt = "
      //	   << lex_cast_qstring(*e->indexes[0]->tok) << ";";
      c_assign (ix.value(), e->indexes[0], "array index copy");
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
      // o->newline() << "c->last_stmt = " << lex_cast_qstring(*sym->tok) << ";";
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
      // o->newline() << "c->last_stmt = " << lex_cast_qstring(*sym->tok) << ";";
      o->newline() << agg << " = " << mvar.get(idx, pre_agg) << ";";
    }
}


string
c_unparser::histogram_index_check(var & base, tmpvar & idx) const
{
  return "((" + idx.value() + " >= 0)"
    + " && (" + idx.value() + " < " + base.buckets() + "))";
}


void
c_tmpcounter::visit_arrayindex (arrayindex *e)
{
  symbol *array;
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      load_map_indices(e);

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

      mapvar mvar = getmap (array->referent, e->tok);
      // o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
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

      if (aggregations_active.count(v->value()))
	load_aggregate(hist->stat, agg, true);
      else
        load_aggregate(hist->stat, agg, false);

      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";

      // PR 2142+2610: empty aggregates
      o->newline() << "if (unlikely (" << agg.value() << " == NULL)"
                   << " || " <<  agg.value() << "->count == 0) {";
      o->newline(1) << "c->last_error = \"empty aggregate\";";
      o->newline() << "goto out;";
      o->newline(-1) << "} else {";
      o->newline(1) << "if (" << histogram_index_check(*v, idx[0]) << ")";
      o->newline(1)  << res << " = " << agg << "->histogram[" << idx[0] << "];";
      o->newline(-1) << "else {";
      o->newline(1)  << "c->last_error = \"histogram index out of range\";";
      o->newline() << "goto out;";
      o->newline(-1) << "}";

      o->newline(-1) << "}";
      o->newline() << res << ";";

      delete v;
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
      parent->load_map_indices(e);

      // The expression rval, lval, and result.
      exp_type ty = rvalue ? rvalue->type : e->type;
      tmpvar rval = parent->parent->gensym (ty);
      tmpvar lval = parent->parent->gensym (ty);
      tmpvar res = parent->parent->gensym (ty);

      prepare_rvalue(rval);
      lval.declare (*(parent->parent));

      if (op == "<<<")
	res.declare (*(parent->parent));
      else
	c_assignop(res);
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
  assert (e->referent != 0);
  functiondecl* r = e->referent;
  // one temporary per argument, unless literal numbers or strings
  for (unsigned i=0; i<r->formal_args.size(); i++)
    {
      tmpvar t = parent->gensym (r->formal_args[i]->type);
      if (e->args[i]->tok->type != tok_number
	  && e->args[i]->tok->type != tok_string)
	t.declare (*parent);
      e->args[i]->visit (this);
    }
}


void
c_unparser::visit_functioncall (functioncall* e)
{
  assert (e->referent != 0);
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

      if (r->formal_args[i]->type != e->args[i]->type)
	throw semantic_error ("function argument type mismatch",
			      e->args[i]->tok, "vs", r->formal_args[i]->tok);

      if (e->args[i]->tok->type == tok_number
	  || e->args[i]->tok->type == tok_string)
	t.override(c_expression(e->args[i]));
      else
        {
	  // o->newline() << "c->last_stmt = "
          // << lex_cast_qstring(*e->args[i]->tok) << ";";
	  c_assign (t.value(), e->args[i],
		    "function actual argument evaluation");
	}
      tmp.push_back(t);
    }

  // copy in actual arguments
  for (unsigned i=0; i<e->args.size(); i++)
    {
      if (r->formal_args[i]->type != e->args[i]->type)
	throw semantic_error ("function argument type mismatch",
			      e->args[i]->tok, "vs", r->formal_args[i]->tok);

      c_assign ("c->locals[c->nesting+1].function_" +
		c_varname (r->name) + "." +
                c_varname (r->formal_args[i]->name),
                tmp[i].value(),
                e->args[i]->type,
                "function actual argument copy",
                e->args[i]->tok);
    }

  // call function
  o->newline() << "function_" << c_varname (r->name) << " (c);";
  o->newline() << "if (unlikely(c->last_error)) goto out;";

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

	  if (e->args[i]->tok->type != tok_number
	      && e->args[i]->tok->type != tok_string)
	    t.declare (*parent);
	  e->args[i]->visit (this);
	}

      // And the result
      exp_type ty = e->print_to_stream ? pe_long : pe_string;
      tmpvar res = parent->gensym (ty);
      if (ty == pe_string)
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
	if (aggregations_active.count(v->value()))
	  load_aggregate(e->hist->stat, agg, true);
	else
          load_aggregate(e->hist->stat, agg, false);

        // PR 2142+2610: empty aggregates
        o->newline() << "if (unlikely (" << agg.value() << " == NULL)"
                     << " || " <<  agg.value() << "->count == 0) {";
        o->newline(1) << "c->last_error = \"empty aggregate\";";
	o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
	o->newline() << "goto out;";
        o->newline(-1) << "} else";
	o->newline(1) << "_stp_stat_print_histogram (" << v->hist() << ", " << agg.value() << ");";
        o->indent(-1);
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

	  // o->newline() << "c->last_stmt = "
          //	       << lex_cast_qstring(*e->args[i]->tok) << ";";

	  // If we've got a numeric or string constant, instead of
	  // assigning the numeric or string constant to a temporary,
	  // then passing the temporary to _stp_printf/_stp_snprintf,
	  // let's just override the temporary with the constant.
	  if (e->args[i]->tok->type == tok_number
	      || e->args[i]->tok->type == tok_string)
	    tmp[i].override(c_expression(e->args[i]));
	  else
	    c_assign (t.value(), e->args[i],
		      "print format actual argument evaluation");
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
	      if (i > 0 && e->print_with_delim)
		components.push_back (e->delimiter);
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

	  if (e->print_with_newline)
	    {
	      print_format::format_component curr;
	      curr.clear();
	      curr.type = print_format::conv_literal;
	      curr.literal_string = "\\n";
	      components.push_back (curr);
	    }
	}

      // Allocate the result
      exp_type ty = e->print_to_stream ? pe_long : pe_string;
      tmpvar res = gensym (ty);
      int use_print = 0;

      string format_string = print_format::components_to_string(components);
      if ((tmp.size() == 0 && format_string.find("%%") == std::string::npos)
          || (tmp.size() == 1 && format_string == "%s"))
	use_print = 1;
      else if (tmp.size() == 1
	       && e->args[0]->tok->type == tok_string
	       && format_string == "%s\\n")
	{
	  use_print = 1;
	  tmp[0].override(tmp[0].value() + "\"\\n\"");
	  components[0].type = print_format::conv_literal;
	}

      // Make the [s]printf call...

      // Generate code to check that any pointer arguments are actually accessible.  */
      int arg_ix = 0;
      for (unsigned i = 0; i < components.size(); ++i) {
	if (components[i].type == print_format::conv_literal)
	  continue;

	/* Take note of the width and precision arguments, if any.  */
	int width_ix = -1, prec_ix= -1;
	if (components[i].widthtype == print_format::width_dynamic)
	  width_ix = arg_ix++;
	if (components[i].prectype == print_format::prec_dynamic)
	  prec_ix = arg_ix++;

	/* Generate a noop call to deref_buffer for %m.  */
	if (components[i].type == print_format::conv_memory
	    || components[i].type == print_format::conv_memory_hex) {
	  this->probe_or_function_needs_deref_fault_handler = true;
	  o->newline() << "deref_buffer (0, " << tmp[arg_ix].value() << ", ";
	  if (prec_ix == -1)
	    if (width_ix != -1)
	      prec_ix = width_ix;
	  if (prec_ix != -1)
	    o->line() << tmp[prec_ix].value();
	  else
	    o->line() << "1";
	  o->line() << ");";
	}

	++arg_ix;
      }

      if (e->print_to_stream)
        {
	  if (e->print_char)
	    {
	      o->newline() << "_stp_print_char (";
	      if (tmp.size())
		o->line() << tmp[0].value() << ");";
	      else
		o->line() << '"' << format_string << "\");";
	      return;
	    }
	  if (use_print)
	    {
	      o->newline() << "_stp_print (";
	      if (tmp.size())
		o->line() << tmp[0].value() << ");";
	      else
		o->line() << '"' << format_string << "\");";
	      return;
	    }

	  // We'll just hardcode the result of 0 instead of using the
	  // temporary.
	  res.override("((int64_t)0LL)");
	  o->newline() << "_stp_printf (";
        }
      else
	o->newline() << "_stp_snprintf (" << res.value() << ", MAXSTRINGLEN, ";

      o->line() << '"' << format_string << '"';

      /* Generate the actual arguments. Make sure that they match the expected type of the
	 format specifier.  */
      arg_ix = 0;
      for (unsigned i = 0; i < components.size(); ++i) {
	if (components[i].type == print_format::conv_literal)
	  continue;

	/* Cast the width and precision arguments, if any, to 'int'.  */
	if (components[i].widthtype == print_format::width_dynamic)
	  o->line() << ", (int)" << tmp[arg_ix++].value();
	if (components[i].prectype == print_format::prec_dynamic)
	  o->line() << ", (int)" << tmp[arg_ix++].value();

	/* The type of the %m argument is 'char*'.  */
	if (components[i].type == print_format::conv_memory
	    || components[i].type == print_format::conv_memory_hex)
	  o->line() << ", (char*)(uintptr_t)" << tmp[arg_ix++].value();
	/* The type of the %c argument is 'int'.  */
	else if (components[i].type == print_format::conv_char)
	  o->line() << ", (int)" << tmp[arg_ix++].value();
	else if (arg_ix < (int) tmp.size())
	  o->line() << ", " << tmp[arg_ix++].value();
      }

      o->line() << ");";
      o->newline() << res.value() << ";";
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
      if (aggregations_active.count(v.value()))
	load_aggregate(e->stat, agg, true);
      else
        load_aggregate(e->stat, agg, false);

      // PR 2142+2610: empty aggregates
      if (e->ctype == sc_count)
        {
          o->newline() << "if (unlikely (" << agg.value() << " == NULL))";
          o->indent(1);
          c_assign(res, "0", e->tok);
          o->indent(-1);
        }
      else
        {
          o->newline() << "if (unlikely (" << agg.value() << " == NULL)"
                       << " || " <<  agg.value() << "->count == 0) {";
          o->newline(1) << "c->last_error = \"empty aggregate\";";
          o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
          o->newline() << "goto out;";
          o->newline(-1) << "}";
        }
      o->newline() << "else";
      o->indent(1);
      switch (e->ctype)
        {
        case sc_average:
          c_assign(res, ("_stp_div64(NULL, " + agg.value() + "->sum, "
                         + agg.value() + "->count)"),
                   e->tok);
          break;
        case sc_count:
          c_assign(res, agg.value() + "->count", e->tok);
          break;
        case sc_sum:
          c_assign(res, agg.value() + "->sum", e->tok);
          break;
        case sc_min:
          c_assign(res, agg.value() + "->min", e->tok);
          break;
        case sc_max:
          c_assign(res, agg.value() + "->max", e->tok);
          break;
        }
      o->indent(-1);
    }
    o->newline() << res << ";";
  }
}


void
c_unparser::visit_hist_op (hist_op*)
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



struct unwindsym_dump_context
{
  systemtap_session& session;
  ostream& output;
  unsigned stp_module_index;
  unsigned long stp_kretprobe_trampoline_addr;
  set<string> undone_unwindsym_modules;
};


// Get the .debug_frame end .eh_frame sections for the given module.
// Also returns the lenght of both sections when found, plus the section
// address (offset) of the eh_frame data.
static void get_unwind_data (Dwfl_Module *m,
			     void **debug_frame, void **eh_frame,
			     size_t *debug_len, size_t *eh_len,
			     Dwarf_Addr *eh_addr)
{
  Dwarf_Addr start, bias = 0;
  GElf_Ehdr *ehdr, ehdr_mem;
  GElf_Shdr *shdr, shdr_mem;
  Elf_Scn *scn;
  Elf_Data *data;
  Elf *elf;

  // fetch .eh_frame info preferably from main elf file.
  dwfl_module_info (m, NULL, &start, NULL, NULL, NULL, NULL, NULL);
  elf = dwfl_module_getelf(m, &bias);
  ehdr = gelf_getehdr(elf, &ehdr_mem);
  scn = NULL;
  while ((scn = elf_nextscn(elf, scn)))
    {
      shdr = gelf_getshdr(scn, &shdr_mem);
      if (strcmp(elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name),
		 ".eh_frame") == 0)
	{
	  data = elf_rawdata(scn, NULL);
	  *eh_frame = data->d_buf;
	  *eh_len = data->d_size;
	  // For ".dynamic" sections we want the offset, not absolute addr.
	  if (dwfl_module_relocations (m) > 0)
	    *eh_addr = shdr->sh_addr - start + bias;
	  else
	    *eh_addr = shdr->sh_addr;
	  break;
	}
    }

  // fetch .debug_frame info preferably from dwarf debuginfo file.
  elf = (dwarf_getelf (dwfl_module_getdwarf (m, &bias))
	 ?: dwfl_module_getelf (m, &bias));
  ehdr = gelf_getehdr(elf, &ehdr_mem);
  scn = NULL;
  while ((scn = elf_nextscn(elf, scn)))
    {
      shdr = gelf_getshdr(scn, &shdr_mem);
      if (strcmp(elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name),
		 ".debug_frame") == 0)
	{
	  data = elf_rawdata(scn, NULL);
	  *debug_frame = data->d_buf;
	  *debug_len = data->d_size;
	  break;
	}
    }
}

static int
dump_unwindsyms (Dwfl_Module *m,
                 void **userdata __attribute__ ((unused)),
                 const char *name,
                 Dwarf_Addr base,
                 void *arg)
{
  unwindsym_dump_context* c = (unwindsym_dump_context*) arg;
  assert (c);
  unsigned stpmod_idx = c->stp_module_index;

  string modname = name;

  if (pending_interrupts)
    return DWARF_CB_ABORT;

  // skip modules/files we're not actually interested in
  if (c->session.unwindsym_modules.find(modname) == c->session.unwindsym_modules.end())
    return DWARF_CB_OK;

  c->stp_module_index ++;

  if (c->session.verbose > 1)
    clog << "dump_unwindsyms " << name
         << " index=" << stpmod_idx
         << " base=0x" << hex << base << dec << endl;

  // We want to extract several bits of information:
  //
  // - parts of the program-header that map the file's physical offsets to the text section
  // - section table: just a list of section (relocation) base addresses
  // - symbol table of the text-like sections, with all addresses relativized to each base
  // - the contents of .debug_frame section, for unwinding purposes
  //
  // In the future, we'll also care about data symbols.

  int syments = dwfl_module_getsymtab(m);
  dwfl_assert ("Getting symbol table for " + modname, syments >= 0);

  //extract build-id from debuginfo file
  int build_id_len = 0;
  unsigned char *build_id_bits;
  GElf_Addr build_id_vaddr;

  if ((build_id_len=dwfl_module_build_id(m,
                                        (const unsigned char **)&build_id_bits,
                                         &build_id_vaddr)) > 0)
  {
    // Enable workaround for elfutils dwfl bug.
    // see https://bugzilla.redhat.com/show_bug.cgi?id=465872
    // and http://sourceware.org/ml/systemtap/2008-q4/msg00579.html
#ifdef _ELFUTILS_PREREQ
  #if _ELFUTILS_PREREQ(0,138)
    // Let's standardize to the buggy "end of build-id bits" behavior.
    build_id_vaddr += build_id_len;
  #endif
  #if !_ELFUTILS_PREREQ(0,141)
    #define NEED_ELFUTILS_BUILDID_WORKAROUND
  #endif
#else
  #define NEED_ELFUTILS_BUILDID_WORKAROUND
#endif

    // And check for another workaround needed.
    // see https://bugzilla.redhat.com/show_bug.cgi?id=489439
    // and http://sourceware.org/ml/systemtap/2009-q1/msg00513.html
#ifdef NEED_ELFUTILS_BUILDID_WORKAROUND
    if (build_id_vaddr < base && dwfl_module_relocations (m) == 1)
      {
        GElf_Addr main_bias;
        dwfl_module_getelf (m, &main_bias);
        build_id_vaddr += main_bias;
      }
#endif
    if (c->session.verbose > 1)
      {
        clog << "Found build-id in " << name
             << ", length " << build_id_len;
        clog << ", end at 0x" << hex << build_id_vaddr
             << dec << endl;
      }
  }

  // Get the canonical path of the main file for comparison at runtime.
  // When given directly by the user through -d or in case of the kernel
  // name and path might differ. path should be used for matching.
  // Use end as sanity check when resolving symbol addresses and to
  // calculate size for .dynamic and .absolute sections.
  const char *mainfile;
  Dwarf_Addr start, end;
  dwfl_module_info (m, NULL, &start, &end, NULL, NULL, &mainfile, NULL);

  // Look up the relocation basis for symbols
  int n = dwfl_module_relocations (m);

  dwfl_assert ("dwfl_module_relocations", n >= 0);


  // XXX: unfortunate duplication with tapsets.cxx:emit_address()

  typedef map<Dwarf_Addr,const char*> addrmap_t; // NB: plain map, sorted by address
  vector<pair<string,unsigned> > seclist; // encountered relocation bases
					// (section names and sizes)
  map<unsigned, addrmap_t> addrmap; // per-relocation-base sorted addrmap

  Dwarf_Addr extra_offset = 0;

  for (int i = 0; i < syments; ++i)
    {
      GElf_Sym sym;
      GElf_Word shndxp;
      const char *name = dwfl_module_getsym(m, i, &sym, &shndxp);
      if (name)
        {
          // NB: Yey, we found the kernel's _stext value.
          // Sess.sym_stext may be unset (0) at this point, since
          // there may have been no kernel probes set.  We could
          // use tapsets.cxx:lookup_symbol_address(), but then
          // we're already iterating over the same data here...
          if (modname == "kernel" && !strcmp(name, "_stext"))
            {
              int ki;
              extra_offset = sym.st_value;
              ki = dwfl_module_relocate_address (m, &extra_offset);
              dwfl_assert ("dwfl_module_relocate_address extra_offset",
                           ki >= 0);
              // Sadly dwfl_module_relocate_address is broken on
              // elfutils < 0.138, so we need to adjust for the module
              // base address outself. (see also below).
              extra_offset = sym.st_value - base;
              if (c->session.verbose > 2)
                clog << "Found kernel _stext extra offset 0x" << hex << extra_offset << dec << endl;
            }

	  // We are only interested in "real" symbols.
	  // We omit symbols that have suspicious addresses (before base,
	  // or after end).
          if ((GELF_ST_TYPE (sym.st_info) == STT_FUNC ||
               GELF_ST_TYPE (sym.st_info) == STT_NOTYPE || // PR10206 ppc fn-desc are in .opd
               GELF_ST_TYPE (sym.st_info) == STT_OBJECT) // PR10000: also need .data
               && !(sym.st_shndx == SHN_UNDEF	// Value undefined,
		    || shndxp == (GElf_Word) -1	// in a non-allocated section,
		    || sym.st_value >= end	// beyond current module,
		    || sym.st_value < base))	// before first section.
            {
              Dwarf_Addr sym_addr = sym.st_value;
              Dwarf_Addr save_addr = sym_addr;
              const char *secname = NULL;

              if (n > 0) // only try to relocate if there exist relocation bases
                {
                  int ki = dwfl_module_relocate_address (m, &sym_addr);
                  dwfl_assert ("dwfl_module_relocate_address", ki >= 0);
                  secname = dwfl_module_relocation_info (m, ki, NULL);

                  // For ET_DYN files (secname == "") we do ignore the
                  // dwfl_module_relocate_address adjustment. libdwfl
                  // up to 0.137 would substract the wrong bias. So we do
                  // it ourself, it is always just the module base address
                  // in this case.
                  if (ki == 0 && secname != NULL && secname[0] == '\0')
                    sym_addr = save_addr - base;
		}

              if (n == 1 && modname == "kernel")
                {
                  // This is a symbol within a (possibly relocatable)
                  // kernel image.

		  // We only need the function symbols to identify kernel-mode
		  // PC's, so we omit undefined or "fake" absolute addresses.
		  // These fake absolute addresses occur in some older i386
		  // kernels to indicate they are vDSO symbols, not real
		  // functions in the kernel. We also omit symbols that have
                  if (GELF_ST_TYPE (sym.st_info) == STT_FUNC
		      && sym.st_shndx == SHN_ABS)
		    continue;

                  secname = "_stext";
                  // NB: don't subtract session.sym_stext, which could be inconveniently NULL.
                  // Instead, sym_addr will get compensated later via extra_offset.

                  // We need to note this for the unwinder.
                  if (c->stp_kretprobe_trampoline_addr == (unsigned long) -1
                      && ! strcmp (name, "kretprobe_trampoline_holder"))
                    c->stp_kretprobe_trampoline_addr = sym_addr;
                }
              else if (n > 0)
                {
                  assert (secname != NULL);
                  // secname adequately set

                  // NB: it may be an empty string for ET_DYN objects
                  // like shared libraries, as their relocation base
                  // is implicit.
                  if (secname[0] == '\0')
                    secname = ".dynamic";
                }
              else
                {
                  assert (n == 0);
                  // sym_addr is absolute, as it must be since there are no relocation bases
                  secname = ".absolute"; // sentinel
                }

              // Compute our section number
              unsigned secidx;
              for (secidx=0; secidx<seclist.size(); secidx++)
                if (seclist[secidx].first==secname) break;

              if (secidx == seclist.size()) // new section name
		{
                  // absolute, dynamic or kernel have just one relocation
                  // section, which covers the whole module address range.
                  unsigned size;
                  if (n <= 1)
                    size = end - start;
                  else
                   {
                     Dwarf_Addr b;
                     Elf_Scn *scn;
                     GElf_Shdr *shdr, shdr_mem;
                     scn = dwfl_module_address_section (m, &save_addr, &b);
                     assert (scn != NULL);
                     shdr = gelf_getshdr(scn, &shdr_mem);
                     size = shdr->sh_size;
                   }
                  seclist.push_back (make_pair(secname,size));
		}

              (addrmap[secidx])[sym_addr] = name;
            }
        }
    }

  // Must be relative to actual kernel load address.
  if (c->stp_kretprobe_trampoline_addr != (unsigned long) -1)
    c->stp_kretprobe_trampoline_addr -= extra_offset;

  // Add unwind data to be included if it exists for this module.
  void *debug_frame = NULL;
  size_t debug_len = 0;
  void *eh_frame = NULL;
  size_t eh_len = 0;
  Dwarf_Addr eh_addr = 0;
  get_unwind_data (m, &debug_frame, &eh_frame, &debug_len, &eh_len, &eh_addr);
  if (debug_frame != NULL && debug_len > 0)
    {
      c->output << "#if defined(STP_USE_DWARF_UNWINDER) && defined(STP_NEED_UNWIND_DATA)\n";
      c->output << "static uint8_t _stp_module_" << stpmod_idx
		<< "_debug_frame[] = \n";
      c->output << "  {";
      for (size_t i = 0; i < debug_len; i++)
	{
	  int h = ((uint8_t *)debug_frame)[i];
	  c->output << "0x" << hex << h << dec << ",";
	  if ((i + 1) % 16 == 0)
	    c->output << "\n" << "   ";
	}
      c->output << "};\n";
      c->output << "#endif /* STP_USE_DWARF_UNWINDER && STP_NEED_UNWIND_DATA */\n";
    }

  if (eh_frame != NULL && eh_len > 0)
    {
      c->output << "#if defined(STP_USE_DWARF_UNWINDER) && defined(STP_NEED_UNWIND_DATA)\n";
      c->output << "static uint8_t _stp_module_" << stpmod_idx
		<< "_eh_frame[] = \n";
      c->output << "  {";
      for (size_t i = 0; i < eh_len; i++)
	{
	  int h = ((uint8_t *)eh_frame)[i];
	  c->output << "0x" << hex << h << dec << ",";
	  if ((i + 1) % 16 == 0)
	    c->output << "\n" << "   ";
	}
      c->output << "};\n";
      c->output << "#endif /* STP_USE_DWARF_UNWINDER && STP_NEED_UNWIND_DATA */\n";
    }

  if (debug_frame == NULL && eh_frame == NULL)
    {
      // There would be only a small benefit to warning.  A user
      // likely can't do anything about this; backtraces for the
      // affected module would just get all icky heuristicy.
      // So only report in verbose mode.
      if (c->session.verbose > 2)
	c->session.print_warning ("No unwind data for " + modname
				  + ", " + dwfl_errmsg (-1));
    }

  for (unsigned secidx = 0; secidx < seclist.size(); secidx++)
    {
      c->output << "static struct _stp_symbol "
                << "_stp_module_" << stpmod_idx<< "_symbols_" << secidx << "[] = {\n";

      // Only include symbols if they will be used
      c->output << "#ifdef STP_NEED_SYMBOL_DATA\n";

      // We write out a *sorted* symbol table, so the runtime doesn't have to sort them later.
      for (addrmap_t::iterator it = addrmap[secidx].begin(); it != addrmap[secidx].end(); it++)
        {
          if (it->first < extra_offset)
            continue; // skip symbols that occur before our chosen base address

          c->output << "  { 0x" << hex << it->first-extra_offset << dec
                    << ", " << lex_cast_qstring (it->second) << " },\n";
        }

      c->output << "#endif /* STP_NEED_SYMBOL_DATA */\n";

      c->output << "};\n";
    }

  c->output << "static struct _stp_section _stp_module_" << stpmod_idx<< "_sections[] = {\n";
  // For the kernel, executables (ET_EXEC) or shared libraries (ET_DYN)
  // there is just one section that covers the whole address space of
  // the module. For kernel modules (ET_REL) there can be multiple
  // sections that get relocated separately.
  for (unsigned secidx = 0; secidx < seclist.size(); secidx++)
    {
      c->output << "{\n"
                << ".name = " << lex_cast_qstring(seclist[secidx].first) << ",\n"
                << ".size = 0x" << hex << seclist[secidx].second << dec << ",\n"
                << ".symbols = _stp_module_" << stpmod_idx << "_symbols_" << secidx << ",\n"
                << ".num_symbols = " << addrmap[secidx].size() << "\n"
                << "},\n";
    }
  c->output << "};\n";

  c->output << "static struct _stp_module _stp_module_" << stpmod_idx << " = {\n";
  c->output << ".name = " << lex_cast_qstring (modname) << ", \n";

  mainfile = canonicalize_file_name(mainfile);
  c->output << ".path = " << lex_cast_qstring (mainfile) << ",\n";

  c->output << ".dwarf_module_base = 0x" << hex << base << dec << ", \n";
  c->output << ".eh_frame_addr = 0x" << hex << eh_addr << dec << ", \n";

  if (debug_frame != NULL)
    {
      c->output << "#if defined(STP_USE_DWARF_UNWINDER) && defined(STP_NEED_UNWIND_DATA)\n";
      c->output << ".debug_frame = "
		<< "_stp_module_" << stpmod_idx << "_debug_frame, \n";
      c->output << ".debug_frame_len = " << debug_len << ", \n";
      c->output << "#else\n";
    }

  c->output << ".debug_frame = NULL,\n";
  c->output << ".debug_frame_len = 0,\n";

  if (debug_frame != NULL)
    c->output << "#endif /* STP_USE_DWARF_UNWINDER && STP_NEED_UNWIND_DATA*/\n";

  if (eh_frame != NULL)
    {
      c->output << "#if defined(STP_USE_DWARF_UNWINDER) && defined(STP_NEED_UNWIND_DATA)\n";
      c->output << ".eh_frame = "
		<< "_stp_module_" << stpmod_idx << "_eh_frame, \n";
      c->output << ".eh_frame_len = " << eh_len << ", \n";
      c->output << "#else\n";
    }

  c->output << ".eh_frame = NULL,\n";
  c->output << ".eh_frame_len = 0,\n";

  if (eh_frame != NULL)
    c->output << "#endif /* STP_USE_DWARF_UNWINDER && STP_NEED_UNWIND_DATA*/\n";

  c->output << ".unwind_hdr = NULL,\n";
  c->output << ".unwind_hdr_len = 0,\n";

  c->output << ".sections = _stp_module_" << stpmod_idx << "_sections" << ",\n";
  c->output << ".num_sections = sizeof(_stp_module_" << stpmod_idx << "_sections)/"
            << "sizeof(struct _stp_section),\n";

  if (build_id_len > 0) {
    c->output << ".build_id_bits = \"" ;
    for (int j=0; j<build_id_len;j++)
      c->output << "\\x" << hex
                << (unsigned short) *(build_id_bits+j) << dec;

    c->output << "\",\n";
    c->output << ".build_id_len = " << build_id_len << ",\n";

    /* XXX: kernel data boot-time relocation works differently from text.
       This hack disables relocation altogether, but that's not necessarily
       correct either.  We may instead need a relocation basis different
       from _stext, such as __start_notes.  */
    if (modname == "kernel")
      c->output << ".build_id_offset = 0x" << hex << build_id_vaddr
                << dec << ",\n";
    else
      c->output << ".build_id_offset = 0x" << hex
                << build_id_vaddr - base
                << dec << ",\n";
  } else
    c->output << ".build_id_len = 0,\n";

  //initialize the note section representing unloaded
  c->output << ".notes_sect = 0,\n";

  c->output << "};\n\n";

  c->undone_unwindsym_modules.erase (modname);

  return DWARF_CB_OK;
}


// Emit symbol table & unwind data, plus any calls needed to register
// them with the runtime.
void emit_symbol_data_done (unwindsym_dump_context*, systemtap_session&);

void
emit_symbol_data (systemtap_session& s)
{
  string symfile = "stap-symbols.h";

  s.op->newline() << "#include " << lex_cast_qstring (symfile);

  ofstream kallsyms_out ((s.tmpdir + "/" + symfile).c_str());

  unwindsym_dump_context ctx = { s, kallsyms_out, 0, ~0, s.unwindsym_modules };

  // Micro optimization, mainly to speed up tiny regression tests
  // using just begin probe.
  if (s.unwindsym_modules.size () == 0)
    {
      emit_symbol_data_done(&ctx, s);
      return;
    }

  // XXX: copied from tapsets.cxx dwflpp::, sadly
  static const char *debuginfo_path_arr = "+:.debug:/usr/lib/debug:build";
  static const char *debuginfo_env_arr = getenv("SYSTEMTAP_DEBUGINFO_PATH");
  static const char *debuginfo_path = (debuginfo_env_arr ?: debuginfo_path_arr);

  // ---- step 1: process any kernel modules listed
  static const Dwfl_Callbacks kernel_callbacks =
    {
      dwfl_linux_kernel_find_elf,
      dwfl_standard_find_debuginfo,
      dwfl_offline_section_address,
      (char **) & debuginfo_path
    };

  Dwfl *dwfl = dwfl_begin (&kernel_callbacks);
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
  if (s.kernel_build_tree == string("/lib/modules/" + s.kernel_release + "/build"))
    elfutils_kernel_path = s.kernel_release;
  else
    elfutils_kernel_path = s.kernel_build_tree;

  int rc = dwfl_linux_kernel_report_offline (dwfl,
                                             elfutils_kernel_path.c_str(),
					     &dwfl_report_offline_predicate);
  dwfl_report_end (dwfl, NULL, NULL);
  if (rc == 0) // tolerate missing data; will warn user about it anyway
    {
      ptrdiff_t off = 0;
      do
        {
          if (pending_interrupts) return;
          if (ctx.undone_unwindsym_modules.empty()) break;
          off = dwfl_getmodules (dwfl, &dump_unwindsyms, (void *) &ctx, 0);
        }
      while (off > 0);
      dwfl_assert("dwfl_getmodules", off == 0);
    }
  dwfl_end(dwfl);


  // ---- step 2: process any user modules (files) listed
  // XXX: see dwflpp::setup_user.
  static const Dwfl_Callbacks user_callbacks =
    {
      NULL, /* dwfl_linux_kernel_find_elf, */
      dwfl_standard_find_debuginfo,
      NULL, /* ET_REL not supported for user space, only ET_EXEC and ET_DYN.
              dwfl_offline_section_address, */
      (char **) & debuginfo_path
    };

  for (std::set<std::string>::iterator it = s.unwindsym_modules.begin();
       it != s.unwindsym_modules.end();
       it++)
    {
      string modname = *it;
      assert (modname.length() != 0);
      if (modname[0] != '/') continue; // user-space files must be full paths
      Dwfl *dwfl = dwfl_begin (&user_callbacks);
      if (!dwfl)
        throw semantic_error ("cannot create dwfl for " + modname);

      dwfl_report_begin (dwfl);
      Dwfl_Module* mod = dwfl_report_offline (dwfl, modname.c_str(), modname.c_str(), -1);
      dwfl_report_end (dwfl, NULL, NULL);
      if (mod != 0) // tolerate missing data; will warn below
        {
          ptrdiff_t off = 0;
          do
            {
              if (pending_interrupts) return;
              if (ctx.undone_unwindsym_modules.empty()) break;
              off = dwfl_getmodules (dwfl, &dump_unwindsyms, (void *) &ctx, 0);
            }
          while (off > 0);
          dwfl_assert("dwfl_getmodules", off == 0);
        }
      dwfl_end(dwfl);
    }

  emit_symbol_data_done (&ctx, s);
}

void
emit_symbol_data_done (unwindsym_dump_context *ctx, systemtap_session& s)
{
  // Print out a definition of the runtime's _stp_modules[] globals.
  ctx->output << "\n";
  ctx->output << "static struct _stp_module *_stp_modules [] = {\n";
  for (unsigned i=0; i<ctx->stp_module_index; i++)
    {
      ctx->output << "& _stp_module_" << i << ",\n";
    }
  ctx->output << "};\n";
  ctx->output << "static unsigned _stp_num_modules = " << ctx->stp_module_index << ";\n";

  ctx->output << "static unsigned long _stp_kretprobe_trampoline = ";
  // Special case for -1, which is invalid in hex if host width > target width.
  if (ctx->stp_kretprobe_trampoline_addr == (unsigned long) -1)
    ctx->output << "-1;\n";
  else
    ctx->output << "0x" << hex << ctx->stp_kretprobe_trampoline_addr << dec
		<< ";\n";

  // Some nonexistent modules may have been identified with "-d".  Note them.
  for (set<string>::iterator it = ctx->undone_unwindsym_modules.begin();
       it != ctx->undone_unwindsym_modules.end();
       it ++)
    {
      s.print_warning ("missing unwind/symbol data for module '" + (*it) + "'");
    }
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

      s.op->newline() << "#ifndef MAXNESTING";
      s.op->newline() << "#define MAXNESTING 10";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXSTRINGLEN";
      s.op->newline() << "#define MAXSTRINGLEN 128";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXACTION";
      s.op->newline() << "#define MAXACTION 1000";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef MAXACTION_INTERRUPTIBLE";
      s.op->newline() << "#define MAXACTION_INTERRUPTIBLE (MAXACTION * 10)";
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
      s.op->newline() << "#ifndef MINSTACKSPACE";
      s.op->newline() << "#define MINSTACKSPACE 1024";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef INTERRUPTIBLE";
      s.op->newline() << "#define INTERRUPTIBLE 1";
      s.op->newline() << "#endif";

      // Overload processing
      s.op->newline() << "#ifndef STP_OVERLOAD_INTERVAL";
      s.op->newline() << "#define STP_OVERLOAD_INTERVAL 1000000000LL";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifndef STP_OVERLOAD_THRESHOLD";
      s.op->newline() << "#define STP_OVERLOAD_THRESHOLD 500000000LL";
      s.op->newline() << "#endif";
      // We allow the user to completely turn overload processing off
      // (as opposed to tuning it by overriding the values above) by
      // running:  stap -DSTP_NO_OVERLOAD {other options}
      s.op->newline() << "#ifndef STP_NO_OVERLOAD";
      s.op->newline() << "#define STP_OVERLOAD";
      s.op->newline() << "#endif";

      s.op->newline() << "#define STP_SKIP_BADVARS " << (s.skip_badvars ? 1 : 0);

      if (s.bulk_mode)
	  s.op->newline() << "#define STP_BULKMODE";

      if (s.timing)
	s.op->newline() << "#define STP_TIMING";

      if (s.perfmon)
	s.op->newline() << "#define STP_PERFMON";

      s.op->newline() << "#include \"runtime.h\"";
      s.op->newline() << "#include \"stack.c\"";
      s.op->newline() << "#include \"stat.c\"";
      s.op->newline() << "#include <linux/string.h>";
      s.op->newline() << "#include <linux/timer.h>";
      s.op->newline() << "#include <linux/sched.h>";
      s.op->newline() << "#include <linux/delay.h>";
      s.op->newline() << "#include <linux/profile.h>";
      s.op->newline() << "#include <linux/random.h>";
      // s.op->newline() << "#include <linux/utsrelease.h>"; // newer kernels only
      s.op->newline() << "#include <linux/vermagic.h>";
      s.op->newline() << "#include <linux/utsname.h>";
      s.op->newline() << "#include <linux/version.h>";
      // s.op->newline() << "#include <linux/compile.h>";
      s.op->newline() << "#include \"loc2c-runtime.h\" ";

      // XXX: old 2.6 kernel hack
      s.op->newline() << "#ifndef read_trylock";
      s.op->newline() << "#define read_trylock(x) ({ read_lock(x); 1; })";
      s.op->newline() << "#endif";

      s.up->emit_common_header (); // context etc.

      for (unsigned i=0; i<s.embeds.size(); i++)
        {
          s.op->newline() << s.embeds[i]->code << "\n";
        }

      if (s.globals.size()>0) {
        s.op->newline() << "static struct {";
        s.op->indent(1);
        for (unsigned i=0; i<s.globals.size(); i++)
          {
            s.up->emit_global (s.globals[i]);
          }
        s.op->newline(-1) << "} global = {";
        s.op->newline(1);
        for (unsigned i=0; i<s.globals.size(); i++)
          {
            if (pending_interrupts) return 1;
            s.up->emit_global_init (s.globals[i]);
          }
        s.op->newline(-1) << "};";
        s.op->assert_0_indent();
      }

      for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
	{
          if (pending_interrupts) return 1;
	  s.op->newline();
	  s.up->emit_functionsig (it->second);
	}
      s.op->assert_0_indent();

      for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
	{
          if (pending_interrupts) return 1;
	  s.op->newline();
	  s.up->emit_function (it->second);
	}
      s.op->assert_0_indent();

      // Run a varuse_collecting_visitor over probes that need global
      // variable locks.  We'll use this information later in
      // emit_locks()/emit_unlocks().
      for (unsigned i=0; i<s.probes.size(); i++)
	{
        if (pending_interrupts) return 1;
        if (s.probes[i]->needs_global_locks())
	    s.probes[i]->body->visit (&cup.vcv_needs_global_locks);
	}
      s.op->assert_0_indent();

      for (unsigned i=0; i<s.probes.size(); i++)
        {
          if (pending_interrupts) return 1;
          s.up->emit_probe (s.probes[i]);
        }
      s.op->assert_0_indent();

      s.op->newline();
      s.up->emit_module_init ();
      s.op->assert_0_indent();
      s.op->newline();
      s.up->emit_module_exit ();
      s.op->assert_0_indent();
      s.op->newline();

      // XXX impedance mismatch
      s.op->newline() << "static int probe_start (void) {";
      s.op->newline(1) << "return systemtap_module_init () ? -1 : 0;";
      s.op->newline(-1) << "}";
      s.op->newline();
      s.op->newline() << "static void probe_exit (void) {";
      s.op->newline(1) << "systemtap_module_exit ();";
      s.op->newline(-1) << "}";
      s.op->assert_0_indent();

      emit_symbol_data (s);

      s.op->newline() << "MODULE_DESCRIPTION(\"systemtap-generated probe\");";
      s.op->newline() << "MODULE_LICENSE(\"GPL\");";
      s.op->assert_0_indent();

      // PR10298: attempt to avoid collisions with symbols
      for (unsigned i=0; i<s.globals.size(); i++)
        {
          s.op->newline();
          s.up->emit_global_param (s.globals[i]);
        }
      s.op->assert_0_indent();
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
    }

  s.op->line() << "\n";

  delete s.op;
  s.op = 0;
  s.up = 0;

  return rc + s.num_errors();
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
