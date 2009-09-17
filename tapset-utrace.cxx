// utrace tapset
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
static const string TOK_BEGIN("begin");
static const string TOK_END("end");
static const string TOK_THREAD("thread");
static const string TOK_SYSCALL("syscall");
static const string TOK_RETURN("return");


// ------------------------------------------------------------------------
// utrace user-space probes
// ------------------------------------------------------------------------

// Note that these flags don't match up exactly with UTRACE_EVENT
// flags (and that's OK).
enum utrace_derived_probe_flags {
  UDPF_NONE,
  UDPF_BEGIN,				// process begin
  UDPF_END,				// process end
  UDPF_THREAD_BEGIN,			// thread begin
  UDPF_THREAD_END,			// thread end
  UDPF_SYSCALL,				// syscall entry
  UDPF_SYSCALL_RETURN,			// syscall exit
  UDPF_NFLAGS
};

struct utrace_derived_probe: public derived_probe
{
  bool has_path;
  string path;
  int64_t pid;
  enum utrace_derived_probe_flags flags;
  bool target_symbol_seen;

  utrace_derived_probe (systemtap_session &s, probe* p, probe_point* l,
                        bool hp, string &pn, int64_t pd,
			enum utrace_derived_probe_flags f);
  void join_group (systemtap_session& s);
};


struct utrace_derived_probe_group: public generic_dpg<utrace_derived_probe>
{
private:
  map<string, vector<utrace_derived_probe*> > probes_by_path;
  typedef map<string, vector<utrace_derived_probe*> >::iterator p_b_path_iterator;
  map<int64_t, vector<utrace_derived_probe*> > probes_by_pid;
  typedef map<int64_t, vector<utrace_derived_probe*> >::iterator p_b_pid_iterator;
  unsigned num_probes;
  bool flags_seen[UDPF_NFLAGS];

  void emit_probe_decl (systemtap_session& s, utrace_derived_probe *p);

public:
  utrace_derived_probe_group(): num_probes(0), flags_seen() { }

