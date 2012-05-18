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

#include <sys/socket.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_arp.h>
#include <linux/netfilter_bridge.h>


using namespace std;
using namespace __gnu_cxx;

static const string TOK_NETFILTER("netfilter");
static const string TOK_HOOK("hook");
static const string TOK_PF("pf");
static const string TOK_PRIORITY("priority");

// ------------------------------------------------------------------------
// netfilter derived probes
// ------------------------------------------------------------------------


struct netfilter_derived_probe: public derived_probe
{
  string hook;
  string pf;
  string priority;

  set<string> context_vars;

  netfilter_derived_probe (systemtap_session &, probe* p,
                           probe_point* l, string h,
                           string pf, string pri);
  virtual void join_group (systemtap_session& s);
};


struct netfilter_derived_probe_group: public generic_dpg<netfilter_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};

struct netfilter_var_expanding_visitor: public var_expanding_visitor
{
  netfilter_var_expanding_visitor(systemtap_session& s, const string& pn);

  systemtap_session& sess;
  string probe_name;
  set<string> context_vars;

  void visit_target_symbol (target_symbol* e);
};

netfilter_derived_probe::netfilter_derived_probe (systemtap_session &s, probe* p,
                                                  probe_point* l, string h,
                                                  string pf, string pri):
  derived_probe (p, l), hook (h), pf (pf), priority (pri)
{

  bool hook_error = false;
  bool pf_error = false;

  // Map the strings passed in to the actual values defined in netfilter_*.h
#define IF_HOOKNAME(n) if (hook == #n) { hook = lex_cast(n); }
#define IF_PRIORITY(n) if (priority == #n) { priority = lex_cast(n); }

  // Validate hook, pf, priority
  if(pf == "NFPROTO_IPV4")
    {
      IF_HOOKNAME (NF_INET_PRE_ROUTING)
      else IF_HOOKNAME (NF_INET_LOCAL_IN)
      else IF_HOOKNAME (NF_INET_FORWARD)
      else IF_HOOKNAME (NF_INET_LOCAL_OUT)
      else IF_HOOKNAME (NF_INET_POST_ROUTING)
      else IF_HOOKNAME (NF_IP_PRE_ROUTING)
      else IF_HOOKNAME (NF_IP_LOCAL_IN)
      else IF_HOOKNAME (NF_IP_FORWARD)
      else IF_HOOKNAME (NF_IP_LOCAL_OUT)
      else IF_HOOKNAME (NF_IP_POST_ROUTING)
      else
        hook_error = true;

      IF_PRIORITY (NF_IP_PRI_FIRST)
      else IF_PRIORITY (NF_IP_PRI_CONNTRACK_DEFRAG)
      else IF_PRIORITY (NF_IP_PRI_RAW)
      else IF_PRIORITY (NF_IP_PRI_SELINUX_FIRST)
      else IF_PRIORITY (NF_IP_PRI_CONNTRACK)
      else IF_PRIORITY (NF_IP_PRI_MANGLE)
      else IF_PRIORITY (NF_IP_PRI_NAT_DST)
      else IF_PRIORITY (NF_IP_PRI_FILTER)
      else IF_PRIORITY (NF_IP_PRI_SECURITY)
      else IF_PRIORITY (NF_IP_PRI_NAT_SRC)
      else IF_PRIORITY (NF_IP_PRI_SELINUX_LAST)
      else IF_PRIORITY (NF_IP_PRI_CONNTRACK_CONFIRM)
      else IF_PRIORITY (NF_IP_PRI_LAST)
    }
  else if(pf=="NFPROTO_IPV6")
    {
      IF_HOOKNAME (NF_IP6_PRE_ROUTING)
      else IF_HOOKNAME (NF_IP6_LOCAL_IN)
      else IF_HOOKNAME (NF_IP6_FORWARD)
      else IF_HOOKNAME (NF_IP6_LOCAL_OUT)
      else IF_HOOKNAME (NF_IP6_POST_ROUTING)
      else
        hook_error = true;

      IF_PRIORITY (NF_IP6_PRI_FIRST)
      else IF_PRIORITY (NF_IP6_PRI_CONNTRACK_DEFRAG)
      else IF_PRIORITY (NF_IP6_PRI_RAW)
      else IF_PRIORITY (NF_IP6_PRI_SELINUX_FIRST)
      else IF_PRIORITY (NF_IP6_PRI_CONNTRACK)
      else IF_PRIORITY (NF_IP6_PRI_MANGLE)
      else IF_PRIORITY (NF_IP6_PRI_NAT_DST)
      else IF_PRIORITY (NF_IP6_PRI_FILTER)
      else IF_PRIORITY (NF_IP6_PRI_SECURITY)
      else IF_PRIORITY (NF_IP6_PRI_NAT_SRC)
      else IF_PRIORITY (NF_IP6_PRI_SELINUX_LAST)
      else IF_PRIORITY (NF_IP6_PRI_LAST)
    }
  else if (pf == "NFPROTO_ARP")
    {
      IF_HOOKNAME(NF_ARP_IN)
      else IF_HOOKNAME (NF_ARP_OUT)
      else IF_HOOKNAME (NF_ARP_FORWARD)
      else
        hook_error = true;
    }
  else if (pf == "NFPROTO_BRIDGE")
    {
      IF_HOOKNAME(NF_BR_PRE_ROUTING)
      else IF_HOOKNAME (NF_BR_LOCAL_IN)
      else IF_HOOKNAME (NF_BR_FORWARD)
      else IF_HOOKNAME (NF_BR_LOCAL_OUT)
      else IF_HOOKNAME (NF_BR_POST_ROUTING)
      else
        hook_error = true;
    }
  else
    pf_error = true;

  // If not running in guru mode, we need more strict checks on hook name,
  // protocol family and priority to avoid people putting in wacky embedded c
  // nastiness. Otherwise, and if it didn't match any of the above lists,
  // pass the string in as is.
  if(!s.guru_mode)
    {
      // At this point the priority should be a 32 bit integer encoded as a string.
      // Ensure that this is the case.
      try
        {
          int prio = lex_cast<int32_t>(priority);
          (void) prio;
        }
      catch (const runtime_error&) 
        {
          throw semantic_error
              (_F("unsupported netfilter priority \"%s\" for protocol family \"%s\"; need stap -g",
              priority.c_str(), pf.c_str()));
        }

      // Complain and abort if there were any hook name errors
      if (hook_error)
            throw semantic_error
                (_F("unsupported netfilter hook \"%s\" for protocol family \"%s\"; need stap -g",
                hook.c_str(), pf.c_str()));

      // Complain and abort if there were any pf errors
      if (pf_error)
        throw semantic_error
            (_F("unsupported netfilter protocol family \"%s\"; need stap -g", pf.c_str()));
    }

#undef IF_HOOKNAME
#undef IF_PRIORITY

  // Expand local variables in the probe body
  netfilter_var_expanding_visitor v (s, name);
  v.replace (this->body);

  // Create probe-local vardecls, before symbol resolution might make
  // one for us, so that we can set the all-important synthetic flag.
  for (set<string>::iterator it = v.context_vars.begin();
       it != v.context_vars.end();
       it++)
    {
      string name = *it;
      this->context_vars.insert(name);
      vardecl *v = new vardecl;
      v->name = name;
      v->tok = this->tok; /* XXX: but really the $context var. */
      v->set_arity (0, this->tok);
      v->type = pe_long;
      v->synthetic = true; // suppress rvalue or lvalue optimizations
      this->locals.push_back (v);
    }
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
  s.op->newline() << "#include <linux/skbuff.h>";
  s.op->newline() << "#include <linux/udp.h>";
  s.op->newline() << "#include <linux/tcp.h>";
  s.op->newline() << "#include <linux/ip.h>";

  for (unsigned i=0; i < probes.size(); i++)
    {
      netfilter_derived_probe *np = probes[i];
      s.op->newline() << "static unsigned int enter_netfilter_probe_" << np->name;
      s.op->newline() << "(unsigned int nf_hooknum, struct sk_buff *nf_skb, const struct net_device *nf_in, const struct net_device *nf_out, int (*nf_okfn)(struct sk_buff *))";
      s.op->newline() << "{";
      s.op->newline(1) << "struct stap_probe * const stp = & stap_probes[" << np->session_index << "];";
      s.op->newline() << "int nf_verdict = NF_ACCEPT;"; // default NF_ACCEPT, to be used by $verdict context var
      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "stp",
                                     "_STP_PROBE_HANDLER_NETFILTER",
                                     false);

      // Copy or pretend-to-touch each incoming parameter.

      string c_p = "c->probe_locals." + lex_cast(np->name); // this is where the $context vars show up

      if (np->context_vars.find("__nf_hooknum") != np->context_vars.end())
        s.op->newline() << c_p + ".__nf_hooknum = (int64_t)(uintptr_t) nf_hooknum;";
      else
        s.op->newline() << "(void) nf_hooknum;";
      if (np->context_vars.find("__nf_skb") != np->context_vars.end())
        s.op->newline() << c_p + ".__nf_skb = (int64_t)(uintptr_t) nf_skb;";
      else
        s.op->newline() << "(void) nf_skb;";
      if (np->context_vars.find("__nf_in") != np->context_vars.end())
        s.op->newline() << c_p + ".__nf_in = (int64_t)(uintptr_t) nf_in;";
      else
        s.op->newline() << "(void) nf_in;";
      if (np->context_vars.find("__nf_out") != np->context_vars.end())
        s.op->newline() << c_p + ".__nf_in = (int64_t)(uintptr_t) nf_out;";
      else
        s.op->newline() << "(void) nf_out;";
      if (np->context_vars.find("__nf_verdict") != np->context_vars.end())
        s.op->newline() << c_p + ".__nf_verdict = (int64_t) nf_verdict;";
      else
        s.op->newline() << "(void) nf_out;";

      // Invoke the probe handler
      s.op->newline() << "(*stp->ph) (c);";

      common_probe_entryfn_epilogue (s.op, false, s.suppress_handler_errors);

      if (np->context_vars.find("__nf_verdict") != np->context_vars.end())
        s.op->newline() << "nf_verdict = (int) "+c_p+".__nf_verdict;";

      s.op->newline() << "return nf_verdict;";
      s.op->newline(-1) << "}";

      // now emit the nf_hook_ops struct for this probe.
      s.op->newline() << "static struct nf_hook_ops netfilter_opts_" << np->name << " = {";
      s.op->newline() << ".hook = enter_netfilter_probe_" << np->name << ",";
      s.op->newline() << ".owner = THIS_MODULE,";

      // XXX: if these strings/numbers are not range-limited / validated before we get here,
      // ie during the netfilter_derived_probe ctor, then we will emit potential trash here,
      // leading to all kinds of horror.  Like zombie women eating roach-filled walnuts.  Dogs
      // and cats living together.  Foreign foods taking over the refrigerator.  Don't let this
      // happen to you!
      s.op->newline() << ".hooknum = " << np->hook << ",";
      s.op->newline() << ".pf = " << np->pf << ",";
      s.op->newline() << ".priority = " << np->priority << ",";
      s.op->newline() << "};";
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
          for (int j=i-1; j>=0; j--) // XXX: j must be signed for loop to work
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


netfilter_var_expanding_visitor::netfilter_var_expanding_visitor (systemtap_session& s,
                                                                  const string& pn):
  sess (s), probe_name (pn)
{
}


void
netfilter_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  try
    {
      assert(e->name.size() > 0 && e->name[0] == '$');

      if (e->addressof)
        throw semantic_error(_("cannot take address of netfilter hook context variable"), e->tok);

      // We map all $context variables to similarly named probe locals.
      // See emit_module_decls for how the parameters & result are handled.
      string c_var;
      bool lvalue_ok = false;
      bool need_guru = false;
      if (e->name == "$hooknum") { c_var = "__nf_hooknum"; }
      else if (e->name == "$skb") { c_var = "__nf_skb"; }
      else if (e->name == "$in") { c_var = "__nf_in"; }
      else if (e->name == "$out") { c_var = "__nf_out"; }
      else if (e->name == "$okfn") { c_var = "__nf_okfn"; }
      else if (e->name == "$verdict") { c_var = "__nf_verdict"; lvalue_ok = true; need_guru = true; }
      // XXX: also support $$vars / $$parms
      else
        throw semantic_error(_("unsupported context variable"), e->tok);

      if (! lvalue_ok && is_active_lvalue (e))
        throw semantic_error(_("write to netfilter parameter not permitted"), e->tok);

      // Writing to variables like $verdict requires guru mode, for obvious reasons
      if(need_guru && !sess.guru_mode)
        throw semantic_error(_("write to netfilter verdict requires guru mode; need stap -g"), e->tok);

      context_vars.insert (c_var);

      // Synthesize a symbol to reference those variables
      symbol* sym = new symbol;
      sym->type = pe_long;
      sym->tok = e->tok;
      sym->name = c_var;
      provide (sym);
    }
  catch (const semantic_error &er)
    {
      e->chain (er);
      provide (e);
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
  string hook;                // no default
  string pf; // no default
  string priority = "0";      // Default: somewhere in the middle

  if(!get_param(parameters, TOK_HOOK, hook))
    throw semantic_error (_("missing hooknum"));

  get_param(parameters, TOK_PF, pf);
  get_param(parameters, TOK_PRIORITY, priority);

  finished_results.push_back(new netfilter_derived_probe(sess, base, location, hook, pf, priority));
}

void
register_tapset_netfilter(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new netfilter_builder();


  //netfilter.hook().pf()
  root->bind(TOK_NETFILTER)->bind_str(TOK_HOOK)->bind_str(TOK_PF)->bind(builder);

  //netfilter.pf().hook()
  root->bind(TOK_NETFILTER)->bind_str(TOK_PF)->bind_str(TOK_HOOK)->bind(builder);

  //netfilter.hook().pf().priority()
  root->bind(TOK_NETFILTER)->bind_str(TOK_HOOK)->bind_str(TOK_PF)->bind_str(TOK_PRIORITY)->bind(builder);

  //netfilter.pf().hook().priority()
  root->bind(TOK_NETFILTER)->bind_str(TOK_PF)->bind_str(TOK_HOOK)->bind_str(TOK_PRIORITY)->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
