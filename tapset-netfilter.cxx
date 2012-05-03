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
static const string TOK_PF("protocol_f");
static const string TOK_PRI("priority");

// ------------------------------------------------------------------------
// netfilter derived probes
// ------------------------------------------------------------------------


struct netfilter_derived_probe: public derived_probe
{
  string hook;
  string protocol_family;
  string priority;
  bool target_symbol_seen;

  netfilter_derived_probe (systemtap_session &, probe* p,
                           probe_point* l, string h,
                           string pf, string pri);
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

struct netfilter_var_expanding_visitor: public var_expanding_visitor
{
  netfilter_var_expanding_visitor(systemtap_session& s, const string& pn);

  systemtap_session& sess;
  string probe_name;
  bool target_symbol_seen;

  void visit_target_symbol (target_symbol* e);
};

netfilter_derived_probe::netfilter_derived_probe (systemtap_session &s, probe* p,
                                                  probe_point* l, string h,
                                                  string pf, string pri):
  derived_probe (p, l), hook (h), protocol_family (pf), priority (pri), target_symbol_seen(false)
{

  if(protocol_family != "PF_INET" && protocol_family != "PF_INET6")
    throw semantic_error (_("invalid protocol family"));

  // Expand local variables in the probe body
  netfilter_var_expanding_visitor v (s, name);
  v.replace (this->body);
  target_symbol_seen = v.target_symbol_seen;
}

void
netfilter_derived_probe::join_group (systemtap_session& s)
{
  if (! s.netfilter_derived_probes)
    {
      s.netfilter_derived_probes = new netfilter_derived_probe_group ();
      // Make sure 'struct _stp_netfilter_data' is defined early.
      embeddedcode *ec = new embeddedcode;
      ec->tok = NULL;
      ec->code = string("struct _stp_netfilter_data {\n")
	  + string("  char *buffer;\n")
	  + string("  size_t bufsize;\n")
	  + string("  size_t count;\n")
	  + string("};\n")
	  + string("#ifndef STP_NETFILTER_BUFSIZE\n")
	  + string("#define STP_NETFILTER_BUFSIZE MAXSTRINGLEN\n")
	  + string("#endif\n");
      s.embeds.push_back(ec);
    }
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
      s.op->newline() << "static unsigned int enter_netfilter_probe_" << np->name;
      s.op->newline() << "(unsigned int hooknum, struct sk_buff *skb, const struct net_device *in, const struct net_device *out, int (*okfn)(struct sk_buff *))";
      s.op->newline() << "{";
      s.op->newline(1) << "struct stap_probe * const stp = & stap_probes[" << np->session_index << "];";
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

      // Output routine to fill in the buffer with our data.  Note that we
      // need to do this even in the case where we have no read probes,
      // but we can skip most of it then.
      s.op->newline();

      s.op->newline() << "struct _stp_netfilter_data pdata;";

      // common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING",
             // "spp->read_probe",
             // "_STP_PROBE_HANDLER_PROCFS");

      s.op->newline() << "pdata.buffer = spp->buffer;";
      s.op->newline() << "pdata.bufsize = spp->bufsize;";
      s.op->newline() << "if (c->ips.netfilter_data == NULL)";
      s.op->newline(1) << "c->ips.netfilter_data = &pdata;";
      s.op->newline(-1) << "else {";

      s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
      s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      s.op->newline() << "_stp_exit ();";
      s.op->newline(-1) << "}";
      s.op->newline() << "atomic_dec (& c->busy);";
      s.op->newline() << "goto probe_epilogue;";
      s.op->newline(-1) << "}";

      // Finally, invoke the probe handler
      s.op->newline() << "(*stp->ph) (c);";

      // Note that _netfilter_value_set copied string data into spp->buffer
      s.op->newline() << "c->ips.netfilter_data = NULL;";
      s.op->newline() << "spp->needs_fill = 0;";
      s.op->newline() << "spp->count = strlen(spp->buffer);";

      common_probe_entryfn_epilogue (s.op, false, s.suppress_handler_errors);
      s.op->newline() << "return NF_ACCEPT;"; // XXX: this could instead be an output from the handler
      s.op->newline(-1) << "}";

      s.op->newline() << "if (spp->needs_fill) {";
      s.op->newline(1) << "spp->needs_fill = 0;";
      s.op->newline() << "return -EIO;";
      s.op->newline(-1) << "}";

      // now emit the nf_hook_ops struct for this probe.
      s.op->newline() << "static struct nf_hook_ops netfilter_opts_" << np->name << " = {";
      s.op->newline() << ".hook = enter_netfilter_probe_" << np->name << ",";
      s.op->newline() << ".owner = THIS_MODULE,";
      s.op->newline() << ".hooknum = " << np->hook << ",";
      s.op->newline() << ".pf = " << np->protocol_family << ",";
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

netfilter_var_expanding_visitor::netfilter_var_expanding_visitor (systemtap_session& s,
							    const string& pn):
  sess (s), probe_name (pn), target_symbol_seen (false)
{
  // netfilter probes can also handle '.='.
  valid_ops.insert (".=");
}

void
netfilter_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  try
    {
      assert(e->name.size() > 0 && e->name[0] == '$');

      if (e->name != "$verdict")
        throw semantic_error (_("invalid target symbol for netfilter probe, $verdict expected"),
                              e->tok);

      e->assert_no_components("netfilter");

      bool lvalue = is_active_lvalue(e);

      if (e->addressof)
        throw semantic_error(_("cannot take address of netfilter variable"), e->tok);

      // Remember that we've seen a target variable.
      target_symbol_seen = true;

      // Synthesize a function.
      functiondecl *fdecl = new functiondecl;
      fdecl->synthetic = true;
      fdecl->tok = e->tok;
      embeddedcode *ec = new embeddedcode;
      ec->tok = e->tok;

      string fname;
      string locvalue = "CONTEXT->ips.netfilter_data";

      if (! lvalue)
        {
          fname = "_netfilter_value_get";
          ec->code = string("    struct _stp_netfilter_data *data = (struct _stp_netfilter_data *)(") + locvalue + string("); /* pure */\n")

            + string("    _stp_copy_from_user(THIS->__retvalue, data->buffer, data->count);\n")
            + string("    THIS->__retvalue[data->count] = '\\0';\n");
        }
      else					// lvalue
        {
          if (*op == "=")
            {
              fname = "_netfilter_value_set";
              ec->code = string("struct _stp_netfilter_data *data = (struct _stp_netfilter_data *)(") + locvalue + string(");\n")
                + string("    strlcpy(data->buffer, THIS->value, data->bufsize);\n")
                + string("    data->count = strlen(data->buffer);\n");
            }
          else if (*op == ".=")
            {
              fname = "_netfilter_value_append";
              ec->code = string("struct _stp_netfilter_data *data = (struct _stp_netfilter_data *)(") + locvalue + string(");\n")
                + string("    strlcat(data->buffer, THIS->value, data->bufsize);\n")
                + string("    data->count = strlen(data->buffer);\n");
            }
          else
            {
              throw semantic_error (_("Only the following assign operators are"
                                    " implemented on netfilter target variables:"
                                    " '=', '.='"), e->tok);
            }
        }
      fname += lex_cast(++tick);

      fdecl->name = fname;
      fdecl->body = ec;
      fdecl->type = pe_string;

      if (lvalue)
        {
          // Modify the fdecl so it carries a single pe_string formal
          // argument called "value".

          vardecl *v = new vardecl;
          v->type = pe_string;
          v->name = "verdict";
          v->tok = e->tok;
          fdecl->formal_args.push_back(v);
        }
      fdecl->join (sess);

      // Synthesize a functioncall.
      functioncall* n = new functioncall;
      n->tok = e->tok;
      n->function = fname;

      if (lvalue)
        {
          // Provide the functioncall to our parent, so that it can be
          // used to substitute for the assignment node immediately above
          // us.
          assert(!target_symbol_setter_functioncalls.empty());
          *(target_symbol_setter_functioncalls.top()) = n;
        }

      provide (n);
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
  string hook;
  string protocol_family = "PF_INET"; // Default ipv4 protocol
  string priority = "NF_IP_PRI_FIRST";

  if(!get_param(parameters, TOK_HOOK, hook))
    throw semantic_error (_("missing hooknum"));

  get_param(parameters, TOK_PF, protocol_family);
  get_param(parameters, TOK_PRI, priority);

  finished_results.push_back(new netfilter_derived_probe(sess, base, location, hook, protocol_family, priority));
}

void
register_tapset_netfilter(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new netfilter_builder();

  // netfilter.hook()
  root->bind(TOK_NETFILTER)->bind_str(TOK_HOOK)->bind(builder);

  //netfilter.hook().protocol_f()
  root->bind(TOK_NETFILTER)->bind_str(TOK_HOOK)->bind_str(TOK_PF)->bind(builder);

  // netfilter.hook().priority()
  root->bind(TOK_NETFILTER)->bind_str(TOK_HOOK)->bind_str(TOK_PRI)->bind(builder);

  //netfilter.hook().protocol_f().priority()
  root->bind(TOK_NETFILTER)->bind_str(TOK_HOOK)->bind_str(TOK_PF)->bind_str(TOK_PRI)->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
