// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
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

struct systemtap_session;
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

  // XXX: instead in systemtap_session?
  void derive_probes (match_node * root, probe *p, std::vector<derived_probe*>& dps);
  
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
  typeresolution_info (systemtap_session& s): session (s) {}
  systemtap_session& session;
  unsigned num_newly_resolved;
  unsigned num_still_unresolved;
  bool assert_resolvability;
  functiondecl* current_function;

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

  virtual ~derived_probe () {}

  virtual void emit_registrations (translator_output* o,
                                   unsigned probeidx) = 0;
  // (from within module_init):
  // rc = ..... ENTRYFN ;

  virtual void emit_deregistrations (translator_output* o,
                                     unsigned probeidx) = 0;
  // (from within module_exit):
  // rc = ..... ENTRYFN ;

  virtual void emit_probe_entries (translator_output* o,
                                   unsigned probeidx) = 0;
  // ... for all probe-points:
  // ELABORATE_SPECIFIC_SIGNATURE ENTRYFN {
  //   /* allocate context */
  //   /* copy parameters, initial state into context */
  //   probe_NUMBER (context);
  //   /* deallocate context */
  // }
};

// ------------------------------------------------------------------------

struct
derived_probe_builder
{
  virtual void build(systemtap_session & sess,
		     probe* base, 
		     probe_point* location,
		     std::map<std::string, literal*> const & parameters,
		     std::vector<probe*> & results_to_expand_further,
		     std::vector<derived_probe*> & finished_results) = 0;
  virtual ~derived_probe_builder() {}
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
};


class
match_node
{
  std::map<match_key, match_node*> sub;
  derived_probe_builder* end;

 public:
  match_node();
  derived_probe_builder* find_builder(std::vector<probe_point::component*> const & components,
				       unsigned pos,
				       std::vector< std::pair<std::string, literal*> > & parameters);
  
  match_node* bind(match_key const & k);
  match_node* bind(std::string const & k);
  match_node* bind_str(std::string const & k);
  match_node* bind_num(std::string const & k);
  void bind(derived_probe_builder* e);
};

// ------------------------------------------------------------------------


class unparser;
class translator_output;

struct systemtap_session
{
  systemtap_session ();

  // command line args
  std::vector<std::string> include_path;
  std::vector<std::string> args;
  std::string kernel_release;
  std::string runtime_path;
  std::string module_name;
  int last_pass;
  bool test_mode;
  bool verbose;
  bool keep_tmpdir;
  bool guru_mode;

  // temporary directory for module builds etc.
  // hazardous - it is "rm -rf"'d at exit
  std::string tmpdir;
  std::string translated_source; // C source code

  match_node* pattern_root;
  void register_library_aliases();

  // parse trees for the various script files
  stapfile* user_file;
  std::vector<stapfile*> library_files;

  // resolved globals/functions/probes for the run as a whole
  std::vector<stapfile*> files;
  std::vector<vardecl*> globals;
  std::vector<functiondecl*> functions;
  std::vector<derived_probe*> probes;
  std::vector<embeddedcode*> embeds;

  // unparser data
  translator_output* op;
  unparser* up;

  unsigned num_errors;
  // void print_error (const parse_error& e);
  void print_error (const semantic_error& e);
};


int semantic_pass (systemtap_session& s);


#endif // ELABORATE_H
