// tapset for procfs
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

#include <cstring>
#include <string>


using namespace std;
using namespace __gnu_cxx;


static const string TOK_PROCFS("procfs");
static const string TOK_READ("read");
static const string TOK_WRITE("write");
static const string TOK_MAXSIZE("maxsize");
static const string TOK_UMASK("umask");


// ------------------------------------------------------------------------
// procfs file derived probes
// ------------------------------------------------------------------------


struct procfs_derived_probe: public derived_probe
{
  string path;
  bool write;
  bool target_symbol_seen;
  int64_t maxsize_val;
  int64_t umask; 


  procfs_derived_probe (systemtap_session &, probe* p, probe_point* l, string ps, bool w, int64_t m, int64_t umask); 
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
                               string path, bool write_probe);

  systemtap_session& sess;
  string probe_name;
  string path;
  bool write_probe;
  bool target_symbol_seen;

  void visit_target_symbol (target_symbol* e);
};


procfs_derived_probe::procfs_derived_probe (systemtap_session &s, probe* p,
                                            probe_point* l, string ps, bool w,
					    int64_t m, int64_t umask):  
    derived_probe(p, l), path(ps), write(w), target_symbol_seen(false),
    maxsize_val(m), umask(umask) 
{
  // Expand local variables in the probe body
  procfs_var_expanding_visitor v (s, name, path, write); 
  v.replace (this->body);
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
	  + string("  char *buffer;\n")
	  + string("  size_t bufsize;\n")
	  + string("  size_t count;\n")
	  + string("};\n")
	  + string("#ifndef STP_PROCFS_BUFSIZE\n")
	  + string("#define STP_PROCFS_BUFSIZE MAXSTRINGLEN\n")
	  + string("#endif\n");
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
        throw semantic_error(_("only one write procfs probe can exist for procfs path \"") + p->path + "\"");
      else if (! p->write && pset->read_probe != NULL)
        throw semantic_error(_("only one read procfs probe can exist for procfs path \"") + p->path + "\"");

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
  s.op->newline() << "#include \"procfs-probes.c\"";

  // Emit the procfs probe buffer structure
  s.op->newline() << "static struct stap_procfs_probe_buffer {";
  s.op->indent(1);
  unsigned buf_index = 0; // used for buffer naming
  for (p_b_p_iterator it = probes_by_path.begin(); it != probes_by_path.end();
       it++)
    {
      procfs_probe_set *pset = it->second;
      s.op->newline() << "char buf_" << buf_index++;

      if (pset->read_probe != NULL)
        {
	  if (pset->read_probe->maxsize_val == 0)
	    s.op->line() << "[STP_PROCFS_BUFSIZE];";
	  else
	    s.op->line() << "[" << pset->read_probe->maxsize_val << "];";
	}
      else
	s.op->line() << "[MAXSTRINGLEN];";
    }
  s.op->newline(-1) << "} stap_procfs_probe_buffers;";

  // Emit the procfs probe data list
  s.op->newline() << "static struct stap_procfs_probe stap_procfs_probes[] = {";
  s.op->indent(1);

  buf_index = 0;
  for (p_b_p_iterator it = probes_by_path.begin(); it != probes_by_path.end();
       it++)
    {
      procfs_probe_set *pset = it->second;

      s.op->newline() << "{";
      s.op->line() << " .path=" << lex_cast_qstring (it->first) << ",";

      if (pset->read_probe != NULL)
        s.op->line() << " .read_probe=" << common_probe_init (pset->read_probe) << ",";

      if (pset->write_probe != NULL)
        s.op->line() << " .write_probe=" << common_probe_init (pset->write_probe) << ",";

      s.op->line() << " .buffer=stap_procfs_probe_buffers.buf_" << buf_index++ << ",";
      if (pset->read_probe != NULL)
	{
	  if (pset->read_probe->maxsize_val == 0)
	    s.op->line() << " .bufsize=STP_PROCFS_BUFSIZE,";
	  else
	    s.op->line() << " .bufsize="
			 << pset->read_probe->maxsize_val << ",";
	}
      else
	s.op->line() << " .bufsize=MAXSTRINGLEN,";

       s.op->line() << " .permissions=" << (((pset->read_probe ? 0444 : 0) 
					 | (pset->write_probe ? 0222 : 0)) &~ 
					   ((pset->read_probe ? pset->read_probe->umask : 0) 
					 | (pset->write_probe ? pset->write_probe->umask : 0))) 
					<< ",";

      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";

  // Output routine to fill in the buffer with our data.  Note that we
  // need to do this even in the case where we have no read probes,
  // but we can skip most of it then.
  s.op->newline();

  s.op->newline() << "static int _stp_proc_fill_read_buffer(struct stap_procfs_probe *spp) {";
  s.op->indent(1);
  if (has_read_probes)
    {
      s.op->newline() << "struct _stp_procfs_data pdata;";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING",
				     "spp->read_probe",
				     "_STP_PROBE_HANDLER_PROCFS");

      s.op->newline() << "pdata.buffer = spp->buffer;";
      s.op->newline() << "pdata.bufsize = spp->bufsize;";
      s.op->newline() << "if (c->ips.procfs_data == NULL)";
      s.op->newline(1) << "c->ips.procfs_data = &pdata;";
      s.op->newline(-1) << "else {";

      s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
      s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      s.op->newline() << "_stp_exit ();";
      s.op->newline(-1) << "}";
      s.op->newline() << "atomic_dec (& c->busy);";
      s.op->newline() << "goto probe_epilogue;";
      s.op->newline(-1) << "}";

      // call probe function
      s.op->newline() << "(*spp->read_probe->ph) (c);";

      // Note that _procfs_value_set copied string data into spp->buffer
      s.op->newline() << "c->ips.procfs_data = NULL;";
      s.op->newline() << "spp->needs_fill = 0;";
      s.op->newline() << "spp->count = strlen(spp->buffer);";

      common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);

      s.op->newline() << "if (spp->needs_fill) {";
      s.op->newline(1) << "spp->needs_fill = 0;";
      s.op->newline() << "return -EIO;";
      s.op->newline(-1) << "}";
    }
  s.op->newline() << "return 0;";
  s.op->newline(-1) << "}";

  // Output routine to read data.  Note that we need to do this even
  // in the case where we have no write probes, but we can skip most
  // of it then.
  s.op->newline() << "static int _stp_process_write_buffer(struct stap_procfs_probe *spp, const char __user *buf, size_t count) {";
  s.op->indent(1);
  s.op->newline() << "int retval = 0;";
  if (has_write_probes)
    {
      s.op->newline() << "struct _stp_procfs_data pdata;";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING",
				     "spp->write_probe",
				     "_STP_PROBE_HANDLER_PROCFS");

      // We've got 2 problems here.  The data count could be greater
      // than MAXSTRINGLEN or greater than the bufsize (if the same
      // procfs file had a size less than MAXSTRINGLEN).
      s.op->newline() << "if (count >= MAXSTRINGLEN)";
      s.op->newline(1) << "count = MAXSTRINGLEN - 1;";
      s.op->indent(-1);
      s.op->newline() << "pdata.bufsize = spp->bufsize;";
      s.op->newline() << "if (count >= pdata.bufsize)";
      s.op->newline(1) << "count = pdata.bufsize - 1;";
      s.op->indent(-1);

      s.op->newline() << "pdata.buffer = (char *)buf;";
      s.op->newline() << "pdata.count = count;";

      s.op->newline() << "if (c->ips.procfs_data == NULL)";
      s.op->newline(1) << "c->ips.procfs_data = &pdata;";
      s.op->newline(-1) << "else {";

      s.op->newline(1) << "if (unlikely (atomic_inc_return (& skipped_count) > MAXSKIPPED)) {";
      s.op->newline(1) << "atomic_set (& session_state, STAP_SESSION_ERROR);";
      s.op->newline() << "_stp_exit ();";
      s.op->newline(-1) << "}";
      s.op->newline() << "atomic_dec (& c->busy);";
      s.op->newline() << "goto probe_epilogue;";
      s.op->newline(-1) << "}";

      // call probe function
      s.op->newline() << "(*spp->write_probe->ph) (c);";

      s.op->newline() << "c->ips.procfs_data = NULL;";
      s.op->newline() << "if (c->last_error == 0) {";
      s.op->newline(1) << "retval = count;";
      s.op->newline(-1) << "}";

      common_probe_entryfn_epilogue (s.op, true, s.suppress_handler_errors);
    }

  s.op->newline() << "return retval;";
  s.op->newline(-1) << "}";
}


