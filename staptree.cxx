// parse tree functions
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "parse.h"
#include <iostream>
#include <typeinfo>
#include <cassert>

using namespace std;


expression::expression ():
  type (pe_unknown), tok (0)
{
} 


expression::~expression ()
{
}


statement::statement ():
  tok (0)
{
} 


statement::~statement ()
{
}


symbol::symbol ():
  referent (0)
{
}


arrayindex::arrayindex ():
  referent (0)
{
}


functioncall::functioncall ():
  referent (0)
{
}


symboldecl::symboldecl ():
  tok (0),
  type (pe_unknown)
{
} 


symboldecl::~symboldecl ()
{
}


probe_point::probe_point ():
  tok (0)
{
}


probe::probe ():
  body (0), tok (0)
{
}


probe_point::component::component ():
  arg (0)
{
}


vardecl::vardecl ():
  arity (-1)
{
}


void
vardecl::set_arity (int a)
{
  assert (a >= 0);

  if (arity != a && arity >= 0)
    throw semantic_error ("inconsistent arity", tok);

  if (arity != a)
    {
      arity = a;
      index_types.resize (arity);
      for (int i=0; i<arity; i++)
	index_types[i] = pe_unknown;
    }
}

bool 
vardecl::compatible_arity (int a)
{
  if (arity == -1)
    return true;
  return arity == a;
}


functiondecl::functiondecl ():
  body (0)
{
}


literal_number::literal_number (long v)
{
  value = v;
  type = pe_long;
}


literal_string::literal_string (const string& v)
{
  value = v;
  type = pe_string;
}


ostream&
operator << (ostream& o, const exp_type& e)
{
  switch (e)
    {
    case pe_unknown: o << "unknown"; break;
    case pe_long: o << "long"; break;
    case pe_string: o << "string"; break;
    case pe_stats: o << "stats"; break;
    default: o << "???"; break;
    }
  return o;
}


// ------------------------------------------------------------------------
// parse tree printing

ostream& operator << (ostream& o, expression& k)
{
  k.print (o);
  return o;
}


void literal_string::print (ostream& o)
{
  // XXX: quote special chars
  o << '"' << value << '"';
}


void literal_number::print (ostream& o)
{
  o << value;
}


void binary_expression::print (ostream& o)
{
  o << '(' << *left << ")" 
    << op 
    << '(' << *right << ")";
}


void unary_expression::print (ostream& o)
{
  o << op << '(' << *operand << ")"; 
}

void array_in::print (ostream& o)
{
  o << "[";
  for (unsigned i=0; i<operand->indexes.size(); i++)
    {
      if (i > 0) o << ", ";
      operand->indexes[i]->print (o);
    }
  o << "] in " << operand->base;
}

void post_crement::print (ostream& o)
{
  o << '(' << *operand << ")" << op; 
}


void ternary_expression::print (ostream& o)
{
  o << "(" << *cond << ") ? ("
    << *truevalue << ") : ("
    << *falsevalue << ")"; 
}


void symbol::print (ostream& o)
{
  o << name;
}


void vardecl::print (ostream& o)
{
  o << name;
  if (arity > 0 || index_types.size() > 0)
    o << "[...]";
}


void vardecl::printsig (ostream& o)
{
  o << name << ":" << type;
  if (index_types.size() > 0)
    {
      o << " [";
      for (unsigned i=0; i<index_types.size(); i++)
        o << (i>0 ? ", " : "") << index_types[i];
      o << "]";
    }
}


void functiondecl::print (ostream& o)
{
  o << "function " << name << " (";
  for (unsigned i=0; i<formal_args.size(); i++)
    o << (i>0 ? ", " : "") << *formal_args[i];
  o << ")" << endl;
  body->print(o);
}


void functiondecl::printsig (ostream& o)
{
  o << name << ":" << type << " (";
  for (unsigned i=0; i<formal_args.size(); i++)
    o << (i>0 ? ", " : "")
      << *formal_args[i]
      << ":"
      << formal_args[i]->type;
  o << ")";
}


void arrayindex::print (ostream& o)
{
  o << base << "[";
  for (unsigned i=0; i<indexes.size(); i++)
    o << (i>0 ? ", " : "") << *indexes[i];
  o << "]";
}


