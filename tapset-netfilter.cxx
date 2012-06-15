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
#include <limits.h>

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
  unsigned nf_index;

  set<string> context_vars;

  netfilter_derived_probe (systemtap_session &, probe* p,
                           probe_point* l, string h,
                           string protof, string pri);
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
                                                  string protof, string pri):
  derived_probe (p, l), hook (h), pf (protof), priority (pri)
{
  static unsigned nf_index_ctr = 0;
  this->nf_index = nf_index_ctr++; // PR14137: need to generate unique
                                   // identifier, since p->name may be
                                   // shared in c_unparser::emit_probe()

  bool hook_error = false;
  bool pf_error = false;

  // Map the strings passed in to the actual values defined in netfilter_*.h
  // NOTE: We need to hard code all the following
  // constants rather than just include the
  // appropriate kernel headers because in different
  // versions of the kernel, certain constants are not
  // defined. This was the best method at the time to
  // get them to compile properly.

  // Validate hook, pf, priority
  if(pf == "NFPROTO_IPV4")
    {
      // Protocol Family
      pf = "2";

      // Hook
      if (hook == "NF_INET_PRE_ROUTING") hook = "0";
      else if (hook == "NF_INET_LOCAL_IN") hook = "1";
      else if (hook == "NF_INET_FORWARD") hook = "2";
      else if (hook == "NF_INET_LOCAL_OUT") hook = "3";
      else if (hook == "NF_INET_POST_ROUTING") hook = "4";
      else if (hook == "NF_IP_PRE_ROUTING") hook = "0";
      else if (hook == "NF_IP_LOCAL_IN") hook = "1";
      else if (hook == "NF_IP_FORWARD") hook = "2";
      else if (hook == "NF_IP_LOCAL_OUT") hook = "3";
      else if (hook == "NF_IP_POST_ROUTING") hook = "4";
      else hook_error = true;

      // Priority
      if (priority == "NF_IP_PRI_FIRST") priority = lex_cast(INT_MIN);
      else if (priority == "NF_IP_PRI_CONNTRACK_DEFRAG") priority = "-400";
      else if (priority == "NF_IP_PRI_RAW") priority = "-300";
      else if (priority == "NF_IP_PRI_SELINUX_FIRST") priority = "-225";
      else if (priority == "NF_IP_PRI_CONNTRACK") priority = "-200";
      else if (priority == "NF_IP_PRI_MANGLE") priority = "-150";
      else if (priority == "NF_IP_PRI_NAT_DST") priority = "-100";
      else if (priority == "NF_IP_PRI_FILTER") priority = "0";
      else if (priority == "NF_IP_PRI_SECURITY") priority = "50";
      else if (priority == "NF_IP_PRI_NAT_SRC") priority = "100";
      else if (priority == "NF_IP_PRI_SELINUX_LAST") priority = "225";
      else if (priority == "NF_IP_PRI_CONNTRACK_CONFIRM") priority = lex_cast(INT_MAX);
      else if (priority == "NF_IP_PRI_LAST") priority = lex_cast(INT_MAX);
    }
  else if(pf=="NFPROTO_IPV6")
    {
      // Protocol Family
      pf = "10";

      // Hook
      if (hook == "NF_IP6_PRE_ROUTING") hook = "0";
      else if (hook == "NF_IP6_LOCAL_IN") hook = "1";
      else if (hook == "NF_IP6_FORWARD") hook = "2";
      else if (hook == "NF_IP6_LOCAL_OUT") hook = "3";
      else if (hook == "NF_IP6_POST_ROUTING") hook = "4";
      else hook_error = true;

      // Priority
      if (priority == "NF_IP6_PRI_FIRST") priority = lex_cast(INT_MIN);
      else if (priority == "NF_IP6_PRI_CONNTRACK_DEFRAG") priority = "-400";
      else if (priority == "NF_IP6_PRI_RAW") priority = "-300";
      else if (priority == "NF_IP6_PRI_SELINUX_FIRST") priority = "-225";
      else if (priority == "NF_IP6_PRI_CONNTRACK") priority = "-200";
      else if (priority == "NF_IP6_PRI_MANGLE") priority = "-150";
      else if (priority == "NF_IP6_PRI_NAT_DST") priority = "-100";
      else if (priority == "NF_IP6_PRI_FILTER") priority = "0";
      else if (priority == "NF_IP6_PRI_SECURITY") priority = "50";
      else if (priority == "NF_IP6_PRI_NAT_SRC") priority = "100";
      else if (priority == "NF_IP6_PRI_SELINUX_LAST") priority = "225";
      else if (priority == "NF_IP6_PRI_LAST") priority = lex_cast(INT_MAX);
    }
  else if (pf == "NFPROTO_ARP")
   {
      // Protocol Family
      pf = "3";

      // Hook
      if (hook == "NF_ARP_IN") hook = "0";
      else if (hook == "NF_ARP_OUT") hook = "1";
      else if (hook == "NF_ARP_FORWARD") hook = "2";
      else hook_error = true;
    }
  else if (pf == "NFPROTO_BRIDGE")
    {
      // Protocol Family
      pf = "7";

      // Hook
      if (hook == "NF_BR_PRE_ROUTING") hook = "0";
      else if (hook == "NF_BR_LOCAL_IN") hook = "1";
      else if (hook == "NF_BR_FORWARD") hook = "2";
      else if (hook == "NF_BR_LOCAL_OUT") hook = "3";
      else if (hook == "NF_BR_POST_ROUTING") hook = "4";
      else hook_error = true;
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
      s.op->newline() << "static unsigned int enter_netfilter_probe_" << np->nf_index;

      // Previous to kernel 2.6.22, the hookfunction definition takes a struct sk_buff **skb,
      // whereas currently it uses a *skb. We need emit the right version so this will
      // compile on RHEL5, for example.
      s.op->newline() << "#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)";
      s.op->newline() << "(unsigned int nf_hooknum, struct sk_buff *nf_skb, const struct net_device *nf_in, const struct net_device *nf_out, int (*nf_okfn)(struct sk_buff *))";
      s.op->newline() << "{";

      s.op->newline() << "#else";

      s.op->newline() << "(unsigned int nf_hooknum, struct sk_buff **nf_pskb, const struct net_device *nf_in, const struct net_device *nf_out, int (*nf_okfn)(struct sk_buff *))";
      s.op->newline() << "{";
      s.op->newline(1) << "struct sk_buff *nf_skb = nf_pskb ? *nf_pskb : NULL;";

      s.op->newline(-1) << "#endif";
      s.op->newline(1) << "struct stap_probe * const stp = & stap_probes[" << np->session_index << "];";
      s.op->newline() << "int nf_verdict = NF_ACCEPT;"; // default NF_ACCEPT, to be used by $verdict context var
      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "stp",
                                     "_STP_PROBE_HANDLER_NETFILTER",
                                     false);

      // Copy or pretend-to-touch each incoming parameter.

      string c_p = "c->probe_locals." + lex_cast(np->name); // this is where the $context vars show up
      // NB: PR14137: this should be the potentially shared name,
      // since the generated probe handler body refers to that name.

      if (np->context_vars.find("__nf_hooknum") != np->context_vars.end())
        s.op->newline() << c_p + "." + s.up->c_localname("__nf_hooknum") + " = (int64_t)(uintptr_t) nf_hooknum;";
      else
        s.op->newline() << "(void) nf_hooknum;";
      if (np->context_vars.find("__nf_skb") != np->context_vars.end())
        s.op->newline() << c_p + "." + s.up->c_localname("__nf_skb") + " = (int64_t)(uintptr_t) nf_skb;";
      else
        s.op->newline() << "(void) nf_skb;";
      if (np->context_vars.find("__nf_in") != np->context_vars.end())
        s.op->newline() << c_p + "." + s.up->c_localname("__nf_in") + " = (int64_t)(uintptr_t) nf_in;";
      else
        s.op->newline() << "(void) nf_in;";
      if (np->context_vars.find("__nf_out") != np->context_vars.end())
        s.op->newline() << c_p + "." + s.up->c_localname("__nf_in") + " = (int64_t)(uintptr_t) nf_out;";
      else
        s.op->newline() << "(void) nf_out;";
      if (np->context_vars.find("__nf_verdict") != np->context_vars.end())
        s.op->newline() << c_p + "." + s.up->c_localname("__nf_verdict") + " = (int64_t) nf_verdict;";
      else
        s.op->newline() << "(void) nf_out;";

      // Invoke the probe handler
      s.op->newline() << "(*stp->ph) (c);";

      common_probe_entryfn_epilogue (s.op, false, s.suppress_handler_errors);

      if (np->context_vars.find("__nf_verdict") != np->context_vars.end())
        s.op->newline() << "nf_verdict = (int) "+c_p+"." + s.up->c_localname("__nf_verdict") + ";";

      s.op->newline() << "return nf_verdict;";
      s.op->newline(-1) << "}";

      // now emit the nf_hook_ops struct for this probe.
      s.op->newline() << "static struct nf_hook_ops netfilter_opts_" << np->nf_index << " = {";
      s.op->newline() << ".hook = enter_netfilter_probe_" << np->nf_index << ",";
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
      s.op->newline() << "rc = nf_register_hook (& netfilter_opts_" << np->nf_index << ");";
      if (i > 0) // unregister others upon failure
        {
          s.op->newline() << "if (rc < 0) {";
          s.op->indent(1);
          for (int j=i-1; j>=0; j--) // XXX: j must be signed for loop to work
            {
              netfilter_derived_probe *np2 = probes[j];
              s.op->newline() << "nf_unregister_hook (& netfilter_opts_" << np2->nf_index << ");";
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
      s.op->newline() << "nf_unregister_hook (& netfilter_opts_" << np->nf_index << ");";
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