void
procfs_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "for (i = 0; i < " << probes_by_path.size() << "; i++) {";
  s.op->newline(1) << "struct stap_procfs_probe *spp = &stap_procfs_probes[i];";

  s.op->newline() << "if (spp->read_probe)";
  s.op->newline(1) << "probe_point = spp->read_probe->pp;";
  s.op->newline(-1) << "else";
  s.op->newline(1) << "probe_point = spp->write_probe->pp;";
  s.op->indent(-1);

  s.op->newline() << "_spp_init(spp);";
  s.op->newline() << "rc = _stp_create_procfs(spp->path, i, &_stp_proc_fops, spp->permissions);";  

  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "_stp_close_procfs();";

  s.op->newline() << "for (i = 0; i < " << probes_by_path.size() << "; i++) {";
  s.op->newline(1) << "spp = &stap_procfs_probes[i];";
  s.op->newline() << "_spp_shutdown(spp);";
  s.op->newline(-1) << "}";
  s.op->newline() << "break;";
  s.op->newline(-1) << "}";

  s.op->newline() << "_stp_procfs_files[i]->data = spp;";
  s.op->newline(-1) << "}"; // for loop
}


void
procfs_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes_by_path.empty())
    return;

  s.op->newline() << "_stp_close_procfs();";
  s.op->newline() << "for (i = 0; i < " << probes_by_path.size() << "; i++) {";
  s.op->newline(1) << "struct stap_procfs_probe *spp = &stap_procfs_probes[i];";
  s.op->newline() << "_spp_shutdown(spp);";
  s.op->newline(-1) << "}";
}


