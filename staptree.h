// -*- C++ -*-
// Copyright 2005 Red Hat Inc.
// GPL

#ifndef STAPTREE_H
#define STAPTREE_H

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <stdexcept>


using namespace std;


struct token; // parse.h
struct semantic_error: public std::runtime_error
{
  const token* tok1;
  const string msg2;
  const token* tok2;

  ~semantic_error () throw () {}
  semantic_error (const string& msg):
    runtime_error (msg), tok1 (0), tok2 (0) {}
  semantic_error (const string& msg, const token* t1): 
    runtime_error (msg), tok1 (t1), tok2 (0) {}
  semantic_error (const string& msg, const token* t1, 
                  const string& m2, const token* t2): 
    runtime_error (msg), tok1 (t1), msg2 (m2), tok2 (t2) {}
};



enum exp_type
  {
    pe_unknown,
    pe_long,
    pe_string, 
    pe_stats 
  };

ostream& operator << (ostream& o, const exp_type& e);

struct token;
struct symresolution_info;
struct typeresolution_info;
struct visitor;

struct expression
{
  exp_type type;
  const token* tok;
  expression ();
  virtual ~expression ();
  virtual void print (ostream& o) = 0;
  virtual void resolve_symbols (symresolution_info& r) = 0;
  virtual void resolve_types (typeresolution_info& r, exp_type t) = 0;
  virtual bool is_lvalue () = 0; // XXX: deprecate
  virtual void visit (visitor* u) = 0;
};

ostream& operator << (ostream& o, expression& k);


struct literal: public expression
{
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
  bool is_lvalue () { return false; }
};


struct literal_string: public literal
{
  string value;
  literal_string (const string& v);
  void print (ostream& o);
  void visit (visitor* u);
};


struct literal_number: public literal
{
  long value;
  literal_number (long v);
  void print (ostream& o);
  void visit (visitor* u);
};


struct binary_expression: public expression
{
  expression* left;
  string op;
  expression* right;
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
  bool is_lvalue () { return false; }
};


struct unary_expression: public expression
{
  string op;
  expression* operand;
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
  bool is_lvalue () { return false; }
};


struct pre_crement: public unary_expression
{
  void visit (visitor* u);
};


struct post_crement: public unary_expression
{
  void print (ostream& o);
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


struct array_in: public binary_expression
{
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


struct exponentiation: public binary_expression
{
  void visit (visitor* u);
};


struct ternary_expression: public expression
{
  expression* cond;
  expression* truevalue;
  expression* falsevalue;
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
  bool is_lvalue () { return false; }
};


struct assignment: public binary_expression
{
  bool is_lvalue ();
  void visit (visitor* u);
};


class vardecl;
struct symbol: public expression
{
  string name;
  vardecl *referent;
  symbol ();
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
  bool is_lvalue () { return true; }
};


struct arrayindex: public expression
{
  string base;
  vector<expression*> indexes;
  vardecl *referent;
  arrayindex ();
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
  bool is_lvalue () { return true; }
};


class functiondecl;
struct functioncall: public expression
{
  string function;
  vector<expression*> args;
  functiondecl *referent;
  functioncall ();
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
  bool is_lvalue () { return false; }
};


// ------------------------------------------------------------------------


struct stapfile;
struct symboldecl;
struct symresolution_info
{
  vector<vardecl*>& locals; // includes incoming function parameters
  vector<vardecl*>& globals;
  vector<functiondecl*>& functions;
  functiondecl* current_function;

  symresolution_info (vector<vardecl*>& l,
                      vector<vardecl*>& g,
                      vector<functiondecl*>& f,
                      functiondecl* cfun);
  symresolution_info (vector<vardecl*>& l,
                      vector<vardecl*>& g,
                      vector<functiondecl*>& f);

  vardecl* find_scalar (const string& name);
  vardecl* find_array (const string& name, const vector<expression*>&);
  functiondecl* find_function (const string& name, const vector<expression*>&);

  void unresolved (const token* tok);
  unsigned num_unresolved;
};


struct typeresolution_info
{
  unsigned num_newly_resolved;
  unsigned num_still_unresolved;
  bool assert_resolvability;
  functiondecl* current_function;

  void mismatch (const token* tok, exp_type t1, exp_type t2);
  void unresolved (const token* tok);
  void resolved (const token* tok, exp_type t);
  void invalid (const token* tok, exp_type t);
};


struct symboldecl // unique object per (possibly implicit) 
		  // symbol declaration
{
  const token* tok;
  string name;
  exp_type type;
  symboldecl ();
  virtual ~symboldecl ();
  virtual void print (ostream &o) = 0;
  virtual void printsig (ostream &o) = 0;
};


ostream& operator << (ostream& o, symboldecl& k);


struct vardecl: public symboldecl
{
  void print (ostream& o);
  void printsig (ostream& o);
  vardecl ();
  vardecl (unsigned arity);
  vector<exp_type> index_types; // for arrays only
};


struct vardecl_builtin: public vardecl
{
};


struct block;
struct functiondecl: public symboldecl
{
  vector<vardecl*> formal_args;
  vector<vardecl*> locals;
  block* body;
  functiondecl ();
  void print (ostream& o);
  void printsig (ostream& o);
};


// ------------------------------------------------------------------------


struct statement
{
  virtual void print (ostream& o) = 0;
  virtual void visit (visitor* u) = 0;
  const token* tok;
  statement ();
  virtual ~statement ();
  virtual void resolve_symbols (symresolution_info& r) = 0;
  virtual void resolve_types (typeresolution_info& r) = 0;
};

ostream& operator << (ostream& o, statement& k);


struct block: public statement
{
  vector<statement*> statements;
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r);
};

struct for_loop: public statement
{
  expression* init;
  expression* cond;
  expression* incr;
  statement* block;
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r);
};


struct null_statement: public statement
{
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r) {}
  void resolve_types (typeresolution_info& r) {}
};


struct expr_statement: public statement
{
  expression* value;  // executed for side-effects
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r);
};


struct if_statement: public statement
{
  expression* condition;
  statement* thenblock;
  statement* elseblock;
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r);
};


struct return_statement: public expr_statement
{
  void print (ostream& o);
  void visit (visitor* u);
  void resolve_types (typeresolution_info& r);
};


struct delete_statement: public expr_statement
{
  void print (ostream& o);
  void visit (visitor* u);
};


struct probe;
struct stapfile
{
  string name;
  vector<probe*> probes;
  vector<functiondecl*> functions;
  vector<vardecl*> globals;
  void print (ostream& o);
};


struct probe_point
{
  struct component // XXX: sort of a restricted functioncall
  { 
    string functor;
    literal* arg; // optional
    component ();
  };
  vector<component*> components;
  const token* tok; // points to first component's functor
  void print (ostream& o);
  probe_point ();
};

ostream& operator << (ostream& o, probe_point& k);


struct probe
{
  vector<probe_point*> locations;
  block* body;
  const token* tok;
  vector<vardecl*> locals;
  probe ();
  void print (ostream& o);
  void printsig (ostream &o);
};



// Output context for systemtap translation, intended to allow
// pretty-printing.
class translator_output
{
  ostream& o;
  unsigned tablevel;

public:
  translator_output (ostream& file);

  ostream& newline (int indent = 0);
  void indent (int indent = 0);
  ostream& line();
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
  virtual void visit_return_statement (return_statement* s) = 0;
  virtual void visit_delete_statement (delete_statement* s) = 0;
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
  virtual void visit_exponentiation (exponentiation* e) = 0;
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



#endif // STAPTREE_H
