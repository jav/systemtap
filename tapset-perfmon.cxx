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

using namespace std;
using namespace __gnu_cxx;


static const string TOK_PERF("perf");
static const string TOK_EVENT("event");
static const string TOK_SAMPLE("sample");


// ------------------------------------------------------------------------
// perf event derived probes
// ------------------------------------------------------------------------
// This is a new interface to the perfmon hw.
//

struct perf_derived_probe: public derived_probe
{
  string event_name;
  string event_type;
  string event_config;
  int64_t interval;
  perf_derived_probe (probe* p, probe_point* l, const string& name,
                      const string& type, const string& config, int64_t i);
  virtual void join_group (systemtap_session& s);
};


struct perf_derived_probe_group: public generic_dpg<perf_derived_probe>
{
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


perf_derived_probe::perf_derived_probe (probe* p, probe_point* l,
                                        const string& name,
                                        const string& type,
                                        const string& config,
                                        int64_t i):
  derived_probe (p, new probe_point(*l) /* .components soon rewritten */),
  event_name (name), event_type (type), event_config (config), interval (i)
{
  vector<probe_point::component*>& comps = this->sole_location()->components;
  comps.clear();
  comps.push_back (new probe_point::component (TOK_PERF));
  comps.push_back (new probe_point::component (TOK_EVENT, new literal_string (event_name)));
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
  s.op->newline() << "static void handle_perf_probe (const char *pp, "
                  << "void (*ph) (struct context*), struct pt_regs *regs);";
  for (unsigned i=0; i < probes.size(); i++)
    s.op->newline() << "static void enter_perf_probe_" << i
                    << " (struct perf_event *e, int nmi, "
                    << "struct perf_sample_data *data, "
                    << "struct pt_regs *regs);";
  s.op->newline();

  /* data structures */
  s.op->newline() << "static struct stap_perf_probe {";
  s.op->newline(1) << "struct perf_event_attr attr;";
  s.op->newline() << "perf_overflow_handler_t cb;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "Perf *perf;";
  s.op->newline(-1) << "} stap_perf_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline() << "{";
      s.op->newline(1) << ".attr={ "
                       << ".type=" << probes[i]->event_type << ", "
                       << ".config=" << probes[i]->event_config << ", "
                       << "{ .sample_period=" << probes[i]->interval << "ULL }},";
      s.op->newline() << ".cb=enter_perf_probe_" << i << ", ";
      s.op->newline() << ".pp=" << lex_cast_qstring (*probes[i]->sole_location()) << ",";
      s.op->newline() << ".ph=" << probes[i]->name << ",";
      s.op->newline(-1) << "},";
    }
  s.op->newline(-1) << "};";
  s.op->newline();

  /* wrapper functions */
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline() << "static void enter_perf_probe_" << i
                      << " (struct perf_event *e, int nmi, "
                      << "struct perf_sample_data *data, "
                      << "struct pt_regs *regs)";
      s.op->newline() << "{";
      s.op->newline(1) << "handle_perf_probe(" << lex_cast_qstring (*probes[i]->sole_location())
                       << ", " << probes[i]->name << ", regs);";
      s.op->newline(-1) << "}";
    }
  s.op->newline();

  s.op->newline() << "static void handle_perf_probe (const char *pp, "
                  << "void (*ph) (struct context*), struct pt_regs *regs)";
  s.op->newline() << "{";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "pp");
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "(*ph) (c);";
  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";
}


void
perf_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_perf_probe* stp = & stap_perf_probes [i];";
  s.op->newline() << "stp->perf = _stp_perf_init(&stp->attr, stp->cb, stp->pp, stp->ph);";
  s.op->newline() << "if (stp->perf == NULL) {";
  s.op->newline(1) << "rc = -EINVAL;";
  s.op->newline() << "probe_point = stp->pp;";
  s.op->newline() << "for (j=0; j<i; j++) {";
  s.op->newline(1) << "stp = & stap_perf_probes [j];";
  s.op->newline() << "_stp_perf_del(stp->perf);";
  s.op->newline() << "stp->perf = NULL;";
  s.op->newline(-1) << "}"; // for unwind loop
  s.op->newline(-1) << "}"; // if-error
  s.op->newline(-1) << "}"; // for loop
}


void
perf_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_perf_probe* stp = & stap_perf_probes [i];";
  s.op->newline() << "_stp_perf_del(stp->perf);";
  s.op->newline() << "stp->perf = NULL;";
  s.op->newline(-1) << "}"; // for loop
}


