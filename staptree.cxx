// parse tree functions
// Copyright 2005 Red Hat Inc.
// GPL

#include "staptree.h"
#include "parse.h"
#include <iostream>
#include <typeinfo>
#include <cassert>


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


vardecl::vardecl ()
{
}


vardecl::vardecl (unsigned arity)
{
  index_types.resize (arity);
  for (unsigned i=0; i<arity; i++)
    index_types[i] = pe_unknown;
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
  if (index_types.size() > 0)
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


void block::print (ostream& o)
{
  o << "{" << endl;
  for (unsigned i=0; i<statements.size(); i++)
    o << *statements [i] << ";" << endl;
  o << "}" << endl;
}


void for_loop::print (ostream& o)
{
  o << "<for_loop>" << endl;
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


void if_statement::print (ostream& o)
{
  o << "if (" << *condition << ") " << endl
    << *thenblock << endl;
  if (elseblock)
    o << "else " << *elseblock << endl;
}


void stapfile::print (ostream& o)
{
  o << "# file " << name << endl;

  for(unsigned i=0; i<probes.size(); i++)
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
  o << endl;
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
// semantic processing: symbol resolution


symresolution_info::symresolution_info (vector<vardecl*>& l,
                                        vector<vardecl*>& g,
                                        vector<functiondecl*>& f):
  locals (l), globals (g), functions (f), current_function (0)
{
  num_unresolved = 0;
}


symresolution_info::symresolution_info (vector<vardecl*>& l,
                                        vector<vardecl*>& g,
                                        vector<functiondecl*>& f,
                                        functiondecl* cf):
  locals (l), globals (g), functions (f), current_function (cf)
{
  num_unresolved = 0;
}


void
literal::resolve_symbols (symresolution_info& r)
{
}


void
binary_expression::resolve_symbols (symresolution_info& r)
{
  left->resolve_symbols (r);
  right->resolve_symbols (r);
}


void
unary_expression::resolve_symbols (symresolution_info& r)
{
  operand->resolve_symbols (r);
}


void
ternary_expression::resolve_symbols (symresolution_info& r)
{
  cond->resolve_symbols (r);
  truevalue->resolve_symbols (r);
  falsevalue->resolve_symbols (r);
}


void
symbol::resolve_symbols (symresolution_info& r)
{
  if (referent)
    return;

  vardecl* d = r.find_scalar (name);
  if (d)
    referent = d;
  else
    {
      // new local
      vardecl* v = new vardecl;
      v->name = name;
      v->tok = tok;
      r.locals.push_back (v);
      referent = v;
    }
}


void
arrayindex::resolve_symbols (symresolution_info& r)
{
  for (unsigned i=0; i<indexes.size(); i++)
    indexes[i]->resolve_symbols (r);

  if (referent)
    return;

  vardecl* d = r.find_array (base, indexes);
  if (d)
    referent = d;
  else
    {
      // new local
      vardecl* v = new vardecl (indexes.size());
      v->name = base;
      v->tok = tok;
      r.locals.push_back (v);
      referent = v;
    }
}


void
functioncall::resolve_symbols (symresolution_info& r)
{
  for (unsigned i=0; i<args.size(); i++)
    args[i]->resolve_symbols (r);

  if (referent)
    return;

  functiondecl* d = r.find_function (function, args);
  if (d)
    referent = d;
  else
    r.unresolved (tok);
}


void
block::resolve_symbols (symresolution_info& r)
{
  for (unsigned i=0; i<statements.size(); i++)
    statements[i]->resolve_symbols (r);
}


void
if_statement::resolve_symbols (symresolution_info& r)
{
  condition->resolve_symbols (r);
  thenblock->resolve_symbols (r);
  elseblock->resolve_symbols (r);
}


void
for_loop::resolve_symbols (symresolution_info& r)
{
  init->resolve_symbols (r);
  cond->resolve_symbols (r);
  incr->resolve_symbols (r);
  block->resolve_symbols (r);
}


void
expr_statement::resolve_symbols (symresolution_info& r)
{
  value->resolve_symbols (r);
}


vardecl* 
symresolution_info::find_scalar (const string& name)
{
  // search locals
  for (unsigned i=0; i<locals.size(); i++)
    if (locals[i]->name == name)
      return locals[i];

  // search builtins that become locals
  // XXX: need to invent a proper formalism for this
  if (name == "$pid" || name == "$tid")
    {
      vardecl_builtin* vb = new vardecl_builtin;
      vb->name = name;
      vb->type = pe_long;

      // XXX: need a better way to synthesize tokens
      token* t = new token;
      t->type = tok_identifier;
      t->content = name;
      t->location.file = "<builtin>";
      vb->tok = t;

      locals.push_back (vb);
      return vb;
    }

  // search function formal parameters (if any)
  if (current_function)
    {
      for (unsigned i=0; i<current_function->formal_args.size(); i++)
        if (current_function->formal_args [i]->name == name)
          return current_function->formal_args [i];
    }

  // search globals
  for (unsigned i=0; i<globals.size(); i++)
    if (globals[i]->name == name)
      return globals[i];

  return 0;
  // XXX: add checking for conflicting array or function
}


vardecl* 
symresolution_info::find_array (const string& name, 
                                const vector<expression*>& indexes)
{
  // search locals
  for (unsigned i=0; i<locals.size(); i++)
    if (locals[i]->name == name)
      return locals[i];

  // search function formal parameters (if any)
  if (current_function)
    {
      for (unsigned i=0; i<current_function->formal_args.size(); i++)
        if (current_function->formal_args [i]->name == name)
          return current_function->formal_args [i];
    }

  // search globals
  for (unsigned i=0; i<globals.size(); i++)
    if (globals[i]->name == name)
      return globals[i];

  return 0;
  // XXX: add checking for conflicting scalar or function
}


functiondecl* 
symresolution_info::find_function (const string& name,
                                   const vector<expression*>& args)
{
  for (unsigned j = 0; j < functions.size(); j++)
    {
      functiondecl* fd = functions[j];
      if (fd->name == name)
        return fd;
    }

  return 0;
  // XXX: add checking for conflicting variables
}



void
symresolution_info::unresolved (const token* tok)
{
  num_unresolved ++;

  cerr << "error: unresolved symbol for ";
  if (tok)
    cerr << *tok;
  else
    cerr << "a token";
  cerr << endl;
}


// ------------------------------------------------------------------------
// semantic processing: type resolution


void
literal::resolve_types (typeresolution_info& r, exp_type t)
{
  assert (type == pe_long || type == pe_string);
  if ((t == type) || (t == pe_unknown))
    return;

  r.mismatch (tok, type, t);
}


void
binary_expression::resolve_types (typeresolution_info& r, exp_type t)
{
  if (op == "<<<") // stats aggregation
    {
      left->resolve_types (r, pe_stats);
      right->resolve_types (r, pe_long);
      if (t == pe_stats || t == pe_string)
        r.mismatch (tok, t, pe_long);
      else if (type == pe_unknown)
        {
          type = pe_long;
          r.resolved (tok, type);
        }
    }
  else if (op == ".") // string concatenation
    {
      left->resolve_types (r, pe_string);
      right->resolve_types (r, pe_string);
      if (t == pe_long || t == pe_stats)
        r.mismatch (tok, t, pe_string);
      else if (type == pe_unknown)
        {
          type = pe_string;
          r.resolved (tok, type);
        }
    }
  else if (op == "=="
           || op == "in" // XXX: really a unary operator
           || false) // XXX: other comparison operators
    {
      left->resolve_types (r, pe_unknown);
      right->resolve_types (r, pe_unknown);
      if (t == pe_string || t == pe_stats)
        r.mismatch (tok, t, pe_long);
      else if (type == pe_unknown)
        {
          type = pe_long;
          r.resolved (tok, type);
        }
    }
  else // general arithmetic operators?
    {
      // propagate type downward 
      exp_type subtype = t;
      if ((t == pe_unknown) && (type != pe_unknown))
        subtype = type;
      left->resolve_types (r, subtype);
      right->resolve_types (r, subtype);

      if ((t == pe_unknown) && (type != pe_unknown))
        ; // already resolved
      else if ((t != pe_unknown) && (type == pe_unknown))
        {
          type = t;
          r.resolved (tok, type);
        }
      else if ((t == pe_unknown) && (left->type != pe_unknown))
        {
          type = left->type;
          r.resolved (tok, type);
        }
      else if ((t == pe_unknown) && (right->type != pe_unknown))
        {
          type = right->type;
          r.resolved (tok, type);
        }
      else if (type != t)
        r.mismatch (tok, t, type);
    }
}


void
unary_expression::resolve_types (typeresolution_info& r, exp_type t)
{
  // all unary operators only work on numerics

  operand->resolve_types (r, pe_long);

  if (t == pe_unknown && type != pe_unknown)
    ; // already resolved
  else if (t == pe_string || t == pe_stats)
    r.mismatch (tok, t, pe_long);
  else if (type == pe_unknown)
    {
      type = pe_long;
      r.resolved (tok, type);
    }
}


void
ternary_expression::resolve_types (typeresolution_info& r, exp_type t)
{
  cond->resolve_types (r, pe_long);
  truevalue->resolve_types (r, t);
  falsevalue->resolve_types (r, t);
}


template <class Referrer, class Referent>
void resolve_2types (Referrer* referrer, Referent* referent,
                    typeresolution_info& r, exp_type t)
{
  exp_type& rtype = referrer->type;
  const token* rtok = referrer->tok;
  exp_type& ttype = referent->type;
  const token* ttok = referent->tok;

  if (t != pe_unknown && rtype == t && rtype == ttype)
    ; // do nothing: all three types in agreement
  else if (t == pe_unknown && rtype != pe_unknown && rtype == ttype)
    ; // do nothing: two known types in agreement
  else if (rtype != pe_unknown && ttype != pe_unknown && rtype != ttype)
    r.mismatch (rtok, rtype, ttype);
  else if (rtype != pe_unknown && t != pe_unknown && rtype != t)
    r.mismatch (rtok, rtype, t);
  else if (ttype != pe_unknown && t != pe_unknown && ttype != t)
    r.mismatch (ttok, ttype, t);
  else if (rtype == pe_unknown && t != pe_unknown)
    {
      // propagate from upstream
      rtype = t;
      r.resolved (rtok, rtype);
      // catch rtype/ttype mismatch later
    }
  else if (rtype == pe_unknown && ttype != pe_unknown)
    {
      // propagate from referent
      rtype = ttype;
      r.resolved (rtok, rtype);
      // catch rtype/t mismatch later
    }
  else if (rtype != pe_unknown && ttype == pe_unknown)
    {
      // propagate to referent
      ttype = rtype;
      r.resolved (ttok, ttype);
      // catch rtype/t mismatch later
    }
  else
    r.unresolved (rtok);
}


void
symbol::resolve_types (typeresolution_info& r, exp_type t)
{
  assert (referent != 0);

  if (referent->index_types.size() > 0)
    r.unresolved (tok); // array
  else
    resolve_2types (this, referent, r, t);
}


void
arrayindex::resolve_types (typeresolution_info& r, exp_type t)
{
  assert (referent != 0);

  resolve_2types (this, referent, r, t);

  // now resolve the array indexes
  if (referent->index_types.size() == 0)
    {
      // designate referent as array
      referent->index_types.resize (indexes.size());
      for (unsigned i=0; i<indexes.size(); i++)
        referent->index_types[i] = pe_unknown;
      // NB: we "fall through" to for loop
    }

  if (indexes.size() != referent->index_types.size())
    r.unresolved (tok);
  else for (unsigned i=0; i<indexes.size(); i++)
    {
      expression* e = indexes[i];
      e->resolve_types (r, referent->index_types[i]);
      exp_type it = e->type;
      referent->index_types[i] = it;
      
      if (it == pe_string || it == pe_long)
        ; // do nothing
      else if (it == pe_stats)
        r.invalid (e->tok, it);
      else // pe_unknown
        r.unresolved (e->tok);
    }
}


void
functioncall::resolve_types (typeresolution_info& r, exp_type t)
{
  assert (referent != 0);

  resolve_2types (this, referent, r, t);

  if (type == pe_stats)
    r.mismatch (tok, pe_unknown, type);

  // XXX: but what about functions that return no value,
  // and are used only as an expression-statement for side effects?

  // now resolve the function parameters
  if (args.size() != referent->formal_args.size())
    r.unresolved (tok);
  else for (unsigned i=0; i<args.size(); i++)
    {
      expression* e = args[i];
      exp_type& ft = referent->formal_args[i]->type;
      const token* ftok = referent->formal_args[i]->tok;
      e->resolve_types (r, ft);
      exp_type at = e->type;
      
      if (((at == pe_string) || (at == pe_long)) && ft == pe_unknown)
        {
          // propagate to formal arg
          ft = at;
          r.resolved (referent->formal_args[i]->tok, ft);
        }
      if (at == pe_stats)
        r.invalid (e->tok, at);
      if (ft == pe_stats)
        r.invalid (ftok, ft);
      if (at != pe_unknown && ft != pe_unknown && ft != at)
        r.mismatch (e->tok, at, ft);
      if (at == pe_unknown)
        r.unresolved (e->tok);
    }
}


void
block::resolve_types (typeresolution_info& r)
{
  for (unsigned i=0; i<statements.size(); i++)
    statements[i]->resolve_types (r);
}


void
if_statement::resolve_types (typeresolution_info& r)
{
  condition->resolve_types (r, pe_long);
  thenblock->resolve_types (r);
  elseblock->resolve_types (r);
}


void
for_loop::resolve_types (typeresolution_info& r)
{
  init->resolve_types (r, pe_unknown);
  cond->resolve_types (r, pe_long);
  incr->resolve_types (r, pe_unknown);
  block->resolve_types (r);
}


void
expr_statement::resolve_types (typeresolution_info& r)
{
  value->resolve_types (r, pe_unknown);
}


void
return_statement::resolve_types (typeresolution_info& r)
{
  // This is like symbol::resolve_types, where the referent is
  // the return value of the function.

  // XXX: need control flow semantic checking; until then:
  if (r.current_function == 0)
    {
      r.unresolved (tok);
      return;
    }

  exp_type& type = r.current_function->type;
  value->resolve_types (r, type);

  if (type != pe_unknown && value->type != pe_unknown
      && type != value->type)
    r.mismatch (r.current_function->tok, type, value->type);
  if (type == pe_unknown && 
      (value->type == pe_long || value->type == pe_string))
    {
      // propagate non-statistics from value
      type = value->type;
      r.resolved (r.current_function->tok, value->type);
    }
  if (value->type == pe_stats)
    r.invalid (value->tok, value->type);
}


void
typeresolution_info::unresolved (const token* tok)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      cerr << "error: unresolved type for ";
      if (tok)
        cerr << *tok;
      else
        cerr << "a token";
      cerr << endl;
    }
}


void
typeresolution_info::invalid (const token* tok, exp_type pe)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      cerr << "error: invalid type " << pe << " for ";
      if (tok)
        cerr << *tok;
      else
        cerr << "a token";
      cerr << endl;
    }
}


