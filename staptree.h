// -*- C++ -*-
// Copyright 2005 Red Hat Inc.
// GPL

#include <string>
#include <vector>
#include <map>
#include <iostream>

using namespace std;


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
struct expression
{
  exp_type type;
  const token* tok;
  virtual void print (ostream& o) = 0;
  expression ();
  virtual ~expression ();
  virtual void resolve_symbols (symresolution_info& r) = 0;
  virtual void resolve_types (typeresolution_info& r, exp_type t) = 0;
};

ostream& operator << (ostream& o, expression& k);


struct literal: public expression
{
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
};


struct literal_string: public literal
{
  string value;
  literal_string (const string& v);
  void print (ostream& o);
};


struct literal_number: public literal
{
  long value;
  literal_number (long v);
  void print (ostream& o);
};


struct binary_expression: public expression
{
  expression* left;
  string op;
  expression* right;
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
};


struct unary_expression: public expression
{
  string op;
  expression* operand;
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
};


struct pre_crement: public unary_expression
{
};


struct post_crement: public unary_expression
{
  void print (ostream& o);
};


struct logical_or_expr: public binary_expression
{
};


struct logical_and_expr: public binary_expression
{
};


struct array_in: public binary_expression
{
};


struct comparison: public binary_expression
{
};


struct concatenation: public binary_expression
{
};


struct exponentiation: public binary_expression
{
};


struct ternary_expression: public expression
{
  expression* cond;
  expression* truevalue;
  expression* falsevalue;
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
};


struct assignment: public binary_expression
{
};


class vardecl;
struct symbol: public expression
{
  string name;
  vardecl *referent;
  symbol ();
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
};


struct arrayindex: public expression
{
  string base;
  vector<expression*> indexes;
  vardecl *referent;
  arrayindex ();
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
};



class functiondecl;
struct functioncall: public expression
{
  string function;
  vector<expression*> args;
  functiondecl *referent;
  functioncall ();
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r, exp_type t);
};


// ------------------------------------------------------------------------


struct stapfile;
struct symboldecl;
struct symresolution_info
{
  vector<vardecl*>& locals; // includes incoming function parameters
  vector<vardecl*>& globals;
  vector<stapfile*>& files;
  stapfile* current_file;
  functiondecl* current_function;

  symresolution_info (vector<vardecl*>& l,
                      vector<vardecl*>& g,
                      vector<stapfile*>& f,
                      stapfile* cfil,
                      functiondecl* cfun);
  symresolution_info (vector<vardecl*>& l,
                      vector<vardecl*>& g,
                      vector<stapfile*>& f,
                      stapfile* cfil);

  vardecl* find (const string& name);

  void unresolved (const token* tok);
  unsigned num_unresolved;
};


struct typeresolution_info
{
  unsigned num_newly_resolved;
  unsigned num_still_unresolved;
  bool assert_resolvability;
  functiondecl* current_function;

  void mismatch (const token* tok, exp_type t1,
                 exp_type t2);
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
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r);
};


struct null_statement: public statement
{
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r) {}
  void resolve_types (typeresolution_info& r) {}
};


struct expr_statement: public statement
{
  expression* value;  // executed for side-effects
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r);
};


struct if_statement: public statement
{
  expression* condition;
  statement* thenblock;
  statement* elseblock;
  void print (ostream& o);
  void resolve_symbols (symresolution_info& r);
  void resolve_types (typeresolution_info& r);
};


struct return_statement: public expr_statement
{
  void print (ostream& o);
  void resolve_types (typeresolution_info& r);
};


struct delete_statement: public expr_statement
{
  void print (ostream& o);
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


struct probe_point_spec // inherit from something or other?
{
  string functor;
  const token* tok;
  literal* arg;
  void print (ostream& o);
};


struct probe
{
  vector<probe_point_spec*> location;
  const token* tok;
  block* body;
  vector<vardecl*> locals;
  void print (ostream& o);
};
