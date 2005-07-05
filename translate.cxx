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


struct c_unparser: public unparser, public visitor
{
  systemtap_session* session;
  translator_output* o;

  derived_probe* current_probe;
  unsigned current_probenum;
  functiondecl* current_function;
  unsigned tmpvar_counter;
  unsigned label_counter;

  c_unparser (systemtap_session *ss):
    session (ss), o (ss->op), current_probe(0), current_function (0),
  tmpvar_counter (0), label_counter (0) {}
  ~c_unparser () {}

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
  void c_assign (const string& lvalue, expression* rvalue, const string& msg);
  void c_assign (const string& lvalue, const string& rvalue, exp_type type,
                 const string& msg, const token* tok);

  void visit_block (block *s);
  void visit_null_statement (null_statement *s);
  void visit_expr_statement (expr_statement *s);
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
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
};


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
  // void visit_foreach_loop (foreach_loop* s);
  // void visit_return_statement (return_statement* s);
  // void visit_delete_statement (delete_statement* s);
  void visit_binary_expression (binary_expression* e);
  // void visit_unary_expression (unary_expression* e);
  void visit_pre_crement (pre_crement* e);
  void visit_post_crement (post_crement* e);
  // void visit_logical_or_expr (logical_or_expr* e);
  // void visit_logical_and_expr (logical_and_expr* e);
  // void visit_array_in (array_in* e);
  // void visit_comparison (comparison* e);
  void visit_concatenation (concatenation* e);
  // void visit_ternary_expression (ternary_expression* e);
  void visit_assignment (assignment* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
};



void
c_unparser::emit_common_header ()
{
  o->newline() << "#if TEST_MODE";
  o->newline() << "#include <string.h>";
  o->newline() << "#else";
  o->newline() << "#include <linux/string.h>";
  // XXX: tapsets.cxx should be able to add additional definitions
  o->newline() << "#endif";

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
  o->newline() << "static int systemtap_module_init (void);";
  o->newline() << "int systemtap_module_init () {";
  o->newline(1) << "int anyrc = 0;";
  o->newline() << "int rc;";

  // XXX: yuck runtime
  o->newline() << "TRANSPORT_OPEN;";

  for (unsigned i=0; i<session->globals.size(); i++)
    {
      vardecl* v = session->globals[i];
      if (v->index_types.size() > 0) // array?
	throw semantic_error ("array init not yet implemented", v->tok);
      else if (v->type == pe_long)
	o->newline() << "global_" << c_varname (v->name)
		     << " = 0;";
      else if (v->type == pe_string)
	o->newline() << "global_" << c_varname (v->name)
		     << "[0] = '\\0';";
      else
	throw semantic_error ("unsupported global variable type",
			      v->tok);
    }

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
  o->newline() << "out:";
  o->newline() << "return anyrc; /* if (anyrc) log badness */";
  o->newline(-1) << "}" << endl;
}