void
typeresolution_info::mismatch (const token* tok, exp_type t1, exp_type t2)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      cerr << "error: type mismatch for ";
      if (tok)
        cerr << *tok;
      else
        cerr << "a token";
      cerr << ": " << t1 << " vs. " << t2 << endl;
    }
}


void
typeresolution_info::resolved (const token* tok, exp_type t)
{
  num_newly_resolved ++;
  // cerr << "resolved " << *tok << " type " << t << endl;
}


// ------------------------------------------------------------------------
// semantic processing: lvalue checking: XXX: unneeded?


bool
assignment::is_lvalue ()
{
  return left->is_lvalue ();
}


// ------------------------------------------------------------------------
// unparser


translator_output::translator_output (ostream& f):
  o (f), tablevel (0)
{
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
// visitors


void
block::visit (visitor* u)
{
  u->visit_block (this);
}

void
for_loop::visit (visitor* u)
{
  u->visit_for_loop (this);
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
exponentiation::visit (visitor* u)
{
  u->visit_exponentiation (this);
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
traversing_visitor::visit_block (block *s)
{
  for (unsigned i=0; i<s->statements.size(); i++)
    s->statements[i]->visit (this);
}

void
traversing_visitor::visit_null_statement (null_statement *s)
{
}

void
traversing_visitor::visit_expr_statement (expr_statement *s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_if_statement (if_statement* s)
{
  s->condition->visit (this);
  s->thenblock->visit (this);
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
  e->left->visit (this);
  e->right->visit (this);
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
traversing_visitor::visit_exponentiation (exponentiation* e)
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
throwing_visitor::visit_block (block *s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_null_statement (null_statement *s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_expr_statement (expr_statement *s)
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
throwing_visitor::visit_exponentiation (exponentiation* e)
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
