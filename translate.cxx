// translation pass
// Copyright (C) 2005-2012 Red Hat Inc.
// Copyright (C) 2005-2008 Intel Corporation.
// Copyright (C) 2010 Novell Corporation.
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
#include "setupdwfl.h"
#include "task_finder.h"
#include "dwflpp.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <cassert>
#include <cstring>
#include <cerrno>

extern "C" {
#include <dwarf.h>
#include <elfutils/libdwfl.h>
#include <elfutils/libdw.h>
#include <ftw.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}

// Max unwind table size (debug or eh) per module. Somewhat arbitrary
// limit (a bit more than twice the .debug_frame size of my local
// vmlinux for 2.6.31.4-83.fc12.x86_64).
// A larger value was recently found in a libxul.so build.
#define MAX_UNWIND_TABLE_SIZE (6 * 1024 * 1024)

#define STAP_T_01 _("\"Array overflow, check ")
#define STAP_T_02 _("\"MAXNESTING exceeded\";")
#define STAP_T_03 _("\"division by 0\";")
#define STAP_T_04 _("\"MAXACTION exceeded\";")
#define STAP_T_05 _("\"aggregation overflow in ")
#define STAP_T_06 _("\"empty aggregate\";")
#define STAP_T_07 _("\"histogram index out of range\";")
using namespace std;

class var;
struct tmpvar;
struct aggvar;
struct mapvar;
class itervar;

struct c_unparser: public unparser, public visitor
{
  systemtap_session* session;
  translator_output* o;

  derived_probe* current_probe;
  functiondecl* current_function;
  unsigned tmpvar_counter;
  unsigned label_counter;
  unsigned action_counter;

  varuse_collecting_visitor vcv_needs_global_locks;

  map<string, string> probe_contents;

  map<pair<bool, string>, string> compiled_printfs;

  c_unparser (systemtap_session* ss):
    session (ss), o (ss->op), current_probe(0), current_function (0),
    tmpvar_counter (0), label_counter (0), action_counter(0),
    vcv_needs_global_locks (*ss) {}
  ~c_unparser () {}

  void emit_map_type_instantiations ();
  void emit_common_header ();
  void emit_global (vardecl* v);
  void emit_global_init (vardecl* v);
  void emit_global_param (vardecl* v);
  void emit_functionsig (functiondecl* v);
  void emit_module_init ();
  void emit_module_refresh ();
  void emit_module_exit ();
  void emit_function (functiondecl* v);
  void emit_lock_decls (const varuse_collecting_visitor& v);
  void emit_locks (const varuse_collecting_visitor& v);
  void emit_probe (derived_probe* v);
  void emit_unlocks (const varuse_collecting_visitor& v);

  void emit_compiled_printfs ();
  void emit_compiled_printf_locals ();
  void declare_compiled_printf (bool print_to_stream, const string& format);
  const string& get_compiled_printf (bool print_to_stream, const string& format);

  // for use by stats (pmap) foreach
  set<string> aggregations_active;

  // values immediately available in foreach_loop iterations
  map<string, string> foreach_loop_values;
  void visit_foreach_loop_value (visitor* vis, foreach_loop* s,
                                 const string& value="");
  bool get_foreach_loop_value (arrayindex* ai, string& value);

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

  var* load_aggregate (expression *e, aggvar & agg);
  string histogram_index_check(var & vase, tmpvar & idx) const;

  void collect_map_index_types(vector<vardecl* > const & vars,
			       set< pair<vector<exp_type>, exp_type> > & types);

  void record_actions (unsigned actions, const token* tok, bool update=false);

  void visit_block (block* s);
  void visit_try_block (try_block* s);
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
  void visit_embedded_expr (embedded_expr* e);
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
  void visit_defined_op (defined_op* e);
  void visit_entry_op (entry_op* e);
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
  void load_aggregate (expression *e);

  void visit_block (block *s);
  void visit_for_loop (for_loop* s);
  void visit_foreach_loop (foreach_loop* s);
  // void visit_return_statement (return_statement* s);
  void visit_delete_statement (delete_statement* s);
  // void visit_embedded_expr (embedded_expr* e);
  void visit_binary_expression (binary_expression* e);
  // void visit_unary_expression (unary_expression* e);
  void visit_pre_crement (pre_crement* e);
  void visit_post_crement (post_crement* e);
  // void visit_logical_or_expr (logical_or_expr* e);
  // void visit_logical_and_expr (logical_and_expr* e);
  void visit_array_in (array_in* e);
  void visit_comparison (comparison* e);
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
                + ", " + lex_cast(sd.linear_low)
                + ", " + lex_cast(sd.linear_high)
                + ", " + lex_cast(sd.linear_step);
              break;

            case statistic_decl::logarithmic:
              prefix += string("HIST_LOG");
              break;

            default:
              throw semantic_error(_F("unsupported stats type for %s", value().c_str()));
            }

          prefix = prefix + "); ";
          return string (prefix + suffix);
        }

      default:
        throw semantic_error(_F("unsupported initializer for %s", value().c_str()));
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
        throw semantic_error(_F("unsupported deallocator for %s", value().c_str()));
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
    : var(true, ty, ("__tmp" + lex_cast(counter++))), overridden(false)
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
    : var(true, pe_stats, ("__tmp" + lex_cast(counter++)))
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

  string get_hist (var& index) const
  {
    return "(" + value() + "->histogram[" + index.value() + "])";
  }
};

struct mapvar
  : public var
{
  vector<exp_type> index_types;
  int maxsize;
  bool wrap;
  mapvar (bool local, exp_type ty,
	  statistic_decl const & sd,
	  string const & name,
	  vector<exp_type> const & index_types,
	  int maxsize, bool wrap)
    : var (local, ty, sd, name),
      index_types (index_types),
      maxsize (maxsize), wrap(wrap)
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
	    throw semantic_error(_("unknown type of map"));
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
	  throw semantic_error(_("index type mismatch"));
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
      throw semantic_error(_("aggregating non-parallel map type"));

    return "_stp_pmap_agg (" + value() + ")";
  }

  string fetch_existing_aggregate() const
  {
    if (!is_parallel())
      throw semantic_error(_("fetching aggregate of non-parallel map type"));

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
      throw semantic_error(_("checking existence of an unsupported map type"));
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
      throw semantic_error(_("getting a value from an unsupported map type"));
  }

  string add (vector<tmpvar> const & indices, tmpvar const & val) const
  {
    string res = "{ int rc = ";

    // impedance matching: empty strings -> NULL
    if (type() == pe_stats)
      res += (call_prefix("add", indices) + ", " + val.value() + ")");
    else
      throw semantic_error(_("adding a value of an unsupported map type"));

    res += "; if (unlikely(rc)) { c->last_error = ";
    res += STAP_T_01 +
      lex_cast(maxsize > 0 ?
	  "size limit (" + lex_cast(maxsize) + ")" : "MAXMAPENTRIES")
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
      throw semantic_error(_("setting a value of an unsupported map type"));

    res += "; if (unlikely(rc)) { c->last_error = ";
    res += STAP_T_01 +
      lex_cast(maxsize > 0 ?
	  "size limit (" + lex_cast(maxsize) + ")" : "MAXMAPENTRIES")
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
      (maxsize > 0 ? lex_cast(maxsize) : "MAXMAPENTRIES") ;

    // See also var::init().

    // Check for errors during allocation.
    string suffix = "if (" + value () + " == NULL) rc = -ENOMEM;";

    if(wrap == true)
      {
        if(mtype == "pmap")
          suffix = suffix + " else { for_each_possible_cpu(cpu) { MAP mp = per_cpu_ptr(" + value() + "->map, cpu); mp->wrap = 1; }} ";
        else
          suffix = suffix + " else " + value() + "->wrap = 1;";
      }
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
	      + ", " + lex_cast(sdecl().linear_low)
	      + ", " + lex_cast(sdecl().linear_high)
	      + ", " + lex_cast(sdecl().linear_step);
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
      name("__tmp" + lex_cast(counter++))
  {
    if (referent_ty == pe_unknown)
      throw semantic_error(_("iterating over unknown reference type"), e->tok);
  }

  string declare () const
  {
    return "struct map_node *" + name + ";";
  }

  string start (mapvar const & mv) const
  {
    string res;

    if (mv.type() != referent_ty)
      throw semantic_error(_("inconsistent iterator type in itervar::start()"));

    if (mv.is_parallel())
      return "_stp_map_start (" + mv.fetch_existing_aggregate() + ")";
    else
      return "_stp_map_start (" + mv.value() + ")";
  }

  string next (mapvar const & mv) const
  {
    if (mv.type() != referent_ty)
      throw semantic_error(_("inconsistent iterator type in itervar::next()"));

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
	return "_stp_key_get_int64 ("+ value() + ", " + lex_cast(i+1) + ")";
      case pe_string:
        // impedance matching: NULL -> empty strings
	return "(_stp_key_get_str ("+ value() + ", " + lex_cast(i+1) + ") ?: \"\")";
      default:
	throw semantic_error(_("illegal key type"));
      }
  }

  string get_value (exp_type ty) const
  {
    if (ty != referent_ty)
      throw semantic_error(_("inconsistent iterator value in itervar::get_value()"));

    switch (ty)
      {
      case pe_long:
	return "_stp_get_int64 ("+ value() + ")";
      case pe_string:
        // impedance matching: NULL -> empty strings
	return "(_stp_get_str ("+ value() + ") ?: \"\")";
      case pe_stats:
	return "_stp_get_stat ("+ value() + ")";
      default:
	throw semantic_error(_("illegal value type"));
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
  tablevel (0),
  filename (filename)
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
  // Common (static atomic) state of the stap session.
  o->newline();
  o->newline() << "#include \"common_session_state.h\"";

  // Per CPU context for probes. Includes common shared state held for
  // all probes (defined in common_probe_context), the probe locals (union)
  // and the function locals (union).
  o->newline() << "struct context {";

  // Common state held shared by probes.
  o->newline(1) << "#include \"common_probe_context.h\"";

  // PR10516: probe locals
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
      oss << "# needs_global_locks: " << dp->needs_global_locks () << endl;
      dp->print_dupe_stamp (oss);
      dp->body->print(oss);
      // NB: dependent probe conditions *could* be listed here, but don't need to be.
      // That's because they're only dependent on the probe body, which is already
      // "hashed" in above.

      if (tmp_probe_contents.count(oss.str()) == 0) // unique
        {
          tmp_probe_contents[oss.str()] = dp->name; // save it

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
          dp->body->visit (& ct);

          o->newline(-1) << "} " << dp->name << ";";
        }
    }
  o->newline(-1) << "} probe_locals;";

  // PR10516: function locals
  o->newline() << "union {";
  o->indent(1);

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
  o->newline(-1) << "} locals [MAXNESTING+1];"; 

  // NB: The +1 above for extra room for outgoing arguments of next nested function.
  // If MAXNESTING is set too small, the args will be written, but the MAXNESTING
  // check done at c_unparser::emit_function will reject.
  //
  // This policy wastes memory (one row of locals[] that cannot really
  // be used), but trades that for smaller code (not having to check
  // c->nesting against MAXNESTING at every call site).

  // Try to catch a crazy user dude passing in -DMAXNESTING=-1, leading to a [0]-sized
  // locals[] array.
  o->newline() << "#if MAXNESTING < 0";
  o->newline() << "#error \"MAXNESTING must be positive\"";
  o->newline() << "#endif";

  // Use a separate union for compiled-printf locals, no nesting required.
  emit_compiled_printf_locals ();

  o->newline(-1) << "};\n";
  o->newline() << "static struct context *contexts[NR_CPUS] = { NULL };\n";

  emit_map_type_instantiations ();

  if (!session->stat_decls.empty())
    o->newline() << "#include \"stat.c\"\n";

  o->newline() << "#ifdef STAP_NEED_GETTIMEOFDAY";
  o->newline() << "#include \"time.c\"";  // Don't we all need more?
  o->newline() << "#endif";

  emit_compiled_printfs();

  o->newline();
}


void
c_unparser::declare_compiled_printf (bool print_to_stream, const string& format)
{
  pair<bool, string> index (print_to_stream, format);
  map<pair<bool, string>, string>::iterator it = compiled_printfs.find(index);
  if (it == compiled_printfs.end())
    compiled_printfs[index] = (print_to_stream ? "stp_printf_" : "stp_sprintf_")
      + lex_cast(compiled_printfs.size() + 1);
}

const string&
c_unparser::get_compiled_printf (bool print_to_stream, const string& format)
{
  map<pair<bool, string>, string>::iterator it =
    compiled_printfs.find(make_pair(print_to_stream, format));
  if (it == compiled_printfs.end())
    throw semantic_error (_("internal error translating printf"));
  return it->second;
}

void
c_unparser::emit_compiled_printf_locals ()
{
  o->newline() << "#ifndef STP_LEGACY_PRINT";
  o->newline() << "union {";
  o->indent(1);
  map<pair<bool, string>, string>::iterator it;
  for (it = compiled_printfs.begin(); it != compiled_printfs.end(); ++it)
    {
      bool print_to_stream = it->first.first;
      const string& format_string = it->first.second;
      const string& name = it->second;
      vector<print_format::format_component> components =
	print_format::string_to_components(format_string);

      o->newline() << "struct " << name << "_locals {";
      o->indent(1);

      size_t arg_ix = 0;
      vector<print_format::format_component>::const_iterator c;
      for (c = components.begin(); c != components.end(); ++c)
	{
	  if (c->type == print_format::conv_literal)
	    continue;

	  // Take note of the width and precision arguments, if any.
	  if (c->widthtype == print_format::width_dynamic)
	    o->newline() << "int64_t arg" << arg_ix++ << ";";
	  if (c->prectype == print_format::prec_dynamic)
	    o->newline() << "int64_t arg" << arg_ix++ << ";";

	  // Output the actual argument.
	  switch (c->type)
	    {
	    case print_format::conv_pointer:
	    case print_format::conv_number:
	    case print_format::conv_char:
	    case print_format::conv_memory:
	    case print_format::conv_memory_hex:
	    case print_format::conv_binary:
	      o->newline() << "int64_t arg" << arg_ix++ << ";";
	      break;

	    case print_format::conv_string:
	      // NB: Since we know incoming strings are immutable, we can use
	      // const char* rather than a private char[] copy.  This is a
	      // special case of the sort of optimizations desired in PR11528.
	      o->newline() << "const char* arg" << arg_ix++ << ";";
	      break;

	    default:
	      assert(false); // XXX
	      break;
	    }
	}


      if (!print_to_stream)
	o->newline() << "char * __retvalue;";

      o->newline(-1) << "} " << name << ";";
    }
  o->newline(-1) << "} printf_locals;";
  o->newline() << "#endif // STP_LEGACY_PRINT";
}

