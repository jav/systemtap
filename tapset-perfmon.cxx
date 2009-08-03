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
#include "util.h"

#include <string>

#ifdef PERFMON
#include <perfmon/pfmlib.h>
#include <perfmon/perfmon.h>
#endif


using namespace std;
using namespace __gnu_cxx;



// ------------------------------------------------------------------------
// perfmon derived probes
// ------------------------------------------------------------------------
// This is a new interface to the perfmon hw.
//


struct perfmon_var_expanding_visitor: public var_expanding_visitor
{
  systemtap_session & sess;
  unsigned counter_number;
  perfmon_var_expanding_visitor(systemtap_session & s, unsigned c):
    sess(s), counter_number(c) {}
  void visit_target_symbol (target_symbol* e);
};


void
perfmon_var_expanding_visitor::visit_target_symbol (target_symbol *e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;
  bool lvalue = is_active_lvalue(e);

  if (lvalue )
    throw semantic_error("writes to $counter not permitted");

  string fname = string("_perfmon_tvar_get")
                  + "_" + e->base_name.substr(1)
                  + "_" + lex_cast<string>(counter_number);

  if (e->base_name != "$counter")
    throw semantic_error ("target variables not available to perfmon probes");

  if (e->addressof)
    throw semantic_error("cannot take address of perfmon variable", e->tok);

  e->assert_no_components("perfmon");

  ec->code = "THIS->__retvalue = _pfm_pmd_x[" +
          lex_cast<string>(counter_number) + "].reg_num;";
  ec->code += "/* pure */";
  fdecl->name = fname;
  fdecl->body = ec;
  fdecl->type = pe_long;
  sess.functions[fdecl->name]=fdecl;

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0;  // NB: must not resolve yet, to ensure inclusion in session

  provide (n);
}


enum perfmon_mode
{
  perfmon_count,
  perfmon_sample
};


struct perfmon_derived_probe: public derived_probe
{
protected:
  static unsigned probes_allocated;

public:
  systemtap_session & sess;
  string event;
  perfmon_mode mode;

  perfmon_derived_probe (probe* p, probe_point* l, systemtap_session &s,
                         string e, perfmon_mode m);
  virtual void join_group (systemtap_session& s);
};


struct perfmon_derived_probe_group: public generic_dpg<perfmon_derived_probe>
{
public:
  void emit_module_decls (systemtap_session&) {}
  void emit_module_init (systemtap_session&) {}
  void emit_module_exit (systemtap_session&) {}
};


struct perfmon_builder: public derived_probe_builder
{
  perfmon_builder() {}
  virtual void build(systemtap_session & sess,
                     probe * base,
                     probe_point * location,
                     literal_map_t const & parameters,
                     vector<derived_probe *> & finished_results)
  {
    string event;
    if (!get_param (parameters, "counter", event))
      throw semantic_error("perfmon requires an event");

    sess.perfmon++;

    // XXX: need to revise when doing sampling
    finished_results.push_back(new perfmon_derived_probe(base, location,
                                                         sess, event,
                                                         perfmon_count));
  }
};


unsigned perfmon_derived_probe::probes_allocated;

perfmon_derived_probe::perfmon_derived_probe (probe* p, probe_point* l,
                                              systemtap_session &s,
                                              string e, perfmon_mode m)
        : derived_probe (p, l), sess(s), event(e), mode(m)
{
  ++probes_allocated;

  // Now expand the local variables in the probe body
  perfmon_var_expanding_visitor v (sess, probes_allocated-1);
  v.replace (this->body);

  if (sess.verbose > 1)
    clog << "perfmon-based probe" << endl;
}


void
perfmon_derived_probe::join_group (systemtap_session& s)
{
  throw semantic_error ("incomplete", this->tok);

  if (! s.perfmon_derived_probes)
    s.perfmon_derived_probes = new perfmon_derived_probe_group ();
  s.perfmon_derived_probes->enroll (this);
}


#if 0
void
perfmon_derived_probe::emit_registrations_start (translator_output* o,
                                                 unsigned index)
{
  for (unsigned i=0; i<locations.size(); i++)
    o->newline() << "enter_" << name << "_" << i << " ();";
}


void
perfmon_derived_probe::emit_registrations_end (translator_output * o,
                                               unsigned index)
{
}


void
perfmon_derived_probe::emit_deregistrations (translator_output * o)
{
}


