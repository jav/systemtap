// semantic analysis pass, beginnings of elaboration
// Copyright 2005 Red Hat Inc.
// GPL

#include "config.h"
#include "staptree.h"
#include "elaborate.h"
#include "translate.h"
#include <iostream>
#include <sstream>

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



// ------------------------------------------------------------------------
// toy provider/unparser pair


struct test_derived_probe: public derived_probe
{
  test_derived_probe (probe* p);
  test_derived_probe (probe* p, probe_point* l);

  void emit_registrations (translator_output* o, unsigned i);
  void emit_deregistrations (translator_output* o, unsigned i);
  void emit_probe_entries (translator_output* o, unsigned i);
};



void
symresolution_info::derive_probes (probe *p, vector<derived_probe*>& dps)
{
  // XXX: there will be real ones coming later
  for (unsigned i=0; i<p->locations.size(); i++)
    dps.push_back (new test_derived_probe (p, p->locations[i]));
}



struct c_unparser: public unparser, public visitor
{
  systemtap_session* session;
  translator_output* o;

  derived_probe* current_probe;
  unsigned current_probenum;
  functiondecl* current_function;
  unsigned tmpvar_counter;

  c_unparser (systemtap_session *ss):
    session (ss), o (ss->op), current_probe(0), current_function (0),
  tmpvar_counter (0) {}
  ~c_unparser () {}

  void emit_common_header ();
  void emit_global (vardecl* v);
  void emit_functionsig (functiondecl* v);
  void emit_module_init ();
  void emit_module_exit ();
  void emit_function (functiondecl* v);
  void emit_probe (derived_probe* v, unsigned i);

  // XXX: for use by loop/nesting constructs
  // vector<string> loop_break_labels;
  // vector<string> loop_continue_labels;

  string c_typename (exp_type e);
  string c_varname (const string& e);
  void c_assign (const string& lvalue, expression* rvalue, const string& msg);
  void c_assign (const string& lvalue, const string& rvalue, exp_type type,
                 const string& msg, const token* tok);