void
c_unparser::emit_compiled_printfs ()
{
  o->newline() << "#ifndef STP_LEGACY_PRINT";
  map<pair<bool, string>, string>::iterator it;
  for (it = compiled_printfs.begin(); it != compiled_printfs.end(); ++it)
    {
      bool print_to_stream = it->first.first;
      const string& format_string = it->first.second;
      const string& name = it->second;
      vector<print_format::format_component> components =
	print_format::string_to_components(format_string);

      o->newline();

      // Might be nice to output the format string in a comment, but we'd have
      // to be extra careful about format strings not escaping the comment...
      o->newline() << "static void " << name
		   << " (struct context* __restrict__ c) {";
      o->newline(1) << "struct " << name << "_locals * __restrict__ l = "
		    << "& c->printf_locals." << name << ";";
      o->newline() << "char *str = NULL, *end = NULL;";
      o->newline() << "const char *src;";
      o->newline() << "int width;";
      o->newline() << "int precision;";
      o->newline() << "unsigned long ptr_value;";
      o->newline() << "int num_bytes;";

      o->newline() << "(void) width;";
      o->newline() << "(void) precision;";
      o->newline() << "(void) ptr_value;";
      o->newline() << "(void) num_bytes;";

      if (print_to_stream)
        {
	  // Compute the buffer size needed for these arguments.
	  size_t arg_ix = 0;
	  o->newline() << "num_bytes = 0;";
	  vector<print_format::format_component>::const_iterator c;
	  for (c = components.begin(); c != components.end(); ++c)
	    {
	      if (c->type == print_format::conv_literal)
		{
		  literal_string ls(c->literal_string);
		  o->newline() << "num_bytes += sizeof(";
		  visit_literal_string(&ls);
		  o->line() << ") - 1;"; // don't count the '\0'
		  continue;
		}

	      o->newline() << "width = ";
	      if (c->widthtype == print_format::width_dynamic)
		o->line() << "clamp_t(int, l->arg" << arg_ix++
		          << ", 0, STP_BUFFER_SIZE);";
	      else if (c->widthtype == print_format::width_static)
		o->line() << "clamp_t(int, " << c->width
		          << ", 0, STP_BUFFER_SIZE);";
	      else
		o->line() << "-1;";

	      o->newline() << "precision = ";
	      if (c->prectype == print_format::prec_dynamic)
		o->line() << "clamp_t(int, l->arg" << arg_ix++
		          << ", 0, STP_BUFFER_SIZE);";
	      else if (c->prectype == print_format::prec_static)
		o->line() << "clamp_t(int, " << c->precision
		          << ", 0, STP_BUFFER_SIZE);";
	      else
		o->line() << "-1;";

	      string value = "l->arg" + lex_cast(arg_ix++);
	      switch (c->type)
		{
		case print_format::conv_pointer:
		  // NB: stap < 1.3 had odd %p behavior... see _stp_vsnprintf
		  if (strverscmp(session->compatible.c_str(), "1.3") < 0)
		    {
		      o->newline() << "ptr_value = " << value << ";";
		      o->newline() << "if (width == -1)";
		      o->newline(1) << "width = 2 + 2 * sizeof(void*);";
		      o->newline(-1) << "precision = width - 2;";
		      if (!c->test_flag(print_format::fmt_flag_left))
			o->newline() << "precision = min_t(int, precision, 2 * sizeof(void*));";
		      o->newline() << "num_bytes += number_size(ptr_value, "
			<< c->base << ", width, precision, " << c->flags << ");";
		      break;
		    }
		  // else fall-through to conv_number
		case print_format::conv_number:
		  o->newline() << "num_bytes += number_size(" << value << ", "
			       << c->base << ", width, precision, " << c->flags << ");";
		  break;

		case print_format::conv_char:
		  o->newline() << "num_bytes += _stp_vsprint_char_size("
			       << value << ", width, " << c->flags << ");";
		  break;

		case print_format::conv_string:
		  o->newline() << "num_bytes += _stp_vsprint_memory_size("
			       << value << ", width, precision, 's', "
			       << c->flags << ");";
		  break;

		case print_format::conv_memory:
		case print_format::conv_memory_hex:
		  o->newline() << "num_bytes += _stp_vsprint_memory_size("
			       << "(const char*)(intptr_t)" << value
			       << ", width, precision, '"
			       << ((c->type == print_format::conv_memory) ? "m" : "M")
			       << "', " << c->flags << ");";
		  break;

		case print_format::conv_binary:
		  o->newline() << "num_bytes += _stp_vsprint_binary_size("
			       << value << ", width, precision);";
		  break;

		default:
		  assert(false); // XXX
		  break;
		}
	    }

	  o->newline() << "num_bytes = clamp(num_bytes, 0, STP_BUFFER_SIZE);";
	  o->newline() << "str = (char*)_stp_reserve_bytes(num_bytes);";
	  o->newline() << "end = str ? str + num_bytes - 1 : 0;";
        }
      else // !print_to_stream
	{
	  // String results are a known buffer and size;
	  o->newline() << "str = l->__retvalue;";
	  o->newline() << "end = str + MAXSTRINGLEN - 1;";
	}

      o->newline() << "if (str && str <= end) {";
      o->indent(1);

      // Generate code to print the actual arguments.
      size_t arg_ix = 0;
      vector<print_format::format_component>::const_iterator c;
      for (c = components.begin(); c != components.end(); ++c)
	{
	  if (c->type == print_format::conv_literal)
	    {
	      literal_string ls(c->literal_string);
	      o->newline() << "src = ";
	      visit_literal_string(&ls);
	      o->line() << ";";
	      o->newline() << "while (*src && str <= end)";
	      o->newline(1) << "*str++ = *src++;";
              o->indent(-1);
	      continue;
	    }

	  o->newline() << "width = ";
	  if (c->widthtype == print_format::width_dynamic)
	    o->line() << "clamp_t(int, l->arg" << arg_ix++
		      << ", 0, end - str + 1);";
	  else if (c->widthtype == print_format::width_static)
	    o->line() << "clamp_t(int, " << c->width
		      << ", 0, end - str + 1);";
	  else
	    o->line() << "-1;";

	  o->newline() << "precision = ";
	  if (c->prectype == print_format::prec_dynamic)
	    o->line() << "clamp_t(int, l->arg" << arg_ix++
		      << ", 0, end - str + 1);";
	  else if (c->prectype == print_format::prec_static)
	    o->line() << "clamp_t(int, " << c->precision
		      << ", 0, end - str + 1);";
	  else
	    o->line() << "-1;";

	  string value = "l->arg" + lex_cast(arg_ix++);
	  switch (c->type)
	    {
	    case print_format::conv_pointer:
	      // NB: stap < 1.3 had odd %p behavior... see _stp_vsnprintf
	      if (strverscmp(session->compatible.c_str(), "1.3") < 0)
		{
		  o->newline() << "ptr_value = " << value << ";";
		  o->newline() << "if (width == -1)";
		  o->newline(1) << "width = 2 + 2 * sizeof(void*);";
		  o->newline(-1) << "precision = width - 2;";
		  if (!c->test_flag(print_format::fmt_flag_left))
		    o->newline() << "precision = min_t(int, precision, 2 * sizeof(void*));";
		  o->newline() << "str = number(str, end, ptr_value, "
		    << c->base << ", width, precision, " << c->flags << ");";
		  break;
		}
	      // else fall-through to conv_number
	    case print_format::conv_number:
	      o->newline() << "str = number(str, end, " << value << ", "
			   << c->base << ", width, precision, " << c->flags << ");";
	      break;

	    case print_format::conv_char:
	      o->newline() << "str = _stp_vsprint_char(str, end, "
			   << value << ", width, " << c->flags << ");";
	      break;

	    case print_format::conv_string:
	      o->newline() << "str = _stp_vsprint_memory(str, end, "
			   << value << ", width, precision, 's', "
			   << c->flags << ");";
	      break;

	    case print_format::conv_memory:
	    case print_format::conv_memory_hex:
	      o->newline() << "str = _stp_vsprint_memory(str, end, "
			   << "(const char*)(intptr_t)" << value
			   << ", width, precision, '"
			   << ((c->type == print_format::conv_memory) ? "m" : "M")
			   << "', " << c->flags << ");";
	      o->newline() << "if (unlikely(str == NULL)) {";
	      o->indent(1);
	      if (print_to_stream)
		  o->newline() << "_stp_unreserve_bytes(num_bytes);";
	      o->newline() << "return;";
	      o->newline(-1) << "}";
	      break;

	    case print_format::conv_binary:
	      o->newline() << "str = _stp_vsprint_binary(str, end, "
			   << value << ", width, precision, "
			   << c->flags << ");";
	      break;

	    default:
	      assert(false); // XXX
	      break;
	    }
	}

      if (!print_to_stream)
	{
	  o->newline() << "if (str <= end)";
	  o->newline(1) << "*str = '\\0';";
	  o->newline(-1) << "else";
	  o->newline(1) << "*end = '\\0';";
	  o->indent(-1);
	}

      o->newline(-1) << "}";

      o->newline(-1) << "}";
    }
  o->newline() << "#endif // STP_LEGACY_PRINT";
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
    {
      g[i]->emit_module_decls (*session);
      o->assert_0_indent(); 
    }

  o->newline();
  o->newline() << "static int systemtap_module_init (void) {";
  o->newline(1) << "int rc = 0;";
  o->newline() << "int cpu;";
  o->newline() << "int i=0, j=0;"; // for derived_probe_group use
  o->newline() << "const char *probe_point = \"\";";

  // Compare actual and targeted kernel releases/machines.  Sometimes
  // one may install the incorrect debuginfo or -devel RPM, and try to
  // run a probe compiled for a different version.  Catch this early,
  // just in case modversions didn't.
  o->newline() << "{";
  o->newline(1) << "const char* release = UTS_RELEASE;";
  o->newline() << "#ifdef STAPCONF_GENERATED_COMPILE";
  o->newline() << "const char* version = UTS_VERSION;";
  o->newline() << "#endif";

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

  o->newline() << "#ifdef STAPCONF_GENERATED_COMPILE";
  o->newline() << "if (strcmp (utsname()->version, version)) {";
  o->newline(1) << "_stp_error (\"module version mismatch (%s vs %s), release %s\", "
                << "version, "
                << "utsname()->version, "
                << "release"
                << ");";
  o->newline() << "rc = -EINVAL;";
  o->newline(-1) << "}";
  o->newline() << "#endif";

  // perform buildid-based checking if able
  o->newline() << "if (_stp_module_check()) rc = -EINVAL;";

  // Perform checking on the user's credentials vs those required to load/run this module.
  o->newline() << "if (_stp_privilege_credentials == 0) {";
  o->newline(1) << "if (STP_PRIVILEGE_CONTAINS(STP_PRIVILEGE, STP_PR_STAPDEV) ||";
  o->newline() << "    STP_PRIVILEGE_CONTAINS(STP_PRIVILEGE, STP_PR_STAPUSR)) {";
  o->newline(1) << "_stp_privilege_credentials = STP_PRIVILEGE;";
  o->newline() << "#ifdef DEBUG_PRIVILEGE";
  o->newline(1) << "_dbug(\"User's privilege credentials default to %s\\n\",";
  o->newline() << "      privilege_to_text(_stp_privilege_credentials));";
  o->newline(-1) << "#endif";
  o->newline(-1) << "}";
  o->newline() << "else {";
  o->newline(1) << "_stp_error (\"Unable to verify that you have the required privilege credentials to run this module (%s required). You must use staprun version 1.7 or higher.\",";
  o->newline() << "            privilege_to_text(STP_PRIVILEGE));";
  o->newline() << "rc = -EINVAL;";
  o->newline(-1) << "}";
  o->newline(-1) << "}";
  o->newline() << "else {";
  o->newline(1) << "#ifdef DEBUG_PRIVILEGE";
  o->newline(1) << "_dbug(\"User's privilege credentials provided as %s\\n\",";
  o->newline() << "      privilege_to_text(_stp_privilege_credentials));";
  o->newline(-1) << "#endif";
  o->newline() << "if (! STP_PRIVILEGE_CONTAINS(_stp_privilege_credentials, STP_PRIVILEGE)) {";
  o->newline(1) << "_stp_error (\"Your privilege credentials (%s) are insufficient to run this module (%s required).\",";
  o->newline () << "            privilege_to_text(_stp_privilege_credentials), privilege_to_text(STP_PRIVILEGE));";
  o->newline() << "rc = -EINVAL;";
  o->newline(-1) << "}";
  o->newline(-1) << "}";

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

  // NB: we don't need per-_stp_module task_finders, since a single common one
  // set up in runtime/sym.c's _stp_sym_init() will scan through all _stp_modules. XXX - check this!
  o->newline() << "(void) probe_point;";
  o->newline() << "(void) i;";
  o->newline() << "(void) j;";
  o->newline() << "atomic_set (&session_state, STAP_SESSION_STARTING);";
  // This signals any other probes that may be invoked in the next little
  // while to abort right away.  Currently running probes are allowed to
  // terminate.  These may set STAP_SESSION_ERROR!

  // per-cpu context
  o->newline() << "for_each_possible_cpu(cpu) {";
  o->indent(1);
  // Module init, so in user context, safe to use "sleeping" allocation.
  o->newline() << "contexts[cpu] = _stp_kzalloc_gfp(sizeof(struct context), STP_ALLOC_SLEEP_FLAGS);";
  o->newline() << "if (contexts[cpu] == NULL) {";
  o->indent(1);
  o->newline() << "_stp_error (\"context (size %lu) allocation failed\", (unsigned long) sizeof (struct context));";
  o->newline() << "rc = -ENOMEM;";
  o->newline() << "goto out;";
  o->newline(-1) << "}";
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
      o->newline(1) << "_stp_error (\"global variable '" << v->name << "' allocation failed\");";
      o->newline() << "goto out;";
      o->newline(-1) << "}";

      o->newline() << "rwlock_init (& global.s_" << c_varname (v->name) << "_lock);";
    }

  // initialize each Stat used for timing information
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "for (i = 0; i < ARRAY_SIZE(stap_probes); ++i)";
  o->newline(1) << "stap_probes[i].timing = _stp_stat_init (HIST_NONE);";
  // NB: we don't check for null return here, but instead at
  // passage to probe handlers and at final printing.
  o->newline(-1) << "#endif";

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
      // If a probe types's emit_module_init() wants to handle error
      // messages itself, it should set probe_point to NULL, 
      o->newline(1) << "if (probe_point)";
      o->newline(1) << "_stp_error (\"probe %s registration error (rc %d)\", probe_point, rc);";
      o->indent(-1);
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
  o->newline() << "atomic_set (&session_state, STAP_SESSION_STOPPED);";
  o->newline() << "#ifdef STAPCONF_SYNCHRONIZE_SCHED";
  o->newline() << "synchronize_sched();";
  o->newline() << "#endif";

  // In case gettimeofday was started, it needs to be stopped
  o->newline() << "#ifdef STAP_NEED_GETTIMEOFDAY";
  o->newline() << " _stp_kill_time();";  // An error is no cause to hurry...
  o->newline() << "#endif";

  // Free up the context memory after an error too
  o->newline() << "for_each_possible_cpu(cpu) {";
  o->indent(1);
  o->newline() << "if (contexts[cpu] != NULL) {";
  o->indent(1);
  o->newline() << "_stp_kfree(contexts[cpu]);";
  o->newline() << "contexts[cpu] = NULL;";
  o->newline(-1) << "}";
  o->newline(-1) << "}";

  o->newline() << "return rc;";
  o->newline(-1) << "}\n";
}


void
c_unparser::emit_module_refresh ()
{
  o->newline() << "static void systemtap_module_refresh (void) {";
  o->newline(1) << "int i=0, j=0;"; // for derived_probe_group use
  o->newline() << "(void) i;";
  o->newline() << "(void) j;";
  vector<derived_probe_group*> g = all_session_groups (*session);
  for (unsigned i=0; i<g.size(); i++)
    {
      g[i]->emit_module_refresh (*session);
    }
  o->newline(-1) << "}\n";
}


