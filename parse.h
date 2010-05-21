// -*- C++ -*-
// Copyright (C) 2005-2010 Red Hat Inc.
// Copyright (C) 2007 Bull S.A.S
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.


#ifndef PARSE_H
#define PARSE_H

#include <string>
#include <iostream>
#include <stdexcept>

struct stapfile;

struct source_loc
{
  stapfile* file;
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
  bool skip_some;
  parse_error (const std::string& msg):
    runtime_error (msg), tok (0), skip_some (true) {}
  parse_error (const std::string& msg, const token* t):
    runtime_error (msg), tok (t), skip_some (true) {}
  parse_error (const std::string& msg, bool skip):
    runtime_error (msg), tok (0), skip_some (skip) {}
};


struct systemtap_session;

stapfile* parse (systemtap_session& s, std::istream& i, bool privileged);
stapfile* parse (systemtap_session& s, const std::string& n, bool privileged);


#endif // PARSE_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