  void enroll (utrace_derived_probe* probe);
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct utrace_var_expanding_visitor: public var_expanding_visitor
{
  utrace_var_expanding_visitor(systemtap_session& s, probe_point* l,
			       const string& pn,
                               enum utrace_derived_probe_flags f):
    sess (s), base_loc (l), probe_name (pn), flags (f),
    target_symbol_seen (false), add_block(NULL), add_probe(NULL) {}

  systemtap_session& sess;
  probe_point* base_loc;
  string probe_name;
  enum utrace_derived_probe_flags flags;
  bool target_symbol_seen;
  block *add_block;
  probe *add_probe;
  std::map<std::string, symbol *> return_ts_map;

  void visit_target_symbol_arg (target_symbol* e);
  void visit_target_symbol_context (target_symbol* e);
  void visit_target_symbol_cached (target_symbol* e);
  void visit_target_symbol (target_symbol* e);
};



utrace_derived_probe::utrace_derived_probe (systemtap_session &s,
                                            probe* p, probe_point* l,
                                            bool hp, string &pn, int64_t pd,
					    enum utrace_derived_probe_flags f):
  derived_probe (p, new probe_point (*l) /* .components soon rewritten */ ),
  has_path(hp), path(pn), pid(pd), flags(f),
  target_symbol_seen(false)
{
  // Expand local variables in the probe body
  utrace_var_expanding_visitor v (s, l, name, flags);
  v.replace (this->body);
  target_symbol_seen = v.target_symbol_seen;

  // If during target-variable-expanding the probe, we added a new block
  // of code, add it to the start of the probe.
  if (v.add_block)
    this->body = new block(v.add_block, this->body);
  // If when target-variable-expanding the probe, we added a new
  // probe, add it in a new file to the list of files to be processed.
  if (v.add_probe)
    {
      stapfile *f = new stapfile;
      f->probes.push_back(v.add_probe);
      s.files.push_back(f);
    }

  // Reset the sole element of the "locations" vector as a
  // "reverse-engineered" form of the incoming (q.base_loc) probe
  // point.  This allows a user to see what program etc.
  // number any particular match of the wildcards.

  vector<probe_point::component*> comps;
  if (hp)
    comps.push_back (new probe_point::component(TOK_PROCESS, new literal_string(path)));
  else if (pid != 0)
    comps.push_back (new probe_point::component(TOK_PROCESS, new literal_number(pid)));
  else
    comps.push_back (new probe_point::component(TOK_PROCESS));

  switch (flags)
    {
    case UDPF_THREAD_BEGIN:
      comps.push_back (new probe_point::component(TOK_THREAD));
      comps.push_back (new probe_point::component(TOK_BEGIN));
      break;
    case UDPF_THREAD_END:
      comps.push_back (new probe_point::component(TOK_THREAD));
      comps.push_back (new probe_point::component(TOK_END));
      break;
    case UDPF_SYSCALL:
      comps.push_back (new probe_point::component(TOK_SYSCALL));
      break;
    case UDPF_SYSCALL_RETURN:
      comps.push_back (new probe_point::component(TOK_SYSCALL));
      comps.push_back (new probe_point::component(TOK_RETURN));
      break;
    case UDPF_BEGIN:
      comps.push_back (new probe_point::component(TOK_BEGIN));
      break;
    case UDPF_END:
      comps.push_back (new probe_point::component(TOK_END));
      break;
    default:
      assert (0);
    }

  // Overwrite it.
  this->sole_location()->components = comps;
}


void
utrace_derived_probe::join_group (systemtap_session& s)
{
  if (! s.utrace_derived_probes)
    {
      s.utrace_derived_probes = new utrace_derived_probe_group ();
    }
  s.utrace_derived_probes->enroll (this);

  enable_task_finder(s);
}


void
utrace_var_expanding_visitor::visit_target_symbol_cached (target_symbol* e)
{
      // Get the full name of the target symbol.
      stringstream ts_name_stream;
      e->print(ts_name_stream);
      string ts_name = ts_name_stream.str();

      // Check and make sure we haven't already seen this target
      // variable in this return probe.  If we have, just return our
      // last replacement.
      map<string, symbol *>::iterator i = return_ts_map.find(ts_name);
      if (i != return_ts_map.end())
	{
	  provide (i->second);
	  return;
	}

      // We've got to do several things here to handle target
      // variables in return probes.

      // (1) Synthesize a global array which is the cache of the
      // target variable value.  We don't need a nesting level counter
      // like the dwarf_var_expanding_visitor::visit_target_symbol()
      // does since a particular thread can only be in one system
      // calls at a time. The array will look like this:
      //
      //   _utrace_tvar_{name}_{num}
      string aname = (string("_utrace_tvar_")
		      + e->base_name.substr(1)
		      + "_" + lex_cast(tick++));
      vardecl* vd = new vardecl;
      vd->name = aname;
      vd->tok = e->tok;
      sess.globals.push_back (vd);

      // (2) Create a new code block we're going to insert at the
      // beginning of this probe to get the cached value into a
      // temporary variable.  We'll replace the target variable
      // reference with the temporary variable reference.  The code
      // will look like this:
      //
      //   _utrace_tvar_tid = tid()
      //   _utrace_tvar_{name}_{num}_tmp
      //       = _utrace_tvar_{name}_{num}[_utrace_tvar_tid]
      //   delete _utrace_tvar_{name}_{num}[_utrace_tvar_tid]

      // (2a) Synthesize the tid temporary expression, which will look
      // like this:
      //
      //   _utrace_tvar_tid = tid()
      symbol* tidsym = new symbol;
      tidsym->name = string("_utrace_tvar_tid");
      tidsym->tok = e->tok;

      if (add_block == NULL)
        {
	   add_block = new block;
	   add_block->tok = e->tok;

	   // Synthesize a functioncall to grab the thread id.
	   functioncall* fc = new functioncall;
	   fc->tok = e->tok;
	   fc->function = string("tid");

	   // Assign the tid to '_utrace_tvar_tid'.
	   assignment* a = new assignment;
	   a->tok = e->tok;
	   a->op = "=";
	   a->left = tidsym;
	   a->right = fc;

	   expr_statement* es = new expr_statement;
	   es->tok = e->tok;
	   es->value = a;
	   add_block->statements.push_back (es);
	}

      // (2b) Synthesize an array reference and assign it to a
      // temporary variable (that we'll use as replacement for the
      // target variable reference).  It will look like this:
      //
      //   _utrace_tvar_{name}_{num}_tmp
      //       = _utrace_tvar_{name}_{num}[_utrace_tvar_tid]

      arrayindex* ai_tvar = new arrayindex;
      ai_tvar->tok = e->tok;

      symbol* sym = new symbol;
      sym->name = aname;
      sym->tok = e->tok;
      ai_tvar->base = sym;

      ai_tvar->indexes.push_back(tidsym);

      symbol* tmpsym = new symbol;
      tmpsym->name = aname + "_tmp";
      tmpsym->tok = e->tok;

      assignment* a = new assignment;
      a->tok = e->tok;
      a->op = "=";
      a->left = tmpsym;
      a->right = ai_tvar;

      expr_statement* es = new expr_statement;
      es->tok = e->tok;
      es->value = a;

      add_block->statements.push_back (es);

      // (2c) Delete the array value.  It will look like this:
      //
      //   delete _utrace_tvar_{name}_{num}[_utrace_tvar_tid]

      delete_statement* ds = new delete_statement;
      ds->tok = e->tok;
      ds->value = ai_tvar;
      add_block->statements.push_back (ds);

      // (3) We need an entry probe that saves the value for us in the
      // global array we created.  Create the entry probe, which will
      // look like this:
      //
      //   probe process(PATH_OR_PID).syscall {
      //     _utrace_tvar_tid = tid()
      //     _utrace_tvar_{name}_{num}[_utrace_tvar_tid] = ${param}
      //   }
      //
      // Why the temporary for tid()?  If we end up caching more
      // than one target variable, we can reuse the temporary instead
      // of calling tid() multiple times.

      if (add_probe == NULL)
        {
	   add_probe = new probe;
	   add_probe->tok = e->tok;

	   // We need the name of the current probe point, minus the
	   // ".return".  Create a new probe point, copying all the
	   // components, stopping when we see the ".return"
	   // component.
	   probe_point* pp = new probe_point;
	   for (unsigned c = 0; c < base_loc->components.size(); c++)
	     {
	        if (base_loc->components[c]->functor == "return")
		  break;
	        else
		  pp->components.push_back(base_loc->components[c]);
	     }
	   pp->tok = e->tok;
	   pp->optional = base_loc->optional;
	   add_probe->locations.push_back(pp);

	   add_probe->body = new block;
	   add_probe->body->tok = e->tok;

	   // Synthesize a functioncall to grab the thread id.
	   functioncall* fc = new functioncall;
	   fc->tok = e->tok;
	   fc->function = string("tid");

	   // Assign the tid to '_utrace_tvar_tid'.
	   assignment* a = new assignment;
	   a->tok = e->tok;
	   a->op = "=";
	   a->left = tidsym;
	   a->right = fc;

	   expr_statement* es = new expr_statement;
	   es->tok = e->tok;
	   es->value = a;
           add_probe->body = new block(add_probe->body, es);

	   vardecl* vd = new vardecl;
	   vd->tok = e->tok;
	   vd->name = tidsym->name;
	   vd->type = pe_long;
	   vd->set_arity(0);
	   add_probe->locals.push_back(vd);
	}

      // Save the value, like this:
      //
      //   _utrace_tvar_{name}_{num}[_utrace_tvar_tid] = ${param}
      a = new assignment;
      a->tok = e->tok;
      a->op = "=";
      a->left = ai_tvar;
      a->right = e;

      es = new expr_statement;
      es->tok = e->tok;
      es->value = a;

      add_probe->body = new block(add_probe->body, es);

      // (4) Provide the '_utrace_tvar_{name}_{num}_tmp' variable to
      // our parent so it can be used as a substitute for the target
      // symbol.
      provide (tmpsym);

      // (5) Remember this replacement since we might be able to reuse
      // it later if the same return probe references this target
      // symbol again.
      return_ts_map[ts_name] = tmpsym;
      return;
}


void
utrace_var_expanding_visitor::visit_target_symbol_arg (target_symbol* e)
{
  if (flags != UDPF_SYSCALL)
    throw semantic_error ("only \"process(PATH_OR_PID).syscall\" support $argN or $$parms.", e->tok);

  if (e->base_name == "$$parms") 
    {
      // copy from tracepoint
      print_format* pf = new print_format;
      token* pf_tok = new token(*e->tok);
      pf_tok->content = "sprintf";
      pf->tok = pf_tok;
      pf->print_to_stream = false;
      pf->print_with_format = true;
      pf->print_with_delim = false;
      pf->print_with_newline = false;
      pf->print_char = false;

      target_symbol_seen = true;

      for (unsigned i = 0; i < 6; ++i)
        {
          if (i > 0)
            pf->raw_components += " ";
          pf->raw_components += "$arg" + lex_cast(i+1);
          target_symbol *tsym = new target_symbol;
          tsym->tok = e->tok;
          tsym->base_name = "$arg" + lex_cast(i+1);
          tsym->saved_conversion_error = 0;
          pf->raw_components += "=%#x"; //FIXME: missing type info

	  functioncall* n = new functioncall; //same as the following
	  n->tok = e->tok;
	  n->function = "_utrace_syscall_arg";
	  n->referent = 0;
	  literal_number *num = new literal_number(i);
	  num->tok = e->tok;
	  n->args.push_back(num);

          pf->args.push_back(n);
        }
      pf->components = print_format::string_to_components(pf->raw_components);

      provide (pf);
     } 
   else // $argN
     {
        string argnum_s = e->base_name.substr(4,e->base_name.length()-4);
        int argnum = lex_cast<int>(argnum_s);

        e->assert_no_components("utrace");

        // FIXME: max argnument number should not be hardcoded.
        if (argnum < 1 || argnum > 6)
           throw semantic_error ("invalid syscall argument number (1-6)", e->tok);

        bool lvalue = is_active_lvalue(e);
        if (lvalue)
           throw semantic_error("utrace '$argN' variable is read-only", e->tok);

        // Remember that we've seen a target variable.
        target_symbol_seen = true;

        // We're going to substitute a synthesized '_utrace_syscall_arg'
        // function call for the '$argN' reference.
        functioncall* n = new functioncall;
        n->tok = e->tok;
        n->function = "_utrace_syscall_arg";
        n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session

        literal_number *num = new literal_number(argnum - 1);
        num->tok = e->tok;
        n->args.push_back(num);

        provide (n);
     }
}

void
utrace_var_expanding_visitor::visit_target_symbol_context (target_symbol* e)
{
  string sname = e->base_name;

  e->assert_no_components("utrace");

  bool lvalue = is_active_lvalue(e);
  if (lvalue)
    throw semantic_error("utrace '" + sname + "' variable is read-only", e->tok);

  string fname;
  if (sname == "$return")
    {
      if (flags != UDPF_SYSCALL_RETURN)
	throw semantic_error ("only \"process(PATH_OR_PID).syscall.return\" support $return.", e->tok);
      fname = "_utrace_syscall_return";
    }
  else if (sname == "$syscall")
    {
      // If we've got a syscall entry probe, we can just call the
      // right function.
      if (flags == UDPF_SYSCALL) {
        fname = "_utrace_syscall_nr";
      }
      // If we're in a syscal return probe, we can't really access
      // $syscall.  So, similar to what
      // dwarf_var_expanding_visitor::visit_target_symbol() does,
      // we'll create an syscall entry probe to cache $syscall, then
      // we'll access the cached value in the syscall return probe.
      else {
	visit_target_symbol_cached (e);

	// Remember that we've seen a target variable.
	target_symbol_seen = true;
	return;
      }
    }
  else
    {
      throw semantic_error ("unknown target variable", e->tok);
    }

  // Remember that we've seen a target variable.
  target_symbol_seen = true;

  // We're going to substitute a synthesized '_utrace_syscall_nr'
  // function call for the '$syscall' reference.
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = fname;
  n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session

  provide (n);
}

void
utrace_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (flags != UDPF_SYSCALL && flags != UDPF_SYSCALL_RETURN)
    throw semantic_error ("only \"process(PATH_OR_PID).syscall\" and \"process(PATH_OR_PID).syscall.return\" probes support target symbols",
			  e->tok);