void functioncall::print (ostream& o)
{
  o << function << "(";
  for (unsigned i=0; i<args.size(); i++)
    o << (i>0 ? ", " : "") << *args[i];
  o << ")";
}  


ostream& operator << (ostream& o, statement& k)
{
  k.print (o);
  return o;
}


void embeddedcode::print (ostream &o)
{
  o << "%{";
  o << code;
  o << "%}";
}

void block::print (ostream& o)
{
  o << "{" << endl;
  for (unsigned i=0; i<statements.size(); i++)
    o << *statements [i] << endl;
  o << "}";
}


void for_loop::print (ostream& o)
{
  o << "for (";
  init->print (o);
  o << "; ";
  cond->print (o);
  o << "; ";
  incr->print (o);
  o << ") ";
  block->print (o);
}


void foreach_loop::print (ostream& o)
{
  o << "foreach ([";
  for (unsigned i=0; i<indexes.size(); i++)
    {
      if (i > 0) o << ", ";
      indexes[i]->print (o);
    }
  o << "] in " << base << ") ";
  block->print (o);
}


void null_statement::print (ostream& o)
{
  o << ";"; 
}


void expr_statement::print (ostream& o)
{
  o << *value;
}


void return_statement::print (ostream& o)
{
  o << "return " << *value;
}


void delete_statement::print (ostream& o)
{
  o << "delete " << *value;
}

void next_statement::print (ostream& o)
{
  o << "next";
}

void break_statement::print (ostream& o)
{
  o << "break";
}

void continue_statement::print (ostream& o)
{
  o << "continue";
}

void if_statement::print (ostream& o)
{
  o << "if (" << *condition << ") "
    << *thenblock << endl;
  if (elseblock)
    o << "else " << *elseblock << endl;
}


void stapfile::print (ostream& o)
{
  o << "# file " << name << endl;

  for (unsigned i=0; i<embeds.size(); i++)
    embeds[i]->print (o);

  for (unsigned i=0; i<globals.size(); i++)
    {
      o << "global ";
      globals[i]->print (o);
      o << endl;
    }

  for (unsigned i=0; i<aliases.size(); i++)
    {
      aliases[i]->print (o);
      o << endl;
    }

  for (unsigned i=0; i<probes.size(); i++)
    {
      probes[i]->print (o);
      o << endl;
    }

  for (unsigned j = 0; j < functions.size(); j++)
    {
      functions[j]->print (o);
      o << endl;
    }
}


void probe::print (ostream& o)
{
  o << "probe ";
  printsig (o);
  o << *body;
}


void probe::printsig (ostream& o)
{
  for (unsigned i=0; i<locations.size(); i++)
    {
      o << (i>0 ? ", " : "");
      locations[i]->print (o);
    }
}


void probe_point::print (ostream& o)
{
  for (unsigned i=0; i<components.size(); i++)
    {
      if (i>0) o << ".";
      probe_point::component* c = components[i];
      o << c->functor;
      if (c->arg)
        o << "(" << *c->arg << ")";
    }
}

probe_alias::probe_alias(std::vector<probe_point*> const & aliases):
  probe (), alias_names (aliases)
{
}

void probe_alias::printsig (ostream& o)
{
  for (unsigned i=0; i<alias_names.size(); i++)
    {
      o << (i>0 ? " = " : "");
      alias_names[i]->print (o);
    }
  o << " = ";
  for (unsigned i=0; i<locations.size(); i++)
    {
      o << (i>0 ? ", " : "");
      locations[i]->print (o);
    }
}


ostream& operator << (ostream& o, probe_point& k)
{
  k.print (o);
  return o;
}


ostream& operator << (ostream& o, symboldecl& k)
{
  k.print (o);
  return o;
}



// ------------------------------------------------------------------------
// visitors


void
block::visit (visitor* u)
{
  u->visit_block (this);
}


void
embeddedcode::visit (visitor* u)
{
  u->visit_embeddedcode (this);
}


void
for_loop::visit (visitor* u)
{
  u->visit_for_loop (this);
}

void
foreach_loop::visit (visitor* u)
{
  u->visit_foreach_loop (this);
}

void
null_statement::visit (visitor* u)
{
  u->visit_null_statement (this);
}

void
expr_statement::visit (visitor* u)
{
  u->visit_expr_statement (this);
}

