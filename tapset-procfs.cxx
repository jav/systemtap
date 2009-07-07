// tapset for procfs
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

#include <cstring>
#include <string>


using namespace std;
using namespace __gnu_cxx;


static const string TOK_PROCFS("procfs");
static const string TOK_READ("read");
static const string TOK_WRITE("write");


// ------------------------------------------------------------------------
// procfs file derived probes
// ------------------------------------------------------------------------


struct procfs_derived_probe: public derived_probe
{
  string path;
  bool write;
  bool target_symbol_seen;

  procfs_derived_probe (systemtap_session &, probe* p, probe_point* l, string ps, bool w);
  void join_group (systemtap_session& s);
};


struct procfs_probe_set
{
  procfs_derived_probe* read_probe;
  procfs_derived_probe* write_probe;

  procfs_probe_set () : read_probe (NULL), write_probe (NULL) {}
};


struct procfs_derived_probe_group: public generic_dpg<procfs_derived_probe>
{
private:
  map<string, procfs_probe_set*> probes_by_path;
  typedef map<string, procfs_probe_set*>::iterator p_b_p_iterator;
  bool has_read_probes;
  bool has_write_probes;

public:
  procfs_derived_probe_group () :
    has_read_probes(false), has_write_probes(false) {}

  void enroll (procfs_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct procfs_var_expanding_visitor: public var_expanding_visitor
{
  procfs_var_expanding_visitor(systemtap_session& s, const string& pn,
                               string path, bool write_probe):
    sess (s), probe_name (pn), path (path), write_probe (write_probe),
    target_symbol_seen (false) {}

  systemtap_session& sess;
  string probe_name;
  string path;
  bool write_probe;
  bool target_symbol_seen;

  void visit_target_symbol (target_symbol* e);
};


procfs_derived_probe::procfs_derived_probe (systemtap_session &s, probe* p,
                                            probe_point* l, string ps, bool w):
  derived_probe(p, l), path(ps), write(w), target_symbol_seen(false)
{
  // Expand local variables in the probe body
  procfs_var_expanding_visitor v (s, name, path, write);
  this->body = v.require (this->body);
  target_symbol_seen = v.target_symbol_seen;
}


void
procfs_derived_probe::join_group (systemtap_session& s)
{
  if (! s.procfs_derived_probes)
    {
      s.procfs_derived_probes = new procfs_derived_probe_group ();

      // Make sure 'struct _stp_procfs_data' is defined early.
      embeddedcode *ec = new embeddedcode;
      ec->tok = NULL;
      ec->code = string("struct _stp_procfs_data {\n")
	  + string("  const char *buffer;\n")
	  + string("  off_t off;\n")
	  + string("  unsigned long count;\n")
	  + string("};\n");
      s.embeds.push_back(ec);
    }
  s.procfs_derived_probes->enroll (this);
}


void
procfs_derived_probe_group::enroll (procfs_derived_probe* p)
{
  procfs_probe_set *pset;

  if (probes_by_path.count(p->path) == 0)
    {
      pset = new procfs_probe_set;
      probes_by_path[p->path] = pset;
    }
  else
    {
      pset = probes_by_path[p->path];

      // You can only specify 1 read and 1 write probe.
      if (p->write && pset->write_probe != NULL)
        throw semantic_error("only one write procfs probe can exist for procfs path \"" + p->path + "\"");
      else if (! p->write && pset->read_probe != NULL)
        throw semantic_error("only one read procfs probe can exist for procfs path \"" + p->path + "\"");

      // XXX: multiple writes should be acceptable
    }

  if (p->write)
  {
    pset->write_probe = p;
    has_write_probes = true;
  }
  else
  {
    pset->read_probe = p;
    has_read_probes = true;
  }
}


void
procfs_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "/* ---- procfs probes ---- */";
  s.op->newline() << "#include \"procfs.c\"";

  // Emit the procfs probe data list
  s.op->newline() << "static struct stap_procfs_probe {";
  s.op->newline(1)<< "const char *path;";
  s.op->newline() << "const char *read_pp;";
  s.op->newline() << "void (*read_ph) (struct context*);";
  s.op->newline() << "const char *write_pp;";
  s.op->newline() << "void (*write_ph) (struct context*);";
  s.op->newline(-1) << "} stap_procfs_probes[] = {";
  s.op->indent(1);

  for (p_b_p_iterator it = probes_by_path.begin(); it != probes_by_path.end();
       it++)
  {
      procfs_probe_set *pset = it->second;

      s.op->newline() << "{";
      s.op->line() << " .path=" << lex_cast_qstring (it->first) << ",";

      if (pset->read_probe != NULL)
        {
          s.op->line() << " .read_pp="
                       << lex_cast_qstring (*pset->read_probe->sole_location())
                       << ",";
          s.op->line() << " .read_ph=&" << pset->read_probe->name << ",";
        }
      else
        {
          s.op->line() << " .read_pp=NULL,";
          s.op->line() << " .read_ph=NULL,";
        }

      if (pset->write_probe != NULL)
        {
          s.op->line() << " .write_pp="
                       << lex_cast_qstring (*pset->write_probe->sole_location())
                       << ",";
          s.op->line() << " .write_ph=&" << pset->write_probe->name;
        }
      else
        {
          s.op->line() << " .write_pp=NULL,";
          s.op->line() << " .write_ph=NULL";
        }
      s.op->line() << " },";
  }
  s.op->newline(-1) << "};";

  if (has_read_probes)
    {
      // Output routine to fill in 'page' with our data.
      s.op->newline();

      s.op->newline() << "static int _stp_procfs_read(char *page, char **start, off_t off, int count, int *eof, void *data) {";

      s.op->newline(1) << "struct stap_procfs_probe *spp = (struct stap_procfs_probe *)data;";
      s.op->newline() << "struct _stp_procfs_data pdata;";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "spp->read_pp");

      s.op->newline() << "pdata.buffer = page;";
      s.op->newline() << "pdata.off = off;";
      s.op->newline() << "pdata.count = count;";
      s.op->newline() << "if (c->data == NULL)";
      s.op->newline(1) << "c->data = &pdata;";
      s.op->newline(-1) << "else {";

      s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
      s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      s.op->newline() << "_stp_exit ();";
      s.op->newline(-1) << "}";
      s.op->newline() << "atomic_dec (& c->busy);";
      s.op->newline() << "goto probe_epilogue;";
      s.op->newline(-1) << "}";

      // call probe function
      s.op->newline() << "(*spp->read_ph) (c);";

      // Note that _procfs_value_set copied string data into 'page'
      s.op->newline() << "c->data = NULL;";
      common_probe_entryfn_epilogue (s.op);
      s.op->newline() << "if (pdata.count == 0)";
      s.op->newline(1) << "*eof = 1;";
      s.op->indent(-1);
      s.op->newline() << "return pdata.count;";

      s.op->newline(-1) << "}";
    }
  if (has_write_probes)
    {
      s.op->newline() << "static int _stp_procfs_write(struct file *file, const char *buffer, unsigned long count, void *data) {";

      s.op->newline(1) << "struct stap_procfs_probe *spp = (struct stap_procfs_probe *)data;";
      s.op->newline() << "struct _stp_procfs_data pdata;";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "spp->write_pp");