  if (e->addressof)
    throw semantic_error("cannot take address of utrace variable", e->tok);

  if (e->base_name.substr(0,4) == "$arg" || e->base_name == "$$parms")
    visit_target_symbol_arg(e);
  else if (e->base_name == "$syscall" || e->base_name == "$return")
    visit_target_symbol_context(e);
  else
    throw semantic_error ("invalid target symbol for utrace probe, $syscall, $return, $argN or $$parms expected",
			  e->tok);
}


struct utrace_builder: public derived_probe_builder
{
  utrace_builder() {}
  virtual void build(systemtap_session & sess,
		     probe * base,
		     probe_point * location,
		     literal_map_t const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    string path;
    int64_t pid;

    bool has_path = get_param (parameters, TOK_PROCESS, path);
    bool has_pid = get_param (parameters, TOK_PROCESS, pid);
    enum utrace_derived_probe_flags flags = UDPF_NONE;

    if (has_null_param (parameters, TOK_THREAD))
      {
	if (has_null_param (parameters, TOK_BEGIN))
	  flags = UDPF_THREAD_BEGIN;
	else if (has_null_param (parameters, TOK_END))
	  flags = UDPF_THREAD_END;
      }
    else if (has_null_param (parameters, TOK_SYSCALL))
      {
	if (has_null_param (parameters, TOK_RETURN))
	  flags = UDPF_SYSCALL_RETURN;
	else
	  flags = UDPF_SYSCALL;
      }
    else if (has_null_param (parameters, TOK_BEGIN))
      flags = UDPF_BEGIN;
    else if (has_null_param (parameters, TOK_END))
      flags = UDPF_END;

    // If we didn't get a path or pid, this means to probe everything.
    // Convert this to a pid-based probe.
    if (! has_path && ! has_pid)
      {
	has_path = false;
	path.clear();
	has_pid = true;
	pid = 0;
      }
    else if (has_path)
      {
        path = find_executable (path);
        sess.unwindsym_modules.insert (path);
      }
    else if (has_pid)
      {
	// We can't probe 'init' (pid 1).  XXX: where does this limitation come from?
	if (pid < 2)
	  throw semantic_error ("process pid must be greater than 1",
				location->tok);

        // XXX: could we use /proc/$pid/exe in unwindsym_modules and elsewhere?
      }

    finished_results.push_back(new utrace_derived_probe(sess, base, location,
                                                        has_path, path, pid,
							flags));
  }
};


