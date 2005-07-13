#ifndef TAPSETS_H
#define TAPSETS_H

// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "elaborate.h"


// Helper class for describing builtin functions. Calls to builtins
// are typechecked and emitted, but the builtin definitions are *not*
// emitted by the translator (in fact, they have no definitions in
// systemtap language); they are assumed to exist outside the
// translator, in the runtime library.

class 
builtin_function
{
  functiondecl *f;
  token *id(std::string const & name);
 public:
  builtin_function(exp_type e, std::string const & name);
  builtin_function & arg(exp_type e, std::string const & name);
  void bind(systemtap_session & sess);
};

void 
register_standard_tapsets(systemtap_session & sess);


#endif // TAPSETS_H
