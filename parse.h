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


enum token_type 
  {
    tok_junk, tok_identifier, tok_operator, tok_string, tok_number,
    tok_embedded
    // XXX: add tok_keyword throughout
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
  parse_error (const std::string& msg): runtime_error (msg) {}
};


class lexer
{
public:
  token* scan ();
  lexer (std::istream&, const std::string&);

private:
  int input_get ();
  int input_peek (unsigned n=0);
  std::istream& input;
  std::string input_name;
  std::vector<int> lookahead;
  unsigned cursor_line;
  unsigned cursor_column;
};


class parser
{
public:
  parser (std::istream& i, bool p);
  parser (const std::string& n, bool p);
  ~parser ();

  stapfile* parse ();

  static stapfile* parse (std::istream& i, bool privileged);
  static stapfile* parse (const std::string& n, bool privileged);

private:
  std::string input_name;
  std::istream* free_input;
  lexer input;
  bool privileged;

  // scanning state
  const token* last ();
  const token* next ();
  const token* peek ();

  const token* last_t; // the last value returned by peek() or next()
  const token* next_t; // lookahead token
  
  // expectations
  const token* expect_known (token_type tt, std::string const & expected);
  const token* expect_unknown (token_type tt, std::string & target);

  // convenience forms
  const token* expect_op (std::string const & expected);
  const token* expect_kw (std::string const & expected);
  const token* expect_number (int64_t & expected);
  const token* expect_ident (std::string & target);
  bool peek_op (std::string const & op);
  bool peek_kw (std::string const & kw);

  void print_error (const parse_error& pe);
  unsigned num_errors;

private: // nonterminals
  void parse_probe (std::vector<probe*>&, std::vector<probe_alias*>&);
  void parse_global (std::vector<vardecl*>&,
		     std::map<std::string, statistic_decl> &stat_decls);
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