      s.op->newline() << "if (count > (MAXSTRINGLEN - 1))";
      s.op->newline(1) << "count = MAXSTRINGLEN - 1;";
      s.op->indent(-1);
      s.op->newline() << "pdata.buffer = buffer;";
      s.op->newline() << "pdata.count = count;";

      s.op->newline() << "if (c->data == NULL)";
      s.op->newline(1) << "c->data = &pdata;";
      s.op->newline(-1) << "else {";

      s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
      s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      s.op->newline() << "_stp_exit ();";
      s.op->newline(-1) << "}";
      s.op->newline() << "atomic_dec (& c->busy);";
      s.op->newline() << "goto probe_epilogue;";
      s.op->newline(-1) << "}";

      // call probe function
      s.op->newline() << "(*spp->write_ph) (c);";

      s.op->newline() << "c->data = NULL;";
      common_probe_entryfn_epilogue (s.op);

      s.op->newline() << "return count;";
      s.op->newline(-1) << "}";
    }
}


void
procfs_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "for (i = 0; i < " << probes_by_path.size() << "; i++) {";
  s.op->newline(1) << "struct stap_procfs_probe *spp = &stap_procfs_probes[i];";

  s.op->newline() << "if (spp->read_pp)";
  s.op->newline(1) << "probe_point = spp->read_pp;";
  s.op->newline(-1) << "else";
  s.op->newline(1) << "probe_point = spp->write_pp;";

  s.op->newline(-1) << "rc = _stp_create_procfs(spp->path, i);";

  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "_stp_close_procfs();";
  s.op->newline() << "break;";
  s.op->newline(-1) << "}";

  if (has_read_probes)
    {
      s.op->newline() << "if (spp->read_pp)";
      s.op->newline(1) << "_stp_procfs_files[i]->read_proc = &_stp_procfs_read;";
      s.op->newline(-1) << "else";
      s.op->newline(1) << "_stp_procfs_files[i]->read_proc = NULL;";
      s.op->indent(-1);
    }
  else
    s.op->newline() << "_stp_procfs_files[i]->read_proc = NULL;";

  if (has_write_probes)
    {
      s.op->newline() << "if (spp->write_pp)";
      s.op->newline(1) << "_stp_procfs_files[i]->write_proc = &_stp_procfs_write;";
      s.op->newline(-1) << "else";
      s.op->newline(1) << "_stp_procfs_files[i]->write_proc = NULL;";
      s.op->indent(-1);
    }
  else
    s.op->newline() << "_stp_procfs_files[i]->write_proc = NULL;";

  s.op->newline() << "_stp_procfs_files[i]->data = spp;";
  s.op->newline(-1) << "}"; // for loop
}


