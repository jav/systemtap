// tapset for HW performance monitoring
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

#include <string>

using namespace std;
using namespace __gnu_cxx;


static const string TOK_PERF("perf");


// ------------------------------------------------------------------------
// perf event derived probes
// ------------------------------------------------------------------------
// This is a new interface to the perfmon hw.
//

struct perf_derived_probe: public derived_probe
{
  int64_t interval;
  perf_derived_probe (probe* p, probe_point* l, int64_t i);
  virtual void join_group (systemtap_session& s);
};


struct perf_derived_probe_group: public generic_dpg<perf_derived_probe>
{
  void emit_interval (translator_output* o);
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


perf_derived_probe::perf_derived_probe (probe* p, probe_point* l,
                                          int64_t i):
  derived_probe (p, l), interval (i)
{
}


void
perf_derived_probe::join_group (systemtap_session& s)
{
  if (! s.perf_derived_probes)
    s.perf_derived_probes = new perf_derived_probe_group ();
  s.perf_derived_probes->enroll (this);
}


void
perf_derived_probe_group::emit_interval (translator_output* o)
{
}

void
perf_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- perf probes ---- */";
  s.op->newline() << "#include \"perf.c\"";
  /* declarations */
  for (unsigned i=0; i < probes.size(); i++)
    {
    }
  s.op->newline();

  /* wrapper functions */
  for (unsigned i=0; i < probes.size(); i++) {
    s.op->newline() << "void enter_perf_probe_" << i <<" (struct perf_event *e, int nmi,";
    s.op->newline(1) << "struct perf_sample_data *data,";
    s.op->newline() << "struct pt_regs *regs)";
    s.op->newline(-1) << "{";
    s.op->newline(1) <<	"/* handle_perf_probe(pp, &probe_NNN, regs); */";
    s.op->newline(-1) << "}";
  }

  s.op->newline() << "static void handle_perf_probe (const char *pp,";
  s.op->newline(1) << "void (*ph) (struct context*),";
  s.op->newline() << "struct pt_regs *regs)";
  s.op->newline(-1) << "{";

  s.op->newline(1) << "if ((atomic_read (&session_state) == STAP_SESSION_STARTING) ||";
  s.op->newline(1) << "    (atomic_read (&session_state) == STAP_SESSION_RUNNING)) {";
  s.op->newline(-1) << "}";
  s.op->newline() << "{";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "pp");
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "(*ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
}


void
perf_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "/* struct stap_perf_probe* stp = & stap_perf_probes [i]; */";
  s.op->newline(-1) << "}"; // for loop
}


void
perf_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
  s.op->newline(1) << "/* FIXME */;";
  s.op->indent(-1);
}


struct perf_builder: public derived_probe_builder
{
    virtual void build(systemtap_session & sess,
                       probe * base, probe_point * location,
                       literal_map_t const & parameters,
                       vector<derived_probe *> & finished_results);

    static void register_patterns(systemtap_session& s);
};


void
perf_builder::build(systemtap_session & sess,
    probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    vector<derived_probe *> & finished_results)
{
  int64_t period;
  /* FIXME */
  if (get_param(parameters, "cycles", period))
    {
    }
  else
    throw semantic_error ("unrecognized perf variant");
  finished_results.push_back
    (new perf_derived_probe(base, location, period));
}


void
register_tapset_perf(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new perf_builder();

  root = root->bind(TOK_PERF);

  root->bind_num("cycles")
    ->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
