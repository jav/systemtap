// tapset for begin/end/error/never
// Copyright (C) 2005-2009 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008 James.Bottomley@HansenPartnership.com
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "session.h"
#include "tapsets.h"
#include "translate.h"
#include "util.h"

#include <algorithm>
#include <string>


using namespace std;
using namespace __gnu_cxx;


static const string TOK_BEGIN("begin");
static const string TOK_END("end");
static const string TOK_ERROR("error");
static const string TOK_NEVER("never");


// ------------------------------------------------------------------------
// begin/end/error probes are run right during registration / deregistration
// ------------------------------------------------------------------------

enum be_t { BEGIN, END, ERROR };

struct be_derived_probe: public derived_probe
{
  be_t type;
  int64_t priority;

  be_derived_probe (probe* p, probe_point* l, be_t t, int64_t pr):
    derived_probe (p, l), type (t), priority (pr) {}

  void join_group (systemtap_session& s);

  static inline bool comp(be_derived_probe const *a,
                          be_derived_probe const *b)
  {
    // This allows the BEGIN/END/ERROR probes to intermingle.
    // But that's OK - they're always treversed with a nested
    // "if (type==FOO)" conditional.
    return a->priority < b->priority;
  }

  bool needs_global_locks () { return false; }
  // begin/end probes don't need locks around global variables, since
  // they aren't run concurrently with any other probes
};


struct be_derived_probe_group: public generic_dpg<be_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};

struct be_builder: public derived_probe_builder
{
  be_t type;

  be_builder(be_t t) : type(t) {}

  virtual void build(systemtap_session &,
                     probe * base,
                     probe_point * location,
                     literal_map_t const & parameters,
                     vector<derived_probe *> & finished_results)
  {
    int64_t priority;
    if ((type == BEGIN && !get_param(parameters, TOK_BEGIN, priority)) ||
        (type == END && !get_param(parameters, TOK_END, priority)) ||
        (type == ERROR && !get_param(parameters, TOK_ERROR, priority)))
      priority = 0;
    finished_results.push_back
      (new be_derived_probe(base, location, type, priority));
  }
};


void
be_derived_probe::join_group (systemtap_session& s)
{
  if (! s.be_derived_probes)
    s.be_derived_probes = new be_derived_probe_group ();
  s.be_derived_probes->enroll (this);
}


void
be_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  map<be_t, const char *> states;
  states[BEGIN] = "STAP_SESSION_STARTING";
  states[END] = "STAP_SESSION_STOPPING";
  states[ERROR] = "STAP_SESSION_ERROR";

  s.op->newline() << "/* ---- begin/end/error probes ---- */";

  // NB: We emit the table in sorted order here, so we don't have to
  // store the priority numbers as integers and sort at run time.

  sort(probes.begin(), probes.end(), be_derived_probe::comp);

  s.op->newline() << "static struct stap_be_probe {";
  s.op->newline(1) << "void (*ph)(struct context*);";
  s.op->newline() << "const char* pp;";
  s.op->newline() << "int state, type;";
  s.op->newline(-1) << "} stap_be_probes[] = {";
  s.op->indent(1);

  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{";
      s.op->line() << " .pp="
                   << lex_cast_qstring (*probes[i]->sole_location()) << ",";
      s.op->line() << " .ph=&" << probes[i]->name << ",";
      s.op->line() << " .state=" << states[probes[i]->type] << ",";
      s.op->line() << " .type=" << probes[i]->type;
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";

  s.op->newline() << "static void enter_be_probe (struct stap_be_probe *stp) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "stp->state", "stp->pp", false);
  s.op->newline() << "(*stp->ph) (c);";
  common_probe_entryfn_epilogue (s.op, false);
  s.op->newline(-1) << "}";
}

void
be_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_be_probe* stp = & stap_be_probes [i];";
  s.op->newline() << "if (stp->type == " << BEGIN << ")";
  s.op->newline(1) << "enter_be_probe (stp); /* rc = 0 */";
  // NB: begin probes that cause errors do not constitute registration
  // failures.  An error message will probably get printed and if
  // MAXERRORS was left at 1, we'll get an stp_exit.  The
  // error-handling probes will be run during the ordinary
  // unregistration phase.
  s.op->newline(-2) << "}";
}

void
be_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_be_probe* stp = & stap_be_probes [i];";
  s.op->newline() << "if (stp->type == " << END << ")";
  s.op->newline(1) << "enter_be_probe (stp);";
  s.op->newline(-2) << "}";

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_be_probe* stp = & stap_be_probes [i];";
  s.op->newline() << "if (stp->type == " << ERROR << ")";
  s.op->newline(1) << "enter_be_probe (stp);";
  s.op->newline(-2) << "}";
}



// ------------------------------------------------------------------------
// never probes are never run
// ------------------------------------------------------------------------

struct never_derived_probe: public derived_probe
{
  never_derived_probe (probe* p): derived_probe (p) {}
  never_derived_probe (probe* p, probe_point* l): derived_probe (p, l) {}
  void join_group (systemtap_session&) { /* thus no probe_group */ }
};


struct never_builder: public derived_probe_builder
{
  never_builder() {}
  virtual void build(systemtap_session &,
                     probe * base,
                     probe_point * location,
                     literal_map_t const &,
                     vector<derived_probe *> & finished_results)
  {
    finished_results.push_back(new never_derived_probe(base, location));
  }
};



// ------------------------------------------------------------------------
// unified registration for begin/end/error/never
// ------------------------------------------------------------------------

void
register_tapset_been(systemtap_session& s)
{
  match_node* root = s.pattern_root;

  root->bind(TOK_BEGIN)
    ->allow_unprivileged()
    ->bind(new be_builder(BEGIN));
  root->bind_num(TOK_BEGIN)
    ->allow_unprivileged()
    ->bind(new be_builder(BEGIN));

  root->bind(TOK_END)
    ->allow_unprivileged()
    ->bind(new be_builder(END));
  root->bind_num(TOK_END)
    ->allow_unprivileged()
    ->bind(new be_builder(END));

  root->bind(TOK_ERROR)
    ->allow_unprivileged()
    ->bind(new be_builder(ERROR));
  root->bind_num(TOK_ERROR)
    ->allow_unprivileged()
    ->bind(new be_builder(ERROR));

  root->bind(TOK_NEVER)
    ->allow_unprivileged()
    ->bind(new never_builder());
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