void
c_unparser::emit_module_exit ()
{
  o->newline() << "static void systemtap_module_exit (void) {";
  // rc?
  o->newline(1) << "int holdon;";
  o->newline() << "int i=0, j=0;"; // for derived_probe_group use
  o->newline() << "int cpu;";
  o->newline() << "unsigned long hold_start;";
  o->newline() << "int hold_index;";

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
  o->newline() << "hold_start = jiffies;";
  o->newline() << "hold_index = -1;";
  o->newline() << "do {";
  o->newline(1) << "int i;";
  o->newline() << "holdon = 0;";
  o->newline() << "for_each_possible_cpu(i)";
  o->newline(1) << "if (contexts[i] != NULL && "
                << "atomic_read (& contexts[i]->busy)) {";
  o->newline(1) << "holdon = 1;";

  // just in case things are really stuck, let's print some diagnostics
  o->newline() << "if (time_after(jiffies, hold_start + HZ) "; // > 1 second
  o->line() << "&& (i > hold_index)) {"; // not already printed
  o->newline(1) << "hold_index = i;";
  o->newline() << "printk(KERN_ERR \"%s context[%d] stuck: %s\\n\", THIS_MODULE->name, i, contexts[i]->probe_point);";
  o->newline(-1) << "}";
  o->newline(-1) << "}";

  // Just in case things are really really stuck, a handler probably
  // suffered a fault, and the kernel probably killed a task/thread
  // already.  We can't be quite sure in what state everything is in,
  // however auxiliary stuff like kprobes / uprobes / locks have
  // already been unregistered.  So it's *probably* safe to
  // pretend/assume/hope everything is OK, and let the cleanup finish.
  //
  // In the worst case, there may occur a fault, as a genuinely
  // running probe handler tries to access script globals (about to be
  // freed), or something accesses module memory (about to be
  // unloaded).  This is sometimes stinky, so the alternative
  // (default) is to change from a livelock to a livelock that sleeps
  // awhile.
  o->newline(-1) << "#ifdef STAP_OVERRIDE_STUCK_CONTEXT";
  o->newline() << "if (time_after(jiffies, hold_start + HZ*10)) { "; // > 10 seconds
  o->newline(1) << "printk(KERN_ERR \"%s overriding stuck context to allow module shutdown.\", THIS_MODULE->name);";
  o->newline() << "holdon = 0;"; // allow loop to exit
  o->newline(-1) << "}";
  o->newline() << "#else";
  o->newline() << "msleep (250);"; // at least stop sucking down the staprun cpu
  o->newline() << "#endif";

  // NB: we run at least one of these during the shutdown sequence:
  o->newline () << "yield ();"; // aka schedule() and then some
  o->newline(-1) << "} while (holdon);";

  // cargo cult epilogue
  o->newline() << "atomic_set (&session_state, STAP_SESSION_STOPPED);";
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

  o->newline() << "for_each_possible_cpu(cpu) {";
  o->indent(1);
  o->newline() << "if (contexts[cpu] != NULL) {";
  o->indent(1);
  o->newline() << "_stp_kfree(contexts[cpu]);";
  o->newline() << "contexts[cpu] = NULL;";
  o->newline(-1) << "}";
  o->newline(-1) << "}";

  // teardown gettimeofday (if needed)
  o->newline() << "#ifdef STAP_NEED_GETTIMEOFDAY";
  o->newline() << " _stp_kill_time();";  // Go to a beach.  Drink a beer.
  o->newline() << "#endif";

  // NB: PR13386 points out that _stp_printf may be called from contexts
  // without already active preempt disabling, which breaks various uses
  // of smp_processor_id().  So we temporary block preemption around this
  // whole printing block.  XXX: get_cpu() / put_cpu() may work just as well.
  o->newline() << "preempt_disable();";

  // print per probe point timing/alibi statistics
  o->newline() << "#if defined(STP_TIMING) || defined(STP_ALIBI)";
  o->newline() << "_stp_printf(\"----- probe hit report: \\n\");";
  o->newline() << "for (i = 0; i < ARRAY_SIZE(stap_probes); ++i) {";
  o->newline(1) << "struct stap_probe *const p = &stap_probes[i];";
  o->newline() << "#ifdef STP_ALIBI";
  o->newline() << "int alibi = atomic_read(&(p->alibi));";
  o->newline() << "if (alibi)";
  o->newline(1) << "_stp_printf (\"%s, (%s), hits: %d,%s\\n\",";
  o->newline(2) << "p->pp, p->location, alibi, p->derivation);";
  o->newline(-3) << "#endif"; // STP_ALIBI
  o->newline() << "#ifdef STP_TIMING";
  o->newline() << "if (likely (p->timing)) {"; // NB: check for null stat object
  o->newline(1) << "struct stat_data *stats = _stp_stat_get (p->timing, 0);";
  o->newline() << "if (stats->count) {";
  o->newline(1) << "int64_t avg = _stp_div64 (NULL, stats->sum, stats->count);";
  o->newline() << "_stp_printf (\"%s, (%s), hits: %lld, cycles: %lldmin/%lldavg/%lldmax,%s\\n\",";
  o->newline(2) << "p->pp, p->location, (long long) stats->count,";
  o->newline() << "(long long) stats->min, (long long) avg, (long long) stats->max,";
  o->newline() << "p->derivation);";
  o->newline(-3) << "}";
  o->newline() << "_stp_stat_del (p->timing);";
  o->newline(-1) << "}";
  o->newline() << "#endif"; // STP_TIMING
  o->newline(-1) << "}";
  o->newline() << "_stp_print_flush();";
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

  // NB: PR13386 needs to restore preemption-blocking counts
  o->newline() << "preempt_enable_no_resched();";

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

  o->newline() << "__label__ out;";
  o->newline()
    << "struct function_" << c_varname (v->name) << "_locals * "
    << " __restrict__ l = "
    << "& c->locals[c->nesting+1].function_" << c_varname (v->name) // NB: nesting+1
    << ";";
  o->newline() << "(void) l;"; // make sure "l" is marked used
  o->newline() << "#define CONTEXT c";
  o->newline() << "#define THIS l";

  // set this, in case embedded-c code sets last_error but doesn't otherwise identify itself
  o->newline() << "c->last_stmt = " << lex_cast_qstring(*v->tok) << ";";

  // check/increment nesting level
  // NB: incoming c->nesting level will be -1 (if we're called directly from a probe),
  // or 0...N (if we're called from another function).  Incoming parameters are already
  // stored in c->locals[c->nesting+1].  See also ::emit_common_header() for more.

  o->newline() << "if (unlikely (c->nesting+1 >= MAXNESTING)) {";
  o->newline(1) << "c->last_error = ";
  o->line() << STAP_T_02;
  o->newline() << "return;";
  o->newline(-1) << "} else {";
  o->newline(1) << "c->nesting ++;";
  o->newline(-1) << "}";

  // initialize locals
  // XXX: optimization: use memset instead
  for (unsigned i=0; i<v->locals.size(); i++)
    {
      if (v->locals[i]->index_types.size() > 0) // array?
	throw semantic_error (_("array locals not supported, missing global declaration?"),
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
  v->body->visit (this);
  o->newline() << "#undef return";

  this->current_function = 0;

  record_actions(0, v->body->tok, true);

  o->newline(-1) << "out:";
  o->newline(1) << "if (0) goto out;"; // make sure out: is marked used

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
        clog << _F("%s elided, duplicates %s\n", v->name.c_str(), dupe.c_str());

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
      o->newline();
      o->newline() << "static void " << v->name << " (struct context * __restrict__ c) ";
      o->line () << "{";
      o->indent (1);

      probe_contents[oss.str()] = v->name;

      o->newline() << "__label__ out;";

      // emit static read/write lock decls for global variables
      varuse_collecting_visitor vut(*session);
      if (v->needs_global_locks ())
        {
	  v->body->visit (& vut);
	  emit_lock_decls (vut);
	}

      // initialize frame pointer
      o->newline() << "struct " << v->name << "_locals * __restrict__ l = "
                   << "& c->probe_locals." << v->name << ";";
      o->newline() << "(void) l;"; // make sure "l" is marked used

      // Emit runtime safety net for unprivileged mode.
      v->emit_privilege_assertion (o);

      // emit probe local initialization block
      v->emit_probe_local_init(o);

      // emit all read/write locks for global variables
      if (v->needs_global_locks ())
	  emit_locks (vut);

      // initialize locals
      for (unsigned j=0; j<v->locals.size(); j++)
        {
	  if (v->locals[j]->synthetic)
            continue;
	  if (v->locals[j]->index_types.size() > 0) // array?
            throw semantic_error (_("array locals not supported, missing global declaration?"),
                                  v->locals[j]->tok);
	  else if (v->locals[j]->type == pe_long)
	    o->newline() << "l->" << c_varname (v->locals[j]->name)
			 << " = 0;";
	  else if (v->locals[j]->type == pe_string)
	    o->newline() << "l->" << c_varname (v->locals[j]->name)
			 << "[0] = '\\0';";
	  else
	    throw semantic_error (_("unsupported local variable type"),
				  v->locals[j]->tok);
        }

      v->initialize_probe_context_vars (o);

      v->body->visit (this);

      record_actions(0, v->body->tok, true);

      o->newline(-1) << "out:";
      // NB: no need to uninitialize locals, except if arrays/stats can
      // someday be local

      o->indent(1);
      if (v->needs_global_locks ())
	emit_unlocks (vut);

      // XXX: do this flush only if the body included a
      // print/printf/etc. routine!
      o->newline() << "_stp_print_flush();";
      o->newline(-1) << "}\n";
    }


  this->current_probe = 0;
}


void
c_unparser::emit_lock_decls(const varuse_collecting_visitor& vut)
{
  unsigned numvars = 0;

  if (session->verbose > 1)
    clog << "probe " << *current_probe->sole_location() << " locks ";

  o->newline() << "static const struct stp_probe_lock locks[] = {";
  o->indent(1);

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

      o->newline() << "{";
      o->newline(1) << ".lock = &global.s_" + v->name + "_lock,";
      o->newline() << ".write_p = " << (write_p ? 1 : 0) << ",";
      o->newline() << "#ifdef STP_TIMING";
      o->newline() << ".skipped = &global.s_" << c_varname (v->name) << "_lock_skip_count,";
      o->newline() << "#endif";
      o->newline(-1) << "},";

      numvars ++;
      if (session->verbose > 1)
        clog << v->name << "[" << (read_p ? "r" : "")
             << (write_p ? "w" : "")  << "] ";
    }

  o->newline(-1) << "};";

  if (session->verbose > 1)
    {
      if (!numvars)
        clog << _("nothing");
      clog << endl;
    }
}


void
c_unparser::emit_locks(const varuse_collecting_visitor&)
{
  o->newline() << "if (!stp_lock_probe(locks, ARRAY_SIZE(locks)))";
  o->newline(1) << "return;";
  o->indent(-1);
}