procfs_var_expanding_visitor::procfs_var_expanding_visitor (systemtap_session& s,
							    const string& pn,
							    string path,
							    bool write_probe):
  sess (s), probe_name (pn), path (path), write_probe (write_probe),
  target_symbol_seen (false)
{
  // procfs probes can also handle '.='.
  valid_ops.insert (".=");
}


void
procfs_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  try
    {
      assert(e->name.size() > 0 && e->name[0] == '$');

      if (e->name != "$value")
        throw semantic_error (_("invalid target symbol for procfs probe, $value expected"),
                              e->tok);

      e->assert_no_components("procfs");

      bool lvalue = is_active_lvalue(e);
      if (write_probe && lvalue)
        throw semantic_error(_("procfs $value variable is read-only in a procfs write probe"), e->tok);
  else if (! write_probe && ! lvalue)
    throw semantic_error(_("procfs $value variable cannot be read in a procfs read probe"), e->tok);

      if (e->addressof)
        throw semantic_error(_("cannot take address of procfs variable"), e->tok);

      // Remember that we've seen a target variable.
      target_symbol_seen = true;

      // Synthesize a function.
      functiondecl *fdecl = new functiondecl;
      fdecl->synthetic = true;
      fdecl->tok = e->tok;
      embeddedcode *ec = new embeddedcode;
      ec->tok = e->tok;

      string fname;
      string locvalue = "CONTEXT->ips.procfs_data";

      if (! lvalue)
        {
          fname = "_procfs_value_get";
          ec->code = string("    struct _stp_procfs_data *data = (struct _stp_procfs_data *)(") + locvalue + string("); /* pure */\n")

            + string("    _stp_copy_from_user(STAP_RETVALUE, data->buffer, data->count);\n")
            + string("    STAP_RETVALUE[data->count] = '\\0';\n");
        }
      else					// lvalue
        {
          if (*op == "=")
            {
              fname = "_procfs_value_set";
              ec->code = string("struct _stp_procfs_data *data = (struct _stp_procfs_data *)(") + locvalue + string(");\n")
                + string("    strlcpy(data->buffer, STAP_ARG_value, data->bufsize);\n")
                + string("    data->count = strlen(data->buffer);\n");
            }
          else if (*op == ".=")
            {
              fname = "_procfs_value_append";
              ec->code = string("struct _stp_procfs_data *data = (struct _stp_procfs_data *)(") + locvalue + string(");\n")
                + string("    strlcat(data->buffer, STAP_ARG_value, data->bufsize);\n")
                + string("    data->count = strlen(data->buffer);\n");
            }
          else
            {
              throw semantic_error (_("Only the following assign operators are"
                                    " implemented on procfs read target variables:"
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
          v->name = "value";
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
  bool has_umask = (parameters.find(TOK_UMASK) != parameters.end()); 
  int64_t maxsize_val = 0;
  int64_t umask_val;
  if(has_umask)  
	   get_param(parameters, TOK_UMASK, umask_val);
  else /* no .umask */
         {
	   if(has_read)
		 umask_val = 0044;
	   else if(has_write)
		 umask_val = 0022;
	   else
		 assert(0);
	 }	
  // Validate '.maxsize(NNN)', if it exists.
  if (get_param(parameters, TOK_MAXSIZE, maxsize_val))
    {
      if (maxsize_val <= 0)
	throw semantic_error (_("maxsize must be greater than 0"));
    }

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
            throw semantic_error (_("procfs path cannot start with a '/'"),
                                  location->components.front()->tok);

          component = path.substr(start_pos, end_pos - start_pos);
          // Make sure it isn't empty.
          if (component.size() == 0)
            throw semantic_error (_("procfs path component cannot be empty"),
                                  location->components.front()->tok);
          // Make sure it isn't relative.
          else if (component == "." || component == "..")
            throw semantic_error (_("procfs path cannot be relative (and contain '.' or '..')"), location->components.front()->tok);

          start_pos = end_pos + 1;
        }
      component = path.substr(start_pos);
      // Make sure it doesn't end with '/'.
      if (component.size() == 0)
        throw semantic_error (_("procfs path cannot end with a '/'"), location->components.front()->tok);
      // Make sure it isn't relative.
      else if (component == "." || component == "..")
        throw semantic_error (_("procfs path cannot be relative (and contain '.' or '..')"), location->components.front()->tok);
    }

  if (!(has_read ^ has_write))
    throw semantic_error (_("need read/write component"), location->components.front()->tok);

  finished_results.push_back(new procfs_derived_probe(sess, base, location,
                                                      path, has_write,
						      maxsize_val, umask_val));
}


void
register_tapset_procfs(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new procfs_builder();

  root->bind(TOK_PROCFS)->bind(TOK_READ)->bind(builder);
  root->bind(TOK_PROCFS)->bind_num(TOK_UMASK)->bind(TOK_READ)->bind(builder);
  root->bind(TOK_PROCFS)->bind(TOK_READ)->bind_num(TOK_MAXSIZE)->bind(builder);
  root->bind(TOK_PROCFS)->bind_num(TOK_UMASK)->bind(TOK_READ)->bind_num(TOK_MAXSIZE)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind(TOK_READ)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind_num(TOK_UMASK)->bind(TOK_READ)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind(TOK_READ)->bind_num(TOK_MAXSIZE)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind_num(TOK_UMASK)->bind(TOK_READ)->bind_num(TOK_MAXSIZE)->bind(builder);

  root->bind(TOK_PROCFS)->bind(TOK_WRITE)->bind(builder);
  root->bind(TOK_PROCFS)->bind_num(TOK_UMASK)->bind(TOK_WRITE)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind(TOK_WRITE)->bind(builder);
  root->bind_str(TOK_PROCFS)->bind_num(TOK_UMASK)->bind(TOK_WRITE)->bind(builder);
}



/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