void
utrace_derived_probe_group::enroll (utrace_derived_probe* p)
{
  if (p->has_path)
    probes_by_path[p->path].push_back(p);
  else
    probes_by_pid[p->pid].push_back(p);
  num_probes++;
  flags_seen[p->flags] = true;

  // XXX: multiple exec probes (for instance) for the same path (or
  // pid) should all share a utrace report function, and have their
  // handlers executed sequentially.
}


void
utrace_derived_probe_group::emit_probe_decl (systemtap_session& s,
					     utrace_derived_probe *p)
{
  s.op->newline() << "{";
  s.op->line() << " .tgt={";

  if (p->has_path)
    {
      s.op->line() << " .procname=\"" << p->path << "\",";
      s.op->line() << " .pid=0,";
    }
  else
    {
      s.op->line() << " .procname=NULL,";
      s.op->line() << " .pid=" << p->pid << ",";
    }

  s.op->line() << " .callback=&_stp_utrace_probe_cb,";
  s.op->line() << " .mmap_callback=NULL,";
  s.op->line() << " .munmap_callback=NULL,";
  s.op->line() << " .mprotect_callback=NULL,";
  s.op->line() << " },";
  s.op->line() << " .pp=" << lex_cast_qstring (*p->sole_location()) << ",";
  s.op->line() << " .ph=&" << p->name << ",";

  // Handle flags
  switch (p->flags)
    {
    // Notice that we'll just call the probe directly when we get
    // notified, since the task_finder layer stops the thread for us.
    case UDPF_BEGIN:				// process begin
      s.op->line() << " .flags=(UDPF_BEGIN),";
      break;
    case UDPF_THREAD_BEGIN:			// thread begin
      s.op->line() << " .flags=(UDPF_THREAD_BEGIN),";
      break;

    // Notice we're not setting up a .ops/.report_death handler for
    // either UDPF_END or UDPF_THREAD_END.  Instead, we'll just call
    // the probe directly when we get notified.
    case UDPF_END:				// process end
      s.op->line() << " .flags=(UDPF_END),";
      break;
    case UDPF_THREAD_END:			// thread end
      s.op->line() << " .flags=(UDPF_THREAD_END),";
      break;

    // For UDPF_SYSCALL/UDPF_SYSCALL_RETURN probes, the .report_death
    // handler isn't strictly necessary.  However, it helps to keep
    // our attaches/detaches symmetrical.  Since the task_finder layer
    // stops the thread, that works around bug 6841.
    case UDPF_SYSCALL:
      s.op->line() << " .flags=(UDPF_SYSCALL),";
      s.op->line() << " .ops={ .report_syscall_entry=stap_utrace_probe_syscall,  .report_death=stap_utrace_task_finder_report_death },";
      s.op->line() << " .events=(UTRACE_EVENT(SYSCALL_ENTRY)|UTRACE_EVENT(DEATH)),";
      break;
    case UDPF_SYSCALL_RETURN:
      s.op->line() << " .flags=(UDPF_SYSCALL_RETURN),";
      s.op->line() << " .ops={ .report_syscall_exit=stap_utrace_probe_syscall, .report_death=stap_utrace_task_finder_report_death },";
      s.op->line() << " .events=(UTRACE_EVENT(SYSCALL_EXIT)|UTRACE_EVENT(DEATH)),";
      break;

    case UDPF_NONE:
      s.op->line() << " .flags=(UDPF_NONE),";
      s.op->line() << " .ops={ },";
      s.op->line() << " .events=0,";
      break;
    default:
      throw semantic_error ("bad utrace probe flag");
      break;
    }
  s.op->line() << " .engine_attached=0,";

  map<derived_probe*, Dwarf_Addr>::iterator its = s.sdt_semaphore_addr.find(p);
  if (its == s.sdt_semaphore_addr.end())
    s.op->line() << " .sdt_sem_address=(unsigned long)0x0,";
  else
    s.op->line() << " .sdt_sem_address=(unsigned long)0x"
                 << hex << its->second << dec << "ULL,";

  s.op->line() << " .tsk=0,";
  s.op->line() << " },";
}


