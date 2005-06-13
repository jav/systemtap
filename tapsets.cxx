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
#include "translate.h"
#include <iostream>
#include <sstream>

using namespace std;



// ------------------------------------------------------------------------



// begin or end probes
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


void
symresolution_info::derive_probes (probe *p, vector<derived_probe*>& dps)
{
  for (unsigned i=0; i<p->locations.size(); i++)
    {
      probe_point *l = p->locations[i];

      // XXX: need a better probe_point matching technique
      if (l->components.size() == 1 &&
          l->components[0]->functor == "begin" &&
          l->components[0]->arg == 0)
        dps.push_back (new be_derived_probe (p, p->locations[i], true));
      else if (l->components.size() == 1 &&
               l->components[0]->functor == "end" &&
               l->components[0]->arg == 0)
        dps.push_back (new be_derived_probe (p, p->locations[i], false));
      else
        throw semantic_error ("no match for probe point", l->tok);
    }
}


// ------------------------------------------------------------------------


// begin/end probes are run right during registration / deregistration


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