  void visit_block (block *s);
  void visit_null_statement (null_statement *s);
  void visit_expr_statement (expr_statement *s);
  void visit_if_statement (if_statement* s);
  void visit_for_loop (for_loop* s);
  void visit_return_statement (return_statement* s);
  void visit_delete_statement (delete_statement* s);
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
  void visit_exponentiation (exponentiation* e);
  void visit_ternary_expression (ternary_expression* e);
  void visit_assignment (assignment* e);
  void visit_symbol (symbol* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
};



// ------------------------------------------------------------------------


// Perform inter-script dependency analysis [XXX: later],
// then provider elaboration and derived probe construction
// and finally semantic analysis
// on the reachable set of probes/functions from the user_file
int
resolution_pass (systemtap_session& s)
{
  int rc = 0;

  for (unsigned i=0; i<s.user_file->probes.size(); i++)
    {
      probe* p = s.user_file->probes[i];
      // XXX: should of course be based on each probe_point
      derived_probe *dp = new test_derived_probe (p);
      s.probes.push_back (dp);
    }

  // XXX: merge functiondecls
  // XXX: handle library files
  // XXX: add builtin variables/functions
  return rc;
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


test_derived_probe::test_derived_probe (probe* p): derived_probe (p)
{
}


test_derived_probe::test_derived_probe (probe* p, probe_point* l):
  derived_probe (p, l)
{
}


void 
test_derived_probe::emit_registrations (translator_output* o, unsigned i)
{
  // XXX
  o->newline() << "rc = 0; /* no registration for probe " << i << " */";
}

void 
test_derived_probe::emit_deregistrations (translator_output* o, unsigned i)
{
  // XXX
  o->newline() << "rc = 0; /* no deregistration for probe " << i << " */";
}


void
test_derived_probe::emit_probe_entries (translator_output* o, unsigned j)
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


// A shadow visitor, meant to generate temporary variable declarations
// for function or probe bodies.  Member functions should exactly match
// the corresponding c_unparser logic and traversal sequence,
// to ensure interlocking naming and declaration of temp variables.
struct c_tmpcounter: public traversing_visitor
{
  c_unparser* parent;
  unsigned tmpvar_counter;
  c_tmpcounter (c_unparser* p): parent (p), tmpvar_counter (0) {}

  // void visit_for_loop (for_loop* s);
  // void visit_return_statement (return_statement* s);
  // void visit_delete_statement (delete_statement* s);
  // void visit_binary_expression (binary_expression* e);
  // void visit_unary_expression (unary_expression* e);
  // void visit_pre_crement (pre_crement* e);
  // void visit_post_crement (post_crement* e);
  // void visit_logical_or_expr (logical_or_expr* e);
  // void visit_logical_and_expr (logical_and_expr* e);
  // void visit_array_in (array_in* e);
  // void visit_comparison (comparison* e);
  // void visit_concatenation (concatenation* e);
  // void visit_exponentiation (exponentiation* e);
  // void visit_ternary_expression (ternary_expression* e);
  void visit_assignment (assignment* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
};



void
c_unparser::emit_common_header ()
{
  o->newline() << "#include <string.h>"; 
  o->newline() << "#define NR_CPU 1"; 
  o->newline() << "#define MAXNESTING 30";
  o->newline() << "#define MAXCONCURRENCY NR_CPU";
  o->newline() << "#define MAXSTRINGLEN 128";
  o->newline() << "#define MAXACTION 1000";
  o->newline();
  o->newline() << "typedef char string_t[MAXSTRINGLEN];";
  o->newline() << "typedef struct { int a; } stats_t;";
  o->newline();
  o->newline() << "unsigned errorcount;";
  o->newline() << "struct context {";
  o->indent(1);
  o->newline() << "unsigned busy;";
  o->newline() << "unsigned actioncount;";
  o->newline() << "unsigned nesting;";
  o->newline() << "union {";
  o->indent(1);
  // XXX: this handles only scalars!

  for (unsigned i=0; i<session->probes.size(); i++)
    {
      derived_probe* dp = session->probes[i];
      o->newline() << "struct probe_" << i << "_locals {";
      o->newline(1) << "/* local variables */";
      for (unsigned j=0; j<dp->locals.size(); j++)
        {
          vardecl* v = dp->locals[j];
          o->newline() << c_typename (v->type) << " " 
		       << c_varname (v->name) << ";";
        }
      o->newline() << "/* temporary variables */";
      c_tmpcounter ct (this);
      dp->body->visit (& ct);
      o->newline(-1) << "} probe_" << i << ";";
    }

  for (unsigned i=0; i<session->functions.size(); i++)
    {
      functiondecl* fd = session->functions[i];
      o->newline()
        << "struct function_" << c_varname (fd->name) << "_locals {";
      o->newline(1) << "/* local variables */";
      for (unsigned j=0; j<fd->locals.size(); j++)
        {
          vardecl* v = fd->locals[j];
          o->newline() << c_typename (v->type) << " " 
  		       << c_varname (v->name) << ";";
        }
      o->newline() << "/* formal arguments */";
      for (unsigned j=0; j<fd->formal_args.size(); j++)
        {
          vardecl* v = fd->formal_args[j];
          o->newline() << c_typename (v->type) << " " 
		       << c_varname (v->name) << ";";
        }
      o->newline() << "/* temporary variables */";
      c_tmpcounter ct (this);
      fd->body->visit (& ct);
      if (fd->type == pe_unknown)
	o->newline() << "/* no return value */";
      else
	{
	  o->newline() << "/* return value */";
	  o->newline() << c_typename (fd->type) << " __retvalue;";
	}
      o->newline(-1) << "} function_" << c_varname (fd->name) << ";";
    }
  o->newline(-1) << "} locals [MAXNESTING];";
  o->newline(-1) << "} contexts [MAXCONCURRENCY];" << endl;
}


void
c_unparser::emit_global (vardecl *v)
{
  o->newline() << "static "
	       << c_typename (v->type)
	       << " "
	       << "global_" << c_varname (v->name)
	       << ";";
  /* XXX
  o->line() << "static DEFINE_RWLOCK("
             << c_varname (v->name) << "_lock"
             << ");";
  */
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
  o->newline() << "int STARTUP () {";
  o->newline(1) << "int anyrc = 0;";
  o->newline() << "int rc;";
  // XXX: initialize globals
  for (unsigned i=0; i<session->probes.size(); i++)
    {
      session->probes[i]->emit_registrations (o, i);
      o->newline() << "anyrc |= rc;";
      o->newline() << "if (rc) {";
      o->indent(1);
      if (i > 0)
        for (unsigned j=i; j>0; j--)
          session->probes[j-1]->emit_deregistrations (o, j-1);
      // XXX: ignore rc
      o->newline() << "goto out;";
      o->newline(-1) << "}";
    }
  o->newline(-1) << "out:";
  o->indent(1);
  o->newline() << "return anyrc; /* if (anyrc) log badness */";
  o->newline(-1) << "}" << endl;
}


void
c_unparser::emit_module_exit ()
{
  o->newline() << "int SHUTDOWN () {";
  o->newline(1) << "int anyrc = 0;";
  o->newline() << "int rc;";
  for (unsigned i=0; i<session->probes.size(); i++)
    {
      session->probes[i]->emit_deregistrations (o, i);
      o->newline() << "anyrc |= rc;";
    }
  // XXX: uninitialize globals
  o->newline() << "return anyrc; /* if (anyrc) log badness */";
  o->newline(-1) << "}" << endl;
}


void
c_unparser::emit_function (functiondecl* v)
{
  o->newline() << "static void function_" << c_varname (v->name)
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
    << "& c->locals[c->nesting].function_" << c_varname (v->name) << ";";
  o->newline(-1);

  // initialize locals
  for (unsigned i=0; i<v->locals.size(); i++)
    {
      if (v->locals[i]->index_types.size() > 0) // array?
	throw semantic_error ("not yet implemented", v->tok);
      else if (v->locals[i]->type == pe_long)
	o->newline() << "l->" << c_varname (v->locals[i]->name)
		     << " = 0;";
      else if (v->locals[i]->type == pe_string)
	o->newline() << "l->" << c_varname (v->locals[i]->name)
		     << "[0] = '\\0';";
      else
	throw semantic_error ("unsupported local variable type",
			      v->locals[i]->tok);
    }

  // initialize return value, if any
  if (v->type == pe_long)
    o->newline() << "l->__retvalue = 0;";
  else if (v->type == pe_string)
    o->newline() << "l->__retvalue[0] = '\\0';";

  v->body->visit (this);
  this->current_function = 0;

  o->newline(-1) << "out:";
  o->newline(1) << ";";

  o->newline(-1) << "}" << endl;
}


void
c_unparser::emit_probe (derived_probe* v, unsigned i)
{
  o->newline() << "static void probe_" << i << " (struct context *c) {";
  o->indent(1);

  // initialize frame pointer
  o->newline() << "struct probe_" << i << "_locals * __restrict__ l =";
  o->newline(1) << "& c->locals[c->nesting].probe_" << i << ";";
  o->indent(-1);

  // initialize locals
  for (unsigned j=0; j<v->locals.size(); j++)
    {
      if (v->locals[j]->index_types.size() > 0) // array?
	throw semantic_error ("not yet implemented", v->tok);
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


string
c_unparser::c_typename (exp_type e)
{
  switch (e)
    {
    case pe_long: return string("long");
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
      o->newline() << "strncpy (" << lvalue << ", ";
      rvalue->visit (this);
      o->line() << ", MAXSTRINGLEN);";
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
      o->newline() << "strncpy (" << lvalue << ", "
                   << rvalue << ", MAXSTRINGLEN);";
    }
  else
    {
      string fullmsg = msg + " type unsupported";
      throw semantic_error (fullmsg, tok);
    }
}



void
c_unparser::visit_block (block *s)
{
  const token* t = s->tok;
  o->newline() << "# " << t->location.line
	       << " \"" << t->location.file << "\" " << endl;

  o->newline() << "{";
  o->indent (1);
  o->newline() << "c->actioncount += " << s->statements.size() << ";";
  o->newline() << "if (c->actioncount > MAXACTION)";
  o->newline(1) << "errorcount ++;";
  o->indent(-1);

  for (unsigned i=0; i<s->statements.size(); i++)
    {
      try
        {
          // XXX: it's probably not necessary to check this so frequently
	  o->newline() << "if (errorcount)";
	  o->newline(1) << "goto out;" << endl;
	  o->indent(-1);

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
c_unparser::visit_null_statement (null_statement *s)
{
  const token* t = s->tok;
  o->newline() << "# " << t->location.line
	       << " \"" << t->location.file << "\" " << endl;
  o->newline() << "/* null */;";
}


void
c_unparser::visit_expr_statement (expr_statement *s)
{
  const token* t = s->tok;
  o->newline() << "# " << t->location.line
	       << " \"" << t->location.file << "\" " << endl;
  o->newline() << "(void) ";
  s->value->visit (this);
  o->line() << ";";
}


void
c_unparser::visit_if_statement (if_statement *s)
{
  const token* t = s->tok;
  o->newline() << "# " << t->location.line
	       << " \"" << t->location.file << "\" " << endl;
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
      s->thenblock->visit (this);
      o->newline(-1) << "}";
    }
}


void
c_unparser::visit_for_loop (for_loop *s)
{
  const token* t = s->tok;
  o->newline() << "# " << t->location.line
	       << " \"" << t->location.file << "\" " << endl;
  throw semantic_error ("not yet implemented", s->tok);
}


void
c_unparser::visit_return_statement (return_statement* s)
{
  const token* t = s->tok;
  o->newline() << "# " << t->location.line
	       << " \"" << t->location.file << "\" " << endl;
  if (current_function == 0)
    throw semantic_error ("cannot return from non-function", s->tok);

  if (s->value->type != current_function->type)
    throw semantic_error ("return type mismatch", current_function->tok,
                         "vs", s->tok);

  o->newline() << "/* " << *s->tok << " */";
  c_assign ("l->__retvalue", s->value, "return value");

  o->newline() << "goto out;";
}


void
c_unparser::visit_delete_statement (delete_statement* s)
{
  const token* t = s->tok;
  o->newline() << "# " << t->location.line
	       << " \"" << t->location.file << "\" " << endl;
  throw semantic_error ("not yet implemented", s->tok);
}

void
c_unparser::visit_literal_string (literal_string* e)
{
  o->line() << '"' << e->value << '"'; // XXX: escape special chars
}

void
c_unparser::visit_literal_number (literal_number* e)
{
  o->line() << e->value;
}

void
c_unparser::visit_binary_expression (binary_expression* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}

void
c_unparser::visit_unary_expression (unary_expression* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}

void
c_unparser::visit_pre_crement (pre_crement* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}

void
c_unparser::visit_post_crement (post_crement* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}

void
c_unparser::visit_logical_or_expr (logical_or_expr* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}

void
c_unparser::visit_logical_and_expr (logical_and_expr* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}

void
c_unparser::visit_array_in (array_in* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}


void
c_unparser::visit_comparison (comparison* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}


void
c_unparser::visit_concatenation (concatenation* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}


void
c_unparser::visit_exponentiation (exponentiation* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}


void
c_unparser::visit_ternary_expression (ternary_expression* e)
{
  throw semantic_error ("not yet implemented", e->tok);
}



struct c_unparser_assignment: public throwing_visitor
{
  c_unparser* parent;
  string op;
  expression* rvalue;
  c_unparser_assignment (c_unparser* p, const string& o, expression* e):
    parent (p), op (o), rvalue (e) {}

  // only symbols and arrayindex nodes are possible lvalues
  void visit_symbol (symbol *e);
  void visit_arrayindex (arrayindex *e);
};


struct c_tmpcounter_assignment: public traversing_visitor
// leave throwing for illegal lvalues to the c_unparser_assignment instance
{
  c_tmpcounter* parent;
  const string& op;
  expression* rvalue;
  c_tmpcounter_assignment (c_tmpcounter* p, const string& o, expression* e):
    parent (p), op (o), rvalue (e) {}

  // only symbols and arrayindex nodes are possible lvalues
  void visit_symbol (symbol *e);
  void visit_arrayindex (arrayindex *e);
};



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

  if (e->op == "=" || e->op == "<<<")
    {
      c_unparser_assignment tav (this, e->op, e->right);
      e->left->visit (& tav);
    }
  else
    throw semantic_error ("not yet implemented ", e->tok);
}


void
c_unparser::visit_symbol (symbol* e)
{
  vardecl* r = e->referent;

  if (r->index_types.size() != 0)
    throw semantic_error ("invalid reference to array", e->tok);

  // XXX: handle special macro symbols

  // maybe the variable is a local
  if (current_probe)
    {
      for (unsigned i=0; i<current_probe->locals.size(); i++)
	{
	  vardecl* rr = current_probe->locals[i];
	  if (rr == r) // comparison of pointers is sufficient
	    {
	      o->line() << "l->" << c_varname (r->name);
	      return;
	    }
	}
    }
  else if (current_function)
    {
      // check locals
      for (unsigned i=0; i<current_function->locals.size(); i++)
	{
	  vardecl* rr = current_function->locals[i];
	  if (rr == r) // comparison of pointers is sufficient
	    {
	      o->line() << "l->" << c_varname (r->name);
	      return;
	    }
	}

      // check formal args
      for (unsigned i=0; i<current_function->formal_args.size(); i++)
	{
	  vardecl* rr = current_function->formal_args[i];
	  if (rr == r) // comparison of pointers is sufficient
	    {
	      o->line() << "l->" << c_varname (r->name);
	      return;
	    }
	}
    }

  // it better be a global
  for (unsigned i=0; i<session->globals.size(); i++)
    {
      if (session->globals[i] == r)
	{
	  // XXX: acquire read lock on global; copy value
	  // into local temporary
	  o->line() << "global_" << c_varname (r->name);
	  return;
	}
    }

  throw semantic_error ("unresolved symbol", e->tok);
}


void
c_tmpcounter_assignment::visit_symbol (symbol *e)
{
  parent->parent->o->newline()
    << parent->parent->c_typename (e->type)
    << " __tmp" << parent->tmpvar_counter ++ << ";"
    << " /* " << e->name << " rvalue */";
  rvalue->visit (parent);
}


void
c_unparser_assignment::visit_symbol (symbol *e)
{
  vardecl* r = e->referent;
  translator_output* o = parent->o;
  functiondecl* current_function = parent->current_function;
  derived_probe* current_probe = parent->current_probe;
  systemtap_session* session = parent->session;

  if (op != "=")
    throw semantic_error ("not yet implemented", e->tok);

  if (r->index_types.size() != 0)
    throw semantic_error ("invalid reference to array", e->tok);

  // XXX: handle special macro symbols

  unsigned tmpidx = parent->tmpvar_counter ++;
  string tmp_base = "l->__tmp";

  // NB: because assignments are nestable expressions, we have
  // to emit C constructs that are nestable expressions too.
  // ... (A = B) ... ==>
  // ... ({ tmp = B; store(A,tmp); tmp; }) ...

  o->line() << "({ ";
  o->indent(1);

  parent->c_assign (tmp_base + stringify (tmpidx), rvalue, "assignment");

  // XXX: strings may be passed safely via a char*, without
  // a full copy in tmpNNN

  // maybe the variable is a local
  if (current_probe)
    {
      for (unsigned i=0; i<current_probe->locals.size(); i++)
	{
	  vardecl* rr = current_probe->locals[i];
	  if (rr == r) // comparison of pointers is sufficient
	    {
              parent->c_assign ("l->" + parent->c_varname (r->name),
                                tmp_base + stringify (tmpidx),
                                rvalue->type,
                                "local variable assignment", rvalue->tok); 

              o->newline() << tmp_base << tmpidx << ";";
	      o->newline(-1) << "})";
	      return;
	    }
	}
    }
  else if (current_function)
    {
      for (unsigned i=0; i<current_function->locals.size(); i++)
	{
	  vardecl* rr = current_function->locals[i];
	  if (rr == r) // comparison of pointers is sufficient
	    {
              parent->c_assign ("l->" + parent->c_varname (r->name),
                                tmp_base + stringify (tmpidx),
                                rvalue->type,
                                "local variable assignment", rvalue->tok); 

	      o->newline() << tmp_base << tmpidx << ";";
	      o->newline(-1) << "})";
	      return;
	    }
	}

      for (unsigned i=0; i<current_function->formal_args.size(); i++)
	{
	  vardecl* rr = current_function->formal_args[i];
	  if (rr == r) // comparison of pointers is sufficient
	    {
              parent->c_assign ("l->" + parent->c_varname (r->name),
                                tmp_base + stringify (tmpidx),
                                rvalue->type,
                                "formal argument assignment", rvalue->tok); 

	      o->newline() << tmp_base << tmpidx << ";";
	      o->newline(-1) << "})";
	      return;
	    }
	}
    }

  // it better be a global
  for (unsigned i=0; i<session->globals.size(); i++)
    {
      if (session->globals[i] == r)
	{
	  // XXX: acquire write lock on global

          parent->c_assign ("global_" + parent->c_varname (r->name),
                            tmp_base + stringify (tmpidx),
                            rvalue->type,
                            "global variable assignment", rvalue->tok); 

	  o->newline() << tmp_base << tmpidx << ";";
	  o->newline(-1) << "})";
	  return;
	}
    }

  throw semantic_error ("unresolved symbol", e->tok);
}


void
c_tmpcounter::visit_arrayindex (arrayindex *e)
{
  vardecl* r = e->referent;
  // one temporary per index dimension
  for (unsigned i=0; i<r->index_types.size(); i++)
    parent->o->newline()
      << parent->c_typename (r->index_types[i])
      << " __tmp" << tmpvar_counter ++ << ";"
      << " /* " << e->base << " idx #" << i << " */";
  // now the result
  parent->o->newline()
    << parent->c_typename (r->type)
    << " __tmp" << tmpvar_counter ++ << ";"
    << " /* " << e->base << "value */";

  for (unsigned i=0; i<e->indexes.size(); i++)
    e->indexes[i]->visit (this);
}


void
c_unparser::visit_arrayindex (arrayindex* e)
{
  vardecl* r = e->referent;

  if (r->index_types.size() == 0 ||
      r->index_types.size() != e->indexes.size())
    throw semantic_error ("invalid array reference", e->tok);

  o->line() << "({";
  o->indent(1);

  // NB: because these expressions are nestable, emit this construct
  // thusly:
  // ({ tmp0=(idx0); ... tmpN=(idxN);
  //    fetch (array,idx0...N, &tmpresult);
  //    tmpresult; })
  //
  // we store all indices in temporary variables to avoid nasty
  // reentrancy issues that pop up with nested expressions:
  // e.g. a[a[c]=5] could deadlock

  unsigned tmpidx_base = tmpvar_counter;
  tmpvar_counter += r->index_types.size() + 1 /* result */;
  string tmp_base = "l->__tmp";
  unsigned residx = tmpidx_base + r->index_types.size();

  for (unsigned i=0; i<r->index_types.size(); i++)
    {
    if (r->index_types[i] != e->indexes[i]->type)
      throw semantic_error ("array index type mismatch", e->indexes[i]->tok);

    unsigned tmpidx = tmpidx_base + i;

    c_assign (tmp_base + stringify (tmpidx),
              e->indexes[i], "array index copy");
    }

  o->newline() << "if (errorcount)";
  o->newline(1) << "goto out;";
  o->indent(-1);

#if 0
  // it better be a global
  for (unsigned i=0; i<session->globals.size(); i++)
    {
      if (session->globals[i] == r)
	{
	  // XXX: acquire read lock on global; copy value
	  // into local temporary
	  o->line() << "global_" << c_varname (r->name);
	  return;
	}
    }

  throw semantic_error ("unresolved symbol", e->tok);
#endif


  o->newline() << tmp_base << residx << ";";
  o->newline(-1) << "})";
}


void
c_tmpcounter_assignment::visit_arrayindex (arrayindex *e)
{
}

void
c_unparser_assignment::visit_arrayindex (arrayindex *e)
{
  throw semantic_error ("not yet implemented", e->tok);
}


void
c_tmpcounter::visit_functioncall (functioncall *e)
{
  functiondecl* r = e->referent;
  // one temporary per argument
  for (unsigned i=0; i<r->formal_args.size(); i++)
    parent->o->newline()
      << parent->c_typename (r->formal_args[i]->type)
      << " __tmp" << tmpvar_counter ++ << ";"
      << " /* " << e->function << " arg #" << i << " */";

  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);
}


void
c_unparser::visit_functioncall (functioncall* e)
{
  functiondecl* r = e->referent;

  if (r->formal_args.size() != e->args.size())
    throw semantic_error ("invalid length argument list", e->tok);

  o->line() << "({";
  o->indent(1);

  // NB: we store all actual arguments in temporary variables,
  // to avoid colliding sharing of context variables with
  // nested function calls: f(f(f(1)))

  o->newline() << "/* compute actual arguments */";

  unsigned tmpidx_base = tmpvar_counter;
  tmpvar_counter += r->formal_args.size();
  string tmp_base = "l->__tmp";

  for (unsigned i=0; i<e->args.size(); i++)
    {
      unsigned tmpidx = tmpidx_base + i;

      if (r->formal_args[i]->type != e->args[i]->type)
	throw semantic_error ("function argument type mismatch",
			      e->args[i]->tok, "vs", r->formal_args[i]->tok);

      c_assign (tmp_base + stringify(tmpidx),
                e->args[i], "function actual argument evaluation");
    }

  o->newline() << "if (c->nesting+2 >= MAXNESTING)";
  o->newline(1) << "errorcount ++;";
  o->newline(-1) << "c->actioncount ++;";
  o->newline() << "if (c->actioncount > MAXACTION)";
  o->newline(1) << "errorcount ++;";
  o->newline(-1) << "if (errorcount)";
  o->newline(1) << "goto out;";
  o->indent(-1);

  // copy in actual arguments
  for (unsigned i=0; i<e->args.size(); i++)
    {
      unsigned tmpidx = tmpidx_base + i;

      if (r->formal_args[i]->type != e->args[i]->type)
	throw semantic_error ("function argument type mismatch",
			      e->args[i]->tok, "vs", r->formal_args[i]->tok);

      c_assign ("c->locals[c->nesting+1].function_" +
		c_varname (r->name) + "." +
                c_varname (r->formal_args[i]->name),
                tmp_base + stringify (tmpidx),
                e->args[i]->type,
                "function actual argument copy",
                e->args[i]->tok);
    }

  // call function
  o->newline() << "c->nesting ++;";
  o->newline() << "function_" << c_varname (r->name) << " (c);";
  o->newline() << "c->nesting --;";
  
  // return result from retvalue slot
  
  if (r->type == pe_unknown)
    // If we passed typechecking, then nothing will use this return value
    o->newline() << "(void) 0;";
  else
    o->newline() << "c->locals[c->nesting+1]"
                 << ".function_" << c_varname (r->name)
                 << ".__retvalue;";
  
  o->newline(-1) << "})";
}



int
translate_pass (systemtap_session& s)
{
  int rc = 0;

  c_unparser cup (& s);
  s.up = & cup;

  try
    {
      s.op->newline() << "/* common header */";
      s.up->emit_common_header ();
      s.op->newline() << "/* globals */";
      for (unsigned i=0; i<s.globals.size(); i++)
        s.up->emit_global (s.globals[i]);
      s.op->newline() << "/* function signatures */";
      for (unsigned i=0; i<s.functions.size(); i++)
        s.up->emit_functionsig (s.functions[i]);
      s.op->newline() << "/* functions */";
      for (unsigned i=0; i<s.functions.size(); i++)
        s.up->emit_function (s.functions[i]);
      s.op->newline() << "/* probes */";
      for (unsigned i=0; i<s.probes.size(); i++)
        s.up->emit_probe (s.probes[i], i);
      s.op->newline() << "/* module init */";
      s.up->emit_module_init ();
      s.op->newline() << "/* module exit */";
      s.up->emit_module_exit ();
      s.op->newline();
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
    }

  s.up = 0;
  return rc + s.num_errors;
}
