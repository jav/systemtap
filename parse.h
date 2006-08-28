// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.


#ifndef PARSE_H
#define PARSE_H

#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>


struct source_loc
{
  std::string file;
  unsigned line;
  unsigned column;
};

std::ostream& operator << (std::ostream& o, const source_loc& loc);

enum parse_context
  {
    con_unknown, con_probe, con_global, con_function, con_embedded
  };


enum token_type 
  {
    tok_junk, tok_identifier, tok_operator, tok_string, tok_number,
    tok_embedded, tok_keyword
  };


struct token
{
  source_loc location;
  token_type type;
  std::string content;
};


std::ostream& operator << (std::ostream& o, const token& t);


struct parse_error: public std::runtime_error
{
  const token* tok;
  parse_error (const std::string& msg): runtime_error (msg), tok (0) {}
  parse_error (const std::string& msg, const token* t): runtime_error (msg),
                                                        tok (t) {}
};


struct systemtap_session;

class lexer
{
public:
  token* scan ();
  lexer (std::istream&, const std::string&, systemtap_session&);

private:
  int input_get ();
  int input_peek (unsigned n=0);
  std::istream& input;
  std::string input_name;
  std::vector<int> lookahead;
  unsigned cursor_line;
  unsigned cursor_column;
  systemtap_session& session;
};


struct stapfile;
struct probe;
struct probe_alias;
struct vardecl;
struct functiondecl;
struct embeddedcode;
struct probe_point;
struct literal;
struct block;
struct for_loop;
struct statement;
struct if_statement;
struct foreach_loop;
struct expr_statement;
struct return_statement;
struct delete_statement;
struct break_statement;
struct next_statement;
struct continue_statement;
struct indexable;
struct expression;
struct hist_op;

class parser
{
public:
  parser (systemtap_session& s, std::istream& i, bool p);
  parser (systemtap_session& s, const std::string& n, bool p);
  ~parser ();

  stapfile* parse ();

  static stapfile* parse (systemtap_session& s, std::istream& i, bool privileged);
  static stapfile* parse (systemtap_session& s, const std::string& n, bool privileged);

private:
  systemtap_session& session;
  std::string input_name;
  std::istream* free_input;
  lexer input;
  bool privileged;
  parse_context context;

  // preprocessing subordinate
  std::vector<const token*> enqueued_pp;
  const token* scan_pp ();

  // scanning state
  const token* last ();
  const token* next ();
  const token* peek ();

  const token* last_t; // the last value returned by peek() or next()
  const token* next_t; // lookahead token
  
  // expectations
  const token* expect_known (token_type tt, std::string const & expected);
  const token* expect_unknown (token_type tt, std::string & target);
  const token* expect_unknown2 (token_type tt1, token_type tt2,
				std::string & target);

  // convenience forms
  const token* expect_op (std::string const & expected);
  const token* expect_kw (std::string const & expected);
  const token* expect_number (int64_t & expected);
  const token* expect_ident (std::string & target);
  const token* expect_ident_or_keyword (std::string & target);
  bool peek_op (std::string const & op);
  bool peek_kw (std::string const & kw);

  void print_error (const parse_error& pe);
  unsigned num_errors;

private: // nonterminals
  void parse_probe (std::vector<probe*>&, std::vector<probe_alias*>&);
  void parse_global (std::vector<vardecl*>&);
  void parse_functiondecl (std::vector<functiondecl*>&);
  embeddedcode* parse_embeddedcode ();
  probe_point* parse_probe_point ();
  literal* parse_literal ();
  block* parse_stmt_block ();
  statement* parse_statement ();
  if_statement* parse_if_statement ();
  for_loop* parse_for_loop ();
  for_loop* parse_while_loop ();
  foreach_loop* parse_foreach_loop ();
  expr_statement* parse_expr_statement ();
  return_statement* parse_return_statement ();
  delete_statement* parse_delete_statement ();
  next_statement* parse_next_statement ();
  break_statement* parse_break_statement ();
  continue_statement* parse_continue_statement ();
  indexable* parse_indexable ();
  const token *parse_hist_op_or_bare_name (hist_op *&hop, std::string &name);
  expression* parse_expression ();
  expression* parse_assignment ();
  expression* parse_ternary ();
  expression* parse_logical_or ();
  expression* parse_logical_and ();
  expression* parse_boolean_or ();
  expression* parse_boolean_xor ();
  expression* parse_boolean_and ();
  expression* parse_array_in ();
  expression* parse_comparison ();
  expression* parse_shift ();
  expression* parse_concatenation ();
  expression* parse_additive ();
  expression* parse_multiplicative ();
  expression* parse_unary ();
  expression* parse_crement ();
  expression* parse_value ();
  expression* parse_symbol ();
};



#endif // PARSE_H