void
perfmon_derived_probe::emit_probe_entries (translator_output * o)
{
  o->newline() << "#ifdef STP_TIMING";
  // NB: This variable may be multiply (but identically) defined.
  o->newline() << "static __cacheline_aligned Stat " << "time_" << basest()->name << ";";
  o->newline() << "#endif";

  for (unsigned i=0; i<locations.size(); i++)
    {
      probe_point *l = locations[i];
      o->newline() << "/* location " << i << ": " << *l << " */";
      o->newline() << "static void enter_" << name << "_" << i << " (void) {";

      o->indent(1);
      o->newline() << "const char* probe_point = "
                   << lex_cast_qstring(*l) << ";";

      o->newline() << "static struct pfarg_ctx _pfm_context;";
      o->newline() << "static void *_pfm_desc;";
      o->newline() << "static struct pfarg_pmc *_pfm_pmc_x;";
      o->newline() << "static int _pfm_num_pmc_x;";
      o->newline() << "static struct pfarg_pmd *_pfm_pmd_x;";
      o->newline() << "static int _pfm_num_pmd_x;";

      emit_probe_prologue (o,
                           (mode == perfmon_count ?
                            "STAP_SESSION_STARTING" :
                            "STAP_SESSION_RUNNING"),
                           "probe_point");

      // NB: locals are initialized by probe function itself
      o->newline() << name << " (c);";

      emit_probe_epilogue (o);

      o->newline(-1) << "}\n";
    }
}
#endif


#if 0
void no_pfm_event_error (string s)
{
  string msg(string("Cannot find event:" + s));
  throw semantic_error(msg);
}


void no_pfm_mask_error (string s)
{
  string msg(string("Cannot find mask:" + s));
  throw semantic_error(msg);
}


void
split(const string& s, vector<string>& v, const string & separator)
{
  string::size_type last_pos = s.find_first_not_of(separator, 0);
  string::size_type pos = s.find_first_of(separator, last_pos);

  while (string::npos != pos || string::npos != last_pos) {
    v.push_back(s.substr(last_pos, pos - last_pos));
    last_pos = s.find_first_not_of(separator, pos);
    pos = s.find_first_of(separator, last_pos);
  }
}


void
perfmon_derived_probe_group::emit_probes (translator_output* op, unparser* up)
{
  for (unsigned i=0; i < probes.size(); i++)
    {
      op->newline ();
      up->emit_probe (probes[i]);
    }
}


