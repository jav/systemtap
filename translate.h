// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "staptree.h"
#include "parse.h"
#include <iostream>
#include <fstream>


// ------------------------------------------------------------------------

// Output context for systemtap translation, intended to allow
// pretty-printing.
class translator_output
{
  std::ofstream* o2;
  std::ostream& o;
  unsigned tablevel;

public:
  translator_output (std::ostream& file);
  translator_output (const std::string& filename);
  ~translator_output ();

  std::ostream& newline (int indent = 0);
  void indent (int indent = 0);
  std::ostream& line();
};


// An unparser instance is in charge of emitting code for generic
// probe bodies, functions, globals.
struct unparser
{
  virtual ~unparser () {}

  virtual void emit_common_header () = 0;
  // #include<...>
  //
  // #define MAXNESTING nnn
  // #define MAXCONCURRENCY mmm
  // #define MAXSTRINGLEN ooo
  //
  // enum session_state_t {
  //   starting, begin, running, suspended, errored, ending, ended
  // };
  // static atomic_t session_state;
  // static atomic_t errorcount; /* subcategorize? */
  //
  // struct context {
  //   unsigned busy;
  //   unsigned actioncount;
  //   unsigned nesting;
  //   union {
  //     struct { .... } probe_NUM_locals;
  //     struct { .... } function_NAME_locals;
  //   } locals [MAXNESTING];
  // } context [MAXCONCURRENCY];

  virtual void emit_global (vardecl* v) = 0;
  // static TYPE global_NAME;
  // static DEFINE_RWLOCK(global_NAME_lock);

  virtual void emit_functionsig (functiondecl* v) = 0;
  // static void function_NAME (context* c);

  virtual void emit_module_init () = 0;
  virtual void emit_module_exit () = 0;
  // XXX

  virtual void emit_function (functiondecl* v) = 0;
  // void function_NAME (struct context* c) {
  //   ....
  // }

  virtual void emit_probe (derived_probe* v, unsigned i) = 0;
  // void probe_NUMBER (struct context* c) {
  //   ... lifecycle
  //   ....
  // }
  // ... then call over to the derived_probe's emit_probe_entries() fn
};


int translate_pass (systemtap_session& s);


#endif // TRANSLATE_H