void
procfs_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "_stp_close_procfs();";
}


void
procfs_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (e->base_name != "$value")
    throw semantic_error ("invalid target symbol for procfs probe, $value expected",
                          e->tok);

  if (e->components.size() > 0)
    {
      switch (e->components[0].first)
        {
        case target_symbol::comp_literal_array_index:
          throw semantic_error("procfs target variable '$value' may not be used as array",
                               e->tok);
          break;
        case target_symbol::comp_struct_member:
          throw semantic_error("procfs target variable '$value' may not be used as a structure",
                               e->tok);
          break;
        default:
          throw semantic_error ("invalid use of procfs target variable '$value'",
                                e->tok);
          break;
        }
    }

  bool lvalue = is_active_lvalue(e);
  if (write_probe && lvalue)
    throw semantic_error("procfs $value variable is read-only in a procfs write probe", e->tok);
  else if (! write_probe && ! lvalue)
    throw semantic_error("procfs $value variable cannot be read in a procfs read probe", e->tok);

  // Remember that we've seen a target variable.
  target_symbol_seen = true;

  // Synthesize a function.
  functiondecl *fdecl = new functiondecl;
  fdecl->tok = e->tok;
  embeddedcode *ec = new embeddedcode;
  ec->tok = e->tok;

  string fname = (string(lvalue ? "_procfs_value_set" : "_procfs_value_get")
                  + "_" + lex_cast<string>(tick++));
  string locvalue = "CONTEXT->data";

  if (! lvalue)
    ec->code = string("_stp_copy_from_user(THIS->__retvalue, ((struct _stp_procfs_data *)(")
      + locvalue + string("))->buffer, ((struct _stp_procfs_data *)(") + locvalue
      + string("))->count); /* pure */");
  else
      ec->code = string("int bytes = 0;\n")
	+ string("    struct _stp_procfs_data *data = (struct _stp_procfs_data *)(") + locvalue + string(");\n")
	+ string("    bytes = strnlen(THIS->value, MAXSTRINGLEN - 1);\n")
	+ string("    if (data->off >= bytes)\n")
	+ string("      bytes = 0;\n")
	+ string("    else {\n")
	+ string("      bytes -= data->off;\n")
	+ string("      if (bytes > data->count)\n")
	+ string("        bytes = data->count;\n")
	+ string("      memcpy((void *)data->buffer, THIS->value + data->off, bytes);\n")
	+ string("    }\n")
	+ string("    data->count = bytes;\n");

  fdecl->name = fname;
  fdecl->body = ec;
  fdecl->type = pe_string;

  if (lvalue)
    {
      // Modify the fdecl so it carries a single pe_string formal
      // argument called "value".

      vardecl *v = new vardecl;
      v->type = pe_string;
      v->name = "value";
      v->tok = e->tok;
      fdecl->formal_args.push_back(v);
    }
  sess.functions[fdecl->name]=fdecl;

  // Synthesize a functioncall.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session

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


