// tapset for timers
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
#include "task_finder.h"
#include "translate.h"
#include "util.h"

#include <cstring>
#include <string>


using namespace std;
using namespace __gnu_cxx;


static const string TOK_PROCESS("process");
static const string TOK_INSN("insn");
static const string TOK_BLOCK("block");


// ------------------------------------------------------------------------
// itrace user-space probes
// ------------------------------------------------------------------------


struct itrace_derived_probe: public derived_probe
{
  bool has_path;
  string path;
  int64_t pid;
  int single_step;

  itrace_derived_probe (systemtap_session &s, probe* p, probe_point* l,
                        bool hp, string &pn, int64_t pd, int ss
			);
  void join_group (systemtap_session& s);
};


struct itrace_derived_probe_group: public generic_dpg<itrace_derived_probe>
{
private:
  map<string, vector<itrace_derived_probe*> > probes_by_path;
  typedef map<string, vector<itrace_derived_probe*> >::iterator p_b_path_iterator;
  map<int64_t, vector<itrace_derived_probe*> > probes_by_pid;
  typedef map<int64_t, vector<itrace_derived_probe*> >::iterator p_b_pid_iterator;
  unsigned num_probes;

  void emit_probe_decl (systemtap_session& s, itrace_derived_probe *p);

public:
  itrace_derived_probe_group(): num_probes(0) { }

  void enroll (itrace_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


itrace_derived_probe::itrace_derived_probe (systemtap_session &s,
                                            probe* p, probe_point* l,
                                            bool hp, string &pn, int64_t pd,
					    int ss
					    ):
  derived_probe(p, l), has_path(hp), path(pn), pid(pd), single_step(ss)
{
}


void
itrace_derived_probe::join_group (systemtap_session& s)
{
  if (! s.itrace_derived_probes)
    s.itrace_derived_probes = new itrace_derived_probe_group ();

  s.itrace_derived_probes->enroll (this);

  enable_task_finder(s);
}

struct itrace_builder: public derived_probe_builder
{
  itrace_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     std::map<std::string, literal *> const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    string path;
    int64_t pid = 0;
    int single_step;

    bool has_path = get_param (parameters, TOK_PROCESS, path);
    bool has_pid = get_param (parameters, TOK_PROCESS, pid);
    // XXX: PR 6445 needs !has_path && !has_pid support
    assert (has_path || has_pid);

    single_step = ! has_null_param (parameters, TOK_BLOCK);

    // If we have a path, we need to validate it.
    if (has_path)
      path = find_executable (path);

    finished_results.push_back(new itrace_derived_probe(sess, base, location,
							has_path, path, pid,
							single_step
							));
  }
};


void
itrace_derived_probe_group::enroll (itrace_derived_probe* p)
{
  if (p->has_path)
    probes_by_path[p->path].push_back(p);
  else
    probes_by_pid[p->pid].push_back(p);
  num_probes++;

  // XXX: multiple exec probes (for instance) for the same path (or
  // pid) should all share a itrace report function, and have their
  // handlers executed sequentially.
}


void
itrace_derived_probe_group::emit_probe_decl (systemtap_session& s,
					     itrace_derived_probe *p)
{
  s.op->newline() << "{";
  s.op->line() << " .tgt={";

  if (p->has_path)
    {
      s.op->line() << " .pathname=\"" << p->path << "\",";
      s.op->line() << " .pid=0,";
    }
  else
    {
      s.op->line() << " .pathname=NULL,";
      s.op->line() << " .pid=" << p->pid << ",";
    }

  s.op->line() << " .callback=&_stp_itrace_probe_cb,";
  s.op->line() << " },";
  s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
  s.op->line() << " .single_step=" << p->single_step << ",";
  s.op->line() << " .ph=&" << p->name << ",";

  s.op->line() << " },";
}


void
itrace_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline();
  s.op->newline() << "/* ---- itrace probes ---- */";

  s.op->newline() << "struct stap_itrace_probe {";
  s.op->indent(1);
  s.op->newline() << "struct stap_task_finder_target tgt;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "int single_step;";
  s.op->newline(-1) << "};";
  s.op->newline() << "static void enter_itrace_probe(struct stap_itrace_probe *p, struct pt_regs *regs, void *data);";
  s.op->newline() << "#include \"itrace.c\"";

