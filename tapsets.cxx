// tapset resolution
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "elaborate.h"
#include "tapsets.h"
#include "translate.h"
#include <iostream>
#include <sstream>
#include <deque>
#include <vector>
#include <map>

using namespace std;

// match_key

match_key::match_key(string const & n) 
  : name(n), 
    have_parameter(false), 
    parameter_type(tok_junk)
{
}

match_key::match_key(probe_point::component const & c)
  : name(c.functor),
    have_parameter(c.arg != NULL),
    parameter_type(c.arg ? c.arg->tok->type : tok_junk)
{
}

match_key &
match_key::with_number() 
{
  have_parameter = true;
  parameter_type = tok_number;
  return *this;
}

match_key &
match_key::with_string() 
{
  have_parameter = true;
  parameter_type = tok_string;
  return *this;
}

string 
match_key::str() const
{
  if (have_parameter)
    switch (parameter_type)
      {
      case tok_string: return name + "(string)";
      case tok_number: return name + "(number)";
      default: return name + "(...)";
      }
  return name;
}

bool 
match_key::operator<(match_key const & other) const
{
  return ((name < other.name)
	  
	  || (name == name 
	      && have_parameter < other.have_parameter)
	  
	  || (name == name 
	      && have_parameter == other.have_parameter 
	      && parameter_type < other.parameter_type));
}


// match_node

match_node::match_node()
  : end(NULL)
{}

match_node & 
match_node::bind(match_key const & k) 
{
  map<match_key, match_node *>::const_iterator i = sub.find(k);
  if (i != sub.end())
    return *i->second;
  match_node * n = new match_node();
  sub.insert(make_pair(k, n));
  return *n;
}

void 
match_node::bind(derived_probe_builder * e)
{
  if (end)
    throw semantic_error("already have a pattern ending");
  end = e;
}

match_node & 
match_node::bind(string const & k)
{
  return bind(match_key(k));
}

match_node & 
match_node::bind_str(string const & k)
{
  return bind(match_key(k).with_string());
}

match_node & 
match_node::bind_num(string const & k)
{
  return bind(match_key(k).with_number());
}

derived_probe_builder * 
match_node::find_builder(vector<probe_point::component *> const & components,
			 unsigned pos,
			 vector< pair<string, literal *> > & parameters)
{
  assert(pos <= components.size());
  if (pos == components.size())
    {
      // probe_point ends here. we match iff we have
      // an "end" entry here. if we don't, it'll be null.
      return end;
    }
  else
    {
      // probe_point contains a component here. we match iff there's
      // an entry in the sub table, and its value matches the rest
      // of the probe_point.
      match_key k(*components[pos]);
      map<match_key, match_node *>::const_iterator i = sub.find(k);
      if (i == sub.end())
	return NULL;
      else
	{
	  derived_probe_builder * builder = NULL;
	  if (k.have_parameter)
	    {
	      assert(components[pos]->arg);
	      parameters.push_back(make_pair(components[pos]->functor, 
					     components[pos]->arg));
	    }
	  builder = i->second->find_builder(components, pos+1, parameters);
	  if (k.have_parameter && !builder)
	    parameters.pop_back();
	  return builder;
	}
    }
}


static void
param_vec_to_map(vector< pair<string, literal *> > const & param_vec, 
		   map<string, literal *> & param_map)
{
  for (vector< pair<string, literal *> >::const_iterator i = param_vec.begin();
       i != param_vec.end(); ++i)
    {
      param_map[i->first] = i->second;
    }
}

// XXX: bind patterns for probe aliases found in AST

struct 
alias_derived_probe
{
  alias_derived_probe(probe_point * expansion) 
    : alias_expansion(expansion) {}
  probe_point * alias_expansion;
  void emit_registrations (translator_output* o, unsigned i) {}
  void emit_deregistrations (translator_output* o, unsigned i) {}
  void emit_probe_entries (translator_output* o, unsigned i) {}
};


// the root of the global pattern-matching tree
static match_node * root_node;


// the match-and-expand loop

void
symresolution_info::derive_probes (probe *p, vector<derived_probe*>& dps)
{
  if (!root_node)
    {
      root_node = new match_node();
      register_standard_tapsets(*root_node);
    }

  assert(root_node);

  deque<probe_point *> work(p->locations.begin(), p->locations.end());

  while(!work.empty())
    {
      probe_point *loc = work.front();
      work.pop_front();

      vector< pair<string, literal *> > param_vec;
      map<string, literal *> param_map;

      derived_probe_builder * builder = 
	root_node->find_builder(loc->components, 0, param_vec);

      if (!builder)
	throw semantic_error ("no match for probe point", loc->tok);

      param_vec_to_map(param_vec, param_map);

      derived_probe *derived = builder->build(p, loc, param_map);
      assert(derived);

      // append to worklist if it's an alias; append to result otherwise
      alias_derived_probe *as_alias = dynamic_cast<alias_derived_probe *>(derived);
      if (as_alias)
	{
	  work.push_back(as_alias->alias_expansion);
	  delete derived;
	}
      else
	dps.push_back (derived);      
    }
}