void
utrace_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline();
  s.op->newline() << "/* ---- utrace probes ---- */";

  s.op->newline() << "enum utrace_derived_probe_flags {";
  s.op->indent(1);
  s.op->newline() << "UDPF_NONE,";
  s.op->newline() << "UDPF_BEGIN,";
  s.op->newline() << "UDPF_END,";
  s.op->newline() << "UDPF_THREAD_BEGIN,";
  s.op->newline() << "UDPF_THREAD_END,";
  s.op->newline() << "UDPF_SYSCALL,";
  s.op->newline() << "UDPF_SYSCALL_RETURN,";
  s.op->newline() << "UDPF_NFLAGS";
  s.op->newline(-1) << "};";

  s.op->newline() << "struct stap_utrace_probe {";
  s.op->indent(1);
  s.op->newline() << "struct stap_task_finder_target tgt;";
  s.op->newline() << "const char *pp;";
  s.op->newline() << "void (*ph) (struct context*);";
  s.op->newline() << "enum utrace_derived_probe_flags flags;";
  s.op->newline() << "struct utrace_engine_ops ops;";
  s.op->newline() << "unsigned long events;";
  s.op->newline() << "int engine_attached;";
  s.op->newline() << "struct task_struct *tsk;";
  s.op->newline() << "unsigned long sdt_sem_address;";
  s.op->newline(-1) << "};";


  // Output handler function for UDPF_BEGIN, UDPF_THREAD_BEGIN,
  // UDPF_END, and UDPF_THREAD_END
  if (flags_seen[UDPF_BEGIN] || flags_seen[UDPF_THREAD_BEGIN]
      || flags_seen[UDPF_END] || flags_seen[UDPF_THREAD_END])
    {
      s.op->newline() << "static void stap_utrace_probe_handler(struct task_struct *tsk, struct stap_utrace_probe *p) {";
      s.op->indent(1);

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "p->pp");

      // call probe function
      s.op->newline() << "(*p->ph) (c);";
      common_probe_entryfn_epilogue (s.op);

      s.op->newline() << "return;";
      s.op->newline(-1) << "}";
    }

  // Output handler function for SYSCALL_ENTRY and SYSCALL_EXIT events
  if (flags_seen[UDPF_SYSCALL] || flags_seen[UDPF_SYSCALL_RETURN])
    {
      s.op->newline() << "#ifdef UTRACE_ORIG_VERSION";
      s.op->newline() << "static u32 stap_utrace_probe_syscall(struct utrace_attached_engine *engine, struct task_struct *tsk, struct pt_regs *regs) {";
      s.op->newline() << "#else";
      s.op->newline() << "static u32 stap_utrace_probe_syscall(enum utrace_resume_action action, struct utrace_attached_engine *engine, struct task_struct *tsk, struct pt_regs *regs) {";
      s.op->newline() << "#endif";

      s.op->indent(1);
      s.op->newline() << "struct stap_utrace_probe *p = (struct stap_utrace_probe *)engine->data;";

      common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "p->pp");
      s.op->newline() << "c->regs = regs;";

      // call probe function
      s.op->newline() << "(*p->ph) (c);";
      common_probe_entryfn_epilogue (s.op);

      s.op->newline() << "if ((atomic_read (&session_state) != STAP_SESSION_STARTING) && (atomic_read (&session_state) != STAP_SESSION_RUNNING)) {";
      s.op->indent(1);
      s.op->newline() << "debug_task_finder_detach();";
      s.op->newline() << "return UTRACE_DETACH;";
      s.op->newline(-1) << "}";
      s.op->newline() << "return UTRACE_RESUME;";
      s.op->newline(-1) << "}";
    }

  // Output task_finder callback routine that gets called for all
  // utrace probe types.
  s.op->newline() << "static int _stp_utrace_probe_cb(struct stap_task_finder_target *tgt, struct task_struct *tsk, int register_p, int process_p) {";
  s.op->indent(1);
  s.op->newline() << "int rc = 0;";
  s.op->newline() << "struct stap_utrace_probe *p = container_of(tgt, struct stap_utrace_probe, tgt);";
  s.op->newline() << "struct utrace_attached_engine *engine;";

  s.op->newline() << "if (register_p) {";
  s.op->indent(1);

  s.op->newline() << "switch (p->flags) {";
  s.op->indent(1);

  // When receiving a UTRACE_EVENT(CLONE) event, we can't call the
  // begin/thread.begin probe directly.  So, we'll just attach an
  // engine that waits for the thread to quiesce.  When the thread
  // quiesces, then call the probe.
  if (flags_seen[UDPF_BEGIN])
  {
      s.op->newline() << "case UDPF_BEGIN:";
      s.op->indent(1);
      s.op->newline() << "if (process_p) {";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_probe_handler(tsk, p);";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
  }
  if (flags_seen[UDPF_THREAD_BEGIN])
  {
      s.op->newline() << "case UDPF_THREAD_BEGIN:";
      s.op->indent(1);
      s.op->newline() << "if (! process_p) {";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_probe_handler(tsk, p);";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
  }

  // For end/thread_end probes, do nothing at registration time.
  // We'll handle these in the 'register_p == 0' case.
  if (flags_seen[UDPF_END] || flags_seen[UDPF_THREAD_END])
    {
      s.op->newline() << "case UDPF_END:";
      s.op->newline() << "case UDPF_THREAD_END:";
      s.op->indent(1);
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  // Attach an engine for SYSCALL_ENTRY and SYSCALL_EXIT events.
  if (flags_seen[UDPF_SYSCALL] || flags_seen[UDPF_SYSCALL_RETURN])
    {
      s.op->newline() << "case UDPF_SYSCALL:";
      s.op->newline() << "case UDPF_SYSCALL_RETURN:";
      s.op->indent(1);
      s.op->newline() << "rc = stap_utrace_attach(tsk, &p->ops, p, p->events);";
      s.op->newline() << "if (rc == 0) {";
      s.op->indent(1);
      s.op->newline() << "p->engine_attached = 1;";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  s.op->newline() << "default:";
  s.op->indent(1);
  s.op->newline() << "_stp_error(\"unhandled flag value %d at %s:%d\", p->flags, __FUNCTION__, __LINE__);";
  s.op->newline() << "break;";
  s.op->indent(-1);
  s.op->newline(-1) << "}";

  s.op->newline() << "if (p->sdt_sem_address != 0) {";
  s.op->newline(1) << "size_t sdt_semaphore;";
  s.op->newline() << "p->tsk = tsk;";
  s.op->newline() << "__access_process_vm (tsk, p->sdt_sem_address, &sdt_semaphore, sizeof (sdt_semaphore), 0);";
  s.op->newline() << "sdt_semaphore += 1;";
  s.op->newline() << "__access_process_vm (tsk, p->sdt_sem_address, &sdt_semaphore, sizeof (sdt_semaphore), 1);";
  s.op->newline(-1) << "}";

  s.op->newline(-1) << "}";

  // Since this engine could be attached to multiple threads, don't
  // call stap_utrace_detach_ops() here, only call
  // stap_utrace_detach() as necessary.
  s.op->newline() << "else {";
  s.op->indent(1);
  s.op->newline() << "switch (p->flags) {";
  s.op->indent(1);
  // For end probes, go ahead and call the probe directly.
  if (flags_seen[UDPF_END])
    {
      s.op->newline() << "case UDPF_END:";
      s.op->indent(1);
      s.op->newline() << "if (process_p) {";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_probe_handler(tsk, p);";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }
  if (flags_seen[UDPF_THREAD_END])
    {
      s.op->newline() << "case UDPF_THREAD_END:";
      s.op->indent(1);
      s.op->newline() << "if (! process_p) {";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_probe_handler(tsk, p);";
      s.op->newline(-1) << "}";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  // For begin/thread_begin probes, we don't need to do anything.
  if (flags_seen[UDPF_BEGIN] || flags_seen[UDPF_THREAD_BEGIN])
  {
      s.op->newline() << "case UDPF_BEGIN:";
      s.op->newline() << "case UDPF_THREAD_BEGIN:";
      s.op->indent(1);
      s.op->newline() << "break;";
      s.op->indent(-1);
  }

  if (flags_seen[UDPF_SYSCALL] || flags_seen[UDPF_SYSCALL_RETURN])
    {
      s.op->newline() << "case UDPF_SYSCALL:";
      s.op->newline() << "case UDPF_SYSCALL_RETURN:";
      s.op->indent(1);
      s.op->newline() << "stap_utrace_detach(tsk, &p->ops);";
      s.op->newline() << "break;";
      s.op->indent(-1);
    }

  s.op->newline() << "default:";
  s.op->indent(1);
  s.op->newline() << "_stp_error(\"unhandled flag value %d at %s:%d\", p->flags, __FUNCTION__, __LINE__);";
  s.op->newline() << "break;";
  s.op->indent(-1);
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";
  s.op->newline() << "return rc;";
  s.op->newline(-1) << "}";

  s.op->newline() << "static struct stap_utrace_probe stap_utrace_probes[] = {";
  s.op->indent(1);

  // Set up 'process(PATH)' probes
  if (! probes_by_path.empty())
    {
      for (p_b_path_iterator it = probes_by_path.begin();
	   it != probes_by_path.end(); it++)
        {
	  for (unsigned i = 0; i < it->second.size(); i++)
	    {
	      utrace_derived_probe *p = it->second[i];
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
	      utrace_derived_probe *p = it->second[i];
	      emit_probe_decl(s, p);
	    }
	}
    }
  s.op->newline(-1) << "};";
}


void
utrace_derived_probe_group::emit_module_init (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty())
    return;

  s.op->newline() << "/* ---- utrace probes ---- */";
  s.op->newline() << "for (i=0; i<ARRAY_SIZE(stap_utrace_probes); i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_utrace_probe *p = &stap_utrace_probes[i];";
  s.op->newline() << "probe_point = p->pp;"; // for error messages
  s.op->newline() << "rc = stap_register_task_finder_target(&p->tgt);";
  s.op->newline() << "if (rc) break;";
  s.op->newline(-1) << "}";

  // rollback all utrace probes
  s.op->newline() << "if (rc) {";
  s.op->indent(1);
  s.op->newline() << "for (j=i-1; j>=0; j--) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_utrace_probe *p = &stap_utrace_probes[j];";

  s.op->newline() << "if (p->engine_attached) {";
  s.op->indent(1);
  s.op->newline() << "stap_utrace_detach_ops(&p->ops);";
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";

  s.op->newline(-1) << "}";
}


void
utrace_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes_by_path.empty() && probes_by_pid.empty()) return;

  s.op->newline();
  s.op->newline() << "/* ---- utrace probes ---- */";
  s.op->newline() << "for (i=0; i<ARRAY_SIZE(stap_utrace_probes); i++) {";
  s.op->indent(1);
  s.op->newline() << "struct stap_utrace_probe *p = &stap_utrace_probes[i];";

  s.op->newline() << "if (p->engine_attached) {";
  s.op->indent(1);
  s.op->newline() << "stap_utrace_detach_ops(&p->ops);";
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}";

  int sem_idx = 0;
  if (! s.sdt_semaphore_addr.empty())
    for (p_b_path_iterator it = probes_by_path.begin();
	 it != probes_by_path.end(); it++)
      {
	s.op->newline() << "{";
	s.op->indent(1);
	s.op->newline() << "size_t sdt_semaphore;";
	s.op->newline() << "for (i=0; i<ARRAY_SIZE(stap_utrace_probes); i++) {";
	s.op->newline(1) << "struct stap_utrace_probe *p = &stap_utrace_probes[i];";

	s.op->newline() << "__access_process_vm (p->tsk, p->sdt_sem_address, &sdt_semaphore, sizeof (sdt_semaphore), 0);";
	s.op->newline() << "sdt_semaphore -= 1;";
	s.op->newline() << "__access_process_vm (p->tsk, p->sdt_sem_address, &sdt_semaphore, sizeof (sdt_semaphore), 1);";
	
	s.op->newline(-1) << "}";
	s.op->newline(-1) << "}";
	sem_idx += it->second.size() - 1;
      }
}


void
register_tapset_utrace(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new utrace_builder();

  vector<match_node*> roots;
  roots.push_back(root->bind(TOK_PROCESS));
  roots.push_back(root->bind_str(TOK_PROCESS));
  roots.push_back(root->bind_num(TOK_PROCESS));

  for (unsigned i = 0; i < roots.size(); ++i)
    {
      roots[i]->bind(TOK_BEGIN)
	->allow_unprivileged()
	->bind(builder);
      roots[i]->bind(TOK_END)
	->allow_unprivileged()
	->bind(builder);
      roots[i]->bind(TOK_THREAD)->bind(TOK_BEGIN)
	->allow_unprivileged()
	->bind(builder);
      roots[i]->bind(TOK_THREAD)->bind(TOK_END)
	->allow_unprivileged()
	->bind(builder);
      roots[i]->bind(TOK_SYSCALL)
	->allow_unprivileged()
	->bind(builder);
      roots[i]->bind(TOK_SYSCALL)->bind(TOK_RETURN)
	->allow_unprivileged()
	->bind(builder);
    }
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
