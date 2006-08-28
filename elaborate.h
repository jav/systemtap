// -*- C++ -*-
// Copyright (C) 2005, 2006 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef ELABORATE_H
#define ELABORATE_H

#include "staptree.h"
#include "parse.h"
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <map>

// ------------------------------------------------------------------------

struct derived_probe;
struct match_node;

struct symresolution_info: public traversing_visitor 
{
protected:
  systemtap_session& session;

public:
  functiondecl* current_function;
  derived_probe* current_probe;
  symresolution_info (systemtap_session& s);

  vardecl* find_var (const std::string& name, int arity);
  functiondecl* find_function (const std::string& name, unsigned arity);

  void visit_block (block *s);
  void visit_symbol (symbol* e);
  void visit_foreach_loop (foreach_loop* e);
  void visit_arrayindex (arrayindex* e);
  void visit_functioncall (functioncall* e);
  void visit_delete_statement (delete_statement* s);
};


struct typeresolution_info: public visitor
{
  typeresolution_info (systemtap_session& s);
  systemtap_session& session;
  unsigned num_newly_resolved;
  unsigned num_still_unresolved;
  bool assert_resolvability;
  functiondecl* current_function;
  derived_probe* current_probe;

  void mismatch (const token* tok, exp_type t1, exp_type t2);
  void unresolved (const token* tok);
  void resolved (const token* tok, exp_type t);
  void invalid (const token* tok, exp_type t);

  exp_type t; // implicit parameter for nested visit call; may clobber

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
  void visit_print_format (print_format* e);
  void visit_stat_op (stat_op* e);
  void visit_hist_op (hist_op* e);
};


// ------------------------------------------------------------------------


// A derived_probe is a probe that has been elaborated by
// binding to a matching provider.  The locations std::vector
// may be smaller or larger than the base probe, since a
// provider may transform it.

class translator_output;

struct derived_probe: public probe
{
  derived_probe (probe* b);
  derived_probe (probe* b, probe_point* l);
  probe* base; // the original parsed probe
  virtual probe* basest () { return base->basest(); }

  virtual ~derived_probe () {}

  virtual void register_probe (systemtap_session& s) = 0;

  virtual void emit_registrations (translator_output* o) = 0;
  // (from within module_init):
  // rc = ..... register_or_whatever (ENTRYFN);

  virtual void emit_deregistrations (translator_output* o) = 0;
  // (from within module_exit):
  // (void) ..... unregister_or_whatever (ENTRYFN);

  virtual void emit_probe_entries (translator_output* o) = 0;
  // ... for all probe-points:
  // ELABORATE_SPECIFIC_SIGNATURE ENTRYFN {
  //   /* allocate context - probe_prologue */
  //   /* copy parameters, initial state into context */
  //   probe_NUMBER (context);
  //   /* deallocate context - probe_epilogue */
  // }

  virtual void emit_probe_context_vars (translator_output* o) {}
  // From within unparser::emit_common_header, add any extra variables
  // to this probe's context locals.

protected:
  void emit_probe_prologue (translator_output* o, const std::string&);
  void emit_probe_epilogue (translator_output* o);

public:
  static void emit_common_header (translator_output* o);
  // from c_unparser::emit_common_header
};

// ------------------------------------------------------------------------

struct be_derived_probe;
struct dwarf_derived_probe;
struct hrtimer_derived_probe;
struct mark_derived_probe;
struct never_derived_probe;
struct profile_derived_probe;
struct timer_derived_probe;
struct unparser;

struct derived_probe_group
{
  virtual ~derived_probe_group () {}

  virtual void register_probe(be_derived_probe* p);
  virtual void register_probe(dwarf_derived_probe* p);
  virtual void register_probe(hrtimer_derived_probe* p);
  virtual void register_probe(mark_derived_probe* p);
  virtual void register_probe(never_derived_probe* p);
  virtual void register_probe(profile_derived_probe* p);
  virtual void register_probe(timer_derived_probe* p);
  virtual size_t size () = 0;

  virtual void emit_probes (translator_output* op, unparser* up) = 0;
};

// ------------------------------------------------------------------------

struct derived_probe_builder
{
  virtual void build(systemtap_session & sess,
		     probe* base, 
		     probe_point* location,
		     std::map<std::string, literal*> const & parameters,
		     std::vector<derived_probe*> & finished_results) = 0;
  virtual ~derived_probe_builder() {}

  static bool get_param (std::map<std::string, literal*> const & parameters,
                         const std::string& key, std::string& value);
  static bool get_param (std::map<std::string, literal*> const & parameters,
                         const std::string& key, int64_t& value);
};


struct
match_key
{
  std::string name;
  bool have_parameter;
  token_type parameter_type;

  match_key(std::string const & n);
  match_key(probe_point::component const & c);

  match_key & with_number();
  match_key & with_string();
  std::string str() const;
  bool operator<(match_key const & other) const;
  bool globmatch(match_key const & other) const;
};


class
match_node
{
  typedef std::map<match_key, match_node*> sub_map_t;
  typedef std::map<match_key, match_node*>::iterator sub_map_iterator_t;
  sub_map_t sub;
  derived_probe_builder* end;

 public:
  match_node();

  void find_and_build (systemtap_session& s,
                       probe* p, probe_point *loc, unsigned pos,
                       std::vector<derived_probe *>& results);

  match_node* bind(match_key const & k);
  match_node* bind(std::string const & k);
  match_node* bind_str(std::string const & k);
  match_node* bind_num(std::string const & k);
  void bind(derived_probe_builder* e);
};

// ------------------------------------------------------------------------

/* struct systemtap_session moved to session.h */

int semantic_pass (systemtap_session& s);
void derive_probes (systemtap_session& s,
                    probe *p, std::vector<derived_probe*>& dps,
                    bool optional = false);

// A helper we use here and in translate, for pulling symbols out of lvalue
// expressions.
symbol * get_symbol_within_expression (expression *e);


struct unparser;

struct derived_probe_group_container: public derived_probe_group
{
private:
  std::vector<derived_probe*> probes;
  derived_probe_group* be_probe_group;
  derived_probe_group* dwarf_probe_group;
  derived_probe_group* hrtimer_probe_group;
  derived_probe_group* mark_probe_group;
  derived_probe_group* never_probe_group;
  derived_probe_group* profile_probe_group;
  derived_probe_group* timer_probe_group;

public:
  derived_probe_group_container ();
  ~derived_probe_group_container ();

  void register_probe (be_derived_probe* p);
  void register_probe (dwarf_derived_probe* p);
  void register_probe (hrtimer_derived_probe* p);
  void register_probe (mark_derived_probe* p);
  void register_probe (never_derived_probe* p);
  void register_probe (profile_derived_probe* p);
  void register_probe (timer_derived_probe* p);
  size_t size () { return (probes.size ()); }

  derived_probe* operator[] (size_t n) { return (probes[n]); }

  void emit_probes (translator_output* op, unparser* up);
};


#endif // ELABORATE_H