void
return_statement::visit (visitor* u)
{
  u->visit_return_statement (this);
}

void
delete_statement::visit (visitor* u)
{
  u->visit_delete_statement (this);
}

void
if_statement::visit (visitor* u)
{
  u->visit_if_statement (this);
}

void
next_statement::visit (visitor* u)
{
  u->visit_next_statement (this);
}

void
break_statement::visit (visitor* u)
{
  u->visit_break_statement (this);
}

void
continue_statement::visit (visitor* u)
{
  u->visit_continue_statement (this);
}

void
literal_string::visit(visitor* u)
{
  u->visit_literal_string (this);
}

void
literal_number::visit(visitor* u)
{
  u->visit_literal_number (this);
}

void
binary_expression::visit (visitor* u)
{
  u->visit_binary_expression (this);
}

void
unary_expression::visit (visitor* u)
{
  u->visit_unary_expression (this);
}

void
pre_crement::visit (visitor* u)
{
  u->visit_pre_crement (this);
}

void
post_crement::visit (visitor* u)
{
  u->visit_post_crement (this);
}

void
logical_or_expr::visit (visitor* u)
{
  u->visit_logical_or_expr (this);
}

void
logical_and_expr::visit (visitor* u)
{
  u->visit_logical_and_expr (this);
}

void
array_in::visit (visitor* u)
{
  u->visit_array_in (this);
}

void
comparison::visit (visitor* u)
{
  u->visit_comparison (this);
}

void
concatenation::visit (visitor* u)
{
  u->visit_concatenation (this);
}

void
ternary_expression::visit (visitor* u)
{
  u->visit_ternary_expression (this);
}

void
assignment::visit (visitor* u)
{
  u->visit_assignment (this);
}

void
symbol::visit (visitor* u)
{
  u->visit_symbol (this);
}

void
arrayindex::visit (visitor* u)
{
  u->visit_arrayindex (this);
}

void
functioncall::visit (visitor* u)
{
  u->visit_functioncall (this);
}


// ------------------------------------------------------------------------

void
traversing_visitor::visit_block (block* s)
{
  for (unsigned i=0; i<s->statements.size(); i++)
    s->statements[i]->visit (this);
}

void
traversing_visitor::visit_embeddedcode (embeddedcode* s)
{
}

void
traversing_visitor::visit_null_statement (null_statement* s)
{
}