void
c_unparser::emit_unlocks(const varuse_collecting_visitor&)
{
  o->newline() << "stp_unlock_probe(locks, ARRAY_SIZE(locks));";
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
      throw semantic_error(_("array type is neither string nor long"));
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
      throw semantic_error(_("array key is neither string nor long"));
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
      throw semantic_error(_("array type is neither string nor long"));
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
      throw semantic_error (_("cannot expand unknown type"));
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
    throw semantic_error(_("unsupported c_expression token type"));

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
      throw semantic_error (_("unknown lvalue type in assignment"), tok);
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
      string fullmsg = msg + _(" type unsupported");
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
      string fullmsg = msg + _(" type unsupported");
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
	throw semantic_error (_("post assignment on strings not supported"),
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
        throw semantic_error (_F("string assignment operator %s unsupported", op.c_str()), tok);
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
	throw semantic_error (_("unknown macop for assignment"), tok);

      if (post)
	{
          if (macop == "/" || macop == "%" || op == "=")
            throw semantic_error (_("invalid post-mode operator"), tok);

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
		  o->newline(1) << "c->last_error = ";
                  o->line() << STAP_T_03;
		  o->newline() << "c->last_stmt = " << lex_cast_qstring(*rvalue->tok) << ";";
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
      throw semantic_error (_("assignment type not yet implemented"), tok);
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
    throw semantic_error (_("unresolved symbol"), tok);
  else
    throw semantic_error (_("unresolved symbol: ") + r->name);
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
    throw semantic_error(_("attempt to use scalar where map expected"), tok);
  statistic_decl sd;
  std::map<std::string, statistic_decl>::const_iterator i;
  i = session->stat_decls.find(v->name);
  if (i != session->stat_decls.end())
    sd = i->second;
  return mapvar (is_local (v, tok), v->type, sd,
      v->name, v->index_types, v->maxsize, v->wrap);
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
c_unparser::record_actions (unsigned actions, const token* tok, bool update)
{
  action_counter += actions;

  // Update if needed, or after queueing up a few actions, in case of very
  // large code sequences.
  if ((update && action_counter > 0) || action_counter >= 10/*<-arbitrary*/)
    {
      o->newline() << "c->actionremaining -= " << action_counter << ";";
      o->newline() << "if (unlikely (c->actionremaining <= 0)) {";
      o->newline(1) << "c->last_error = ";
      o->line() << STAP_T_04;

      // XXX it really ought to be illegal for anything to be missing a token,
      // but until we're sure of that, we need to defend against NULL.
      if (tok)
        o->newline() << "c->last_stmt = " << lex_cast_qstring(*tok) << ";";

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


void c_unparser::visit_try_block (try_block *s)
{
  record_actions(0, s->tok, true); // flush prior actions

  o->newline() << "{";
  o->newline(1) << "__label__ normal_fallthrough;";
  o->newline(1) << "{";
  o->newline() << "__label__ out;";

  assert (!session->unoptimized || s->try_block); // dead_stmtexpr_remover would zap it
  if (s->try_block)
    {
      s->try_block->visit (this);
      record_actions(0, s->try_block->tok, true); // flush accumulated actions
    }
  o->newline() << "goto normal_fallthrough;";

  o->newline() << "if (0) goto out;"; // to prevent 'unused label' warnings
  o->newline() << "out:";
  o->newline() << ";"; // to have _some_ statement

  // Close the scope of the above nested 'out' label, to make sure
  // that the catch block, should it encounter errors, does not resolve
  // a 'goto out;' to the above label, causing infinite looping.
  o->newline(-1) << "}";

  o->newline() << "if (likely(c->last_error == NULL)) goto out;";

  if (s->catch_error_var)
    {
      var cev(getvar(s->catch_error_var->referent, s->catch_error_var->tok));
      c_strcpy (cev.value(), "c->last_error");
    }
  o->newline() << "c->last_error = NULL;";

  // Prevent the catch{} handler from even starting if MAXACTIONS have
  // already been used up.  Add one for the act of catching too.
  record_actions(1, s->tok, true);

  if (s->catch_block)
    {
      s->catch_block->visit (this);
      record_actions(0, s->catch_block->tok, true); // flush accumulated actions
    }

  o->newline() << "normal_fallthrough:";
  o->newline() << ";"; // to have _some_ statement
  o->newline(-1) << "}";
}


void
c_unparser::visit_embeddedcode (embeddedcode *s)
{
  // Automatically add a call to assert_is_myproc to any code tagged with
  // /* myproc-unprivileged */
  if (s->code.find ("/* myproc-unprivileged */") != string::npos)
    o->newline() << "assert_is_myproc();";
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
  record_actions(1, s->tok);
}


void
c_unparser::visit_if_statement (if_statement *s)
{
  record_actions(1, s->tok, true);
  o->newline() << "if (";
  o->indent (1);
  s->condition->visit (this);
  o->indent (-1);
  o->line() << ") {";
  o->indent (1);
  s->thenblock->visit (this);
  record_actions(0, s->thenblock->tok, true);
  o->newline(-1) << "}";
  if (s->elseblock)
    {
      o->newline() << "else {";
      o->indent (1);
      s->elseblock->visit (this);
      record_actions(0, s->elseblock->tok, true);
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
  string ctr = lex_cast (label_counter++);
  string toplabel = "top_" + ctr;
  string contlabel = "continue_" + ctr;
  string breaklabel = "break_" + ctr;

  // initialization
  if (s->init) s->init->visit (this);
  record_actions(1, s->tok, true);

  // condition
  o->newline(-1) << toplabel << ":";

  // Emit an explicit action here to cover the act of iteration.
  // Equivalently, it can stand for the evaluation of the condition
  // expression.
  o->indent(1);
  record_actions(1, s->tok);

  o->newline() << "if (! (";
  if (s->cond->type != pe_long)
    throw semantic_error (_("expected numeric type"), s->cond->tok);
  s->cond->visit (this);
  o->line() << ")) goto " << breaklabel << ";";

  // body
  loop_break_labels.push_back (breaklabel);
  loop_continue_labels.push_back (contlabel);
  s->block->visit (this);
  record_actions(0, s->block->tok, true);
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


// Look for opportunities to used a saved value at the beginning of the loop
void
c_unparser::visit_foreach_loop_value (visitor* vis, foreach_loop* s,
                                      const string& value)
{
  bool stable_value = false;

  // There are three possible cases that we might easily retrieve the value:
  //   1. foreach ([keys] in any_array_type)
  //   2. foreach (idx in @hist_*(stat))
  //   3. foreach (idx in @hist_*(stat[keys]))
  //
  // For 1 and 2, we just need to check that the keys/idx are const throughout
  // the loop.  For 3, we'd have to check also that the arbitrary keys
  // expressions indexing the stat are const -- much harder, so I'm punting
  // that case for now.

  symbol *array;
  hist_op *hist;
  classify_indexable (s->base, array, hist);

  if (!(hist && get_symbol_within_expression(hist->stat)->referent->arity > 0))
    {
      set<vardecl*> indexes;
      for (unsigned i=0; i < s->indexes.size(); ++i)
        indexes.insert(s->indexes[i]->referent);

      varuse_collecting_visitor v(*session);
      s->block->visit (&v);
      v.embedded_seen = false; // reset because we only care about the indexes
      if (v.side_effect_free_wrt(indexes))
        stable_value = true;
    }

  if (stable_value)
    {
      // Rather than trying to compare arrayindexes to this foreach_loop
      // manually, we just create a fake arrayindex that would match the
      // foreach_loop, render it as a string, and later render encountered
      // arrayindexes as strings and compare.
      arrayindex ai;
      ai.base = s->base;
      for (unsigned i=0; i < s->indexes.size(); ++i)
        ai.indexes.push_back(s->indexes[i]);
      string loopai = lex_cast(ai);
      foreach_loop_values[loopai] = value;
      s->block->visit (vis);
      foreach_loop_values.erase(loopai);
    }
  else
    s->block->visit (vis);
}


bool
c_unparser::get_foreach_loop_value (arrayindex* ai, string& value)
{
  if (!ai)
    return false;
  map<string,string>::iterator it = foreach_loop_values.find(lex_cast(*ai));
  if (it == foreach_loop_values.end())
    return false;
  value = it->second;
  return true;
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
       throw semantic_error(_("Invalid indexing of histogram"), s->tok);

      // Then declare what we need to form the aggregate we're
      // iterating over, and all the tmpvars needed by our call to
      // load_aggregate().

      aggvar agg = parent->gensym_aggregate ();
      agg.declare(*(this->parent));
      load_aggregate (hist->stat);
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

  parent->visit_foreach_loop_value(this, s);
}

void
c_unparser::visit_foreach_loop (foreach_loop *s)
{
  symbol *array;
  hist_op *hist;
  classify_indexable (s->base, array, hist);

  string ctr = lex_cast (label_counter++);
  string toplabel = "top_" + ctr;
  string contlabel = "continue_" + ctr;
  string breaklabel = "break_" + ctr;

  if (array)
    {
      mapvar mv = getmap (array->referent, s->tok);
      itervar iv = getiter (array);
      vector<var> keys;

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
	  o->newline(1) << "c->last_error = ";
          o->line() << STAP_T_05 << mv << "\";";
	  o->newline() << "c->last_stmt = " << lex_cast_qstring(*s->tok) << ";";
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
		sort_column = -5; /* runtime/map.c SORT_COUNT */
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

      record_actions(1, s->tok, true);

      // condition
      o->newline(-1) << toplabel << ":";

      // Emit an explicit action here to cover the act of iteration.
      // Equivalently, it can stand for the evaluation of the
      // condition expression.
      o->indent(1);
      record_actions(1, s->tok);

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

      if (s->value)
        {
	  var v = getvar (s->value->referent);
	  c_assign (v, iv.get_value (v.type()), s->tok);
        }

      visit_foreach_loop_value(this, s, iv.get_value(array->type));
      record_actions(0, s->block->tok, true);
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

      var *v = load_aggregate(hist->stat, agg);
      v->assert_hist_compatible(*hist);

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

      record_actions(1, s->tok, true);
      o->newline() << "for (" << bucketvar << " = 0; "
		   << bucketvar << " < " << v->buckets() << "; "
		   << bucketvar << "++) { ";
      o->newline(1);
      loop_break_labels.push_back (breaklabel);
      loop_continue_labels.push_back (contlabel);

      if (s->limit)
      {
	  // If we've been through LIMIT loop iterations, quit.
	  o->newline() << "if (" << *limitv << "++ >= " << *res_limit
		       << ") break;";

	  // We're done with limitv and res_limit.
	  delete limitv;
	  delete res_limit;
      }

      if (s->value)
        {
          var v = getvar (s->value->referent);
          c_assign (v, agg.get_hist (bucketvar), s->tok);
        }

      visit_foreach_loop_value(this, s, agg.get_hist(bucketvar));
      record_actions(1, s->block->tok, true);

      o->newline(-1) << contlabel << ":";
      o->newline(1) << "continue;";
      o->newline(-1) << breaklabel << ":";
      o->newline(1) << "break;";
      o->newline(-1) << "}";
      loop_break_labels.pop_back ();
      loop_continue_labels.pop_back ();

      delete v;
    }
}


void
c_unparser::visit_return_statement (return_statement* s)
{
  if (current_function == 0)
    throw semantic_error (_("cannot 'return' from probe"), s->tok);

  if (s->value->type != current_function->type)
    throw semantic_error (_("return type mismatch"), current_function->tok,
                          s->tok);

  c_assign ("l->__retvalue", s->value, "return value");
  record_actions(1, s->tok, true);
  o->newline() << "goto out;";
}


void
c_unparser::visit_next_statement (next_statement* s)
{
  if (current_probe == 0)
    throw semantic_error (_("cannot 'next' from function"), s->tok);

  record_actions(1, s->tok, true);
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
    throwing_visitor (_("invalid operand of delete expression")),
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
	  throw semantic_error(_("Cannot delete unknown expression type"), e->tok);
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
      throw semantic_error(_("cannot delete histogram bucket entries\n"), e->tok);
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
      throw semantic_error(_("cannot delete histogram bucket entries\n"), e->tok);
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
  record_actions(1, s->tok);
}


void
c_unparser::visit_break_statement (break_statement* s)
{
  if (loop_break_labels.empty())
    throw semantic_error (_("cannot 'break' outside loop"), s->tok);

  record_actions(1, s->tok, true);
  o->newline() << "goto " << loop_break_labels.back() << ";";
}


void
c_unparser::visit_continue_statement (continue_statement* s)
{
  if (loop_continue_labels.empty())
    throw semantic_error (_("cannot 'continue' outside loop"), s->tok);

  record_actions(1, s->tok, true);
  o->newline() << "goto " << loop_continue_labels.back() << ";";
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
c_unparser::visit_embedded_expr (embedded_expr* e)
{
  o->line() << "(";

  // Automatically add a call to assert_is_myproc to any code tagged with
  // /* myproc-unprivileged */
  if (e->code.find ("/* myproc-unprivileged */") != string::npos)
    o->line() << "({ assert_is_myproc(); }), ";

  if (e->type == pe_long)
    o->line() << "((int64_t) (" << e->code << "))";
  else if (e->type == pe_string)
    o->line() << "((const char *) (" << e->code << "))";
  else
    throw semantic_error (_("expected numeric or string type"), e->tok);

  o->line() << ")";
}


void
c_unparser::visit_binary_expression (binary_expression* e)
{
  if (e->type != pe_long ||
      e->left->type != pe_long ||
      e->right->type != pe_long)
    throw semantic_error (_("expected numeric types"), e->tok);

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
      o->newline(1) << "c->last_error = ";
      o->line() << STAP_T_03;
      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
      o->newline() << "goto out;";
      o->newline(-1) << "}";
      o->newline() << ((e->op == "/") ? "_stp_div64" : "_stp_mod64")
		   << " (NULL, " << left << ", " << right << ");";

      o->newline(-1) << "})";
    }
  else
    throw semantic_error (_("operator not yet implemented"), e->tok);
}


void
c_unparser::visit_unary_expression (unary_expression* e)
{
  if (e->type != pe_long ||
      e->operand->type != pe_long)
    throw semantic_error (_("expected numeric types"), e->tok);

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
    throw semantic_error (_("expected numeric types"), e->tok);

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
    throw semantic_error (_("expected numeric types"), e->tok);

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
c_tmpcounter::visit_comparison (comparison* e)
{
  // When computing string operands, their results may be in overlapping
  // __retvalue memory, so we need to save at least one in a tmpvar.
  if (e->left->type == pe_string)
    {
      tmpvar left = parent->gensym (pe_string);
      if (e->left->tok->type != tok_string)
        left.declare (*parent);
    }

  e->left->visit (this);
  e->right->visit (this);
}


void
c_unparser::visit_comparison (comparison* e)
{
  o->line() << "(";

  if (e->left->type == pe_string)
    {
      if (e->right->type != pe_string)
        throw semantic_error (_("expected string types"), e->tok);

      o->line() << "({";
      o->indent(1);

      tmpvar left = gensym (pe_string);
      if (e->left->tok->type == tok_string)
        left.override(c_expression(e->left));
      else
        c_assign (left.value(), e->left, "assignment");

      o->newline() << "strncmp (" << left << ", ";
      e->right->visit (this);
      o->line() << ", MAXSTRINGLEN) " << e->op << " 0;";
      o->newline(-1) << "})";
    }
  else if (e->left->type == pe_long)
    {
      if (e->right->type != pe_long)
        throw semantic_error (_("expected numeric types"), e->tok);

      o->line() << "((";
      e->left->visit (this);
      o->line() << ") " << e->op << " (";
      e->right->visit (this);
      o->line() << "))";
    }
  else
    throw semantic_error (_("unexpected type"), e->left->tok);

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
    throw semantic_error (_("unexpected concatenation operator"), e->tok);

  if (e->type != pe_string ||
      e->left->type != pe_string ||
      e->right->type != pe_string)
    throw semantic_error (_("expected string types"), e->tok);

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
    throw semantic_error (_("expected numeric condition"), e->cond->tok);

  if (e->truevalue->type != e->falsevalue->type ||
      e->type != e->truevalue->type ||
      (e->truevalue->type != pe_long && e->truevalue->type != pe_string))
    throw semantic_error (_("expected matching types"), e->tok);

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
	throw semantic_error (_("non-number <<< expression"), e->tok);

      if (e->left->type != pe_stats)
	throw semantic_error (_("non-stats left operand to <<< expression"), e->left->tok);

      if (e->right->type != pe_long)
	throw semantic_error (_("non-number right operand to <<< expression"), e->right->tok);

    }
  else
    {
      if (e->type != e->left->type)
	throw semantic_error (_("type mismatch"), e->tok, e->left->tok);
      if (e->right->type != e->left->type)
	throw semantic_error (_("type mismatch"), e->right->tok, e->left->tok);
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
    throw semantic_error (_("expected numeric type"), e->tok);

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
    throw semantic_error (_("expected numeric type"), e->tok);

  c_unparser_assignment tav (this, e->op, true);
  e->operand->visit (& tav);
}


void
c_unparser::visit_symbol (symbol* e)
{
  assert (e->referent != 0);
  vardecl* r = e->referent;

  if (r->index_types.size() != 0)
    throw semantic_error (_("invalid reference to array"), e->tok);

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
        throw semantic_error (_("need rvalue for assignment"), tok);
    }
}

void
c_unparser_assignment::visit_symbol (symbol *e)
{
  stmt_expr block(*parent);

  assert (e->referent != 0);
  if (e->referent->index_types.size() != 0)
    throw semantic_error (_("unexpected reference to array"), e->tok);

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
  throw semantic_error(_("cannot translate general target-symbol expression"), e->tok);
}


void
c_unparser::visit_cast_op (cast_op* e)
{
  throw semantic_error(_("cannot translate general @cast expression"), e->tok);
}


void
c_unparser::visit_defined_op (defined_op* e)
{
  throw semantic_error(_("cannot translate general @defined expression"), e->tok);
}


void
c_unparser::visit_entry_op (entry_op* e)
{
  throw semantic_error(_("cannot translate general @entry expression"), e->tok);
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
	throw semantic_error (_("invalid array reference"), e->tok);

      for (unsigned i=0; i<r->index_types.size(); i++)
	{
	  if (r->index_types[i] != e->indexes[i]->type)
	    throw semantic_error (_("array index type mismatch"), e->indexes[i]->tok);

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
c_tmpcounter::load_aggregate (expression *e)
{
  symbol *sym = get_symbol_within_expression (e);
  string agg_value;
  arrayindex* arr = NULL;
  expression_is_arrayindex (e, arr);

  // If we have a foreach_loop value, we don't need tmps for indexes
  if (sym->referent->arity != 0 &&
      !parent->get_foreach_loop_value(arr, agg_value))
    {
      if (!arr)
	throw semantic_error(_("expected arrayindex expression"), e->tok);
      load_map_indices (arr);
    }
}


var*
c_unparser::load_aggregate (expression *e, aggvar & agg)
{
  symbol *sym = get_symbol_within_expression (e);

  if (sym->referent->type != pe_stats)
    throw semantic_error (_("unexpected aggregate of non-statistic"), sym->tok);

  var *v;
  if (sym->referent->arity == 0)
    {
      v = new var(getvar(sym->referent, sym->tok));
      // o->newline() << "c->last_stmt = " << lex_cast_qstring(*sym->tok) << ";";
      o->newline() << agg << " = _stp_stat_get (" << *v << ", 0);";
    }
  else
    {
      mapvar *mv = new mapvar(getmap(sym->referent, sym->tok));
      v = mv;

      arrayindex *arr = NULL;
      if (!expression_is_arrayindex (e, arr))
	throw semantic_error(_("unexpected aggregate of non-arrayindex"), e->tok);

      // If we have a foreach_loop value, we don't need to index the map
      string agg_value;
      if (get_foreach_loop_value(arr, agg_value))
        o->newline() << agg << " = " << agg_value << ";";
      else
        {
          vector<tmpvar> idx;
          load_map_indices (arr, idx);
          // o->newline() << "c->last_stmt = " << lex_cast_qstring(*sym->tok) << ";";
	  bool pre_agg = (aggregations_active.count(mv->value()) > 0);
          o->newline() << agg << " = " << mv->get(idx, pre_agg) << ";";
        }
    }

  return v;
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
  // If we have a foreach_loop value, no other tmps are needed
  string ai_value;
  if (parent->get_foreach_loop_value(e, ai_value))
    return;

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
	throw semantic_error(_("Invalid indexing of histogram"), e->tok);
      tmpvar ix = parent->gensym (pe_long);
      ix.declare (*parent);
      e->indexes[0]->visit(this);
      tmpvar res = parent->gensym (pe_long);
      res.declare (*parent);

      // Then the aggregate, and all the tmpvars needed by our call to
      // load_aggregate().

      aggvar agg = parent->gensym_aggregate ();
      agg.declare(*(this->parent));
      load_aggregate (hist->stat);
    }
}


void
c_unparser::visit_arrayindex (arrayindex* e)
{
  // If we have a foreach_loop value, use it and call it a day!
  string ai_value;
  if (get_foreach_loop_value(e, ai_value))
    {
      o->line() << ai_value;
      return;
    }

  symbol *array;
  hist_op *hist;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      // Visiting an statistic-valued array in a non-lvalue context is prohibited.
      if (array->referent->type == pe_stats)
	throw semantic_error (_("statistic-valued array in rvalue context"), e->tok);

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

      var *v = load_aggregate(hist->stat, agg);
      v->assert_hist_compatible(*hist);

      o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";

      // PR 2142+2610: empty aggregates
      o->newline() << "if (unlikely (" << agg.value() << " == NULL)"
                   << " || " <<  agg.value() << "->count == 0) {";
      o->newline(1) << "c->last_error = ";
      o->line() << STAP_T_06;
      o->newline() << "goto out;";
      o->newline(-1) << "} else {";
      o->newline(1) << "if (" << histogram_index_check(*v, idx[0]) << ")";
      o->newline(1)  << res << " = " << agg << "->histogram[" << idx[0] << "];";
      o->newline(-1) << "else {";
      o->newline(1)  << "c->last_error = ";
      o->line() << STAP_T_07;
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
      throw semantic_error(_("cannot assign to histogram buckets"), e->tok);
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
	throw semantic_error (_("unexpected reference to scalar"), e->tok);

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
      throw semantic_error(_("cannot assign to histogram buckets"), e->tok);
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
    throw semantic_error (_("invalid length argument list"), e->tok);

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
	throw semantic_error (_("function argument type mismatch"),
			      e->args[i]->tok, r->formal_args[i]->tok);

      if (e->args[i]->tok->type == tok_number
	  || e->args[i]->tok->type == tok_string)
	t.override(c_expression(e->args[i]));
      else
        {
	  // o->newline() << "c->last_stmt = "
          // << lex_cast_qstring(*e->args[i]->tok) << ";";
	  c_assign (t.value(), e->args[i],
		    _("function actual argument evaluation"));
	}
      tmp.push_back(t);
    }

  // copy in actual arguments
  for (unsigned i=0; i<e->args.size(); i++)
    {
      if (r->formal_args[i]->type != e->args[i]->type)
	throw semantic_error (_("function argument type mismatch"),
			      e->args[i]->tok, r->formal_args[i]->tok);

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


static int
preprocess_print_format(print_format* e, vector<tmpvar>& tmp,
                        vector<print_format::format_component>& components,
                        string& format_string)
{
  int use_print = 0;

  if (e->print_with_format)
    {
      format_string = e->raw_components;
      components = e->components;
    }
  else
    {
      string delim;
      if (e->print_with_delim)
	{
	  stringstream escaped_delim;
	  const string& dstr = e->delimiter.literal_string;
	  for (string::const_iterator i = dstr.begin();
	       i != dstr.end(); ++i)
	    {
	      if (*i == '%')
		escaped_delim << '%';
	      escaped_delim << *i;
	    }
	  delim = escaped_delim.str();
	}

      // Synthesize a print-format string if the user didn't
      // provide one; the synthetic string simply contains one
      // directive for each argument.
      stringstream format;
      for (unsigned i = 0; i < e->args.size(); ++i)
	{
	  if (i > 0 && e->print_with_delim)
	    format << delim;
	  switch (e->args[i]->type)
	    {
	    default:
	    case pe_unknown:
	      throw semantic_error(_("cannot print unknown expression type"), e->args[i]->tok);
	    case pe_stats:
	      throw semantic_error(_("cannot print a raw stats object"), e->args[i]->tok);
	    case pe_long:
	      format << "%d";
	      break;
	    case pe_string:
	      format << "%s";
	      break;
	    }
	}
      if (e->print_with_newline)
	format << "\\n";

      format_string = format.str();
      components = print_format::string_to_components(format_string);
    }


  if ((tmp.size() == 0 && format_string.find("%%") == string::npos)
      || (tmp.size() == 1 && format_string == "%s"))
    use_print = 1;
  else if (tmp.size() == 1
	   && e->args[0]->tok->type == tok_string
	   && format_string == "%s\\n")
    {
      use_print = 1;
      tmp[0].override(tmp[0].value() + "\"\\n\"");
    }

  return use_print;
}

void
c_tmpcounter::visit_print_format (print_format* e)
{
  if (e->hist)
    {
      aggvar agg = parent->gensym_aggregate ();
      agg.declare(*(this->parent));
      load_aggregate (e->hist->stat);

      // And the result for sprint[ln](@hist_*)
      if (!e->print_to_stream)
        {
          exp_type ty = pe_string;
          tmpvar res = parent->gensym(ty);
          res.declare(*parent);
        }
    }
  else
    {
      // One temporary per argument
      vector<tmpvar> tmp;
      for (unsigned i=0; i < e->args.size(); i++)
	{
	  tmpvar t = parent->gensym (e->args[i]->type);
	  tmp.push_back(t);
	  if (e->args[i]->type == pe_unknown)
	    {
	      throw semantic_error(_("unknown type of arg to print operator"),
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

      // Munge so we can find our compiled printf
      vector<print_format::format_component> components;
      string format_string;
      int use_print = preprocess_print_format(e, tmp, components, format_string);

      // If not in a shortcut case, declare the compiled printf
      if (!(e->print_to_stream && (e->print_char || use_print)))
	parent->declare_compiled_printf(e->print_to_stream, format_string);
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
      aggvar agg = gensym_aggregate ();

      var *v = load_aggregate(e->hist->stat, agg);
      v->assert_hist_compatible(*e->hist);

      {
        // PR 2142+2610: empty aggregates
        o->newline() << "if (unlikely (" << agg.value() << " == NULL)"
                     << " || " <<  agg.value() << "->count == 0) {";
        o->newline(1) << "c->last_error = ";
        o->line() << STAP_T_06;
	o->newline() << "c->last_stmt = " << lex_cast_qstring(*e->tok) << ";";
	o->newline() << "goto out;";
        o->newline(-1) << "} else";
        if (e->print_to_stream)
          {
            o->newline(1) << "_stp_stat_print_histogram (" << v->hist() << ", " << agg.value() << ");";
            o->indent(-1);
          }
        else
          {
            exp_type ty = pe_string;
            tmpvar res = gensym (ty);
            o->newline(1) << "_stp_stat_print_histogram_buf (" << res.value() << ", MAXSTRINGLEN, " << v->hist() << ", " << agg.value() << ");";
            o->newline(-1) << res.value() << ";";
          }
      }

      delete v;
    }
  else
    {
      stmt_expr block(*this);

      // PR10750: Enforce a reasonable limit on # of varargs
      // 32 varargs leads to max 256 bytes on the stack
      if (e->args.size() > 32)
        throw semantic_error(_F(ngettext("additional argument to print", "too many arguments to print (%zu)",
                                e->args.size()), e->args.size()), e->tok);

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

      // Allocate the result
      exp_type ty = e->print_to_stream ? pe_long : pe_string;
      tmpvar res = gensym (ty);

      // Munge so we can find our compiled printf
      vector<print_format::format_component> components;
      string format_string, format_string_out;
      int use_print = preprocess_print_format(e, tmp, components, format_string);
      format_string_out = print_format::components_to_string(components);

      // Make the [s]printf call...

      // Generate code to check that any pointer arguments are actually accessible.
      size_t arg_ix = 0;
      for (unsigned i = 0; i < components.size(); ++i) {
	if (components[i].type == print_format::conv_literal)
	  continue;

	/* Take note of the width and precision arguments, if any.  */
	int width_ix = -1, prec_ix= -1;
	if (components[i].widthtype == print_format::width_dynamic)
	  width_ix = arg_ix++;
	if (components[i].prectype == print_format::prec_dynamic)
	  prec_ix = arg_ix++;

        (void) width_ix; /* XXX: notused */

        /* %m and %M need special care for digging into memory. */
	if (components[i].type == print_format::conv_memory
	    || components[i].type == print_format::conv_memory_hex)
	  {
	    string mem_size;
	    const token* prec_tok = e->tok;
	    if (prec_ix != -1)
	      {
		mem_size = tmp[prec_ix].value();
		prec_tok = e->args[prec_ix]->tok;
	      }
	    else if (components[i].prectype == print_format::prec_static &&
		     components[i].precision > 0)
	      mem_size = lex_cast(components[i].precision) + "LL";
	    else
	      mem_size = "1LL";

	    /* Limit how much can be printed at a time. (see also PR10490) */
	    o->newline() << "c->last_stmt = " << lex_cast_qstring(*prec_tok) << ";";
	    o->newline() << "if (" << mem_size << " > 1024) {";
	    o->newline(1) << "snprintf(c->error_buffer, sizeof(c->error_buffer), "
			  << "\"%lld is too many bytes for a memory dump\", (long long)"
			  << mem_size << ");";
	    o->newline() << "c->last_error = c->error_buffer;";
	    o->newline() << "goto out;";
	    o->newline(-1) << "}";
	  }

	++arg_ix;
      }

      // Shortcuts for cases that aren't formatted at all
      if (e->print_to_stream)
        {
	  if (e->print_char)
	    {
	      o->newline() << "_stp_print_char (";
	      if (tmp.size())
		o->line() << tmp[0].value() << ");";
	      else
		o->line() << '"' << format_string_out << "\");";
	      return;
	    }
	  if (use_print)
	    {
	      o->newline() << "_stp_print (";
	      if (tmp.size())
		o->line() << tmp[0].value() << ");";
	      else
		o->line() << '"' << format_string_out << "\");";
	      return;
	    }
	}

      // The default it to use the new compiled-printf, but one can fall back
      // to the old code with -DSTP_LEGACY_PRINT if desired.
      o->newline() << "#ifndef STP_LEGACY_PRINT";
      o->indent(1);

      // Copy all arguments to the compiled-printf's space, then call it
      const string& compiled_printf =
	get_compiled_printf (e->print_to_stream, format_string);
      for (unsigned i = 0; i < tmp.size(); ++i)
	o->newline() << "c->printf_locals." << compiled_printf
		     << ".arg" << i << " = " << tmp[i].value() << ";";
      if (e->print_to_stream)
	// We'll just hardcode the result of 0 instead of using the
	// temporary.
	res.override("((int64_t)0LL)");
      else
	o->newline() << "c->printf_locals." << compiled_printf
		     << ".__retvalue = " << res.value() << ";";
      o->newline() << compiled_printf << " (c);";

      o->newline(-1) << "#else // STP_LEGACY_PRINT";
      o->indent(1);

      // Generate the legacy call that goes through _stp_vsnprintf.
      if (e->print_to_stream)
	o->newline() << "_stp_printf (";
      else
	o->newline() << "_stp_snprintf (" << res.value() << ", MAXSTRINGLEN, ";
      o->line() << '"' << format_string_out << '"';

      // Make sure arguments match the expected type of the format specifier.
      arg_ix = 0;
      for (unsigned i = 0; i < components.size(); ++i)
	{
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
	  else if (arg_ix < tmp.size())
	    o->line() << ", " << tmp[arg_ix++].value();
	}
      o->line() << ");";
      o->newline(-1) << "#endif // STP_LEGACY_PRINT";
      o->newline() << "if (unlikely(c->last_error)) goto out;";
      o->newline() << res.value() << ";";
    }
}


void
c_tmpcounter::visit_stat_op (stat_op* e)
{
  aggvar agg = parent->gensym_aggregate ();
  tmpvar res = parent->gensym (pe_long);

  agg.declare(*(this->parent));
  res.declare(*(this->parent));

  load_aggregate (e->stat);
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
    aggvar agg = gensym_aggregate ();
    tmpvar res = gensym (pe_long);
    var *v = load_aggregate(e->stat, agg);
    {
      // PR 2142+2610: empty aggregates
      if ((e->ctype == sc_count) ||
          (e->ctype == sc_sum &&
           strverscmp(session->compatible.c_str(), "1.5") >= 0))
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
          o->newline(1) << "c->last_error = ";
          o->line() << STAP_T_06;
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
    delete v;
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


typedef map<Dwarf_Addr,const char*> addrmap_t; // NB: plain map, sorted by address

struct unwindsym_dump_context
{
  systemtap_session& session;
  ostream& output;
  unsigned stp_module_index;

  int build_id_len;
  unsigned char *build_id_bits;
  GElf_Addr build_id_vaddr;

  unsigned long stp_kretprobe_trampoline_addr;
  Dwarf_Addr stext_offset;

  vector<pair<string,unsigned> > seclist; // encountered relocation bases
                                          // (section names and sizes)
  map<unsigned, addrmap_t> addrmap; // per-relocation-base sorted addrmap

  void *debug_frame;
  size_t debug_len;
  void *debug_frame_hdr;
  size_t debug_frame_hdr_len;
  Dwarf_Addr debug_frame_off;
  void *eh_frame;
  void *eh_frame_hdr;
  size_t eh_len;
  size_t eh_frame_hdr_len;
  Dwarf_Addr eh_addr;
  Dwarf_Addr eh_frame_hdr_addr;

  set<string> undone_unwindsym_modules;
};

static void create_debug_frame_hdr (const unsigned char e_ident[],
				    Elf_Data *debug_frame,
				    void **debug_frame_hdr,
				    size_t *debug_frame_hdr_len,
				    Dwarf_Addr *debug_frame_off,
				    systemtap_session& session,
				    Dwfl_Module *mod)
{
  *debug_frame_hdr = NULL;
  *debug_frame_hdr_len = 0;

  int cies = 0;
  set< pair<Dwarf_Addr, Dwarf_Off> > fdes;
  set< pair<Dwarf_Addr, Dwarf_Off> >::iterator it;

  // In the .debug_frame the FDE encoding is always DW_EH_PE_absptr.
  // So there is no need to read the CIEs.  And the size is either 4
  // or 8, depending on the elf class from e_ident.
  int size = (e_ident[EI_CLASS] == ELFCLASS32) ? 4 : 8;
  int res = 0;
  Dwarf_Off off = 0;
  Dwarf_CFI_Entry entry;

  while (res != 1)
    {
      Dwarf_Off next_off;
      res = dwarf_next_cfi (e_ident, debug_frame, false, off, &next_off,
			    &entry);
      if (res == 0)
	{
	  if (entry.CIE_id == DW_CIE_ID_64)
	    cies++; // We can just ignore the CIEs.
	  else
	    {
	      Dwarf_Addr addr;
	      if (size == 4)
		addr = (*((uint32_t *) entry.fde.start));
	      else
		addr = (*((uint64_t *) entry.fde.start));
	      fdes.insert(pair<Dwarf_Addr, Dwarf_Off>(addr, off));
	    }
	}
      else if (res > 0)
	; // Great, all done.
      else
	{
	  // Warn, but continue, backtracing will be slow...
          if (session.verbose > 2 && ! session.suppress_warnings)
	    {
	      const char *modname = dwfl_module_info (mod, NULL,
						      NULL, NULL, NULL,
						      NULL, NULL, NULL);
	      session.print_warning("Problem creating debug frame hdr for "
				    + lex_cast_qstring(modname)
				    + ", " + dwarf_errmsg (-1));
	    }
	  return;
	}
      off = next_off;
    }

  if (fdes.size() > 0)
    {
      it = fdes.begin();
      Dwarf_Addr first_addr = (*it).first;
      int res = dwfl_module_relocate_address (mod, &first_addr);
      dwfl_assert ("create_debug_frame_hdr, dwfl_module_relocate_address",
		   res >= 0);
      *debug_frame_off = (*it).first - first_addr;
    }

  size_t total_size = 4 + (2 * size) + (2 * size * fdes.size());
  uint8_t *hdr = (uint8_t *) malloc(total_size);
  *debug_frame_hdr = hdr;
  *debug_frame_hdr_len = total_size;

  hdr[0] = 1; // version
  hdr[1] = DW_EH_PE_absptr; // ptr encoding
  hdr[2] = (size == 4) ? DW_EH_PE_udata4 : DW_EH_PE_udata8; // count encoding
  hdr[3] = DW_EH_PE_absptr; // table encoding
  if (size == 4)
    {
      uint32_t *table = (uint32_t *)(hdr + 4);
      *table++ = (uint32_t) 0; // eh_frame_ptr, unused
      *table++ = (uint32_t) fdes.size();
      for (it = fdes.begin(); it != fdes.end(); it++)
	{
	  *table++ = (*it).first;
	  *table++ = (*it).second;
	}
    }
  else
    {
      uint64_t *table = (uint64_t *)(hdr + 4);
      *table++ = (uint64_t) 0; // eh_frame_ptr, unused
      *table++ = (uint64_t) fdes.size();
      for (it = fdes.begin(); it != fdes.end(); it++)
	{
	  *table++ = (*it).first;
	  *table++ = (*it).second;
	}
    }
}

// Get the .debug_frame end .eh_frame sections for the given module.
// Also returns the lenght of both sections when found, plus the section
// address (offset) of the eh_frame data. If a debug_frame is found, a
// synthesized debug_frame_hdr is also returned.
static void get_unwind_data (Dwfl_Module *m,
			     void **debug_frame, void **eh_frame,
			     size_t *debug_len, size_t *eh_len,
			     Dwarf_Addr *eh_addr,
			     void **eh_frame_hdr, size_t *eh_frame_hdr_len,
			     void **debug_frame_hdr,
			     size_t *debug_frame_hdr_len,
			     Dwarf_Addr *debug_frame_off,
			     Dwarf_Addr *eh_frame_hdr_addr,
			     systemtap_session& session)
{
  Dwarf_Addr start, bias = 0;
  GElf_Ehdr *ehdr, ehdr_mem;
  GElf_Shdr *shdr, shdr_mem;
  Elf_Scn *scn;
  Elf_Data *data = NULL;
  Elf *elf;

  // fetch .eh_frame info preferably from main elf file.
  dwfl_module_info (m, NULL, &start, NULL, NULL, NULL, NULL, NULL);
  elf = dwfl_module_getelf(m, &bias);
  ehdr = gelf_getehdr(elf, &ehdr_mem);
  scn = NULL;
  while ((scn = elf_nextscn(elf, scn)))
    {
      bool eh_frame_seen = false;
      bool eh_frame_hdr_seen = false;
      shdr = gelf_getshdr(scn, &shdr_mem);
      const char* scn_name = elf_strptr(elf, ehdr->e_shstrndx, shdr->sh_name);
      if (!eh_frame_seen
	  && strcmp(scn_name, ".eh_frame") == 0
	  && shdr->sh_type == SHT_PROGBITS)
	{
	  data = elf_rawdata(scn, NULL);
	  *eh_frame = data->d_buf;
	  *eh_len = data->d_size;
	  // For ".dynamic" sections we want the offset, not absolute addr.
	  // Note we don't trust dwfl_module_relocations() for ET_EXEC.
	  if (ehdr->e_type != ET_EXEC && dwfl_module_relocations (m) > 0)
	    *eh_addr = shdr->sh_addr - start + bias;
	  else
	    *eh_addr = shdr->sh_addr;
	  eh_frame_seen = true;
	}
      else if (!eh_frame_hdr_seen
	       && strcmp(scn_name, ".eh_frame_hdr") == 0
	       && shdr->sh_type == SHT_PROGBITS)
        {
          data = elf_rawdata(scn, NULL);
          *eh_frame_hdr = data->d_buf;
          *eh_frame_hdr_len = data->d_size;
          if (ehdr->e_type != ET_EXEC && dwfl_module_relocations (m) > 0)
	    *eh_frame_hdr_addr = shdr->sh_addr - start + bias;
	  else
	    *eh_frame_hdr_addr = shdr->sh_addr;
          eh_frame_hdr_seen = true;
        }
      if (eh_frame_seen && eh_frame_hdr_seen)
        break;
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

  if (*debug_frame != NULL && *debug_len > 0)
    create_debug_frame_hdr (ehdr->e_ident, data,
			    debug_frame_hdr, debug_frame_hdr_len,
			    debug_frame_off, session, m);
}

static int
dump_build_id (Dwfl_Module *m,
	       unwindsym_dump_context *c,
	       const char *name, Dwarf_Addr base)
{
  string modname = name;

  //extract build-id from debuginfo file
  int build_id_len = 0;
  unsigned char *build_id_bits;
  GElf_Addr build_id_vaddr;

  if ((build_id_len=dwfl_module_build_id(m,
                                        (const unsigned char **)&build_id_bits,
                                         &build_id_vaddr)) > 0)
  {
     if (modname != "kernel")
      {
        Dwarf_Addr reloc_vaddr = build_id_vaddr;
        const char *secname;
        int i;

        i = dwfl_module_relocate_address (m, &reloc_vaddr);
        dwfl_assert ("dwfl_module_relocate_address reloc_vaddr", i >= 0);

        secname = dwfl_module_relocation_info (m, i, NULL);

        // assert same section name as in runtime/transport/symbols.c
        // NB: this is applicable only to module("...") probes.
        // process("...") ones may have relocation bases like '.dynamic',
        // and so we'll have to store not just a generic offset but
        // the relocation section/symbol name too: just like we do
        // for probe PC addresses themselves.  We want to set build_id_vaddr for
        // user modules even though they will not have a secname.

	if (modname[0] != '/')
	  if (!secname || strcmp(secname, ".note.gnu.build-id"))
	    throw semantic_error (_("unexpected build-id reloc section ") +
				  string(secname ?: "null"));

        build_id_vaddr = reloc_vaddr;
      }

    if (c->session.verbose > 1)
      {
        clog << _F("Found build-id in %s, length %d, start at %#" PRIx64,
                   name, build_id_len, build_id_vaddr) << endl;
      }

    c->build_id_len = build_id_len;
    c->build_id_vaddr = build_id_vaddr;
    c->build_id_bits = build_id_bits;
  }

  return DWARF_CB_OK;
}

static int
dump_section_list (Dwfl_Module *m,
                   unwindsym_dump_context *c,
                   const char *name, Dwarf_Addr base)
{
  // Depending on ELF section names normally means you are doing it WRONG.
  // Sadly it seems we do need it for the kernel modules. Which are ET_REL
  // files, which are "dynamically loaded" by the kernel. We keep a section
  // list for them to know which symbol corresponds to which section.
  //
  // Luckily for the kernel, normal executables (ET_EXEC) or shared
  // libraries (ET_DYN) we don't need it. We just have one "section",
  // which we will just give the arbitrary names "_stext", ".absolute"
  // or ".dynamic"

  string modname = name;

  // Use start and end as to calculate size for _stext, .dynamic and
  // .absolute sections.
  Dwarf_Addr start, end;
  dwfl_module_info (m, NULL, &start, &end, NULL, NULL, NULL, NULL);

  // Look up the relocation basis for symbols
  int n = dwfl_module_relocations (m);
  dwfl_assert ("dwfl_module_relocations", n >= 0);

 if (n == 0)
    {
      // ET_EXEC, no relocations.
      string secname = ".absolute";
      unsigned size = end - start;
      c->seclist.push_back (make_pair (secname, size));
      return DWARF_CB_OK;
    }
  else if (n == 1)
    {
      // kernel or shared library (ET_DYN).
      string secname;
      secname = (modname == "kernel") ? "_stext" : ".dynamic";
      unsigned size = end - start;
      c->seclist.push_back (make_pair (secname, size));
      return DWARF_CB_OK;
    }
  else if (n > 1)
    {
      // ET_REL, kernel module.
      string secname;
      unsigned size;
      Dwarf_Addr bias;
      GElf_Ehdr *ehdr, ehdr_mem;
      GElf_Shdr *shdr, shdr_mem;
      Elf *elf = dwfl_module_getelf(m, &bias);
      ehdr = gelf_getehdr(elf, &ehdr_mem);
      Elf_Scn *scn = NULL;
      while ((scn = elf_nextscn(elf, scn)))
	{
	  // Just the "normal" sections with program bits please.
	  shdr = gelf_getshdr(scn, &shdr_mem);
	  if ((shdr->sh_type == SHT_PROGBITS || shdr->sh_type == SHT_NOBITS)
	      && (shdr->sh_flags & SHF_ALLOC))
	    {
	      size = shdr->sh_size;
	      const char* scn_name = elf_strptr(elf, ehdr->e_shstrndx,
						shdr->sh_name);
	      secname = scn_name;
	      c->seclist.push_back (make_pair (secname, size));
	    }
	}

      return DWARF_CB_OK;
    }

  // Impossible... dflw_assert above will have triggered.
  return DWARF_CB_ABORT;
}

/* Some architectures create special local symbols that are not
   interesting. */
static int
skippable_arch_symbol (GElf_Half e_machine, const char *name, GElf_Sym *sym)
{
  /* Filter out ARM mapping symbols */
  if (e_machine == EM_ARM
      && GELF_ST_TYPE (sym->st_info) == STT_NOTYPE
      && (! strcmp(name, "$a") || ! strcmp(name, "$t")
	  || ! strcmp(name, "$t.x") || ! strcmp(name, "$d")
	  || ! strcmp(name, "$v") || ! strcmp(name, "$d.realdata")))
    return 1;

  return 0;
}

static int
dump_symbol_tables (Dwfl_Module *m,
		    unwindsym_dump_context *c,
		    const char *modname, Dwarf_Addr base)
{
  // Use end as sanity check when resolving symbol addresses.
  Dwarf_Addr end;
  dwfl_module_info (m, NULL, NULL, &end, NULL, NULL, NULL, NULL);

  int syments = dwfl_module_getsymtab(m);
  dwfl_assert (_F("Getting symbol table for %s", modname), syments >= 0);

  // Look up the relocation basis for symbols
  int n = dwfl_module_relocations (m);
  dwfl_assert ("dwfl_module_relocations", n >= 0);

  /* Needed on ppc64, for function descriptors. */
  Dwarf_Addr elf_bias;
  GElf_Ehdr *ehdr, ehdr_mem;
  Elf *elf;
  elf = dwfl_module_getelf(m, &elf_bias);
  ehdr = gelf_getehdr(elf, &ehdr_mem);

  // XXX: unfortunate duplication with tapsets.cxx:emit_address()

  // extra_offset is for the special kernel case.
  Dwarf_Addr extra_offset = 0;
  Dwarf_Addr kretprobe_trampoline_addr = (unsigned long) -1;
  int is_kernel = !strcmp(modname, "kernel");

  /* Set to bail early if we are just examining the kernel
     and don't need anything more. */
  int done = 0;
  for (int i = 0; i < syments && !done; ++i)
    {
      if (pending_interrupts)
        return DWARF_CB_ABORT;

      GElf_Sym sym;
      GElf_Word shndxp;

      const char *name = dwfl_module_getsym(m, i, &sym, &shndxp);
      if (name)
        {
          Dwarf_Addr sym_addr = sym.st_value;

	  // We always need two special values from the kernel.
	  // _stext for extra_offset and kretprobe_trampoline_holder
	  // for the unwinder.
          if (is_kernel)
	    {
	      // NB: Yey, we found the kernel's _stext value.
	      // Sess.sym_stext may be unset (0) at this point, since
	      // there may have been no kernel probes set.  We could
	      // use tapsets.cxx:lookup_symbol_address(), but then
	      // we're already iterating over the same data here...
	      if (! strcmp(name, "_stext"))
		{
		  int ki;
		  extra_offset = sym_addr;
		  ki = dwfl_module_relocate_address (m, &extra_offset);
		  dwfl_assert ("dwfl_module_relocate_address extra_offset",
			       ki >= 0);

		  if (c->session.verbose > 2)
		    clog << _F("Found kernel _stext extra offset %#" PRIx64,
			       extra_offset) << endl;

		  if (! c->session.need_symbols
		      && (kretprobe_trampoline_addr != (unsigned long) -1
			  || ! c->session.need_unwind))
		    done = 1;
		}
	      else if (kretprobe_trampoline_addr == (unsigned long) -1
		       && c->session.need_unwind
		       && ! strcmp(name, "kretprobe_trampoline_holder"))
		{
		  int ki;
                  kretprobe_trampoline_addr = sym_addr;
                  ki = dwfl_module_relocate_address(m,
						    &kretprobe_trampoline_addr);
                  dwfl_assert ("dwfl_module_relocate_address, kretprobe_trampoline_addr", ki >= 0);

		  if (! c->session.need_symbols
		      && extra_offset != 0)
		    done = 1;
		}
            }

	  // We are only interested in "real" symbols.
	  // We omit symbols that have suspicious addresses
	  // (before base, or after end).
          if (!done && c->session.need_symbols
	      && ! skippable_arch_symbol(ehdr->e_machine, name, &sym)
	      && (GELF_ST_TYPE (sym.st_info) == STT_FUNC
		  || (GELF_ST_TYPE (sym.st_info) == STT_NOTYPE
		      && (ehdr->e_type == ET_REL // PR10206 ppc fn-desc in .opd
			  || is_kernel)) // kernel entry functions are NOTYPE
		  || GELF_ST_TYPE (sym.st_info) == STT_OBJECT) // PR10000: .data
               && !(sym.st_shndx == SHN_UNDEF	// Value undefined,
		    || shndxp == (GElf_Word) -1	// in a non-allocated section,
		    || sym_addr >= end	// beyond current module,
		    || sym_addr < base))	// before first section.
            {
              const char *secname = NULL;
              unsigned secidx = 0; /* Most things have just one section. */
	      Dwarf_Addr func_desc_addr = 0; /* Function descriptor */

	      /* PPC64 uses function descriptors.
		 Note: for kernel ET_REL modules we rely on finding the
		 .function symbols instead of going through the opd function
		 descriptors. */
	      if (ehdr->e_machine == EM_PPC64
		  && GELF_ST_TYPE (sym.st_info) == STT_FUNC
		  && ehdr->e_type != ET_REL)
		{
		  Elf64_Addr opd_addr;
		  Dwarf_Addr opd_bias;
		  Elf_Scn *opd;

		  func_desc_addr = sym_addr;

		  opd = dwfl_module_address_section (m, &sym_addr, &opd_bias);
		  dwfl_assert ("dwfl_module_address_section opd", opd != NULL);

		  Elf_Data *opd_data = elf_rawdata (opd, NULL);
		  assert(opd_data != NULL);

		  Elf_Data opd_in, opd_out;
		  opd_out.d_buf = &opd_addr;
		  opd_in.d_buf = (char *) opd_data->d_buf + sym_addr;
		  opd_out.d_size = opd_in.d_size = sizeof (Elf64_Addr);
		  opd_out.d_type = opd_in.d_type = ELF_T_ADDR;
		  if (elf64_xlatetom (&opd_out, &opd_in,
				      ehdr->e_ident[EI_DATA]) == NULL)
		    throw runtime_error ("elf64_xlatetom failed");

		  // So the real address of the function is...
		  sym_addr = opd_addr + opd_bias;
		}

              if (n > 0) // only try to relocate if there exist relocation bases
                {
                  int ki = dwfl_module_relocate_address (m, &sym_addr);
                  dwfl_assert ("dwfl_module_relocate_address sym_addr", ki >= 0);
                  secname = dwfl_module_relocation_info (m, ki, NULL);

		  if (func_desc_addr != 0)
		    dwfl_module_relocate_address (m, &func_desc_addr);
		}

              if (n == 1 && is_kernel)
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
                  // NB: don't subtract session.sym_stext, which could be
                  // inconveniently NULL. Instead, sym_addr will get
                  // compensated later via extra_offset.
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
		  else
		    {
		      // Compute our section number
		      for (secidx = 0; secidx < c->seclist.size(); secidx++)
			if (c->seclist[secidx].first==secname)
			  break;

		      if (secidx == c->seclist.size()) // whoa! We messed up...
			{
			  string m = _F("%s has unknown section %s for sym %s",
					modname, secname, name);
			  throw runtime_error(m);
			}
		    }
                }
              else
                {
                  assert (n == 0);
                  // sym_addr is absolute, as it must be since there are
                  // no relocation bases
                  secname = ".absolute"; // sentinel
                }

              (c->addrmap[secidx])[sym_addr] = name;
	      /* If we have a function descriptor, register that address
	         under the same name */
	      if (func_desc_addr != 0)
		(c->addrmap[secidx])[func_desc_addr] = name;
            }
        }
    }

  if (is_kernel)
    {
      c->stext_offset = extra_offset;
      // Must be relative to actual kernel load address.
      if (kretprobe_trampoline_addr != (unsigned long) -1)
	c->stp_kretprobe_trampoline_addr = (kretprobe_trampoline_addr
					    - extra_offset);
    }

  return DWARF_CB_OK;
}

static int
dump_unwind_tables (Dwfl_Module *m,
		    unwindsym_dump_context *c,
		    const char *name, Dwarf_Addr base)
{
  // Add unwind data to be included if it exists for this module.
  get_unwind_data (m, &c->debug_frame, &c->eh_frame,
		   &c->debug_len, &c->eh_len,
		   &c->eh_addr, &c->eh_frame_hdr, &c->eh_frame_hdr_len,
		   &c->debug_frame_hdr, &c->debug_frame_hdr_len,
		   &c->debug_frame_off, &c->eh_frame_hdr_addr,
                   c->session);
  return DWARF_CB_OK;
}

static int
dump_unwindsym_cxt (Dwfl_Module *m,
		    unwindsym_dump_context *c,
		    const char *name, Dwarf_Addr base)
{
  string modname = name;
  unsigned stpmod_idx = c->stp_module_index;
  void *debug_frame = c->debug_frame;
  size_t debug_len = c->debug_len;
  void *debug_frame_hdr = c->debug_frame_hdr;
  size_t debug_frame_hdr_len = c->debug_frame_hdr_len;
  Dwarf_Addr debug_frame_off = c->debug_frame_off;
  void *eh_frame = c->eh_frame;
  void *eh_frame_hdr = c->eh_frame_hdr;
  size_t eh_len = c->eh_len;
  size_t eh_frame_hdr_len = c->eh_frame_hdr_len;
  Dwarf_Addr eh_addr = c->eh_addr;
  Dwarf_Addr eh_frame_hdr_addr = c->eh_frame_hdr_addr;

  if (debug_frame != NULL && debug_len > 0)
    {
      c->output << "#if defined(STP_USE_DWARF_UNWINDER) && defined(STP_NEED_UNWIND_DATA)\n";
      c->output << "static uint8_t _stp_module_" << stpmod_idx
		<< "_debug_frame[] = \n";
      c->output << "  {";
      if (debug_len > MAX_UNWIND_TABLE_SIZE)
        {
          c->session.print_warning ("skipping module " + modname + " debug_frame unwind table (too big: " +
                                      lex_cast(debug_len) + " > " + lex_cast(MAX_UNWIND_TABLE_SIZE) + ")");
        }
      else
        for (size_t i = 0; i < debug_len; i++)
          {
            int h = ((uint8_t *)debug_frame)[i];
            c->output << h << ","; // decimal is less wordy than hex
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
      if (eh_len > MAX_UNWIND_TABLE_SIZE)
        {
          c->session.print_warning ("skipping module " + modname + " eh_frame table (too big: " +
                                      lex_cast(eh_len) + " > " + lex_cast(MAX_UNWIND_TABLE_SIZE) + ")");
        }
      else
        for (size_t i = 0; i < eh_len; i++)
          {
            int h = ((uint8_t *)eh_frame)[i];
            c->output << h << ","; // decimal is less wordy than hex
            if ((i + 1) % 16 == 0)
              c->output << "\n" << "   ";
          }
      c->output << "};\n";
      c->output << "#endif /* STP_USE_DWARF_UNWINDER && STP_NEED_UNWIND_DATA */\n";
    }

  if (eh_frame_hdr != NULL && eh_frame_hdr_len > 0)
    {
      c->output << "#if defined(STP_USE_DWARF_UNWINDER) && defined(STP_NEED_UNWIND_DATA)\n";
      c->output << "static uint8_t _stp_module_" << stpmod_idx
		<< "_eh_frame_hdr[] = \n";
      c->output << "  {";
      if (eh_frame_hdr_len > MAX_UNWIND_TABLE_SIZE)
        {
          c->session.print_warning (_F("skipping module %s eh_frame_hdr table (too big: %s > %s)",
                                          modname.c_str(), lex_cast(eh_frame_hdr_len).c_str(),
                                          lex_cast(MAX_UNWIND_TABLE_SIZE).c_str()));
        }
      else
        for (size_t i = 0; i < eh_frame_hdr_len; i++)
          {
            int h = ((uint8_t *)eh_frame_hdr)[i];
            c->output << h << ","; // decimal is less wordy than hex
            if ((i + 1) % 16 == 0)
              c->output << "\n" << "   ";
          }
      c->output << "};\n";
      c->output << "#endif /* STP_USE_DWARF_UNWINDER && STP_NEED_UNWIND_DATA */\n";
    }
  
  if (c->session.need_unwind && debug_frame == NULL && eh_frame == NULL)
    {
      // There would be only a small benefit to warning.  A user
      // likely can't do anything about this; backtraces for the
      // affected module would just get all icky heuristicy.
      // So only report in verbose mode.
      if (c->session.verbose > 2)
	c->session.print_warning ("No unwind data for " + modname
				  + ", " + dwfl_errmsg (-1));
    }

  for (unsigned secidx = 0; secidx < c->seclist.size(); secidx++)
    {
      c->output << "static struct _stp_symbol "
                << "_stp_module_" << stpmod_idx<< "_symbols_" << secidx << "[] = {\n";

      string secname = c->seclist[secidx].first;
      Dwarf_Addr extra_offset;
      extra_offset = (secname == "_stext") ? c->stext_offset : 0;

      // Only include symbols if they will be used
      if (c->session.need_symbols)
	{
	  // We write out a *sorted* symbol table, so the runtime doesn't
	  // have to sort them later.
	  for (addrmap_t::iterator it = c->addrmap[secidx].begin();
	       it != c->addrmap[secidx].end(); it++)
	    {
	      // skip symbols that occur before our chosen base address
	      if (it->first < extra_offset)
		continue;

	      c->output << "  { 0x" << hex << it->first-extra_offset << dec
			<< ", " << lex_cast_qstring (it->second) << " },\n";
              // XXX: these literal strings all suffer ELF relocation bloat too.
              // See if the tapsets.cxx:dwarf_derived_probe_group::emit_module_decls
              // CALCIT hack could work here.
	    }
	}

      c->output << "};\n";

      /* For now output debug_frame index only in "magic" sections. */
      if (secname == ".dynamic" || secname == ".absolute"
	  || secname == ".text" || secname == "_stext")
	{
	  if (debug_frame_hdr != NULL && debug_frame_hdr_len > 0)
	    {
	      c->output << "#if defined(STP_USE_DWARF_UNWINDER)"
			<< " && defined(STP_NEED_UNWIND_DATA)\n";
	      c->output << "static uint8_t _stp_module_" << stpmod_idx
			<< "_debug_frame_hdr_" << secidx << "[] = \n";
	      c->output << "  {";
	      if (debug_frame_hdr_len > MAX_UNWIND_TABLE_SIZE)
		{
                  c->session.print_warning (_F("skipping module %s, section %s debug_frame_hdr"
                                                 " table (too big: %s > %s)", modname.c_str(),
                                                 secname.c_str(), lex_cast(debug_frame_hdr_len).c_str(),
                                                 lex_cast(MAX_UNWIND_TABLE_SIZE).c_str()));
		}
	      else
		for (size_t i = 0; i < debug_frame_hdr_len; i++)
		  {
		    int h = ((uint8_t *)debug_frame_hdr)[i];
                    c->output << h << ","; // decimal is less wordy than hex
		    if ((i + 1) % 16 == 0)
		      c->output << "\n" << "   ";
		  }
	      c->output << "};\n";
	      c->output << "#endif /* STP_USE_DWARF_UNWINDER"
			<< " && STP_NEED_UNWIND_DATA */\n";
	    }
	}
    }

  c->output << "static struct _stp_section _stp_module_" << stpmod_idx<< "_sections[] = {\n";
  // For the kernel, executables (ET_EXEC) or shared libraries (ET_DYN)
  // there is just one section that covers the whole address space of
  // the module. For kernel modules (ET_REL) there can be multiple
  // sections that get relocated separately.
  for (unsigned secidx = 0; secidx < c->seclist.size(); secidx++)
    {
      c->output << "{\n"
                << ".name = " << lex_cast_qstring(c->seclist[secidx].first) << ",\n"
                << ".size = 0x" << hex << c->seclist[secidx].second << dec << ",\n"
                << ".symbols = _stp_module_" << stpmod_idx << "_symbols_" << secidx << ",\n"
                << ".num_symbols = " << c->addrmap[secidx].size() << ",\n";

      /* For now output debug_frame index only in "magic" sections. */
      string secname = c->seclist[secidx].first;
      if (debug_frame_hdr && (secname == ".dynamic" || secname == ".absolute"
			      || secname == ".text" || secname == "_stext"))
	{
	  c->output << "#if defined(STP_USE_DWARF_UNWINDER)"
		    << " && defined(STP_NEED_UNWIND_DATA)\n";

          c->output << ".debug_hdr = "
		    << "_stp_module_" << stpmod_idx
		    << "_debug_frame_hdr_" << secidx << ",\n";
          c->output << ".debug_hdr_len = " << debug_frame_hdr_len << ", \n";

	  Dwarf_Addr dwbias = 0;
	  dwfl_module_getdwarf (m, &dwbias);
	  c->output << ".sec_load_offset = 0x"
		    << hex << debug_frame_off - dwbias << dec << "\n";

	  c->output << "#else\n";
	  c->output << ".debug_hdr = NULL,\n";
	  c->output << ".debug_hdr_len = 0,\n";
	  c->output << ".sec_load_offset = 0\n";
	  c->output << "#endif /* STP_USE_DWARF_UNWINDER"
		    << " && STP_NEED_UNWIND_DATA */\n";

	}
      else
	{
	  c->output << ".debug_hdr = NULL,\n";
	  c->output << ".debug_hdr_len = 0,\n";
	  c->output << ".sec_load_offset = 0\n";
	}

	c->output << "},\n";
    }
  c->output << "};\n";

  // Get the canonical path of the main file for comparison at runtime.
  // When given directly by the user through -d or in case of the kernel
  // name and path might differ. path should be used for matching.
  const char *mainfile;
  dwfl_module_info (m, NULL, NULL, NULL, NULL, NULL, &mainfile, NULL);

  // For user space modules store canonical path and base name.
  // For kernel modules just the name itself.
  const char *mainpath = canonicalize_file_name(mainfile);
  const char *mainname = strrchr(mainpath, '/');
  if (modname[0] == '/')
    mainname++;
  else
    mainname = modname.c_str();

  c->output << "static struct _stp_module _stp_module_" << stpmod_idx << " = {\n";
  c->output << ".name = " << lex_cast_qstring (mainname) << ", \n";
  c->output << ".path = " << lex_cast_qstring (path_remove_sysroot(c->session,mainpath)) << ",\n";
  c->output << ".eh_frame_addr = 0x" << hex << eh_addr << dec << ", \n";
  c->output << ".unwind_hdr_addr = 0x" << hex << eh_frame_hdr_addr
	    << dec << ", \n";

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
      if (eh_frame_hdr)
        {
          c->output << ".unwind_hdr = "
                    << "_stp_module_" << stpmod_idx << "_eh_frame_hdr, \n";
          c->output << ".unwind_hdr_len = " << eh_frame_hdr_len << ", \n";
        }
      else
        {
          c->output << ".unwind_hdr = NULL,\n";
          c->output << ".unwind_hdr_len = 0,\n";
        }
      c->output << "#else\n";
    }

  c->output << ".eh_frame = NULL,\n";
  c->output << ".eh_frame_len = 0,\n";
  c->output << ".unwind_hdr = NULL,\n";
  c->output << ".unwind_hdr_len = 0,\n";
  if (eh_frame != NULL)
    c->output << "#endif /* STP_USE_DWARF_UNWINDER && STP_NEED_UNWIND_DATA*/\n";
  c->output << ".sections = _stp_module_" << stpmod_idx << "_sections" << ",\n";
  c->output << ".num_sections = sizeof(_stp_module_" << stpmod_idx << "_sections)/"
            << "sizeof(struct _stp_section),\n";

  /* Don't save build-id if it is located before _stext.
   * This probably means that build-id will not be loaded at all and
   * happens for example with ARM kernel.  Allow user space modules since the
   * check fails for a shared object.
   *
   * See also:
   *    http://sourceware.org/ml/systemtap/2009-q4/msg00574.html
   */
  if (c->build_id_len > 0
      && (modname != "kernel" || (c->build_id_vaddr > base + c->stext_offset))) {
    c->output << ".build_id_bits = \"" ;
    for (int j=0; j<c->build_id_len;j++)
      c->output << "\\x" << hex
                << (unsigned short) *(c->build_id_bits+j) << dec;

    c->output << "\",\n";
    c->output << ".build_id_len = " << c->build_id_len << ",\n";

    /* XXX: kernel data boot-time relocation works differently from text.
       This hack assumes that offset between _stext and build id
       stays constant after relocation, but that's not necessarily
       correct either.  We may instead need a relocation basis different
       from _stext, such as __start_notes.  */
    if (modname == "kernel")
      c->output << ".build_id_offset = 0x" << hex << c->build_id_vaddr - (base + c->stext_offset)
                << dec << ",\n";
    // ET_DYN: task finder gives the load address. ET_EXEC: this is absolute address
    else
      c->output << ".build_id_offset = 0x" << hex
                << c->build_id_vaddr /* - base */
                << dec << ",\n";
  } else
    c->output << ".build_id_len = 0,\n";

  //initialize the note section representing unloaded
  c->output << ".notes_sect = 0,\n";

  c->output << "};\n\n";

  c->undone_unwindsym_modules.erase (modname);

  // release various malloc'd tables
  // if (eh_frame_hdr) free (eh_frame_hdr); -- nope, this one comes from the elf image in memory
  if (debug_frame_hdr) free (debug_frame_hdr);

  return DWARF_CB_OK;
}

static int
dump_unwindsyms (Dwfl_Module *m,
                 void **userdata __attribute__ ((unused)),
                 const char *name,
                 Dwarf_Addr base,
                 void *arg)
{
  if (pending_interrupts)
    return DWARF_CB_ABORT;

  unwindsym_dump_context *c = (unwindsym_dump_context*) arg;
  assert (c);

  // skip modules/files we're not actually interested in
  string modname = name;
  if (c->session.unwindsym_modules.find(modname)
      == c->session.unwindsym_modules.end())
    return DWARF_CB_OK;

  if (c->session.verbose > 1)
    clog << "dump_unwindsyms " << name
         << " index=" << c->stp_module_index
         << " base=0x" << hex << base << dec << endl;

  // We want to extract several bits of information:
  //
  // - parts of the program-header that map the file's physical offsets to the text section
  // - section table: just a list of section (relocation) base addresses
  // - symbol table of the text-like sections, with all addresses relativized to each base
  // - the contents of .debug_frame and/or .eh_frame section, for unwinding purposes

  int res = DWARF_CB_OK;

  c->build_id_len = 0;
  c->build_id_vaddr = 0;
  c->build_id_bits = NULL;
  res = dump_build_id (m, c, name, base);

  c->seclist.clear();
  if (res == DWARF_CB_OK)
    res = dump_section_list(m, c, name, base);

  // We always need to check the symbols of the kernel if we use it,
  // for the extra_offset (also used for build_ids) and possibly
  // stp_kretprobe_trampoline_addr for the dwarf unwinder.
  c->addrmap.clear();
  if (res == DWARF_CB_OK
      && (c->session.need_symbols || ! strcmp(name, "kernel")))
    res = dump_symbol_tables (m, c, name, base);

  c->debug_frame = NULL;
  c->debug_len = 0;
  c->debug_frame_hdr = NULL;
  c->debug_frame_hdr_len = 0;
  c->debug_frame_off = 0;
  c->eh_frame = NULL;
  c->eh_frame_hdr = NULL;
  c->eh_len = 0;
  c->eh_frame_hdr_len = 0;
  c->eh_addr = 0;
  c->eh_frame_hdr_addr = 0;
  if (res == DWARF_CB_OK && c->session.need_unwind)
    res = dump_unwind_tables (m, c, name, base);

  /* And finally dump everything collected in the output. */
  if (res == DWARF_CB_OK)
    res = dump_unwindsym_cxt (m, c, name, base);

  if (res == DWARF_CB_OK)
    c->stp_module_index++;

  return res;
}


// Emit symbol table & unwind data, plus any calls needed to register
// them with the runtime.
void emit_symbol_data_done (unwindsym_dump_context*, systemtap_session&);


void
add_unwindsym_iol_callback (void *q, const char *data)
{
  std::set<std::string> *added = (std::set<std::string>*)q;
  added->insert (string (data));
}


static int
query_module (Dwfl_Module *mod,
              void **,
              const char *,
              Dwarf_Addr,
              void *arg)
{
  ((struct dwflpp*)arg)->focus_on_module(mod, NULL);
  return DWARF_CB_OK;
}


void
add_unwindsym_ldd (systemtap_session &s)
{
  std::set<std::string> added;

  for (std::set<std::string>::iterator it = s.unwindsym_modules.begin();
       it != s.unwindsym_modules.end();
       it++)
    {
      string modname = *it;
      assert (modname.length() != 0);
      if (! is_user_module (modname)) continue;

      struct dwflpp *mod_dwflpp = new dwflpp(s, modname, false);
      mod_dwflpp->iterate_over_modules (&query_module, mod_dwflpp);
      if (mod_dwflpp->module) // existing binary
        {
          assert (mod_dwflpp->module_name != "");
          mod_dwflpp->iterate_over_libraries (&add_unwindsym_iol_callback, &added);
        }
      delete mod_dwflpp;
    }

  s.unwindsym_modules.insert (added.begin(), added.end());
}

static set<string> vdso_paths;

static int find_vdso(const char *path, const struct stat *, int type)
{
  if (type == FTW_F)
    {
      const char *name = strrchr(path, '/');
      if (name)
	{
	  name++;
	  const char *ext = strrchr(name, '.');
	  if (ext
	      && strncmp("vdso", name, 4) == 0
	      && strcmp(".so", ext) == 0)
	    vdso_paths.insert(path);
	}
    }
  return 0;
}

void
add_unwindsym_vdso (systemtap_session &s)
{
  // This is to disambiguate between -r REVISION vs -r BUILDDIR.
  // See also dwflsetup.c (setup_dwfl_kernel). In case of only
  // having the BUILDDIR we need to do a deep search (the specific
  // arch name dir in the kernel build tree is unknown).
  string vdso_dir;
  if (s.kernel_build_tree == string(s.sysroot + "/lib/modules/"
				    + s.kernel_release
				    + "/build"))
    vdso_dir = s.sysroot + "/lib/modules/" + s.kernel_release + "/vdso";
  else
    vdso_dir = s.kernel_build_tree + "/arch/";

  if (s.verbose > 1)
    clog << _("Searching for vdso candidates: ") << vdso_dir << endl;

  ftw(vdso_dir.c_str(), find_vdso, 1);

  for (set<string>::iterator it = vdso_paths.begin();
       it != vdso_paths.end();
       it++)
    {
      s.unwindsym_modules.insert(*it);
      if (s.verbose > 1)
	clog << _("vdso candidate: ") << *it << endl;
    }
}

static void
prepare_symbol_data (systemtap_session& s)
{
  // step 0: run ldd on any user modules if requested
  if (s.unwindsym_ldd)
    add_unwindsym_ldd (s);
  // step 0.5: add vdso(s) when vma tracker was requested
  if (vma_tracker_enabled (s))
    add_unwindsym_vdso (s);
  // NB: do this before the ctx.unwindsym_modules copy is taken
}

void
emit_symbol_data (systemtap_session& s)
{
  string symfile = "stap-symbols.h";

  s.op->newline() << "#include " << lex_cast_qstring (symfile);

  ofstream kallsyms_out ((s.tmpdir + "/" + symfile).c_str());

  vector<pair<string,unsigned> > seclist;
  map<unsigned, addrmap_t> addrmap;
  unwindsym_dump_context ctx = { s, kallsyms_out,
				 0, /* module index */
				 0, NULL, 0, /* build_id len, bits, vaddr */
				 ~0UL, /* stp_kretprobe_trampoline_addr */
				 0, /* stext_offset */
				 seclist, addrmap,
				 NULL, /* debug_frame */
				 0, /* debug_len */
				 NULL, /* debug_frame_hdr */
				 0, /* debug_frame_hdr_len */
				 0, /* debug_frame_off */
				 NULL, /* eh_frame */
				 NULL, /* eh_frame_hdr */
				 0, /* eh_len */
				 0, /* eh_frame_hdr_len */
				 0, /* eh_addr */
				 0, /* eh_frame_hdr_addr */
				 s.unwindsym_modules };

  // Micro optimization, mainly to speed up tiny regression tests
  // using just begin probe.
  if (s.unwindsym_modules.size () == 0)
    {
      emit_symbol_data_done(&ctx, s);
      return;
    }

  // ---- step 1: process any kernel modules listed
  set<string> offline_search_modules;
  unsigned count;
  for (set<string>::iterator it = s.unwindsym_modules.begin();
       it != s.unwindsym_modules.end();
       it++)
    {
      string foo = *it;
      if (! is_user_module (foo)) /* Omit user-space, since we're only
				     using this for kernel space
				     offline searches. */
        offline_search_modules.insert (foo);
    }
  DwflPtr dwfl_ptr = setup_dwfl_kernel (offline_search_modules, &count, s);
  Dwfl *dwfl = dwfl_ptr.get()->dwfl;
  /* NB: It's not an error to find a few fewer modules than requested.
     There might be third-party modules loaded (e.g. uprobes). */
  /* dwfl_assert("all kernel modules found",
     count >= offline_search_modules.size()); */

  ptrdiff_t off = 0;
  do
    {
      assert_no_interrupts();
      if (ctx.undone_unwindsym_modules.empty()) break;
      off = dwfl_getmodules (dwfl, &dump_unwindsyms, (void *) &ctx, off);
    }
  while (off > 0);
  dwfl_assert("dwfl_getmodules", off == 0);
  dwfl_ptr.reset();

  // ---- step 2: process any user modules (files) listed
  for (std::set<std::string>::iterator it = s.unwindsym_modules.begin();
       it != s.unwindsym_modules.end();
       it++)
    {
      string modname = *it;
      assert (modname.length() != 0);
      if (! is_user_module (modname)) continue;
      DwflPtr dwfl_ptr = setup_dwfl_user (modname);
      Dwfl *dwfl = dwfl_ptr.get()->dwfl;
      if (dwfl != NULL) // tolerate missing data; will warn below
        {
          ptrdiff_t off = 0;
          do
            {
              assert_no_interrupts();
              if (ctx.undone_unwindsym_modules.empty()) break;
              off = dwfl_getmodules (dwfl, &dump_unwindsyms, (void *) &ctx, off);
            }
          while (off > 0);
          dwfl_assert("dwfl_getmodules", off == 0);
        }
      dwfl_ptr.reset();
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
  if (! s.suppress_warnings)
    for (set<string>::iterator it = ctx->undone_unwindsym_modules.begin();
	 it != ctx->undone_unwindsym_modules.end();
	 it ++)
      s.print_warning (_("missing unwind/symbol data for module '")
		       + (*it) + "'");
}




struct recursion_info: public traversing_visitor
{
  recursion_info (systemtap_session& s): sess(s), nesting_max(0), recursive(false) {}
  systemtap_session& sess;
  unsigned nesting_max;
  bool recursive;
  std::vector <functiondecl *> current_nesting;

  void visit_functioncall (functioncall* e) {
    traversing_visitor::visit_functioncall (e); // for arguments

    // check for nesting level
    unsigned nesting_depth = current_nesting.size() + 1;
    if (nesting_max < nesting_depth)
      {
        if (sess.verbose > 3)
          clog << _F("identified max-nested function: %s (%d)",
                     e->referent->name.c_str(), nesting_depth) << endl;
        nesting_max = nesting_depth;
      }

    // check for (direct or mutual) recursion
    for (unsigned j=0; j<current_nesting.size(); j++)
      if (current_nesting[j] == e->referent)
        {
          recursive = true;
          if (sess.verbose > 3)
            clog << _F("identified recursive function: %s", e->referent->name.c_str()) << endl;
          return;
        }

    // non-recursive traversal
    current_nesting.push_back (e->referent);
    e->referent->body->visit (this);
    current_nesting.pop_back ();
  }
};


void translate_runtime(systemtap_session& s)
{
  s.op->newline() << "#define STAP_MSG_RUNTIME_H_01 "
                  << lex_cast_qstring(_("myproc-unprivileged tapset function called "
                                        "without is_myproc checking for pid %d (euid %d)"));

  s.op->newline() << "#define STAP_MSG_LOC2C_01 "
                  << lex_cast_qstring(_("kernel read fault at 0x%p (%s)"));
  s.op->newline() << "#define STAP_MSG_LOC2C_02 "
                  << lex_cast_qstring(_("kernel write fault at 0x%p (%s)"));
  s.op->newline() << "#define STAP_MSG_LOC2C_03 "
                  << lex_cast_qstring(_("divide by zero in DWARF operand (%s)"));
}


int
prepare_translate_pass (systemtap_session& s)
{
  int rc = 0;
  try
    {
      prepare_symbol_data (s);
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
      rc = 1;
    }

  return rc;
}


int
translate_pass (systemtap_session& s)
{
  int rc = 0;

  s.op = new translator_output (s.translated_source);
  // additional outputs might be found in s.auxiliary_outputs
  c_unparser cup (& s);
  s.up = & cup;
  translate_runtime(s);

  try
    {
      int64_t major=0, minor=0;
      try
	{
	  vector<string> versions;
	  tokenize (s.compatible, versions, ".");
	  if (versions.size() >= 1)
	    major = lex_cast<int64_t> (versions[0]);
	  if (versions.size() >= 2)
	    minor = lex_cast<int64_t> (versions[1]);
	  if (versions.size() >= 3 && s.verbose > 1)
	    clog << _F("ignoring extra parts of compat version: %s", s.compatible.c_str()) << endl;
	}
      catch (const runtime_error)
	{
	  throw semantic_error(_F("parse error in compatibility version: %s", s.compatible.c_str()));
	}
      if (major < 0 || major > 255 || minor < 0 || minor > 255)
	throw semantic_error(_F("compatibility version out of range: %s", s.compatible.c_str()));
      s.op->newline() << "#define STAP_VERSION(a, b) ( ((a) << 8) + (b) )";
      s.op->newline() << "#ifndef STAP_COMPAT_VERSION";
      s.op->newline() << "#define STAP_COMPAT_VERSION STAP_VERSION("
		      << major << ", " << minor << ")";
      s.op->newline() << "#endif";

      recursion_info ri (s);

      // NB: we start our traversal from the s.functions[] rather than the probes.
      // We assume that each function is called at least once, or else it would have
      // been elided already.
      for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
	{
          functiondecl *fd = it->second;
          fd->body->visit (& ri);
	}

      if (s.verbose > 1)
        clog << _F("function recursion-analysis: max-nesting %d %s", ri.nesting_max,
                  (ri.recursive ? _(" recursive") : _(" non-recursive"))) << endl;
      unsigned nesting = ri.nesting_max + 1; /* to account for initial probe->function call */
      if (ri.recursive) nesting += 10;

      // This is at the very top of the file.
      // All "static" defines (not dependend on session state).
      s.op->newline() << "#include \"runtime_defines.h\"";

      // Generated macros describing the privilege level required to load/run this module.
      s.op->newline() << "#define STP_PR_STAPUSR 0x" << hex << pr_stapusr << dec;
      s.op->newline() << "#define STP_PR_STAPSYS 0x" << hex << pr_stapsys << dec;
      s.op->newline() << "#define STP_PR_STAPDEV 0x" << hex << pr_stapdev << dec;
      s.op->newline() << "#define STP_PRIVILEGE 0x" << hex << s.privilege << dec;

      // Generate a section containing a mask of the privilege levels required to load/run this
      // module.
      s.op->newline() << "int stp_required_privilege "
		      << "__attribute__ ((section (\"" << STAP_PRIVILEGE_SECTION <<"\")))"
		      << " = STP_PRIVILEGE;";

      s.op->newline() << "#ifndef MAXNESTING";
      s.op->newline() << "#define MAXNESTING " << nesting;
      s.op->newline() << "#endif";

      s.op->newline() << "#define STP_SKIP_BADVARS " << (s.skip_badvars ? 1 : 0);

      if (s.bulk_mode)
	  s.op->newline() << "#define STP_BULKMODE";

      if (s.timing)
	s.op->newline() << "#define STP_TIMING";

      if (s.need_unwind)
	s.op->newline() << "#define STP_NEED_UNWIND_DATA 1";

      s.op->newline() << "#include \"runtime.h\"";

      // Emit embeds ahead of time, in case they affect context layout
      for (unsigned i=0; i<s.embeds.size(); i++)
        {
          s.op->newline() << s.embeds[i]->code << "\n";
        }

      s.up->emit_common_header (); // context etc.

      s.op->newline() << "#include \"runtime_context.h\"";
      if (s.need_unwind)
	s.op->newline() << "#include \"stack.c\"";

      s.op->newline() << "#include \"probe_lock.h\" ";

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
            assert_no_interrupts();
            s.up->emit_global_init (s.globals[i]);
          }
        s.op->newline(-1) << "};";
        s.op->assert_0_indent();
      }

      for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
	{
          assert_no_interrupts();
	  s.op->newline();
	  s.up->emit_functionsig (it->second);
	}
      s.op->assert_0_indent();

      for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
	{
          assert_no_interrupts();
	  s.op->newline();
	  s.up->emit_function (it->second);
	}
      s.op->assert_0_indent();

      // Run a varuse_collecting_visitor over probes that need global
      // variable locks.  We'll use this information later in
      // emit_locks()/emit_unlocks().
      for (unsigned i=0; i<s.probes.size(); i++)
	{
        assert_no_interrupts();
        if (s.probes[i]->needs_global_locks())
	    s.probes[i]->body->visit (&cup.vcv_needs_global_locks);
	}
      s.op->assert_0_indent();

      for (unsigned i=0; i<s.probes.size(); i++)
        {
          assert_no_interrupts();
          s.up->emit_probe (s.probes[i]);
        }
      s.op->assert_0_indent();

      // Let's find some stats for the embedded pp strings.  Maybe they
      // are small and uniform enough to justify putting char[MAX]'s into
      // the array instead of relocated char*'s.
      size_t pp_max = 0, pn_max = 0, location_max = 0, derivation_max = 0;
      size_t pp_tot = 0, pn_tot = 0, location_tot = 0, derivation_tot = 0;
      for (unsigned i=0; i<s.probes.size(); i++)
        {
          derived_probe* p = s.probes[i];
#define DOIT(var,expr) do {                             \
        size_t var##_size = (expr) + 1;                 \
        var##_max = max (var##_max, var##_size);        \
        var##_tot += var##_size; } while (0)
          DOIT(pp, lex_cast_qstring(*p->sole_location()).size());
          DOIT(pn, lex_cast_qstring(*p->script_location()).size());
          DOIT(location, lex_cast_qstring(p->tok->location).size());
          DOIT(derivation, lex_cast_qstring(p->derived_locations()).size());
#undef DOIT
        }

      // Decide whether it's worthwhile to use char[] or char* by comparing
      // the amount of average waste (max - avg) to the relocation data size
      // (3 native long words).
#define CALCIT(var)                                                             \
      if (s.verbose > 2)                                                        \
        clog << "adapt " << #var << ":" << var##_max << "max - " << var##_tot << "/" << s.probes.size() << "tot =>"; \
      if ((var##_max-(var##_tot/s.probes.size())) < (3 * sizeof(void*)))        \
        {                                                                       \
          s.op->newline() << "const char " << #var << "[" << var##_max << "];"; \
          if (s.verbose > 2)                                                    \
            clog << "[]" << endl;                                               \
        }                                                                       \
      else                                                                      \
        {                                                                       \
          s.op->newline() << "const char * const " << #var << ";";              \
          if (s.verbose > 2)                                                    \
            clog << "*" << endl;                                                \
        }

      s.op->newline() << "static struct stap_probe {";
      s.op->newline(1) << "void (* const ph) (struct context*);";
      s.op->newline() << "#ifdef STP_ALIBI";
      s.op->newline() << "atomic_t alibi;";
      s.op->newline() << "#define STAP_PROBE_INIT_ALIBI() "
                      << ".alibi=ATOMIC_INIT(0),";
      s.op->newline() << "#else";
      s.op->newline() << "#define STAP_PROBE_INIT_ALIBI()";
      s.op->newline() << "#endif";
      s.op->newline() << "#ifdef STP_TIMING";
      s.op->newline() << "Stat timing;";
      s.op->newline() << "#endif";
      s.op->newline() << "#if defined(STP_TIMING) || defined(STP_ALIBI)";
      CALCIT(location);
      CALCIT(derivation);
      s.op->newline() << "#define STAP_PROBE_INIT_TIMING(L, D) "
                      << ".location=(L), .derivation=(D),";
      s.op->newline() << "#else";
      s.op->newline() << "#define STAP_PROBE_INIT_TIMING(L, D)";
      s.op->newline() << "#endif";
      CALCIT(pp);
      s.op->newline() << "#ifdef STP_NEED_PROBE_NAME";
      CALCIT(pn);
      s.op->newline() << "#define STAP_PROBE_INIT_NAME(PN) .pn=(PN),";
      s.op->newline() << "#else";
      s.op->newline() << "#define STAP_PROBE_INIT_NAME(PN)";
      s.op->newline() << "#endif";
      s.op->newline() << "#define STAP_PROBE_INIT(PH, PP, PN, L, D) "
                      << "{ .ph=(PH), .pp=(PP), "
                      << "STAP_PROBE_INIT_NAME(PN) "
                      << "STAP_PROBE_INIT_ALIBI() "
                      << "STAP_PROBE_INIT_TIMING(L, D) "
                      << "}";
      s.op->newline(-1) << "} stap_probes[] = {";
      s.op->indent(1);
      for (unsigned i=0; i<s.probes.size(); ++i)
        {
          derived_probe* p = s.probes[i];
          p->session_index = i;
          s.op->newline() << "STAP_PROBE_INIT(&" << p->name << ", "
                          << lex_cast_qstring (*p->sole_location()) << ", "
                          << lex_cast_qstring (*p->script_location()) << ", "
                          << lex_cast_qstring (p->tok->location) << ", "
                          << lex_cast_qstring (p->derived_locations()) << "),";
        }
      s.op->newline(-1) << "};";
#undef CALCIT

      s.op->newline();
      s.up->emit_module_init ();
      s.op->assert_0_indent();
      s.op->newline();
      s.up->emit_module_refresh ();
      s.op->assert_0_indent();
      s.op->newline();
      s.up->emit_module_exit ();
      s.op->assert_0_indent();
      s.op->newline();

      emit_symbol_data (s);

      s.op->newline() << "MODULE_DESCRIPTION(\"systemtap-generated probe\");";
      s.op->newline() << "MODULE_LICENSE(\"GPL\");";

      for (unsigned i = 0; i < s.modinfos.size(); i++)
        {
          const string& mi = s.modinfos[i];
          size_t loc = mi.find('=');
          string tag = mi.substr (0, loc);
          string value = mi.substr (loc+1);
          s.op->newline() << "MODULE_INFO(" << tag << "," << lex_cast_qstring(value) << ");";
        }

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