void
perfmon_derived_probe_group::emit_module_init (translator_output* o)
{
  int ret;
  pfmlib_input_param_t inp;
  pfmlib_output_param_t outp;
  pfarg_pmd_t pd[PFMLIB_MAX_PMDS];
  pfarg_pmc_t pc[PFMLIB_MAX_PMCS];
  pfarg_ctx_t ctx;
  pfarg_load_t load_args;
  pfmlib_options_t pfmlib_options;
  unsigned int max_counters;

  if ( probes.size() == 0)
          return;
  ret = pfm_initialize();
  if (ret != PFMLIB_SUCCESS)
    throw semantic_error("Unable to generate performance monitoring events (no libpfm)");

  pfm_get_num_counters(&max_counters);

  memset(&pfmlib_options, 0, sizeof(pfmlib_options));
  pfmlib_options.pfm_debug   = 0; /* set to 1 for debug */
  pfmlib_options.pfm_verbose = 0; /* set to 1 for debug */
  pfm_set_options(&pfmlib_options);

  memset(pd, 0, sizeof(pd));
  memset(pc, 0, sizeof(pc));
  memset(&ctx, 0, sizeof(ctx));
  memset(&load_args, 0, sizeof(load_args));

  /*
   * prepare parameters to library.
   */
  memset(&inp,0, sizeof(inp));
  memset(&outp,0, sizeof(outp));

  /* figure out the events */
  for (unsigned i=0; i<probes.size(); ++i)
    {
      if (probes[i]->event == "cycles") {
        if (pfm_get_cycle_event( &inp.pfp_events[i].event) != PFMLIB_SUCCESS)
          no_pfm_event_error(probes[i]->event);
      } else if (probes[i]->event == "instructions") {
        if (pfm_get_inst_retired_event( &inp.pfp_events[i].event) !=
            PFMLIB_SUCCESS)
          no_pfm_event_error(probes[i]->event);
      } else {
        unsigned int event_id = 0;
        unsigned int mask_id = 0;
        vector<string> event_spec;
        split(probes[i]->event, event_spec, ":");
        int num =  event_spec.size();
        int masks = num - 1;

        if (num == 0)
          throw semantic_error("No events found");

        /* setup event */
        if (pfm_find_event(event_spec[0].c_str(), &event_id) != PFMLIB_SUCCESS)
          no_pfm_event_error(event_spec[0]);
        inp.pfp_events[i].event = event_id;

        /* set up masks */
        if (masks > PFMLIB_MAX_MASKS_PER_EVENT)
          throw semantic_error("Too many unit masks specified");

        for (int j=0; j < masks; j++) {
                if (pfm_find_event_mask(event_id, event_spec[j+1].c_str(),
                                        &mask_id) != PFMLIB_SUCCESS)
            no_pfm_mask_error(string(event_spec[j+1]));
          inp.pfp_events[i].unit_masks[j] = mask_id;
        }
        inp.pfp_events[i].num_masks = masks;
      }
    }

  /* number of counters in use */
  inp.pfp_event_count = probes.size();

  // XXX: no elimination of duplicated counters
  if (inp.pfp_event_count>max_counters)
          throw semantic_error("Too many performance monitoring events.");

  /* count events both in kernel and user-space */
  inp.pfp_dfl_plm   = PFM_PLM0 | PFM_PLM3;

  /* XXX: some cases a perfmon register might be used of watch dog
     this code doesn't handle that case */

  /* figure out the pmcs for the events */
  if ((ret=pfm_dispatch_events(&inp, NULL, &outp, NULL)) != PFMLIB_SUCCESS)
          throw semantic_error("Cannot configure events");

  for (unsigned i=0; i < outp.pfp_pmc_count; i++) {
    pc[i].reg_num   = outp.pfp_pmcs[i].reg_num;
    pc[i].reg_value = outp.pfp_pmcs[i].reg_value;
  }

  /*
   * There could be more pmc settings than pmd.
   * Figure out the actual pmds to use.
   */
  for (unsigned i=0, j=0; i < inp.pfp_event_count; i++) {
    pd[i].reg_num   = outp.pfp_pmcs[j].reg_pmd_num;
    for(; j < outp.pfp_pmc_count; j++)
      if (outp.pfp_pmcs[j].reg_evt_idx != i) break;
  }

  // Output the be probes create function
  o->newline() << "static int register_perfmon_probes (void) {";
  o->newline(1) << "int rc = 0;";

  o->newline() << "/* data for perfmon */";
  o->newline() << "static int _pfm_num_pmc = " << outp.pfp_pmc_count << ";";
  o->newline() << "static struct pfarg_pmc _pfm_pmc[" << outp.pfp_pmc_count
               << "] = {";
  /* output the needed bits for pmc here */
  for (unsigned i=0; i < outp.pfp_pmc_count; i++) {
    o->newline() << "{.reg_num=" << pc[i].reg_num << ", "
                 << ".reg_value=" << lex_cast_hex<string>(pc[i].reg_value)
                 << "},";
  }

  o->newline() << "};";
  o->newline() << "static int _pfm_num_pmd = " << inp.pfp_event_count << ";";
  o->newline() << "static struct pfarg_pmd _pfm_pmd[" << inp.pfp_event_count
               << "] = {";
  /* output the needed bits for pmd here */
  for (unsigned i=0; i < inp.pfp_event_count; i++) {
    o->newline() << "{.reg_num=" << pd[i].reg_num << ", "
                 << ".reg_value=" << pd[i].reg_value << "},";
  }
  o->newline() << "};";
  o->newline();

  o->newline() << "_pfm_pmc_x=_pfm_pmc;";
  o->newline() << "_pfm_num_pmc_x=_pfm_num_pmc;";
  o->newline() << "_pfm_pmd_x=_pfm_pmd;";
  o->newline() << "_pfm_num_pmd_x=_pfm_num_pmd;";

  // call all the function bodies associated with perfcounters
  for (unsigned i=0; i < probes.size (); i++)
    probes[i]->emit_registrations_start (o,i);

  /* generate call to turn on instrumentation */
  o->newline() << "_pfm_context.ctx_flags |= PFM_FL_SYSTEM_WIDE;";
  o->newline() << "rc = rc || _stp_perfmon_setup(&_pfm_desc, &_pfm_context,";
  o->newline(1) << "_pfm_pmc, _pfm_num_pmc,";
  o->newline() << "_pfm_pmd, _pfm_num_pmd);";
  o->newline(-1);

  o->newline() << "return rc;";
  o->newline(-1) << "}\n";

  // Output the be probes destroy function
  o->newline() << "static void unregister_perfmon_probes (void) {";
  o->newline(1) << "_stp_perfmon_shutdown(_pfm_desc);";
  o->newline(-1) << "}\n";
}
#endif


void
register_tapset_perfmon(systemtap_session& s)
{
  s.pattern_root->bind("perfmon")->bind_str("counter")
    ->bind(new perfmon_builder());
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
