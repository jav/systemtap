// -*- C++ -*-
// Copyright 2005 Red Hat Inc.
// GPL

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
    tok_junk, tok_identifier, tok_operator, tok_string, tok_number 
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
  std::istream& input;
  std::string input_name;
  unsigned cursor_line;
  unsigned cursor_column;
};


class parser
{
public:
  parser (std::istream& i);
  parser (const std::string& n);
  ~parser ();

  stapfile* parse ();

private:
  std::string input_name;
  std::istream* free_input;
  lexer input;

  // scanning state
  const token* last ();
  const token* next ();
  const token* peek ();

  const token* last_t; // the last value returned by peek() or next()
  const token* next_t; // lookahead token

  void print_error (const parse_error& pe);
  unsigned num_errors;

private: // nonterminals
  probe* parse_probe ();
  probe_point* parse_probe_point ();
  literal* parse_literal ();
  void parse_global (vector<vardecl*>&);
  functiondecl* parse_functiondecl ();
  block* parse_stmt_block ();
  statement* parse_statement ();
  if_statement* parse_if_statement ();
  return_statement* parse_return_statement ();
  delete_statement* parse_delete_statement ();
  expression* parse_expression ();
  expression* parse_assignment ();
  expression* parse_ternary ();
  expression* parse_logical_or ();
  expression* parse_logical_and ();
  expression* parse_array_in ();
  expression* parse_comparison ();
  expression* parse_concatenation ();
  expression* parse_additive ();
  expression* parse_multiplicative ();
  expression* parse_unary ();
  expression* parse_exponentiation ();
  expression* parse_crement ();
  expression* parse_value ();
  expression* parse_symbol ();
  symbol* parse_symbol_plain ();
};
