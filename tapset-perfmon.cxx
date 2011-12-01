// tapset for HW performance monitoring
// Copyright (C) 2005-2010 Red Hat Inc.
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

extern "C" {
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}

using namespace std;
using namespace __gnu_cxx;


static const string TOK_PERF("perf");
static const string TOK_TYPE("type");
static const string TOK_CONFIG("config");
static const string TOK_SAMPLE("sample");


// ------------------------------------------------------------------------
// perf event derived probes
// ------------------------------------------------------------------------
// This is a new interface to the perfmon hw.
//

struct perf_derived_probe: public derived_probe
{
  int64_t event_type;
  int64_t event_config;
  int64_t interval;
  perf_derived_probe (probe* p, probe_point* l, int64_t type, int64_t config, int64_t i);
  virtual void join_group (systemtap_session& s);
};


struct perf_derived_probe_group: public generic_dpg<perf_derived_probe>
{
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


perf_derived_probe::perf_derived_probe (probe* p, probe_point* l,
                                        int64_t type,
                                        int64_t config,
                                        int64_t i):
  derived_probe (p, l, true /* .components soon rewritten */),
  event_type (type), event_config (config), interval (i)
{
  vector<probe_point::component*>& comps = this->sole_location()->components;
  comps.clear();
  comps.push_back (new probe_point::component (TOK_PERF));
  comps.push_back (new probe_point::component (TOK_TYPE, new literal_number(type)));
  comps.push_back (new probe_point::component (TOK_CONFIG, new literal_number (config)));
  comps.push_back (new probe_point::component (TOK_SAMPLE, new literal_number (interval)));
}


void
perf_derived_probe::join_group (systemtap_session& s)
{
  if (! s.perf_derived_probes)
    s.perf_derived_probes = new perf_derived_probe_group ();
  s.perf_derived_probes->enroll (this);
}


void
perf_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- perf probes ---- */";
  s.op->newline() << "#include \"perf.c\"";
  s.op->newline();

  /* declarations */
  s.op->newline() << "static void handle_perf_probe (unsigned i, struct pt_regs *regs);";
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline() << "#ifdef STAPCONF_PERF_HANDLER_NMI";
      s.op->newline() << "static void enter_perf_probe_" << i
                      << " (struct perf_event *e, int nmi, "
                      << "struct perf_sample_data *data, "
                      << "struct pt_regs *regs);";
      s.op->newline() << "#else";
      s.op->newline() << "static void enter_perf_probe_" << i
                      << " (struct perf_event *e, "
                      << "struct perf_sample_data *data, "
                      << "struct pt_regs *regs);";
      s.op->newline() << "#endif";
    }
  s.op->newline();

  /* data structures */
  s.op->newline() << "static struct stap_perf_probe stap_perf_probes ["
                  << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline() << "{";
      s.op->newline(1) << ".attr={ "
                       << ".type=" << probes[i]->event_type << "ULL, "
                       << ".config=" << probes[i]->event_config << "ULL, "
                       << "{ .sample_period=" << probes[i]->interval << "ULL }},";
      s.op->newline() << ".callback=enter_perf_probe_" << i << ", ";
      s.op->newline() << ".probe=" << common_probe_init (probes[i]) << ", ";
      s.op->newline(-1) << "},";
    }
  s.op->newline(-1) << "};";
  s.op->newline();

  /* wrapper functions */
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline() << "#ifdef STAPCONF_PERF_HANDLER_NMI";
      s.op->newline() << "static void enter_perf_probe_" << i
                      << " (struct perf_event *e, int nmi, "
                      << "struct perf_sample_data *data, "
                      << "struct pt_regs *regs)";
      s.op->newline() << "#else";
      s.op->newline() << "static void enter_perf_probe_" << i
                      << " (struct perf_event *e, "
                      << "struct perf_sample_data *data, "
                      << "struct pt_regs *regs)";
      s.op->newline() << "#endif";
      s.op->newline() << "{";
      s.op->newline(1) << "handle_perf_probe(" << i << ", regs);";
      s.op->newline(-1) << "}";
    }
  s.op->newline();

  s.op->newline() << "static void handle_perf_probe (unsigned i, struct pt_regs *regs)";
  s.op->newline() << "{";
  s.op->newline(1) << "struct stap_perf_probe* stp = & stap_perf_probes [i];";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "stp->probe",
				 "_STP_PROBE_HANDLER_PERF");
  s.op->newline() << "if (user_mode(regs)) {";
  s.op->newline(1)<< "c->probe_flags |= _STP_PROBE_STATE_USER_MODE;";
  s.op->newline() << "c->uregs = regs;";
  s.op->newline(-1) << "} else {";
  s.op->newline(1) << "c->kregs = regs;";
  s.op->newline(-1) << "}";

  s.op->newline() << "(*stp->probe->ph) (c);";
  common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
  s.op->newline(-1) << "}";
}


void
perf_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_perf_probe* stp = & stap_perf_probes [i];";
  s.op->newline() << "rc = _stp_perf_init(stp);";
  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "probe_point = stp->probe->pp;";
  s.op->newline() << "for (j=0; j<i; j++) {";
  s.op->newline(1) << "_stp_perf_del(& stap_perf_probes [j]);";
  s.op->newline(-1) << "}"; // for unwind loop
  s.op->newline(-1) << "}"; // if-error
  s.op->newline() << "break;";
  s.op->newline(-1) << "}"; // for loop
}


void
perf_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "_stp_perf_del(& stap_perf_probes [i]);";
  s.op->newline(-1) << "}"; // for loop
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
  // XXX need additional version checks too?
  // --- perhaps look for export of perf_event_create_kernel_counter
  if (sess.kernel_exports.find("perf_event_create_kernel_counter") == sess.kernel_exports.end())
    throw semantic_error (_("perf probes not available without exported perf_event_create_kernel_counter"));
  if (sess.kernel_config["CONFIG_PERF_EVENTS"] != "y")
    throw semantic_error (_("perf probes not available without CONFIG_PERF_EVENTS"));

  int64_t type;
  bool has_type = get_param(parameters, TOK_TYPE, type);
  assert(has_type);

  int64_t config;
  bool has_config = get_param(parameters, TOK_CONFIG, config);
  assert(has_config);

  int64_t period;
  bool has_period = get_param(parameters, TOK_SAMPLE, period);
  if (!has_period)
    period = 1000000; // XXX: better parametrize this default
  else if (period < 1)
    throw semantic_error(_("invalid perf sample period ") + lex_cast(period),
                         parameters.find(TOK_SAMPLE)->second->tok);

  if (sess.verbose > 1)
    clog << _F("perf probe type=%" PRId64 " config=%" PRId64 " period=%" PRId64, type, config, period) << endl;

  finished_results.push_back
    (new perf_derived_probe(base, location, type, config, period));
}


void
register_tapset_perf(systemtap_session& s)
{
  // NB: at this point, the binding is *not* unprivileged.

  derived_probe_builder *builder = new perf_builder();
  match_node* perf = s.pattern_root->bind(TOK_PERF);

  match_node* event = perf->bind_num(TOK_TYPE)->bind_num(TOK_CONFIG);
  event->bind(builder);
  event->bind_num(TOK_SAMPLE)->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