struct procfs_builder: public derived_probe_builder
{
  procfs_builder() {}
  virtual void build(systemtap_session & sess,
                     probe * base,
                     probe_point * location,
                     literal_map_t const & parameters,
                     vector<derived_probe *> & finished_results);
};


void
procfs_builder::build(systemtap_session & sess,
                      probe * base,
                      probe_point * location,
                      literal_map_t const & parameters,
                      vector<derived_probe *> & finished_results)
{
  string path;
  bool has_procfs = get_param(parameters, TOK_PROCFS, path);
  bool has_read = (parameters.find(TOK_READ) != parameters.end());
  bool has_write = (parameters.find(TOK_WRITE) != parameters.end());

  // If no procfs path, default to "command".  The runtime will do
  // this for us, but if we don't do it here, we'll think the
  // following 2 probes are attached to different paths:
  //
  //   probe procfs("command").read {}"
  //   probe procfs.write {}

  if (! has_procfs)
    path = "command";
  // If we have a path, we need to validate it.
  else
    {
      string::size_type start_pos, end_pos;
      string component;
      start_pos = 0;
      while ((end_pos = path.find('/', start_pos)) != string::npos)
        {
          // Make sure it doesn't start with '/'.
          if (end_pos == 0)
            throw semantic_error ("procfs path cannot start with a '/'",
                                  location->tok);

          component = path.substr(start_pos, end_pos - start_pos);
          // Make sure it isn't empty.
          if (component.size() == 0)
            throw semantic_error ("procfs path component cannot be empty",
                                  location->tok);
          // Make sure it isn't relative.
          else if (component == "." || component == "..")
            throw semantic_error ("procfs path cannot be relative (and contain '.' or '..')", location->tok);

          start_pos = end_pos + 1;
        }
      component = path.substr(start_pos);
      // Make sure it doesn't end with '/'.
      if (component.size() == 0)
        throw semantic_error ("procfs path cannot end with a '/'", location->tok);
      // Make sure it isn't relative.
      else if (component == "." || component == "..")
        throw semantic_error ("procfs path cannot be relative (and contain '.' or '..')", location->tok);
    }

  if (!(has_read ^ has_write))
    throw semantic_error ("need read/write component", location->tok);

  finished_results.push_back(new procfs_derived_probe(sess, base, location,
                                                      path, has_write));
}


void
register_tapset_procfs(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new procfs_builder();

  root->bind(TOK_PROCFS)->bind(TOK_READ)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind(TOK_READ)->bind(builder);
  root->bind(TOK_PROCFS)->bind(TOK_WRITE)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind(TOK_WRITE)->bind(builder);
}



/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