// ------------------------------------------------------------------------
// begin/end probes are run right during registration / deregistration
// ------------------------------------------------------------------------

struct be_derived_probe: public derived_probe
{
  bool begin;
  be_derived_probe (probe* p, bool b): derived_probe (p), begin (b) {}
  be_derived_probe (probe* p, probe_point* l, bool b):
    derived_probe (p, l), begin (b) {}

  void emit_registrations (translator_output* o, unsigned i);
  void emit_deregistrations (translator_output* o, unsigned i);
  void emit_probe_entries (translator_output* o, unsigned i);
};

struct
be_builder 
  : public derived_probe_builder
{
  bool begin;
  be_builder(bool b) : begin(b) {}
  virtual derived_probe * build(probe * base, 
				probe_point * location,
				map<string, literal *> const & parameters)
  {
    return new be_derived_probe(base, location, begin);
  }
  virtual ~be_builder() {}
};


void 
be_derived_probe::emit_registrations (translator_output* o, unsigned j)
{
  if (begin)
    for (unsigned i=0; i<locations.size(); i++)
      {
        o->newline() << "enter_" << j << "_" << i << " ()";
        o->newline() << "rc = errorcount;";
      }
  else
    o->newline() << "rc = 0;";
}


void 
be_derived_probe::emit_deregistrations (translator_output* o, unsigned j)
{
  if (begin)
    o->newline() << "rc = 0;";
  else
    for (unsigned i=0; i<locations.size(); i++)
      {
        o->newline() << "enter_" << j << "_" << i << " ()";
        o->newline() << "rc = errorcount;";
      }
}


void
be_derived_probe::emit_probe_entries (translator_output* o, unsigned j)
{
  for (unsigned i=0; i<locations.size(); i++)
    {
      probe_point *l = locations[i];
      o->newline() << "/* location " << i << ": " << *l << " */";
      o->newline() << "static void enter_" << j << "_" << i << " ()";
      o->newline() << "{";
      o->newline(1) << "struct context* c = & contexts [0];";
      // XXX: assert #0 is free; need locked search instead
      o->newline() << "if (c->busy) { errorcount ++; return; }";
      o->newline() << "c->busy ++;";
      o->newline() << "c->actioncount = 0;";
      o->newline() << "c->nesting = 0;";
      // NB: locals are initialized by probe function itself
      o->newline() << "probe_" << j << " (c);";
      o->newline() << "c->busy --;";
      o->newline(-1) << "}" << endl;
    }
}


// ------------------------------------------------------------------------
//  dwarf derived probes XXX: todo dwfl integration
// ------------------------------------------------------------------------

struct dwarf_derived_probe: public derived_probe
{
  bool kernel;
  map<string, literal *> params;

  dwarf_derived_probe (probe* p, probe_point* l, bool kernel, 
		       map<string, literal *> const & params):
    derived_probe (p, l), 
    kernel (kernel) {}

  virtual void emit_registrations (translator_output* o, unsigned i);
  virtual void emit_deregistrations (translator_output* o, unsigned i);
  virtual void emit_probe_entries (translator_output* o, unsigned i);
  virtual ~dwarf_derived_probe() {}
};

struct
dwarf_builder 
  : public derived_probe_builder
{
  bool begin;
  dwarf_builder(bool b) : begin(b) {}
  virtual derived_probe * build(probe * base, 
				probe_point * location,
				map<string, literal *> const & parameters)
  {
    return new dwarf_derived_probe(base, location, begin, parameters);
  }
  virtual ~dwarf_builder() {}
};

void 
dwarf_derived_probe::emit_registrations (translator_output* o, unsigned i)
{
}

void 
dwarf_derived_probe::emit_deregistrations (translator_output* o, unsigned i)
{
}

void 
dwarf_derived_probe::emit_probe_entries (translator_output* o, unsigned i)
{
}



// ------------------------------------------------------------------------
//  standard tapset registry
// ------------------------------------------------------------------------

void 
register_standard_tapsets(match_node & root)
{
  // rudimentary binders for begin and end targets
  root.bind("begin").bind(new be_builder(true));
  root.bind("end").bind(new be_builder(false));

  // various flavours of dwarf lookup (on the kernel)
  dwarf_builder *kern = new dwarf_builder(true);
  root.bind("kernel").bind_str("function").bind(kern);
  root.bind("kernel").bind_str("function").bind_num("line").bind(kern);
  root.bind_str("module").bind(kern);
  root.bind_str("module").bind_str("function").bind(kern);

}