void
c_unparser::emit_module_exit ()
{
  // XXX: double-yuck runtime
  o->newline() << "void probe_exit () {";
  // need to reference these static functions for -Werror avoidance
  o->newline(1) << "if (0) next_fmt ((void *) 0, (void *) 0);";
  o->newline() << "if (0) _stp_dbug(\"\", 0, \"\");";
  o->newline(-1) << "}";
  //
  o->newline() << "static void systemtap_module_exit (void);";
  o->newline() << "void systemtap_module_exit () {";
  o->newline(1) << "int anyrc = 0;";
  o->newline() << "int rc;";
  for (unsigned i=0; i<session->probes.size(); i++)
    {
      session->probes[i]->emit_deregistrations (o, i);
      o->newline() << "anyrc |= rc;";
    }
  // XXX: uninitialize globals
  o->newline() << "_stp_transport_close ();";
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

  // initialize locals
  for (unsigned i=0; i<v->locals.size(); i++)
    {
      if (v->locals[i]->index_types.size() > 0) // array?
	throw semantic_error ("array locals not supported", v->tok);
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
  o->newline() << "{";
  o->indent (1);
  o->newline() << "c->actioncount += " << s->statements.size() << ";";
  o->newline() << "if (c->actioncount > MAXACTION) errorcount ++;";

  for (unsigned i=0; i<s->statements.size(); i++)
    {
      try
        {
          // XXX: it's probably not necessary to check this so frequently
	  o->newline() << "if (errorcount) goto out;";
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
  o->newline() << "/* null */;";
}


void
c_unparser::visit_expr_statement (expr_statement *s)
{
  o->newline() << "(void) ";
  s->value->visit (this);
  o->line() << ";";
}


void
c_unparser::visit_if_statement (if_statement *s)
{
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
  s->init->visit (this);
  string ctr = stringify (label_counter++);
  string contlabel = "continue_" + ctr;
  string breaklabel = "break_" + ctr;

  o->newline() << contlabel << ":";

  o->newline() << "c->actioncount ++;";
  o->newline() << "if (c->actioncount > MAXACTION) errorcount ++;";
  o->newline() << "if (errorcount) goto out;";

  o->newline() << "if (! (";
  if (s->cond->type != pe_long)
    throw semantic_error ("expected numeric type", s->cond->tok);
  s->cond->visit (this);
  o->line() << ")) goto " << breaklabel << ";";

  loop_break_labels.push_back (breaklabel);
  loop_continue_labels.push_back (contlabel);
  s->block->visit (this);
  loop_break_labels.pop_back ();
  loop_continue_labels.pop_back ();

  s->incr->visit (this);
  o->newline() << "goto " << contlabel << ";";

  o->newline() << breaklabel << ": ; /* dummy statement */";
}


void
c_unparser::visit_foreach_loop (foreach_loop *s)
{
  throw semantic_error ("foreach loop not yet implemented", s->tok);
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
  o->newline() << "goto out;";
}


void
c_unparser::visit_next_statement (next_statement* s)
{
  if (current_probe == 0)
    throw semantic_error ("cannot 'next' from function", s->tok);

  o->newline() << "goto out;";
}


void
c_unparser::visit_delete_statement (delete_statement* s)
{
  throw semantic_error ("delete statement not yet implemented", s->tok);
}


void
c_unparser::visit_break_statement (break_statement* s)
{
  if (loop_break_labels.size() == 0)
    throw semantic_error ("cannot 'break' outside loop", s->tok);

  string label = loop_break_labels[loop_break_labels.size()-1];
  o->newline() << "goto " << label << ";";
}


void
c_unparser::visit_continue_statement (continue_statement* s)
{
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
  o->line() << e->value;
}



void
c_tmpcounter::visit_binary_expression (binary_expression* e)
{
  if (e->op == "/" || e->op == "%")
    {
      parent->o->newline()
        << parent->c_typename (pe_long)
        << " __tmp" << tmpvar_counter ++ << ";";
      parent->o->newline()
        << parent->c_typename (pe_long)
        << " __tmp" << tmpvar_counter ++ << ";";
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
      unsigned tmpidx1 = tmpvar_counter++;
      unsigned tmpidx2 = tmpvar_counter++;
      string tmp1 = "l->__tmp" + stringify (tmpidx1);
      string tmp2 = "l->__tmp" + stringify (tmpidx2);
      o->line() << "({";

      o->newline(1) << tmp1 << " = ";
      e->left->visit (this);
      o->line() << ";";

      o->newline() << tmp2 << " = ";
      e->right->visit (this);
      o->line() << ";";

      o->newline() << "if (" << tmp2 << " == 0) {";
      o->newline(1) << "errorcount ++;";
      o->newline() << tmp2 << " = 1;";
      o->newline(-1) << "}";

      o->newline() << tmp1 << " " << e->op << " " << tmp2 << ";";
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
c_unparser::visit_array_in (array_in* e)
{
  throw semantic_error ("array-in expression not yet implemented", e->tok);
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
  parent->o->newline() << parent->c_typename (e->type)
                       << " __tmp" << tmpvar_counter ++ << ";";
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

  string tmpvar = "l->__tmp" + stringify (tmpvar_counter ++);
  
  o->line() << "({ ";
  o->indent(1);
  c_assign (tmpvar, e->left, "assignment");
  o->newline() << "strncat (" << tmpvar << ", ";
  e->right->visit (this);
  o->line() << ", MAXSTRINGLEN);";
  o->newline() << tmpvar << ";";
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


struct c_unparser_assignment: public throwing_visitor
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
  // tmp
  parent->parent->o->newline()
    << parent->parent->c_typename (e->type)
    << " __tmp" << parent->tmpvar_counter ++ << ";";
  // res
  parent->parent->o->newline()
    << parent->parent->c_typename (e->type)
    << " __tmp" << parent->tmpvar_counter ++ << ";";

  if (rvalue)
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

  if (r->index_types.size() != 0)
    throw semantic_error ("unexpected reference to array", e->tok);

  unsigned tmpidx = parent->tmpvar_counter ++;
  unsigned residx = parent->tmpvar_counter ++;
  string tmp_base = "l->__tmp";
  string tmpvar = tmp_base + stringify (tmpidx);
  string resvar = tmp_base + stringify (residx);
  o->line() << "({ ";
  o->indent(1);

  // part 1: "tmp = B"
  if (rvalue)
    parent->c_assign (tmpvar, rvalue, "assignment");
  else
    {
      if (op == "++" || op == "--")
        o->newline() << tmpvar << " = 1;";
      else
        // internal error
        throw semantic_error ("need rvalue for assignment", e->tok);
    }
  // OPT: literal rvalues could be used without a tmp* copy

  // part 2: "check (B)"
  if (op == "/=" || op == "%=") 
    {
      // need division-by-zero check
      o->newline() << "if (" << tmpvar << " == 0) {";
      o->newline(1) << "errorcount ++;";
      o->newline() << tmpvar << " = 1;";
      o->newline(-1) << "}";
    }

  // maybe the variable is a local
  string lvaluename;
  bool lock_global = false;

  if (current_probe)
    for (unsigned i=0; i<current_probe->locals.size(); i++)
      if (current_probe->locals[i] == r)
        lvaluename = "l->" + parent->c_varname (r->name);
  if (current_function)
    {
      for (unsigned i=0; i<current_function->locals.size(); i++)
        if (current_function->locals[i] == r)
          lvaluename = "l->" + parent->c_varname (r->name);
      
      for (unsigned i=0; i<current_function->formal_args.size(); i++)
        if (current_function->formal_args[i] == r)
          lvaluename = "l->" + parent->c_varname (r->name);
    }
  if (lvaluename == "")
    for (unsigned i=0; i<session->globals.size(); i++)
      if (session->globals[i] == r)
        {
          lvaluename = "global_" + parent->c_varname (r->name);
          lock_global = true;
        }

  if (lvaluename == "")
    throw semantic_error ("unresolved assignment to ", e->tok);

  // part 3: "lock(A)"
  if (lock_global)
    o->newline() << "/* XXX lock " << lvaluename << " */";

  // part 4/5
  if (e->type == pe_string)
    {
      if (pre)
        throw semantic_error ("pre assignment on strings not supported", 
                              e->tok);
      if (op == "=")
        {
          o->newline() << "strncpy (" << lvaluename
                       << ", " << tmpvar
                       << ", MAXSTRINGLEN);";
          // no need for second copy
          resvar = tmpvar;
        }
      else if (op == ".=")
        {
          // shortcut two-step construction of concatenated string in
          // empty resvar, then copy to tmpvar: instead concatenate
          // to lvalue directly, then copy back to resvar
          o->newline() << "strncat (" << lvaluename
                       << ", " << tmpvar
                       << ", MAXSTRINGLEN);";
          o->newline() << "strncpy (" << resvar
                       << ", " << lvaluename
                       << ", MAXSTRINGLEN);";
        }
      else
        throw semantic_error ("string assignment operator " +
                              op + " unsupported", e->tok);
    }
  else if (e->type == pe_long)
    {
      // a lot of operators come through this "gate":
      // - vanilla assignment "="
      // - stats aggregation "<<<"
      // - modify-accumulate "+=" and many friends
      // - pre/post-crement "++"/"--"

      // compute the modify portion of a modify-accumulate
      string macop;
      unsigned oplen = op.size();
      if (op == "=")
        macop = "* 0 +"; // clever (?) trick to select rvalue (tmp) only
      else if (oplen > 1 && op[oplen-1] == '=') // for +=, %=, <<=, etc...
        macop = op.substr(0, oplen-1);
      else if (op == "<<<")
        throw semantic_error ("stats aggregation not yet implemented", e->tok);
      else if (op == "++")
        macop = "+";
      else if (op == "--")
        macop = "-";
      else
        // internal error
        throw semantic_error ("unknown macop for assignment", e->tok);

      // part 4
      if (pre)
        o->newline() << resvar << " = " << lvaluename << ";";
      else
        o->newline() << resvar << " = "
                     << lvaluename << " " << macop << " " << tmpvar << ";";

      // part 5
      if (pre)
        o->newline() << lvaluename << " = "
                     << resvar << " " << macop << " " << tmpvar << ";";
      else
        o->newline() << lvaluename << " = " << resvar << ";";        
    }
  else
    throw semantic_error ("assignment type not yet implemented", e->tok);

  // part 6: "unlock(A)"
  if (lock_global)
    o->newline() << "/* XXX unlock " << lvaluename << " */";

  // part 7: "res"
  o->newline() << resvar << ";";
  o->newline(-1) << "})";
}


void
c_tmpcounter::visit_arrayindex (arrayindex *e)
{
  vardecl* r = e->referent;
  // one temporary per index dimension
  for (unsigned i=0; i<r->index_types.size(); i++)
    parent->o->newline()
      << parent->c_typename (r->index_types[i])
      << " __tmp" << tmpvar_counter ++ << ";";
  // now the result
  parent->o->newline()
    << parent->c_typename (r->type)
    << " __tmp" << tmpvar_counter ++ << ";";

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

  throw semantic_error ("array read not supported", e->tok);

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

  o->newline() << "if (errorcount) goto out;";

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
  throw semantic_error ("array write not supported", e->tok);
}


void
c_tmpcounter::visit_functioncall (functioncall *e)
{
  functiondecl* r = e->referent;
  // one temporary per argument
  for (unsigned i=0; i<r->formal_args.size(); i++)
    parent->o->newline()
      << parent->c_typename (r->formal_args[i]->type)
      << " __tmp" << tmpvar_counter ++ << ";";

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

  // compute actual arguments
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

  o->newline();
  o->newline() << "if (c->nesting+2 >= MAXNESTING) errorcount ++;";
  o->newline() << "c->actioncount ++;";
  o->newline() << "if (c->actioncount > MAXACTION) errorcount ++;";
  o->newline() << "if (errorcount) goto out;";
  o->newline();

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

  s.op = new translator_output (s.translated_source);
  c_unparser cup (& s);
  s.up = & cup;

  try
    {
      s.op->line() << "#define TEST_MODE " << (s.test_mode ? 1 : 0)
                   << endl;
      
      // XXX: until the runtime can handle user-level tests properly
      s.op->newline() << "#if ! TEST_MODE";
      s.op->newline() << "#define STP_NETLINK_ONLY"; // XXX
      s.op->newline() << "#include \"runtime.h\"";
      s.op->newline() << "#endif" << endl;

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

      s.up->emit_module_init ();
      s.up->emit_module_exit ();

      s.op->newline();
      s.op->newline() << "#if TEST_MODE";

      s.op->newline() << "/* test mode mainline */";
      s.op->newline() << "int main () {";
      s.op->newline(1) << "int rc = systemtap_module_init ();";
      s.op->newline() << "if (!rc) rc = systemtap_module_exit ();";
      s.op->newline() << "return rc;";
      s.op->newline(-1) << "}";

      s.op->newline() << "#else";

      s.op->newline();
      s.op->newline() << "static int __init _systemtap_module_init (void);";
      s.op->newline() << "int _systemtap_module_init () {";
      s.op->newline(1) << "return systemtap_module_init ();";
      s.op->newline(-1) << "}";

      s.op->newline();
      s.op->newline() << "static void __exit _systemtap_module_exit (void);";
      s.op->newline() << "void _systemtap_module_exit () {";
      s.op->newline(1) << "systemtap_module_exit ();";
      s.op->newline(-1) << "}";

      s.op->newline();
      s.op->newline() << "module_init (_systemtap_module_init);";
      s.op->newline() << "module_exit (_systemtap_module_exit);";
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