struct perf_builder: public derived_probe_builder
{
    virtual void build(systemtap_session & sess,
                       probe * base, probe_point * location,
                       literal_map_t const & parameters,
                       vector<derived_probe *> & finished_results);

    static void register_patterns(systemtap_session& s);
private:
    typedef map<string, pair<string, string> > event_map_t;
    event_map_t events;
    void translate_event(const string& name, string& type, string& config);
};


void
perf_builder::build(systemtap_session & sess,
    probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    vector<derived_probe *> & finished_results)
{
  string name, type, config;
  bool has_name = get_param(parameters, TOK_EVENT, name);
  assert(has_name);
  translate_event(name, type, config);
  if (type.empty() || config.empty())
    throw semantic_error("invalid perf event " + lex_cast_qstring(name),
                         parameters.find(TOK_EVENT)->second->tok);
  // TODO support wildcards, e.g. stap -l 'perf.event("*")'

  int64_t period;
  bool has_period = get_param(parameters, TOK_SAMPLE, period);
  if (!has_period)
    period = 1000000;
  else if (period < 1)
    throw semantic_error("invalid perf sample period " + lex_cast(period),
                         parameters.find(TOK_SAMPLE)->second->tok);

  if (sess.verbose > 1)
    clog << "perf probe event=" << lex_cast_qstring(name) << " type=" << type
         << " config=" << config << " period=" << period << endl;

  finished_results.push_back
    (new perf_derived_probe(base, location, name, type, config, period));
}


void
perf_builder::translate_event(const string& name, string& type, string& config)
{
  if (events.empty())
    {
      // XXX These are hard-coded in the perf tool, and we have to do the same.
      // --- (see linux-2.6/tools/perf/util/parse-events.c)
      // --- It would be much nicer to somehow generate this dynamically...

#define CHW(event) event_map_t::mapped_type("PERF_TYPE_HARDWARE", "PERF_COUNT_HW_" #event)
      events["cpu-cycles"]              = CHW(CPU_CYCLES);
      events["cycles"]                  = CHW(CPU_CYCLES);
      events["instructions"]            = CHW(INSTRUCTIONS);
      events["cache-references"]        = CHW(CACHE_REFERENCES);
      events["cache-misses"]            = CHW(CACHE_MISSES);
      events["branch-instructions"]     = CHW(BRANCH_INSTRUCTIONS);
      events["branches"]                = CHW(BRANCH_INSTRUCTIONS);
      events["branch-misses"]           = CHW(BRANCH_MISSES);
      events["bus-cycles"]              = CHW(BUS_CYCLES);
#undef CHW

#define CSW(event) event_map_t::mapped_type("PERF_TYPE_SOFTWARE", "PERF_COUNT_SW_" #event)
      events["cpu-clock"]               = CSW(CPU_CLOCK);
      events["task-clock"]              = CSW(TASK_CLOCK);
      events["page-faults"]             = CSW(PAGE_FAULTS);
      events["faults"]                  = CSW(PAGE_FAULTS);
      events["minor-faults"]            = CSW(PAGE_FAULTS_MIN);
      events["major-faults"]            = CSW(PAGE_FAULTS_MAJ);
      events["context-switches"]        = CSW(CONTEXT_SWITCHES);
      events["cs"]                      = CSW(CONTEXT_SWITCHES);
      events["cpu-migrations"]          = CSW(CPU_MIGRATIONS);
      events["migrations"]              = CSW(CPU_MIGRATIONS);
      events["alignment-faults"]        = CSW(ALIGNMENT_FAULTS);
      events["emulation-faults"]        = CSW(EMULATION_FAULTS);
#undef CSW
    }

  event_map_t::iterator it = events.find(name);
  if (it != events.end())
    {
      type = it->second.first;
      config = it->second.second;
    }
}


void
register_tapset_perf(systemtap_session& s)
{
  // make sure we have support before registering anything
  // XXX need additional version checks too?
  // --- perhaps look for export of perf_event_create_kernel_counter
  if (s.kernel_config["CONFIG_PERF_EVENTS"] != "y")
    return;

  derived_probe_builder *builder = new perf_builder();
  match_node* perf = s.pattern_root->bind(TOK_PERF);

  match_node* event = perf->bind_str(TOK_EVENT);
  event->bind(builder);
  event->bind_num(TOK_SAMPLE)->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
