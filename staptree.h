// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef STAPTREE_H
#define STAPTREE_H

#include "session.h"
#include <map>
#include <stack>
#include <set>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cassert>
extern "C" {
#include <stdint.h>
}

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

// ------------------------------------------------------------------------

/* struct statistic_decl moved to session.h */

// ------------------------------------------------------------------------

enum exp_type
  {
    pe_unknown,
    pe_long,   // int64_t
    pe_string, // std::string
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
  virtual void print (std::ostream& o) const = 0;
  virtual void visit (visitor* u) = 0;
};

std::ostream& operator << (std::ostream& o, const expression& k);


struct literal: public expression
{
};


struct literal_string: public literal
{
  std::string value;
  literal_string (const std::string& v);
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct literal_number: public literal
{
  int64_t value;
  literal_number (int64_t v);
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct binary_expression: public expression
{
  expression* left;
  std::string op;
  expression* right;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct unary_expression: public expression
{
  std::string op;
  expression* operand;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct pre_crement: public unary_expression
{
  void visit (visitor* u);
};


struct post_crement: public unary_expression
{
  void print (std::ostream& o) const;
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
  void print (std::ostream& o) const;
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
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct assignment: public binary_expression
{
  void visit (visitor* u);
};

struct symbol;
struct hist_op;
struct indexable
{
  // This is a helper class which, type-wise, acts as a disjoint union
  // of symbols and histograms. You can ask it whether it's a
  // histogram or a symbol, and downcast accordingly.
  void print_indexable (std::ostream& o) const;
  void visit_indexable (visitor* u);
  virtual bool is_symbol(symbol *& sym_out);
  virtual bool is_hist_op(hist_op *& hist_out);
  virtual bool is_const_symbol(const symbol *& sym_out) const;
  virtual bool is_const_hist_op(const hist_op *& hist_out) const;
  virtual const token *get_tok() const = 0;
  virtual ~indexable() {}
};

// Perform a downcast to one out-value and NULL the other, throwing an
// exception if neither downcast succeeds. This is (sadly) about the
// best we can accomplish in C++.
void
classify_indexable(indexable* ix,
		   symbol *& array_out,
		   hist_op *& hist_out);

void
classify_const_indexable(const indexable* ix,
			 symbol const *& array_out,
			 hist_op const *& hist_out);

class vardecl;
struct symbol:
  public expression,
  public indexable
{
  std::string name;
  vardecl *referent;
  symbol ();
  void print (std::ostream& o) const;
  void visit (visitor* u);
  // overrides of type 'indexable'
  const token *get_tok() const;
  bool is_const_symbol(const symbol *& sym_out) const;
  bool is_symbol(symbol *& sym_out);
};


struct target_symbol : public expression
{
  enum component_type
    {
      comp_struct_member,
      comp_literal_array_index
    };
  std::string base_name;
  std::vector<std::pair<component_type, std::string> > components;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct arrayindex: public expression
{
  std::vector<expression*> indexes;
  indexable *base;
  arrayindex ();
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


class functiondecl;
struct functioncall: public expression
{
  std::string function;
  std::vector<expression*> args;
  functiondecl *referent;
  functioncall ();
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct print_format: public expression
{
  bool print_with_format;
  bool print_to_stream;

  enum format_flag
    {
      fmt_flag_zeropad = 1,
      fmt_flag_plus = 2,
      fmt_flag_space = 4,
      fmt_flag_left = 8,
      fmt_flag_special = 16
    };

  enum conversion_type
    {
      conv_unspecified,
      conv_signed_decimal,
      conv_unsigned_decimal,
      conv_unsigned_octal,
      conv_unsigned_ptr,
      conv_unsigned_uppercase_hex,
      conv_unsigned_lowercase_hex,
      conv_string,
      conv_literal
    };

  struct format_component
  {
    unsigned long flags;
    unsigned width;
    unsigned precision;
    conversion_type type;
    std::string literal_string;
    bool is_empty() const
    {
      return flags == 0
	&& width == 0
	&& precision == 0
	&& type == conv_unspecified
	&& literal_string.empty();
    }
    void clear()
    {
      flags = 0;
      width = 0;
      precision = 0;
      type = conv_unspecified;
      literal_string.clear();
    }
  };

  print_format()
    : hist(NULL)
  {}

  std::string raw_components;
  std::vector<format_component> components;
  std::vector<expression*> args;
  hist_op *hist;

  static std::string components_to_string(std::vector<format_component> const & components);
  static std::vector<format_component> string_to_components(std::string const & str);

  void print (std::ostream& o) const;
  void visit (visitor* u);
};


enum stat_component_type
  {
    sc_average,
    sc_count,
    sc_sum,
    sc_min,
    sc_max,
  };

struct stat_op: public expression
{
  stat_component_type ctype;
  expression* stat;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};

enum histogram_type
  {
    hist_linear,
    hist_log
  };

struct hist_op: public indexable
{
  const token* tok;
  histogram_type htype;
  expression* stat;
  std::vector<int64_t> params;
  void print (std::ostream& o) const;
  void visit (visitor* u);
  // overrides of type 'indexable'
  const token *get_tok() const;
  bool is_const_hist_op(const hist_op *& hist_out) const;
  bool is_hist_op(hist_op *& hist_out);
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
  virtual void print (std::ostream &o) const = 0;
  virtual void printsig (std::ostream &o) const = 0;
};


std::ostream& operator << (std::ostream& o, const symboldecl& k);


struct vardecl: public symboldecl
{
  void print (std::ostream& o) const;
  void printsig (std::ostream& o) const;
  vardecl ();
  void set_arity (int arity);
  bool compatible_arity (int a);
  int arity; // -1: unknown; 0: scalar; >0: array
  std::vector<exp_type> index_types; // for arrays only
};


struct vardecl_builtin: public vardecl
{
};


struct statement;
struct functiondecl: public symboldecl
{
  std::vector<vardecl*> formal_args;
  std::vector<vardecl*> locals;
  statement* body;
  functiondecl ();
  void print (std::ostream& o) const;
  void printsig (std::ostream& o) const;
};


// ------------------------------------------------------------------------


struct statement
{
  virtual void print (std::ostream& o) const = 0;
  virtual void visit (visitor* u) = 0;
  const token* tok;
  statement ();
  virtual ~statement ();
};

std::ostream& operator << (std::ostream& o, const statement& k);


struct embeddedcode: public statement
{
  std::string code;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct block: public statement
{
  std::vector<statement*> statements;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct expr_statement;
struct for_loop: public statement
{
  expr_statement* init; // may be 0
  expression* cond;
  expr_statement* incr; // may be 0
  statement* block;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct foreach_loop: public statement
{
  // this part is a specialization of arrayindex
  std::vector<symbol*> indexes;
  indexable *base;
  int sort_direction; // -1: decreasing, 0: none, 1: increasing
  unsigned sort_column; // 0: value, 1..N: index

  statement* block;
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct null_statement: public statement
{
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct expr_statement: public statement
{
  expression* value;  // executed for side-effects
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct if_statement: public statement
{
  expression* condition;
  statement* thenblock;
  statement* elseblock; // may be 0
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct return_statement: public expr_statement
{
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct delete_statement: public expr_statement
{
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct break_statement: public statement
{
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct continue_statement: public statement
{
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct next_statement: public statement
{
  void print (std::ostream& o) const;
  void visit (visitor* u);
};


struct probe;
struct probe_alias;
struct embeddedcode;
struct stapfile
{
  std::string name;
  std::vector<probe*> probes;
  std::vector<probe_alias*> aliases;
  std::vector<functiondecl*> functions;
  std::vector<vardecl*> globals;
  std::vector<embeddedcode*> embeds;
  bool privileged;
  stapfile (): privileged (false) {}
  void print (std::ostream& o) const;
};




struct probe_point
{
  struct component // XXX: sort of a restricted functioncall
  {
    std::string functor;
    literal* arg; // optional
    component ();
    component(std::string const & f, literal * a = NULL);
  };
  std::vector<component*> components;
  const token* tok; // points to first component's functor
  void print (std::ostream& o) const;
  probe_point ();
  probe_point(std::vector<component*> const & comps,const token * t);
};

std::ostream& operator << (std::ostream& o, const probe_point& k);


struct probe
{
  std::vector<probe_point*> locations;
  block* body;
  const token* tok;
  std::vector<vardecl*> locals;
  probe ();
  void print (std::ostream& o) const;
  virtual void printsig (std::ostream &o) const;
  virtual ~probe() {}
};

struct probe_alias: public probe
{
  probe_alias(std::vector<probe_point*> const & aliases);
  std::vector<probe_point*> alias_names;
  virtual void printsig (std::ostream &o) const;
};


// A derived visitor instance is used to visit the entire
// statement/expression tree.
struct visitor
{
  // Machinery for differentiating lvalue visits from non-lvalue.
  std::vector<expression *> active_lvalues;
  bool is_active_lvalue(expression *e);
  void push_active_lvalue(expression *e);
  void pop_active_lvalue();

  virtual ~visitor () {}
  virtual void visit_block (block *s) = 0;
  virtual void visit_embeddedcode (embeddedcode *s) = 0;
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
  virtual void visit_target_symbol (target_symbol* e) = 0;
  virtual void visit_arrayindex (arrayindex* e) = 0;
  virtual void visit_functioncall (functioncall* e) = 0;
  virtual void visit_print_format (print_format* e) = 0;
  virtual void visit_stat_op (stat_op* e) = 0;
  virtual void visit_hist_op (hist_op* e) = 0;
};


// A simple kind of visitor, which travels down to the leaves of the
// statement/expression tree, up to but excluding following vardecls
// and functioncalls.
struct traversing_visitor: public visitor
{
  void visit_block (block *s);
  void visit_embeddedcode (embeddedcode *s);
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
  void visit_target_symbol (target_symbol* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
  void visit_print_format (print_format* e);
  void visit_stat_op (stat_op* e);
  void visit_hist_op (hist_op* e);
};


// A kind of traversing visitor, which also follows function calls.
// It uses an internal set object to prevent infinite recursion.
struct functioncall_traversing_visitor: public traversing_visitor
{
  std::set<functiondecl*> traversed;
  functiondecl* current_function;
  functioncall_traversing_visitor(): current_function(0) {}
  void visit_functioncall (functioncall* e);
};


// A kind of traversing visitor, which also follows function calls,
// and stores the vardecl* referent of each variable read and/or
// written and other such sundry side-effect data.  It's used by
// the elaboration-time optimizer pass.
struct varuse_collecting_visitor: public functioncall_traversing_visitor
{
  std::set<vardecl*> read;
  std::set<vardecl*> written;
  bool embedded_seen;
  expression* current_lvalue;
  expression* current_lrvalue;
  varuse_collecting_visitor():
    embedded_seen (false),
    current_lvalue(0),
    current_lrvalue(0) {}
  void visit_embeddedcode (embeddedcode *s);
  void visit_delete_statement (delete_statement *s);
  void visit_print_format (print_format *e);
  void visit_assignment (assignment *e);
  void visit_arrayindex (arrayindex *e);
  void visit_symbol (symbol *e);
  void visit_pre_crement (pre_crement *e);
  void visit_post_crement (post_crement *e);
  void visit_foreach_loop (foreach_loop *s);
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
  void visit_embeddedcode (embeddedcode *s);
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
  void visit_target_symbol (target_symbol* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
  void visit_print_format (print_format* e);
  void visit_stat_op (stat_op* e);
  void visit_hist_op (hist_op* e);
};

// A visitor which performs a deep copy of the root node it's applied
// to. NB: It does not copy any of the variable or function
// declarations; those fields are set to NULL, assuming you want to
// re-infer the declarations in a new context (the one you're copying
// to).

struct deep_copy_visitor: public visitor
{
  std::stack<void *> targets;

  static statement *deep_copy (statement *s);
  static block *deep_copy (block *s);

  virtual void visit_block (block *s);
  virtual void visit_embeddedcode (embeddedcode *s);
  virtual void visit_null_statement (null_statement *s);
  virtual void visit_expr_statement (expr_statement *s);
  virtual void visit_if_statement (if_statement* s);
  virtual void visit_for_loop (for_loop* s);
  virtual void visit_foreach_loop (foreach_loop* s);
  virtual void visit_return_statement (return_statement* s);
  virtual void visit_delete_statement (delete_statement* s);
  virtual void visit_next_statement (next_statement* s);
  virtual void visit_break_statement (break_statement* s);
  virtual void visit_continue_statement (continue_statement* s);
  virtual void visit_literal_string (literal_string* e);
  virtual void visit_literal_number (literal_number* e);
  virtual void visit_binary_expression (binary_expression* e);
  virtual void visit_unary_expression (unary_expression* e);
  virtual void visit_pre_crement (pre_crement* e);
  virtual void visit_post_crement (post_crement* e);
  virtual void visit_logical_or_expr (logical_or_expr* e);
  virtual void visit_logical_and_expr (logical_and_expr* e);
  virtual void visit_array_in (array_in* e);
  virtual void visit_comparison (comparison* e);
  virtual void visit_concatenation (concatenation* e);
  virtual void visit_ternary_expression (ternary_expression* e);
  virtual void visit_assignment (assignment* e);
  virtual void visit_symbol (symbol* e);
  virtual void visit_target_symbol (target_symbol* e);
  virtual void visit_arrayindex (arrayindex* e);
  virtual void visit_functioncall (functioncall* e);
  virtual void visit_print_format (print_format* e);
  virtual void visit_stat_op (stat_op* e);
  virtual void visit_hist_op (hist_op* e);
};

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

template <> static void
require <indexable *> (deep_copy_visitor* v, indexable** dst, indexable* src)
{
  if (src != NULL)
    {
      symbol *array_src=NULL, *array_dst=NULL;
      hist_op *hist_src=NULL, *hist_dst=NULL;

      classify_indexable(src, array_src, hist_src);

      *dst = NULL;

      if (array_src)
	{
	  require <symbol*> (v, &array_dst, array_src);
	  *dst = array_dst;
	}
      else
	{
	  require <hist_op*> (v, &hist_dst, hist_src);
	  *dst = hist_dst;
	}
      assert (*dst);
    }
}

template <typename T> static void
provide (deep_copy_visitor* v, T src)
{
  assert(!v->targets.empty());
  *(static_cast<T*>(v->targets.top())) = src;
}

#endif // STAPTREE_H
