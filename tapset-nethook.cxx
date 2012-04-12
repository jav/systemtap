// tapset for netfilter hooks
// Copyright (C) 2012 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.


#include "session.h"
#include "tapsets.h"
#include "translate.h"
#include "util.h"

#include <cstring>
#include <string>


using namespace std;
using namespace __gnu_cxx;

static const string TOK_NETFILTER("netfilter");
static const string TOK_HOOK("hook");


// ------------------------------------------------------------------------
// netfilter derived probes
// ------------------------------------------------------------------------


struct netfilter_derived_probe: public derived_probe
{
  netfilter_derived_probe (probe* p, probe_point* l);
  virtual void join_group (systemtap_session& s);
  void print_dupe_stamp(ostream& o) { print_dupe_stamp_unprivileged (o); }
};


struct netfilter_derived_probe_group: public generic_dpg<netfilter_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


netfilter_derived_probe::netfilter_derived_probe (probe* p, probe_point* l):
  derived_probe (p, l)
{
}


void
netfilter_derived_probe::join_group (systemtap_session& s)
{
  if (! s.netfilter_derived_probes)
    s.netfilter_derived_probes = new netfilter_derived_probe_group ();
  s.netfilter_derived_probes->enroll (this);
}


void
netfilter_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  // Here we emit any global data structures and functions, including callback functions
  // to be invoked by netfilter.
  // 
  // For other kernel callbacks, a token is passed back to help identify a particular
  // probe-point registration.  For netfilter, nope, so once we're in a notification callback,
  // we can't find out exactly on whose (which probe point's) behalf we were called.
  // 
  // So, we just emit one netfilter callback function per systemtap probe, each with its
  // own nf_hook_ops structure.  Note that the translator already emits a stp_probes[] array,
  // pre-filled with probe names and handler functions and that sort of stuff.
 
  s.op->newline() << "/* ---- netfilter probes ---- */";

  s.op->newline() << "#include <linux/netfilter.h>";
  s.op->newline() << "#include <linux/netfilter_ipv4.h>";
  s.op->newline() << "#include <linux/skbuff.h>";
  s.op->newline() << "#include <linux/udp.h>";
  s.op->newline() << "#include <linux/tcp.h>";
  s.op->newline() << "#include <linux/ip.h>";

  for (unsigned i=0; i < probes.size(); i++)
    {
      netfilter_derived_probe *np = probes[i];
      s.op->newline() << "static void enter_netfilter_probe_" << np->name;
      s.op->newline() << "(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in, const struct net_device *out, int (*okfn)(struct sk_buff *))";
      s.op->newline() << "{";
      s.op->newline(1) << "struct stap_probe * const stp = & stap_probes[", p->session_index << "];";
      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "stp",
                                     "_STP_PROBE_HANDLER_NETFILTER", 
                                     false);
      // Pretend to touch each netfilter hook callback argument, so we
      // don't get complaints about unused parameters.
      s.op->newline() << "(void) hooknum;";
      s.op->newline() << "(void) skb;";
      s.op->newline() << "(void) in;";
      s.op->newline() << "(void) out;";
      s.op->newline() << "(void) okfn;";
      // Finally, invoke the probe handler
      s.op->newline() << "(*stp->ph) (c);";
      common_probe_entryfn_epilogue (s.op, false, s.suppress_handler_errors);
      s.op->newline() << "return NF_ACCEPT;"; // XXX: this could instead be an output from the handler
      s.op->newline(-1) << "}";

      // now emit the nf_hook_ops struct for this probe.
      s.op->newline() << "static struct nf_hook_ops netfilter_opts_ = {" << np->name;
      s.op->newline() << ".hook = & enter_netfilter_probe_" << np->name << ",";
      s.op->newline() << ".owner = THIS_MODULE,";
      s.op->newline() << ".hooknum = 0,"; // XXX: should be probe point argument
      s.op->newline() << ".pf = PF_INET,"; // XXX: should be probe point argument
      s.op->newline() << ".priority = PF_INET,"; // XXX: should be probe point argument
      s.op->newline(-1) << "};";
    }
  s.op->newline();
}


void
netfilter_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  // We register (do not execute) the probes here.
  // NB: since we anticipate only a few netfilter/hook type probes, there is no need to
  // emit an initialization loop into the generated C code.  We can simply unroll it.
  for (unsigned i=0; i < probes.size(); i++) 
    {
      netfilter_derived_probe *np = probes[i];
      s.op->newline() << "rc = nf_register_hook (& netfilter_opts_" << np->name << ");";
      if (i > 0) // unregister others upon failure
        {
          s.op->newline() << "if (rc < 0) {";
          s.op->newline(1);
          for (unsigned j=i-1; j>=0; j++) 
            {
              netfilter_derived_probe *np2 = probes[j];
              s.op->newline() << "nf_unregister_hook (& netfilter_opts_" << np2->name << ");";
            }
          s.op->newline(-1) << "}";
        }
    }
}


void
netfilter_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty()) return;
  
  // We register (do not execute) the probes here.
  for (unsigned i=0; i < probes.size(); i++) 
    {
      netfilter_derived_probe *np = probes[i];
      s.op->newline() << "nf_unregister_hook (& netfilter_opts_" << np->name << ");";
    }
}


// ------------------------------------------------------------------------
// unified probe builder for netfilter probes
// ------------------------------------------------------------------------


struct netfilter_builder: public derived_probe_builder
{
    virtual void build(systemtap_session & sess,
                       probe * base, probe_point * location,
                       literal_map_t const & parameters,
                       vector<derived_probe *> & finished_results);

    static void register_patterns(systemtap_session& s);
};


void
netfilter_builder::build(systemtap_session & sess,
    probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    vector<derived_probe *> & finished_results)
{
  // XXX: extract optional probe point arguments later
  finished_results.push_back(new netfilter_derived_probe(base, location));
}


void
register_tapset_netfilters(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new netfilter_builder();

  // netfilter.hook
  root->bind(TOK_NETFILTER)->bind(TOK_HOOK)->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
