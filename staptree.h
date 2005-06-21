// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef STAPTREE_H
#define STAPTREE_H

#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>

extern bool verbose;

struct token; // parse.h
struct semantic_error: public std::runtime_error
{
  const token* tok1;
  const std::string msg2;
  const token* tok2;

  ~semantic_error () throw () {}
  semantic_error (const std::string& msg):
    runtime_error (msg), tok1 (0), tok2 (0) {}
  semantic_error (const std::string& msg, const token* t1): 
    runtime_error (msg), tok1 (t1), tok2 (0) {}
  semantic_error (const std::string& msg, const token* t1, 
                  const std::string& m2, const token* t2): 
    runtime_error (msg), tok1 (t1), msg2 (m2), tok2 (t2) {}
};



enum exp_type
  {
    pe_unknown,
    pe_long,
    pe_string, 
    pe_stats 
  };

std::ostream& operator << (std::ostream& o, const exp_type& e);

struct token;
struct visitor;

struct expression
{
  exp_type type;
  const token* tok;
  expression ();
  virtual ~expression ();
  virtual void print (std::ostream& o) = 0;
  virtual void visit (visitor* u) = 0;
};

std::ostream& operator << (std::ostream& o, expression& k);


struct literal: public expression
{
};


struct literal_string: public literal
{
  std::string value;
  literal_string (const std::string& v);
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct literal_number: public literal
{
  // XXX: s/long/long long/ throughout
  long value;
  literal_number (long v);
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct binary_expression: public expression
{
  expression* left;
  std::string op;
  expression* right;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct unary_expression: public expression
{
  std::string op;
  expression* operand;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct pre_crement: public unary_expression
{
  void visit (visitor* u);
};


struct post_crement: public unary_expression
{
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct logical_or_expr: public binary_expression
{
  void visit (visitor* u);
};


struct logical_and_expr: public binary_expression
{
  void visit (visitor* u);
};


struct arrayindex;
struct array_in: public expression
{
  arrayindex* operand;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct comparison: public binary_expression
{
  void visit (visitor* u);
};


struct concatenation: public binary_expression
{
  void visit (visitor* u);
};


struct ternary_expression: public expression
{
  expression* cond;
  expression* truevalue;
  expression* falsevalue;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct assignment: public binary_expression
{
  void visit (visitor* u);
};


class vardecl;
struct symbol: public expression
{
  std::string name;
  vardecl *referent;
  symbol ();
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct arrayindex: public expression
{
  std::string base;
  std::vector<expression*> indexes;
  vardecl *referent;
  arrayindex ();
  void print (std::ostream& o);
  void visit (visitor* u);
};


class functiondecl;
struct functioncall: public expression
{
  std::string function;
  std::vector<expression*> args;
  functiondecl *referent;
  functioncall ();
  void print (std::ostream& o);
  void visit (visitor* u);
};


// ------------------------------------------------------------------------


struct symboldecl // unique object per (possibly implicit) 
		  // symbol declaration
{
  const token* tok;
  std::string name;
  exp_type type;
  symboldecl ();
  virtual ~symboldecl ();
  virtual void print (std::ostream &o) = 0;
  virtual void printsig (std::ostream &o) = 0;
};


std::ostream& operator << (std::ostream& o, symboldecl& k);


struct vardecl: public symboldecl
{
  void print (std::ostream& o);
  void printsig (std::ostream& o);
  vardecl ();
  void set_arity (int arity);
  int arity; // -1: unknown; 0: scalar; >0: array
  std::vector<exp_type> index_types; // for arrays only
};


struct vardecl_builtin: public vardecl
{
};


struct block;
struct functiondecl: public symboldecl
{
  std::vector<vardecl*> formal_args;
  std::vector<vardecl*> locals;
  block* body;
  functiondecl ();
  void print (std::ostream& o);
  void printsig (std::ostream& o);
};


// ------------------------------------------------------------------------


struct statement
{
  virtual void print (std::ostream& o) = 0;
  virtual void visit (visitor* u) = 0;
  const token* tok;
  statement ();
  virtual ~statement ();
};

std::ostream& operator << (std::ostream& o, statement& k);


struct block: public statement
{
  std::vector<statement*> statements;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct expr_statement;
struct for_loop: public statement
{
  expr_statement* init;
  expression* cond;
  expr_statement* incr;
  statement* block;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct foreach_loop: public statement
{
  // this part is a specialization of arrayindex
  std::vector<symbol*> indexes;
  std::string base;
  vardecl* base_referent;

  statement* block;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct null_statement: public statement
{
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct expr_statement: public statement
{
  expression* value;  // executed for side-effects
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct if_statement: public statement
{
  expression* condition;
  statement* thenblock;
  statement* elseblock;
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct return_statement: public expr_statement
{
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct delete_statement: public expr_statement
{
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct break_statement: public statement
{
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct continue_statement: public statement
{
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct next_statement: public statement
{
  void print (std::ostream& o);
  void visit (visitor* u);
};


struct probe;
struct stapfile
{
  std::string name;
  std::vector<probe*> probes;
  std::vector<functiondecl*> functions;
  std::vector<vardecl*> globals;
  void print (std::ostream& o);
};


struct probe_point
{
  struct component // XXX: sort of a restricted functioncall
  { 
    std::string functor;
    literal* arg; // optional
    component ();
  };
  std::vector<component*> components;
  // XXX: probe aliases
  const token* tok; // points to first component's functor
  void print (std::ostream& o);
  probe_point ();
};

std::ostream& operator << (std::ostream& o, probe_point& k);


struct probe
{
  std::vector<probe_point*> locations;
  block* body;
  const token* tok;
  std::vector<vardecl*> locals;
  probe ();
  void print (std::ostream& o);
  void printsig (std::ostream &o);
};



// An derived visitor instance is used to visit the entire
// statement/expression tree.
struct visitor
{
  virtual ~visitor () {}
  virtual void visit_block (block *s) = 0;
  virtual void visit_null_statement (null_statement *s) = 0;
  virtual void visit_expr_statement (expr_statement *s) = 0;
  virtual void visit_if_statement (if_statement* s) = 0;
  virtual void visit_for_loop (for_loop* s) = 0;
  virtual void visit_foreach_loop (foreach_loop* s) = 0;
  virtual void visit_return_statement (return_statement* s) = 0;
  virtual void visit_delete_statement (delete_statement* s) = 0;
  virtual void visit_next_statement (next_statement* s) = 0;
  virtual void visit_break_statement (break_statement* s) = 0;
  virtual void visit_continue_statement (continue_statement* s) = 0;
  virtual void visit_literal_string (literal_string* e) = 0;
  virtual void visit_literal_number (literal_number* e) = 0;
  virtual void visit_binary_expression (binary_expression* e) = 0;
  virtual void visit_unary_expression (unary_expression* e) = 0;
  virtual void visit_pre_crement (pre_crement* e) = 0;
  virtual void visit_post_crement (post_crement* e) = 0;
  virtual void visit_logical_or_expr (logical_or_expr* e) = 0;
  virtual void visit_logical_and_expr (logical_and_expr* e) = 0;
  virtual void visit_array_in (array_in* e) = 0;
  virtual void visit_comparison (comparison* e) = 0;
  virtual void visit_concatenation (concatenation* e) = 0;
  virtual void visit_ternary_expression (ternary_expression* e) = 0;
  virtual void visit_assignment (assignment* e) = 0;
  virtual void visit_symbol (symbol* e) = 0;
  virtual void visit_arrayindex (arrayindex* e) = 0;
  virtual void visit_functioncall (functioncall* e) = 0;
};


// A default kind of visitor, which by default travels down
// to the leaves of the statement/expression tree, up to
// but excluding following vardecls (referent pointers).
struct traversing_visitor: public visitor
{
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


// A kind of visitor that throws an semantic_error exception
// whenever a non-overridden method is called.
struct throwing_visitor: public visitor
{
  std::string msg;
  throwing_visitor (const std::string& m);
  throwing_visitor ();

  virtual void throwone (const token* t);

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



#endif // STAPTREE_H