  // output routine to call itrace probe
  s.op->newline() << "static void enter_itrace_probe(struct stap_itrace_probe *p, struct pt_regs *regs, void *data) {";
  s.op->indent(1);

  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "p->pp");
  s.op->newline() << "c->regs = regs;";
  s.op->newline() << "c->data = data;";

  // call probe function
  s.op->newline() << "(*p->ph) (c);";
  common_probe_entryfn_epilogue (s.op);

  s.op->newline() << "return;";
  s.op->newline(-1) << "}";

  // Output task finder callback routine that gets called for all
  // itrace probe types.
  s.op->newline() << "static int _stp_itrace_probe_cb(struct stap_task_finder_target *tgt, struct task_struct *tsk, int register_p, int process_p) {";
  s.op->indent(1);
  s.op->newline() << "int rc = 0;";
  s.op->newline() << "struct stap_itrace_probe *p = container_of(tgt, struct stap_itrace_probe, tgt);";

  s.op->newline() << "if (register_p) ";
  s.op->indent(1);

  s.op->newline() << "rc = usr_itrace_init(p->single_step, tsk, p);";
  s.op->newline(-1) << "else";
  s.op->newline(1) << "remove_usr_itrace_info(find_itrace_info(p->tgt.pid));";
  s.op->newline(-1) << "return rc;";
  s.op->newline(-1) << "}";

  // Emit vma callbacks.
  s.op->newline() << "#ifdef STP_NEED_VMA_TRACKER";
  s.op->newline() << "static struct stap_task_finder_target stap_itrace_vmcbs[] = {";
  s.op->indent(1);
  if (! probes_by_path.empty())
    {
      for (p_b_path_iterator it = probes_by_path.begin();
           it != probes_by_path.end(); it++)
        emit_vma_callback_probe_decl (s, it->first, (int64_t)0);
    }
  if (! probes_by_pid.empty())
    {
      for (p_b_pid_iterator it = probes_by_pid.begin();
           it != probes_by_pid.end(); it++)
        emit_vma_callback_probe_decl (s, "", it->first);
    }
  s.op->newline(-1) << "};";
  s.op->newline() << "#endif";

  s.op->newline() << "static struct stap_itrace_probe stap_itrace_probes[] = {";
  s.op->indent(1);

  // Set up 'process(PATH)' probes
  if (! probes_by_path.empty())
    {
      for (p_b_path_iterator it = probes_by_path.begin();
	   it != probes_by_path.end(); it++)
        {
	  for (unsigned i = 0; i < it->second.size(); i++)
	    {
	      itrace_derived_probe *p = it->second[i];
	      emit_probe_decl(s, p);
	    }
	}
    }

  // Set up 'process(PID)' probes
  if (! probes_by_pid.empty())
    {
      for (p_b_pid_iterator it = probes_by_pid.begin();
	   it != probes_by_pid.end(); it++)
        {
	  for (unsigned i = 0; i < it->second.size(); i++)
	    {
	      itrace_derived_probe *p = it->second[i];
	      emit_probe_decl(s, p);
	    }
	}
    }
  s.op->newline(-1) << "};";
}


void
itrace_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline();
  s.op->newline() << "#ifdef STP_NEED_VMA_TRACKER";
  s.op->newline() << "_stp_sym_init();";
  s.op->newline() << "/* ---- itrace vma callbacks ---- */";
  s.op->newline() << "for (i=0; i<ARRAY_SIZE(stap_itrace_vmcbs); i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_task_finder_target *r = &stap_itrace_vmcbs[i];";
  s.op->newline() << "rc = stap_register_task_finder_target(r);";
  s.op->newline(-1) << "}";
  s.op->newline() << "#endif";

  s.op->newline();
  s.op->newline() << "/* ---- itrace probes ---- */";

  s.op->newline() << "for (i=0; i<" << num_probes << "; i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_itrace_probe *p = &stap_itrace_probes[i];";

  // 'arch_has_single_step' needs to be defined for either single step mode
  // or branch mode.
  s.op->newline() << "if (!arch_has_single_step()) {";
  s.op->indent(1);
  s.op->newline() << "_stp_error (\"insn probe init: arch does not support step mode\");";
  s.op->newline() << "rc = -EPERM;";
  s.op->newline() << "break;";
  s.op->newline(-1) << "}";
  s.op->newline() << "if (!p->single_step && !arch_has_block_step()) {";
  s.op->indent(1);
  s.op->newline() << "_stp_error (\"insn probe init: arch does not support block step mode\");";
  s.op->newline() << "rc = -EPERM;";
  s.op->newline() << "break;";
  s.op->newline(-1) << "}";

  s.op->newline() << "rc = stap_register_task_finder_target(&p->tgt);";
  s.op->newline(-1) << "}";
}


void
itrace_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty()) return;
  s.op->newline();
  s.op->newline() << "/* ---- itrace probes ---- */";
  s.op->newline() << "cleanup_usr_itrace();";
}

void
register_tapset_itrace(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new itrace_builder();

  root->bind_str(TOK_PROCESS)->bind(TOK_INSN)->bind(builder);
  root->bind_num(TOK_PROCESS)->bind(TOK_INSN)->bind(builder);
  root->bind_str(TOK_PROCESS)->bind(TOK_INSN)->bind(TOK_BLOCK)->bind(builder);
  root->bind_num(TOK_PROCESS)->bind(TOK_INSN)->bind(TOK_BLOCK)->bind(builder);
}



/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
