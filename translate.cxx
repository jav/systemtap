// translation pass
// Copyright (C) 2005 Red Hat Inc.
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

struct var;
struct tmpvar;
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
  void emit_functionsig (functiondecl* v);
  void emit_module_init ();
  void emit_module_exit ();
  void emit_function (functiondecl* v);
  void emit_probe (derived_probe* v, unsigned i);

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
  var getvar(vardecl* v, token const* tok = NULL);
  itervar getiter(foreach_loop* f);
  mapvar getmap(vardecl* v, token const* tok = NULL);

  void load_map_indices(arrayindex* e,
			vector<tmpvar> & idx);

  void collect_map_index_types(vector<vardecl* > const & vars,
			       set< exp_type > & value_types,
			       set< vector<exp_type> > & index_types);

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

  void visit_for_loop (for_loop* s);
  void visit_foreach_loop (foreach_loop* s);
  // void visit_return_statement (return_statement* s);
  // void visit_delete_statement (delete_statement* s);
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
};

struct c_unparser_assignment: 
  public throwing_visitor
{
  c_unparser* parent;
  string op;
  expression* rvalue;
  bool pre;
  c_unparser_assignment (c_unparser* p, const string& o, expression* e):
    throwing_visitor ("invalid lvalue type"),
    parent (p), op (o), rvalue (e), pre (false) {}
  c_unparser_assignment (c_unparser* p, const string& o, bool pp):
    throwing_visitor ("invalid lvalue type"),
    parent (p), op (o), rvalue (0), pre (pp) {}

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

class var
{
  bool local;
  exp_type ty;
  string name;

public:

  var(bool local, exp_type ty, string const & name)
    : local(local), ty(ty), name(name)
  {}

  bool is_local() const
  {
    return local;
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

  string init() const
  {
    switch (type())
      {
      case pe_string:
	return qname() + "[0] = '\\0';";
      case pe_long:
	return qname() + " = 0;";
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
    c.o->line() << "({";
    c.o->indent(1);
  }
  ~stmt_expr()
  {
    c.o->newline(-1) << "})";
  }
};

struct varlock
{
  c_unparser & c;
  var const & v;
  bool w;

  varlock(c_unparser & c, var const & v, bool w): c(c), v(v), w(w)
  {
    if (v.is_local()) return;
    c.o->newline() << (w ? "write_lock" : "read_lock")
                   << " (& " << v << "_lock);";
  }

  ~varlock()
  {
    if (v.is_local()) return;
    c.o->newline() << (w ? "write_unlock" : "read_unlock")
                   << " (& " << v << "_lock);";
  }
};

struct varlock_r: public varlock {
  varlock_r (c_unparser& c, var const& v): varlock (c, v, false) {}
};

struct varlock_w: public varlock {
  varlock_w (c_unparser& c, var const& v): varlock (c, v, true) {}
};


struct tmpvar
  : public var
{
  tmpvar(exp_type ty, 
	 unsigned & counter) 
    : var(true, ty, ("__tmp" + stringify(counter++)))
  {}
};

struct mapvar
  : public var
{
  vector<exp_type> index_types;
  mapvar (bool local, exp_type ty, 
	  string const & name, 
	  vector<exp_type> const & index_types)
    : var (local, ty, name),
      index_types (index_types)
  {}
  
  static string shortname(exp_type e);
  static string key_typename(exp_type e);
  static string value_typename(exp_type e);

  string del () const
  {
    return "_stp_map_key_del (" + qname() + ")";
  }

  string exists () const
  {
    return "_stp_map_entry_exists (" + qname() + ")";
  }

  string seek (vector<tmpvar> const & indices) const
  {
    string result = "_stp_map_key" + mangled_indices() + " (";
    result += qname();
    for (unsigned i = 0; i < indices.size(); ++i)
      {
	if (indices[i].type() != index_types[i])
	  throw semantic_error("index type mismatch");
	result += ", ";
	result += indices[i].qname();
      }
    result += ")";
    return result;
  }

  string get () const
  {
    // see also itervar::get_key
    if (type() == pe_string)
        // impedance matching: NULL -> empty strings
      return "({ char *v = "
        "_stp_map_get_" + shortname(type()) + " (" + qname() + "); "
        "if (!v) v = \"\"; v; })";
    else // long?
      return "_stp_map_get_" + shortname(type()) + " (" + qname() + ")";
  }

  string set (tmpvar const & tmp) const
  {
    // impedance matching: empty strings -> NULL
    if (type() == pe_string)
      return ("_stp_map_set_" + shortname(type()) + " (" + qname()
              + ", (" + tmp.qname() + "[0] ? " + tmp.qname() + " : NULL))");
    else
      return ("_stp_map_set_" + shortname(type()) + " (" + qname()
              + ", " + tmp.qname() + ")");
  }

  string mangled_indices() const
  {
    string result;
    for (unsigned i = 0; i < index_types.size(); ++i)
      {
	result += "_";
	result += shortname(index_types[i]);
      }
    return result;
  }
		
  string init () const
  {
    return (qname() + " = _stp_map_new" + mangled_indices() 
	    + " (MAXMAPENTRIES, " + value_typename (type()) + ");");
  }

  string fini () const
  {
    return "_stp_map_del (" + qname() + ");";
  }
  
};

class itervar
{
  exp_type referent_ty;
  string name;

public:

  itervar (foreach_loop* e, unsigned & counter)
    : referent_ty(e->base_referent->type), 
      name("__tmp" + stringify(counter++))
  {
    if (referent_ty != pe_long && referent_ty != pe_string)
      throw semantic_error("iterating over illegal reference type", e->tok);
  }
  
  string declare () const
  {
    return "struct map_node *" + name + ";";
  }

  string start (mapvar const & mv) const
  {
    if (mv.type() != referent_ty)
      throw semantic_error("inconsistent iterator type in itervar::start()");

    return "_stp_map_start (" + mv.qname() + ")";
  }

  string next (mapvar const & mv) const
  {
    if (mv.type() != referent_ty)
      throw semantic_error("inconsistent iterator type in itervar::next()");

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
  o2 (0), o (f), tablevel (0)
{
}


translator_output::translator_output (const string& filename):
  o2 (new ofstream (filename.c_str ())), o (*o2), tablevel (0)
{
}


translator_output::~translator_output ()
{
  delete o2;
}


ostream&
translator_output::newline (int indent)
{
  assert (indent > 0 || tablevel >= (unsigned)-indent);
  tablevel += indent;
  o << endl;
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

  o->newline() << "#include \"loc2c-runtime.h\" ";
  o->newline() << "#define MAXNESTING 30";
  o->newline() << "#define MAXCONCURRENCY NR_CPUS";
  o->newline() << "#define MAXSTRINGLEN 128";
  o->newline() << "#define MAXACTION 1000";
  o->newline() << "#define MAXMAPENTRIES 2048";
  o->newline();
  o->newline() << "typedef char string_t[MAXSTRINGLEN];";
  o->newline() << "typedef struct { } stats_t;";
  o->newline();
  o->newline() << "#define STAP_SESSION_STARTING 0";
  o->newline() << "#define STAP_SESSION_RUNNING 1";
  o->newline() << "#define STAP_SESSION_ERROR 2";
  o->newline() << "#define STAP_SESSION_STOPPING 3";
  o->newline() << "#define STAP_SESSION_STOPPED 4";
  o->newline() << "atomic_t session_state = ATOMIC_INIT (STAP_SESSION_STARTING);";
  o->newline();
  o->newline() << "struct context {";
  o->newline(1) << "unsigned busy;"; // XXX: should be atomic_t ?
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
          o->newline() << c_typename (v->type) << " " 
		       << c_varname (v->name) << ";";
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
          o->newline() << c_typename (v->type) << " " 
  		       << c_varname (v->name) << ";";
        }
      for (unsigned j=0; j<fd->formal_args.size(); j++)
        {
          vardecl* v = fd->formal_args[j];
          o->newline() << c_typename (v->type) << " " 
		       << c_varname (v->name) << ";";
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
  o->newline(-1) << "} contexts [MAXCONCURRENCY];" << endl;

  emit_map_type_instantiations ();
}


void
c_unparser::emit_global (vardecl *v)
{
  if (v->arity == 0)
    o->newline() << "static "
		 << c_typename (v->type)
		 << " "
		 << "global_" << c_varname (v->name)
		 << ";";
  else
    o->newline() << "static MAP global_" 
		 << c_varname(v->name) << ";";
  o->newline() << "static DEFINE_RWLOCK("
               << "global_" << c_varname (v->name) << "_lock"
               << ");";
}


void
c_unparser::emit_functionsig (functiondecl* v)
{
  o->newline() << "static void function_" << v->name
	       << " (struct context *c);";
}


void
c_unparser::emit_module_init ()
{
  o->newline() << "static int systemtap_module_init (void);";
  o->newline() << "int systemtap_module_init () {";
  o->newline(1) << "int rc = 0;";

  o->newline() << "atomic_set (&session_state, STAP_SESSION_STARTING);";
  // This signals any other probes that may be invoked in the next little
  // while to abort right away.  Currently running probes are allowed to
  // terminate.  These may set STAP_SESSION_ERROR!

  for (unsigned i=0; i<session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];      
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).init();
      else
	o->newline() << getvar (v).init();
    }

  for (unsigned i=0; i<session->probes.size(); i++)
    {
      o->newline() << "/* register " << i << " */";
      session->probes[i]->emit_registrations (o, i);

      o->newline() << "if (unlikely (rc)) {";
      // In case it's just a lower-layer (kprobes) error that set rc
      // but not session_state, do that here to prevent any other BEGIN
      // probe from attempting to run.
      o->newline(1) << "atomic_set (&session_state, STAP_SESSION_ERROR);";

      // We need to deregister any already probes set up - this is
      // essential for kprobes.
      if (i > 0)
        o->newline() << "goto unregister_" << (i-1) << ";";

      o->newline(-1) << "}";
    }

  // BEGIN probes would have all been run by now.  One of them may
  // have triggered a STAP_SESSION_ERROR (which would incidentally
  // block later BEGIN ones).  If so, let that indication stay, and
  // otherwise act like probe insertion was a success.
  o->newline() << "if (atomic_read (&session_state) == STAP_SESSION_STARTING)";
  o->newline(1) << "atomic_set (&session_state, STAP_SESSION_RUNNING);";
  // XXX: else maybe set anyrc and thus return a failure from module_init?
  o->newline(-1) << "goto out;";

  // recovery code for partially successful registration (rc != 0)
  o->newline();
  for (int i=session->probes.size()-2; i >= 0; i--) // NB: -2
    {
      o->newline(-1) << "unregister_" << i << ":";
      o->indent(1);
      session->probes[i]->emit_deregistrations (o, i);
      // NB: This may be an END probe.  It will refuse to run
      // if the session_state was ERRORed.
    }  
  o->newline();

  o->newline(-1) << "out:";
  o->newline(1) << "return rc;";
  o->newline(-1) << "}" << endl;
}


void
c_unparser::emit_module_exit ()
{
  o->newline() << "static void systemtap_module_exit (void);";
  o->newline() << "void systemtap_module_exit () {";
  // rc?
  o->newline(1) << "int holdon;";
  
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
  o->newline() << "mb ();";
  o->newline() << "for (i=0; i<NR_CPUS; i++)";
  o->newline(1) << "if (contexts[i].busy) holdon = 1;";
  // o->newline(-1) << "if (holdon) msleep (5);";
  o->newline(-1) << "} while (holdon);";
  o->newline(-1);
  // XXX: might like to have an escape hatch, in case some probe is
  // genuinely stuck

  for (int i=session->probes.size()-1; i>=0; i--)
    session->probes[i]->emit_deregistrations (o, i);

  for (unsigned i=0; i<session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];      
      if (v->index_types.size() > 0)
	o->newline() << getmap (v).fini();
    }
  // XXX: if anyrc, log badness
  o->newline(-1) << "}" << endl;
}


void
c_unparser::emit_function (functiondecl* v)
{
  o->newline() << "void function_" << c_varname (v->name)
            << " (struct context* c) {";
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
	throw semantic_error ("array locals not supported", v->tok);

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
  o->newline(-1) << "}" << endl;
}


void
c_unparser::emit_probe (derived_probe* v, unsigned i)
{
  o->newline() << "static void probe_" << i << " (struct context *c);";
  o->newline() << "void probe_" << i << " (struct context *c) {";
  o->indent(1);

  // initialize frame pointer
  o->newline() << "struct probe_" << i << "_locals * __restrict__ l =";
  o->newline(1) << "& c->locals[c->nesting].probe_" << i << ";";
  o->newline(-1) << "(void) l;"; // make sure "l" is marked used

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
  
  this->current_function = 0;
  this->current_probe = v;
  this->current_probenum = i;
  this->tmpvar_counter = 0;
  v->body->visit (this);
  this->current_probe = 0;
  this->current_probenum = 0; // not essential

  o->newline(-1) << "out:";
  o->newline(1) << ";";
  
  // XXX: uninitialize locals

  o->newline(-1) << "}" << endl;
  
  v->emit_probe_entries (o, i);
}


void 
c_unparser::collect_map_index_types(vector<vardecl *> const & vars,
				    set< exp_type > & value_types,
				    set< vector<exp_type> > & index_types)
{
  for (unsigned i = 0; i < vars.size(); ++i)
    {
      vardecl *v = vars[i];
      if (v->arity > 0)
	{
	  value_types.insert(v->type);
	  index_types.insert(v->index_types);
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
      throw semantic_error("array type is neither string nor long");
      break;
    }	      
}

string
mapvar::shortname(exp_type e)
{
  switch (e)
    {
    case pe_long:
      return "int64";
      break;
    case pe_string:
      return "str";
      break;
    default:
      throw semantic_error("array type is neither string nor long");
      break;
    }	      
}


void
c_unparser::emit_map_type_instantiations ()
{
  set< exp_type > value_types;
  set< vector<exp_type> > index_types;
  
  collect_map_index_types(session->globals, value_types, index_types);

  for (unsigned i = 0; i < session->probes.size(); ++i)
    collect_map_index_types(session->probes[i]->locals, 
			    value_types, index_types);

  for (unsigned i = 0; i < session->functions.size(); ++i)
    collect_map_index_types(session->functions[i]->locals, 
			    value_types, index_types);

  for (set<exp_type>::const_iterator i = value_types.begin();
       i != value_types.end(); ++i)
    {
      string ktype = mapvar::key_typename(*i);
      o->newline() << "#define NEED_" << ktype << "_VALS";
    }

  for (set< vector<exp_type> >::const_iterator i = index_types.begin();
       i != index_types.end(); ++i)
    {
      for (unsigned j = 0; j < i->size(); ++j)
	{
	  string ktype = mapvar::key_typename(i->at(j));
	  o->newline() << "#define KEY" << (j+1) << "_TYPE " << ktype;
	}
      o->newline() << "#include \"map-keys.c\"";
      for (unsigned j = 0; j < i->size(); ++j)
	{
	  o->newline() << "#undef KEY" << (j+1) << "_TYPE";
	}      
    }

  if (!value_types.empty())
    o->newline() << "#include \"map.c\"";
};


string
c_unparser::c_typename (exp_type e)
{
  switch (e)
    {
    case pe_long: return string("int64_t");
    case pe_string: return string("string_t"); 
    case pe_stats: return string("stats_t");
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
      throw semantic_error ("unknown rvalue type in assignment", tok);
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
      if (pre)
	throw semantic_error ("pre assignment on strings not supported", 
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
      else if (op == "<<<")
	throw semantic_error ("stats aggregation not yet implemented", tok);
      else if (op == "++")
	macop = "+";
      else if (op == "--")
	macop = "-";
      else
	// internal error
	throw semantic_error ("unknown macop for assignment", tok);

      if (pre)
	{
          if (macop == "/" || macop == "%" || op == "=")
            throw semantic_error ("invalid pre-mode operator", tok);

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
  o->newline() << "strncpy (" 
		   << lvalue << ", " 
		   << rvalue << ", MAXSTRINGLEN);";
}


void 
c_unparser::c_strcpy (const string& lvalue, expression* rvalue) 
{
  o->newline() << "strncpy (" << lvalue << ", ";
  rvalue->visit (this);
  o->line() << ", MAXSTRINGLEN);";
}


void 
c_unparser::c_strcat (const string& lvalue, const string& rvalue) 
{
  o->newline() << "strncat (" 
		   << lvalue << ", " 
		   << rvalue << ", MAXSTRINGLEN);";
}


void 
c_unparser::c_strcat (const string& lvalue, expression* rvalue) 
{
  o->newline() << "strncat (" << lvalue << ", ";
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


var 
c_unparser::getvar(vardecl *v, token const *tok) 
{ 
  return var (is_local (v, tok), v->type, v->name);
}


mapvar 
c_unparser::getmap(vardecl *v, token const *tok) 
{   
  if (v->arity < 1)
    throw new semantic_error("attempt to use scalar where map expected", tok);
  return mapvar (is_local (v, tok), v->type, v->name, v->index_types);
}


itervar 
c_unparser::getiter(foreach_loop *f)
{ 
  return itervar (f, tmpvar_counter);
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
  o->newline() << "c->last_stmt = \"" << *s->tok << "\";";
  if (actions > 0)
    {
      o->newline() << "c->actioncount += " << actions << ";";
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
c_tmpcounter::visit_for_loop (for_loop *s)
{
  s->init->visit (this);
  s->cond->visit (this);
  s->block->visit (this);
  s->incr->visit (this);
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
  s->init->visit (this);

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
  s->incr->visit (this);
  o->newline() << "goto " << toplabel << ";";

  // exit
  o->newline(-1) << breaklabel << ":";
  o->newline(1) << "; /* dummy statement */";
}


void
c_tmpcounter::visit_foreach_loop (foreach_loop *s)
{
  itervar iv = parent->getiter (s);
  parent->o->newline() << iv.declare();
  s->block->visit (this);
}

void
c_unparser::visit_foreach_loop (foreach_loop *s)
{
  visit_statement (s, 1);

  mapvar mv = getmap (s->base_referent, s->tok);
  itervar iv = getiter (s);
  vector<var> keys;

  string ctr = stringify (label_counter++);
  string toplabel = "top_" + ctr;
  string contlabel = "continue_" + ctr;
  string breaklabel = "break_" + ctr;

  // NB: structure parallels for_loop

  // initialization
  varlock_r guard (*this, mv);
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
  o->indent(1);
  // varlock dtor will show up here
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
  mapvar mvar = parent->getmap(e->referent, e->tok);  
  varlock_w guard (*parent, mvar);
  parent->o->newline() << mvar.fini ();
  parent->o->newline() << mvar.init ();  
}

void 
delete_statement_operand_visitor::visit_arrayindex (arrayindex* e)
{
  vector<tmpvar> idx;
  parent->load_map_indices (e, idx);

  {
    mapvar mvar = parent->getmap (e->referent, e->tok);
    varlock_w guard (*parent, mvar);
    parent->o->newline() << mvar.seek (idx) << ";";
    parent->o->newline() << mvar.del () << ";";
  }
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
  o->line() << '"' << e->value << '"'; // XXX: escape special chars
}


void
c_unparser::visit_literal_number (literal_number* e)
{
  // This looks ugly, but tries to be warning-free on 32- and 64-bit
  // hosts.
  o->line() << "((uint64_t)" << e->value << "LL)";
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
  vardecl* r = e->operand->referent;

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


void
c_unparser::visit_array_in (array_in* e)
{
  stmt_expr block(*this);  

  vector<tmpvar> idx;
  load_map_indices (e->operand, idx);

  tmpvar res = gensym (pe_long);

  { // block used to control varlock_r lifespan
    mapvar mvar = getmap (e->operand->referent, e->tok);
    varlock_r guard (*this, mvar);
    o->newline() << mvar.seek (idx) << ";";
    c_assign (res, mvar.exists(), e->tok);
  }

  o->newline() << res << ";";
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
  if (e->type != e->left->type)
    throw semantic_error ("type mismatch", e->tok,
                         "vs", e->left->tok);
  if (e->right->type != e->left->type)
    throw semantic_error ("type mismatch", e->right->tok,
                         "vs", e->left->tok);

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

  c_unparser_assignment tav (this, e->op, true);
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

  c_unparser_assignment tav (this, e->op, false);
  e->operand->visit (& tav);
}


void
c_unparser::visit_symbol (symbol* e)
{
  vardecl* r = e->referent;

  if (r->index_types.size() != 0)
    throw semantic_error ("invalid reference to array", e->tok);

  // XXX: handle special macro symbols
  // XXX: acquire read lock on global; copy value
  // into local temporary

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
  tmpvar tmp = parent->parent->gensym (e->type);
  tmpvar res = parent->parent->gensym (e->type);

  tmp.declare (*(parent->parent));
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

  tmpvar rval = parent->gensym (e->type);
  tmpvar res = parent->gensym (e->type);

  prepare_rvalue (op, rval, e->tok);

  {
    var lvar = parent->getvar (e->referent, e->tok);
    varlock_w guard (*parent, lvar);
    c_assignop (res, lvar, rval, e->tok);     
  }

  parent->o->newline() << res << ";";
}


void 
c_unparser::visit_target_symbol (target_symbol* e)
{
  throw semantic_error("cannot translate general target-symbol expression");
}


void
c_tmpcounter::visit_arrayindex (arrayindex *e)
{
  vardecl* r = e->referent;

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


void
c_unparser::load_map_indices(arrayindex *e,
			     vector<tmpvar> & idx)
{
  idx.clear();

  vardecl* r = e->referent;

  if (r->index_types.size() == 0 ||
      r->index_types.size() != e->indexes.size())
    throw semantic_error ("invalid array reference", e->tok);

  for (unsigned i=0; i<r->index_types.size(); i++)
    {
      if (r->index_types[i] != e->indexes[i]->type)
	throw semantic_error ("array index type mismatch", e->indexes[i]->tok);
      
      tmpvar ix = gensym (r->index_types[i]);
      c_assign (ix.qname(), e->indexes[i], "array index copy");
      idx.push_back (ix);
    }
}


void
c_unparser::visit_arrayindex (arrayindex* e)
{
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
  //    seek (array, idx0...N);
  //    res = fetch (array);
  //    unlock (array);
  //    res; })
  //
  // we store all indices in temporary variables to avoid nasty
  // reentrancy issues that pop up with nested expressions:
  // e.g. a[a[c]=5] could deadlock
  
  { // block used to control varlock_r lifespan
    mapvar mvar = getmap (e->referent, e->tok);
    varlock_r guard (*this, mvar);
    o->newline() << mvar.seek (idx) << ";";
    c_assign (res, mvar.get(), e->tok);
  }

  o->newline() << res << ";";
}


void
c_tmpcounter_assignment::visit_arrayindex (arrayindex *e)
{
  vardecl* r = e->referent;

  // One temporary per index dimension.
  for (unsigned i=0; i<r->index_types.size(); i++)
    {
      tmpvar ix = parent->parent->gensym (r->index_types[i]);
      ix.declare (*(parent->parent));
      e->indexes[i]->visit(parent);
    }
 
 // The expression rval, lval, and result.
  tmpvar rval = parent->parent->gensym (e->type);
  rval.declare (*(parent->parent));

  tmpvar lval = parent->parent->gensym (e->type);
  lval.declare (*(parent->parent));

  tmpvar res = parent->parent->gensym (e->type);
  res.declare (*(parent->parent));

  if (rvalue)
    rvalue->visit (parent);
}

void
c_unparser_assignment::visit_arrayindex (arrayindex *e)
{
  stmt_expr block(*parent);  

  translator_output *o = parent->o;

  if (e->referent->index_types.size() == 0)
    throw semantic_error ("unexpected reference to scalar", e->tok);

  // nb: Do not adjust the order of the next few lines; the tmpvar
  // allocation order must remain the same between
  // c_unparser_assignment::visit_arrayindex and
  // c_tmpcounter_assignment::visit_arrayindex
  
  vector<tmpvar> idx;
  parent->load_map_indices (e, idx);
  tmpvar rvar = parent->gensym (e->type);
  tmpvar lvar = parent->gensym (e->type);
  tmpvar res = parent->gensym (e->type);
  
  // NB: because these expressions are nestable, emit this construct
  // thusly:
  // ({ tmp0=(idx0); ... tmpN=(idxN); rvar=(rhs); lvar; res;
  //    rvar = ...;
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
  
  prepare_rvalue (op, rvar, e->tok);
  
  { // block used to control varlock_w lifespan
    mapvar mvar = parent->getmap (e->referent, e->tok);
    varlock_w guard (*parent, mvar);
    o->newline() << mvar.seek (idx) << ";";
    if (op != "=") // don't bother fetch slot if we will just overwrite it
      parent->c_assign (lvar, mvar.get(), e->tok);
    c_assignop (res, lvar, rvar, e->tok); 
    o->newline() << mvar.set (lvar) << ";";
  }

  o->newline() << res << ";";
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
    }

  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);
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

      c_assign (t.qname(), e->args[i], "function actual argument evaluation");
    }

  o->newline();
  o->newline() << "if (unlikely (c->nesting+2 >= MAXNESTING)) {";
  o->newline(1) << "c->last_error = \"MAXNESTING exceeded\";";
  o->newline(-1) << "} else {";
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


int
translate_pass (systemtap_session& s)
{
  int rc = 0;

  s.op = new translator_output (s.translated_source);
  c_unparser cup (& s);
  s.up = & cup;

  try
    {
      s.op->line() << "#define TEST_MODE " << (s.test_mode ? 1 : 0) << endl;

      s.op->newline() << "#if TEST_MODE";
      s.op->newline() << "#include \"runtime.h\"";
      s.op->newline() << "#else";
      s.op->newline() << "#include \"runtime.h\"";
      s.op->newline() << "#include <linux/string.h>";
      s.op->newline() << "#include <linux/timer.h>";
      // XXX
      s.op->newline() << "#define KALLSYMS_LOOKUP_NAME \"\"";
      s.op->newline() << "#define KALLSYMS_LOOKUP 0";
      s.op->newline() << "#endif";

      s.up->emit_common_header ();

      for (unsigned i=0; i<s.embeds.size(); i++)
        {
          s.op->newline() << s.embeds[i]->code << endl;
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
      s.op->newline() << "#if TEST_MODE";

      s.op->newline() << "/* test mode mainline */";
      s.op->newline() << "int main () {";
      s.op->newline(1) << "int rc = systemtap_module_init ();";
      s.op->newline() << "if (!rc) systemtap_module_exit ();";
      s.op->newline() << "return rc;";
      s.op->newline(-1) << "}";

      s.op->newline() << "#else";

      s.op->newline();
      // XXX
      s.op->newline() << "int probe_start () {";
      s.op->newline(1) << "return systemtap_module_init ();";
      s.op->newline(-1) << "}";
      s.op->newline();
      s.op->newline() << "void probe_exit () {";
      // XXX: need to reference these static functions for -Werror avoidance
      s.op->newline(1) << "if (0) next_fmt ((void *) 0, (void *) 0);";
      s.op->newline() << "if (0) _stp_dbug(\"\", 0, \"\");";
      s.op->newline() << "systemtap_module_exit ();";
      s.op->newline(-1) << "}";

      s.op->newline() << "MODULE_DESCRIPTION(\"systemtap probe\");";
      s.op->newline() << "MODULE_LICENSE(\"GPL\");"; // XXX
      s.op->newline() << "#endif";

      s.op->line() << endl;
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
    }

  delete s.op;
  s.op = 0;
  s.up = 0;
  return rc + s.num_errors;
}
