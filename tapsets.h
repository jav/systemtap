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
#include "translate.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <map>

struct
derived_probe_builder
{
  virtual derived_probe * build(probe * base, 
				probe_point * location,
				std::map<std::string, literal *> const & parameters) = 0;
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
  std::map<match_key, match_node *> sub;
  derived_probe_builder * end;

 public:
  match_node();
  derived_probe_builder * find_builder(std::vector<probe_point::component *> const & components,
				       unsigned pos,
				       std::vector< std::pair<std::string, literal *> > & parameters);
  
  match_node & bind(match_key const & k);
  match_node & bind(std::string const & k);
  match_node & bind_str(std::string const & k);
  match_node & bind_num(std::string const & k);
  void bind(derived_probe_builder * e);
};


void 
register_standard_tapsets(match_node & root);


#endif // TAPSETS_H
