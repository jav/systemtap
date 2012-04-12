// tapset for nethooks
// Copyright (C) 2005-2011 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008
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

static const string TOK_NETHOOK("nethook");


// ------------------------------------------------------------------------
// nethook derived probes
// ------------------------------------------------------------------------


struct nethook_derived_probe: public derived_probe
{
  int64_t hook, protocol;
  nethook_derived_probe (probe* p, probe_point* l,
                       int64_t hook, int64_t hooknum);
  virtual void join_group (systemtap_session& s);

  // No assertion need be emitted, since this probe is allowed for unprivileged
  // users.
  void emit_privilege_assertion (translator_output*) {}
  void print_dupe_stamp(ostream& o) { print_dupe_stamp_unprivileged (o); }
};


struct nethook_derived_probe_group: public generic_dpg<nethook_derived_probe>
{
  void emit_netfunc (translator_output* o);
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


nethook_derived_probe::nethook_derived_probe (probe* p, probe_point* l,
                                          int64_t h, int64_t proto, int64_t r):
  derived_probe (p, l), hook (h), protocol (proto), resault(r)
{
  if (hook < 0 || hook > 4) // h must be within hooks
    //TRANSLATORS: 'nethook' is the name of a probe point
    throw semantic_error (_("invalid hook for nethook"));

  if (proto < 0 || proto > 255)
    //TRANSLATORS: 'protocol' is a key word
    throw semantic_error (_("invalid protocol for nethook"));

  if (locations.size() != 1)
    throw semantic_error (_("only expect one probe point"));
  // so we don't have to loop over them in the other functions
}


void
nethook_derived_probe::join_group (systemtap_session& s)
{
  if (! s.nethook_derived_probes)
    s.nethook_derived_probes = new nethook_derived_probe_group ();
  s.nethook_derived_probes->enroll (this);
}


void
nethook_derived_probe_group::emit_netfunc (systemtap_session& s)
{
  s.op->newline() << "unsigned int hook_func(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in, const struct net_device *out, int (*okfn)(struct sk_buff *))";
  s.op->newline() << "{";
  s.op->newline(1) << "sock_buff = skb;"; 
  s.op->newline() << "if(!sock_buff) { return NF_ACCEPT;}";
  s.op->newline() << "ip_header = (struct iphdr *)skb_network_header(sock_buff);";
  s.op->newline() << "if(!ip_header) { return NF_ACCEPT;}";
  s.op->newline() << "if (ip_header->protocol==TCP) {";
  s.op->newline(1) << "tcp_header = (struct tcphdr *)skb_transport_header(sock_buff);";
  s.op->newline() << "printk(KERN_INFO \"[TCP, HTTP] Packet Recieved...\n   From: %d.%d.%d.%d:%d\n   To:%d.%d.%d.%d:%d\",";
  s.op->newline(1) << "(ip_header->saddr & 0x000000FF),";
  s.op->newline() << "(ip_header->saddr & 0x0000FF00) >> 8,";
  s.op->newline() << "(ip_header->saddr & 0x00FF0000) >> 16,";
  s.op->newline() << "(ip_header->saddr & 0xFF000000) >> 24,";
  s.op->newline() << "ntohs(tcp_header->source),";
  s.op->newline() << "(ip_header->daddr & 0x000000FF),";
  s.op->newline() << "(ip_header->daddr & 0x0000FF00) >> 8,";
  s.op->newline() << "(ip_header->daddr & 0x00FF0000) >> 16,";
  s.op->newline() << "(ip_header->daddr & 0xFF000000) >> 24,";
  s.op->newline() << "ntohs(tcp_header->dest));";
  s.op->newline(-1) << "return NF_ACCEPT;";
  s.op->newline(-1) << "}";
  s.op->newline() << "return NF_ACCEPT;";
  s.op->newline(-1) << "}";
}


void
nethook_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "/* ---- nethook probes ---- */";