void
traversing_visitor::visit_expr_statement (expr_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_if_statement (if_statement* s)
{
  s->condition->visit (this);
  s->thenblock->visit (this);
  if (s->elseblock)
    s->elseblock->visit (this);
}

void
traversing_visitor::visit_for_loop (for_loop* s)
{
  s->init->visit (this);
  s->cond->visit (this);
  s->incr->visit (this);
  s->block->visit (this);
}

void
traversing_visitor::visit_foreach_loop (foreach_loop* s)
{
  for (unsigned i=0; i<s->indexes.size(); i++)
    s->indexes[i]->visit (this);
  s->block->visit (this);
}

void
traversing_visitor::visit_return_statement (return_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_delete_statement (delete_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_next_statement (next_statement* s)
{
}

void
traversing_visitor::visit_break_statement (break_statement* s)
{
}

void
traversing_visitor::visit_continue_statement (continue_statement* s)
{
}

void
traversing_visitor::visit_literal_string (literal_string* e)
{
}

void
traversing_visitor::visit_literal_number (literal_number* e)
{
}

void
traversing_visitor::visit_binary_expression (binary_expression* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_unary_expression (unary_expression* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_pre_crement (pre_crement* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_post_crement (post_crement* e)
{
  e->operand->visit (this);
}


void
traversing_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_array_in (array_in* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_comparison (comparison* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_concatenation (concatenation* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_ternary_expression (ternary_expression* e)
{
  e->cond->visit (this);
  e->truevalue->visit (this);
  e->falsevalue->visit (this);
}

void
traversing_visitor::visit_assignment (assignment* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_symbol (symbol* e)
{
}

void
traversing_visitor::visit_arrayindex (arrayindex* e)
{
  for (unsigned i=0; i<e->indexes.size(); i++)
    e->indexes[i]->visit (this);
}

void
traversing_visitor::visit_functioncall (functioncall* e)
{
  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);
}


// ------------------------------------------------------------------------


throwing_visitor::throwing_visitor (const std::string& m): msg (m) {}
throwing_visitor::throwing_visitor (): msg ("invalid element") {}


void
throwing_visitor::throwone (const token* t)
{
  throw semantic_error (msg, t);
}

void
throwing_visitor::visit_block (block* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_embeddedcode (embeddedcode* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_null_statement (null_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_expr_statement (expr_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_if_statement (if_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_for_loop (for_loop* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_foreach_loop (foreach_loop* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_return_statement (return_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_delete_statement (delete_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_next_statement (next_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_break_statement (break_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_continue_statement (continue_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_literal_string (literal_string* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_literal_number (literal_number* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_binary_expression (binary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_unary_expression (unary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_pre_crement (pre_crement* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_post_crement (post_crement* e)
{
  throwone (e->tok);
}


void
throwing_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_array_in (array_in* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_comparison (comparison* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_concatenation (concatenation* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_ternary_expression (ternary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_assignment (assignment* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_symbol (symbol* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_arrayindex (arrayindex* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_functioncall (functioncall* e)
{
  throwone (e->tok);
}


// ------------------------------------------------------------------------

template <typename T> static void
require (deep_copy_visitor* v, T* dst, T src)
{
  *dst = NULL;
  if (src != NULL)
    {
      v->targets.push(static_cast<void* >(dst));
      src->visit(v);
      v->targets.pop();
      assert(*dst);
    }
}

template <typename T> static void
provide (deep_copy_visitor* v, T src)
{
  assert(!v->targets.empty());
  *(static_cast<T*>(v->targets.top())) = src;
}

void
deep_copy_visitor::visit_block (block* s)
{
  block* n = new block;
  n->tok = s->tok;
  for (unsigned i = 0; i < s->statements.size(); ++i)
    {
      statement* ns;
      require <statement*> (this, &ns, s->statements[i]);
      n->statements.push_back(ns);
    }
  provide <block*> (this, n);
}

void
deep_copy_visitor::visit_embeddedcode (embeddedcode* s)
{
  embeddedcode* n = new embeddedcode;
  n->tok = s->tok;
  n->code = s->code;
  provide <embeddedcode*> (this, n);
}

void
deep_copy_visitor::visit_null_statement (null_statement* s)
{
  null_statement* n = new null_statement;
  n->tok = s->tok;
  provide <null_statement*> (this, n);
}

void
deep_copy_visitor::visit_expr_statement (expr_statement* s)
{
  expr_statement* n = new expr_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->value), s->value);
  provide <expr_statement*> (this, n);
}

void
deep_copy_visitor::visit_if_statement (if_statement* s)
{
  if_statement* n = new if_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->condition), s->condition);
  require <statement*> (this, &(n->thenblock), s->thenblock);
  require <statement*> (this, &(n->elseblock), s->elseblock);
  provide <if_statement*> (this, n);
}

void
deep_copy_visitor::visit_for_loop (for_loop* s)
{
  for_loop* n = new for_loop;
  n->tok = s->tok;
  require <expr_statement*> (this, &(n->init), s->init);
  require <expression*> (this, &(n->cond), s->cond);
  require <expr_statement*> (this, &(n->incr), s->incr);
  require <statement*> (this, &(n->block), s->block);  
  provide <for_loop*> (this, n);
}

void
deep_copy_visitor::visit_foreach_loop (foreach_loop* s)
{
  foreach_loop* n = new foreach_loop;
  n->tok = s->tok;
  for (unsigned i = 0; i < s->indexes.size(); ++i)
    {
      symbol* sym;
      require <symbol*> (this, &sym, s->indexes[i]);
      n->indexes.push_back(sym);
    }
  n->base = s->base;
  n->base_referent = NULL;
  require <statement*> (this, &(n->block), s->block);
  provide <foreach_loop*> (this, n);
}

void
deep_copy_visitor::visit_return_statement (return_statement* s)
{
  return_statement* n = new return_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->value), s->value);
  provide <return_statement*> (this, n);
}

void
deep_copy_visitor::visit_delete_statement (delete_statement* s)
{
  delete_statement* n = new delete_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->value), s->value);
  provide <delete_statement*> (this, n);
}

void
deep_copy_visitor::visit_next_statement (next_statement* s)
{
  next_statement* n = new next_statement;
  n->tok = s->tok;
  provide <next_statement*> (this, n);
}

void
deep_copy_visitor::visit_break_statement (break_statement* s)
{
  break_statement* n = new break_statement;
  n->tok = s->tok;
  provide <break_statement*> (this, n);
}

void
deep_copy_visitor::visit_continue_statement (continue_statement* s)
{
  continue_statement* n = new continue_statement;
  n->tok = s->tok;
  provide <continue_statement*> (this, n);
}

void
deep_copy_visitor::visit_literal_string (literal_string* e)
{
  literal_string* n = new literal_string(e->value);
  n->tok = e->tok;
  provide <literal_string*> (this, n);
}

void
deep_copy_visitor::visit_literal_number (literal_number* e)
{
  literal_number* n = new literal_number(e->value);
  n->tok = e->tok;
  provide <literal_number*> (this, n);
}

void
deep_copy_visitor::visit_binary_expression (binary_expression* e)
{
  binary_expression* n = new binary_expression;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <binary_expression*> (this, n);
}

void
deep_copy_visitor::visit_unary_expression (unary_expression* e)
{
  unary_expression* n = new unary_expression;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->operand), e->operand);
  provide <unary_expression*> (this, n);
}

void
deep_copy_visitor::visit_pre_crement (pre_crement* e)
{
  pre_crement* n = new pre_crement;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->operand), e->operand);
  provide <pre_crement*> (this, n);
}

void
deep_copy_visitor::visit_post_crement (post_crement* e)
{
  post_crement* n = new post_crement;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->operand), e->operand);
  provide <post_crement*> (this, n);
}


void
deep_copy_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  logical_or_expr* n = new logical_or_expr;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <logical_or_expr*> (this, n);
}

void
deep_copy_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  logical_and_expr* n = new logical_and_expr;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <logical_and_expr*> (this, n);
}

void
deep_copy_visitor::visit_array_in (array_in* e)
{
  array_in* n = new array_in;
  n->tok = e->tok;
  require <arrayindex*> (this, &(n->operand), e->operand);
  provide <array_in*> (this, n);
}

void
deep_copy_visitor::visit_comparison (comparison* e)
{
  comparison* n = new comparison;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <comparison*> (this, n);
}

void
deep_copy_visitor::visit_concatenation (concatenation* e)
{
  concatenation* n = new concatenation;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <concatenation*> (this, n);
}

void
deep_copy_visitor::visit_ternary_expression (ternary_expression* e)
{
  ternary_expression* n = new ternary_expression;
  n->tok = e->tok;
  require <expression*> (this, &(n->cond), e->cond);
  require <expression*> (this, &(n->truevalue), e->truevalue);
  require <expression*> (this, &(n->falsevalue), e->falsevalue);
  provide <ternary_expression*> (this, n);
}

void
deep_copy_visitor::visit_assignment (assignment* e)
{
  assignment* n = new assignment;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <assignment*> (this, n);
}

void
deep_copy_visitor::visit_symbol (symbol* e)
{
  symbol* n = new symbol;
  n->tok = e->tok;
  n->name = e->name;
  n->referent = NULL;
  provide <symbol*> (this, n);
}

void
deep_copy_visitor::visit_arrayindex (arrayindex* e)
{
  arrayindex* n = new arrayindex;
  n->tok = e->tok;
  n->base = e->base;
  n->referent = NULL;
  for (unsigned i = 0; i < e->indexes.size(); ++i)
    {
      expression* ne;
      require <expression*> (this, &ne, e->indexes[i]);
      n->indexes.push_back(ne);
    }
  provide <arrayindex*> (this, n);
}

void
deep_copy_visitor::visit_functioncall (functioncall* e)
{
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = e->function;
  n->referent = NULL;
  for (unsigned i = 0; i < e->args.size(); ++i)
    {
      expression* na;
      require <expression*> (this, &na, e->args[i]);
      n->args.push_back(na);
    }
  provide <functioncall*> (this, n);
}

block* 
deep_copy_visitor::deep_copy (block* b)
{
  block* n;
  deep_copy_visitor v;
  require <block*> (&v, &n, b);
  return n;
}

statement* 
deep_copy_visitor::deep_copy (statement* s)
{
  statement* n;
  deep_copy_visitor v;
  require <statement*> (&v, &n, s);
  return n;
}