  s.op->newline() << "static struct stap_nethook_probe {";
  s.op->newline(1) << "struct nethook_list nethook_list;";
  s.op->newline() << "struct stap_probe * const probe;";
  s.op->newline() << "unsigned hook, protocol;";
  s.op->newline(-1) << "} stap_nethook_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{";
      s.op->line() << " .probe=" << common_probe_init (probes[i]) << ",";
      s.op->line() << " .hook=" << probes[i]->hook << ",";
      s.op->line() << " .protocol=" << probes[i]->protocol << ",";
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();

  s.op->newline() << "static void enter_nethook_probe (struct stap_be_probe *stp) {";
  s.op->indent(1);
  common_probe_entryfn_prologue (s.op, "stp->state", "stp->probe",
				 "_STP_PROBE_HANDLER_NETHOOK", false);
  s.op->newline() << "(*stp->probe->ph) (c);";
  common_probe_entryfn_epilogue (s.op, false, s.suppress_handler_errors);
  s.op->newline(-1) << "}";

  s.op->newline() << "#include <linux/netfilter.h>";
  s.op->newline() << "#include <linux/netfilter_ipv4.h>";
  s.op->newline() << "#include <linux/skbuff.h>";
  s.op->newline() << "#include <linux/udp.h>";
  s.op->newline() << "#include <linux/tcp.h>";
  s.op->newline() << "#include <linux/ip.h>";
  s.op->newline() << "#define TCP 6";
  s.op->newline() << "#define UDP 17";
  s.op->newline() << "static struct nf_hook_ops nfho;";
  s.op->newline() << "struct sk_buff *sock_buff;";
  s.op->newline() << "struct udphdr *udp_header;";
  s.op->newline() << "struct tcphdr *tcp_header;";
  s.op->newline() << "struct iphdr *ip_header;";
}


void
nethook_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes.empty()) return;

  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_nethook_probe* stp = & stap_nethook_probes [i];";
  s.op->newline(1) << "enter_nethook_probe (stp); /* rc = 0 */";
  s.op->newline() << "stap_nethook_probes[i].hook = hook_func;";
  s.op->newline() << "stap_nethook_probes[i].hooknum = 0;";
  s.op->newline() << "stap_nethook_probes[i].pf = PF_INET;";
  s.op->newline() << "stap_nethook_probes[i].priority = NF_IP_PRI_FIRST;";
  s.op->newline() << "int rc = nf_register_hook(&stap_nethook_probes[i]);";
  s.op->newline() << "if (rc < 0)";
  s.op->newline() << "{";
  s.op->newline(1) << "for (j=i-1; j>=0; j--)";
  s.op->newline(1) << "nf_unregister_hook(&stap_nethook_probes[j]);";
  s.op->newline(-2) << "}";
  s.op->newline(-1) << "}";

  emit_netfunc(s.op);
}


void
nethook_derived_probe_group::emit_module_exit (systemtap_session& s)
{
 if (probes.empty()) return;
  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++)";
 s.op->newline(1) << "nf_unregister_hook(&stap_nethook_probes[i]);";
  s.op->indent(-1);


}


// ------------------------------------------------------------------------
// unified probe builder for nethook probes
// ------------------------------------------------------------------------


struct nethook_builder: public derived_probe_builder
{
    virtual void build(systemtap_session & sess,
                       probe * base, probe_point * location,
                       literal_map_t const & parameters,
                       vector<derived_probe *> & finished_results);

    static void register_patterns(systemtap_session& s);
};

void
nethook_builder::build(systemtap_session & sess,
    probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    vector<derived_probe *> & finished_results)
{
  int64_t hook, protocol;


  if (get_param(parameters, "hook", hook) && get_param(parameters, "protocol", protocol));
    {
      finished_results.push_back
        (new nethook_derived_probe(base, location, hook, protocol));
    }
  return;
}

void
register_tapset_nethooks(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new nethook_builder();

  root = root->bind(TOK_NETHOOK);

  root->bind_num(TOK_NETHOOK)
    ->bind_privilege(pr_all)
    ->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
