// tapset for kernel static markers
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

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include <fnmatch.h>
}


using namespace std;
using namespace __gnu_cxx;


static const string TOK_KERNEL("kernel");
static const string TOK_MARK("mark");
static const string TOK_FORMAT("format");


// ------------------------------------------------------------------------
// statically inserted macro-based derived probes
// ------------------------------------------------------------------------

struct mark_arg
{
  bool str;
  bool isptr;
  string c_type;
  exp_type stp_type;
};

struct mark_derived_probe: public derived_probe
{
  mark_derived_probe (systemtap_session &s,
                      const string& probe_name, const string& probe_format,
                      probe* base_probe, probe_point* location);

  systemtap_session& sess;
  string probe_name, probe_format;
  vector <struct mark_arg *> mark_args;
  bool target_symbol_seen;

  void join_group (systemtap_session& s);
  void print_dupe_stamp (ostream& o);
  void emit_probe_context_vars (translator_output* o);
  void initialize_probe_context_vars (translator_output* o);
  void printargs (std::ostream &o) const;

  void parse_probe_format ();
};


struct mark_derived_probe_group: public generic_dpg<mark_derived_probe>
{
public:
  void emit_module_decls (systemtap_session& s);
  void emit_module_init (systemtap_session& s);
  void emit_module_exit (systemtap_session& s);
};


struct mark_var_expanding_visitor: public var_expanding_visitor
{
  mark_var_expanding_visitor(systemtap_session& s, const string& pn,
                             vector <struct mark_arg *> &mark_args):
    sess (s), probe_name (pn), mark_args (mark_args),
    target_symbol_seen (false) {}
  systemtap_session& sess;
  string probe_name;
  vector <struct mark_arg *> &mark_args;
  bool target_symbol_seen;

  void visit_target_symbol (target_symbol* e);
  void visit_target_symbol_arg (target_symbol* e);
  void visit_target_symbol_context (target_symbol* e);
};


void
mark_var_expanding_visitor::visit_target_symbol_arg (target_symbol* e)
{
  string argnum_s = e->base_name.substr(4,e->base_name.length()-4);
  int argnum = atoi (argnum_s.c_str());

  if (argnum < 1 || argnum > (int)mark_args.size())
    throw semantic_error ("invalid marker argument number", e->tok);

  if (is_active_lvalue (e))
    throw semantic_error("write to marker parameter not permitted", e->tok);

  e->assert_no_components("marker");

  // Remember that we've seen a target variable.
  target_symbol_seen = true;

  e->probe_context_var = "__mark_arg" + lex_cast<string>(argnum);
  e->type = mark_args[argnum-1]->stp_type;
  provide (e);
}


void
mark_var_expanding_visitor::visit_target_symbol_context (target_symbol* e)
{
  string sname = e->base_name;

  if (is_active_lvalue (e))
    throw semantic_error("write to marker '" + sname + "' not permitted", e->tok);

  e->assert_no_components("marker");

  if (e->base_name == "$format" || e->base_name == "$name") {
     string fname;
     if (e->base_name == "$format") {
        fname = string("_mark_format_get");
     } else {
        fname = string("_mark_name_get");
     }

     // Synthesize a functioncall.
     functioncall* n = new functioncall;
     n->tok = e->tok;
     n->function = fname;
     n->referent = 0; // NB: must not resolve yet, to ensure inclusion in session
     provide (n);
  }
 else if (e->base_name == "$$vars" || e->base_name == "$$parms") 
  {
     //copy from tracepoint
     print_format* pf = new print_format;
     token* pf_tok = new token(*e->tok);
     pf_tok->content = "sprintf";
     pf->tok = pf_tok;
     pf->print_to_stream = false;
     pf->print_with_format = true;
     pf->print_with_delim = false;
     pf->print_with_newline = false;
     pf->print_char = false;

     for (unsigned i = 0; i < mark_args.size(); ++i)
        {
          if (i > 0)
            pf->raw_components += " ";
          pf->raw_components += "$arg" + lex_cast<string>(i+1);
          target_symbol *tsym = new target_symbol;
          tsym->tok = e->tok;
          tsym->base_name = "$arg" + lex_cast<string>(i+1);

          tsym->saved_conversion_error = 0;
          expression *texp = require (tsym); //same treatment as tracepoint
          assert (!tsym->saved_conversion_error);
          switch (mark_args[i]->stp_type)
           {
             case pe_long:
               pf->raw_components += mark_args[i]->isptr ? "=%p" : "=%#x";
               break;
             case pe_string:
               pf->raw_components += "=%s";
               break;
             default:
               pf->raw_components += "=%#x";
               break;
            }
          pf->args.push_back(texp);
        }
     pf->components = print_format::string_to_components(pf->raw_components);
     provide (pf);
  }
}

void
mark_var_expanding_visitor::visit_target_symbol (target_symbol* e)
{
  assert(e->base_name.size() > 0 && e->base_name[0] == '$');

  if (e->addressof)
    throw semantic_error("cannot take address of marker variable", e->tok);

  if (e->base_name.substr(0,4) == "$arg")
    visit_target_symbol_arg (e);
  else if (e->base_name == "$format" || e->base_name == "$name" 
           || e->base_name == "$$parms" || e->base_name == "$$vars")
    visit_target_symbol_context (e);
  else
    throw semantic_error ("invalid target symbol for marker, $argN, $name, $format, $$parms or $$vars expected",
			  e->tok);
}



mark_derived_probe::mark_derived_probe (systemtap_session &s,
                                        const string& p_n,
                                        const string& p_f,
                                        probe* base, probe_point* loc):
  derived_probe (base, new probe_point(*loc) /* .components soon rewritten */),
  sess (s), probe_name (p_n), probe_format (p_f),
  target_symbol_seen (false)
{
  // create synthetic probe point name; preserve condition
  vector<probe_point::component*> comps;
  comps.push_back (new probe_point::component (TOK_KERNEL));
  comps.push_back (new probe_point::component (TOK_MARK, new literal_string (probe_name)));
  comps.push_back (new probe_point::component (TOK_FORMAT, new literal_string (probe_format)));
  this->sole_location()->components = comps;

  // expand the marker format
  parse_probe_format();

  // Now expand the local variables in the probe body
  mark_var_expanding_visitor v (sess, name, mark_args);
  v.replace (this->body);
  target_symbol_seen = v.target_symbol_seen;

  if (sess.verbose > 2)
    clog << "marker-based " << name << " mark=" << probe_name
	 << " fmt='" << probe_format << "'" << endl;
}


static int
skip_atoi(const char **s)
{
  int i = 0;
  while (isdigit(**s))
    i = i * 10 + *((*s)++) - '0';
  return i;
}


void
mark_derived_probe::parse_probe_format()
{
  const char *fmt = probe_format.c_str();
  int qualifier;		// 'h', 'l', or 'L' for integer fields
  mark_arg *arg;

  for (; *fmt ; ++fmt)
    {
      if (*fmt != '%')
        {
	  /* Skip text */
	  continue;
	}

repeat:
      ++fmt;

      // skip conversion flags (if present)
      switch (*fmt)
        {
	case '-':
	case '+':
	case ' ':
	case '#':
	case '0':
	  goto repeat;
	}

      // skip minimum field witdh (if present)
      if (isdigit(*fmt))
	skip_atoi(&fmt);

      // skip precision (if present)
      if (*fmt == '.')
        {
	  ++fmt;
	  if (isdigit(*fmt))
	    skip_atoi(&fmt);
	}

      // get the conversion qualifier (if present)
      qualifier = -1;
      if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L')
        {
	  qualifier = *fmt;
	  ++fmt;
	  if (qualifier == 'l' && *fmt == 'l')
	    {
	      qualifier = 'L';
	      ++fmt;
	    }
	}

      // get the conversion type
      switch (*fmt)
        {
	case 'c':
	  arg = new mark_arg;
	  arg->str = false;
	  arg->isptr = false;
	  arg->c_type = "int";
	  arg->stp_type = pe_long;
	  mark_args.push_back(arg);
	  continue;

	case 's':
	  arg = new mark_arg;
	  arg->str = true;
	  arg->isptr = false;
	  arg->c_type = "char *";
	  arg->stp_type = pe_string;
	  mark_args.push_back(arg);
	  continue;

	case 'p':
	  arg = new mark_arg;
	  arg->str = false;
	  arg->isptr = true;
	  // This should really be 'void *'.  But, then we'll get a
	  // compile error when we assign the void pointer to an
	  // integer without a cast.  So, we use 'long' instead, since
	  // it should have the same size as 'void *'.
	  arg->c_type = "long";
	  arg->stp_type = pe_long;
	  mark_args.push_back(arg);
	  continue;

	case '%':
	  continue;

	case 'o':
	case 'X':
	case 'x':
	case 'd':
	case 'i':
	case 'u':
	  // fall through...
	  break;

	default:
	  if (!*fmt)
	    --fmt;
	  continue;
	}

      arg = new mark_arg;
      arg->str = false;
      arg->isptr = false;
      arg->stp_type = pe_long;
      switch (qualifier)
        {
	case 'L':
	  arg->c_type = "long long";
	  break;

	case 'l':
	  arg->c_type = "long";
	  break;

	case 'h':
	  arg->c_type = "short";
	  break;

	default:
	  arg->c_type = "int";
	  break;
	}
      mark_args.push_back(arg);
    }
}


void
mark_derived_probe::join_group (systemtap_session& s)
{
  if (! s.mark_derived_probes)
    {
      s.mark_derived_probes = new mark_derived_probe_group ();

      // Make sure <linux/marker.h> is included early.
      embeddedcode *ec = new embeddedcode;
      ec->tok = NULL;
      ec->code = string("#if ! defined(CONFIG_MARKERS)\n")
	+ string("#error \"Need CONFIG_MARKERS!\"\n")
	+ string("#endif\n")
	+ string("#include <linux/marker.h>\n");

      s.embeds.push_back(ec);
    }
  s.mark_derived_probes->enroll (this);
}


void
mark_derived_probe::print_dupe_stamp (ostream& o)
{
  if (target_symbol_seen)
    for (unsigned i = 0; i < mark_args.size(); i++)
      o << mark_args[i]->c_type << " __mark_arg" << (i+1) << endl;
}


void
mark_derived_probe::emit_probe_context_vars (translator_output* o)
{
  // If we haven't seen a target symbol for this probe, quit.
  if (! target_symbol_seen)
    return;

  for (unsigned i = 0; i < mark_args.size(); i++)
    {
      string localname = "__mark_arg" + lex_cast<string>(i+1);
      switch (mark_args[i]->stp_type)
        {
	case pe_long:
	  o->newline() << "int64_t " << localname << ";";
	  break;
	case pe_string:
	  o->newline() << "string_t " << localname << ";";
	  break;
	default:
	  throw semantic_error ("cannot expand unknown type");
	  break;
	}
    }
}


void
mark_derived_probe::initialize_probe_context_vars (translator_output* o)
{
  // If we haven't seen a target symbol for this probe, quit.
  if (! target_symbol_seen)
    return;

  bool deref_fault_needed = false;
  for (unsigned i = 0; i < mark_args.size(); i++)
    {
      string localname = "l->__mark_arg" + lex_cast<string>(i+1);
      switch (mark_args[i]->stp_type)
        {
	case pe_long:
	  o->newline() << localname << " = va_arg(*c->mark_va_list, "
		       << mark_args[i]->c_type << ");";
	  break;

	case pe_string:
	  // We're assuming that this is a kernel string (this code is
	  // basically the guts of kernel_string), not a user string.
	  o->newline() << "{ " << mark_args[i]->c_type
		       << " tmp_str = va_arg(*c->mark_va_list, "
		       << mark_args[i]->c_type << ");";
	  o->newline() << "deref_string (" << localname
		       << ", tmp_str, MAXSTRINGLEN); }";
	  deref_fault_needed = true;
	  break;

	default:
	  throw semantic_error ("cannot expand unknown type");
	  break;
	}
    }
  if (deref_fault_needed)
    // Need to report errors?
    o->newline() << "deref_fault: ;";
}

void
mark_derived_probe::printargs(std::ostream &o) const
{
  for (unsigned i = 0; i < mark_args.size(); i++)
    {
      string localname = "$arg" + lex_cast<string>(i+1);
      switch (mark_args[i]->stp_type)
        {
        case pe_long:
          o << " " << localname << ":long";
          break;
        case pe_string:
          o << " " << localname << ":string";
          break;
        default:
          o << " " << localname << ":unknown";
          break;
        }
    }
}


void
mark_derived_probe_group::emit_module_decls (systemtap_session& s)
{
  if (probes.empty())
    return;

  s.op->newline() << "/* ---- marker probes ---- */";

  s.op->newline() << "static struct stap_marker_probe {";
  s.op->newline(1) << "const char * const name;";
  s.op->newline() << "const char * const format;";
  s.op->newline() << "const char * const pp;";
  s.op->newline() << "void (* const ph) (struct context *);";

  s.op->newline(-1) << "} stap_marker_probes [" << probes.size() << "] = {";
  s.op->indent(1);
  for (unsigned i=0; i < probes.size(); i++)
    {
      s.op->newline () << "{";
      s.op->line() << " .name=" << lex_cast_qstring(probes[i]->probe_name)
		   << ",";
      s.op->line() << " .format=" << lex_cast_qstring(probes[i]->probe_format)
		   << ",";
      s.op->line() << " .pp=" << lex_cast_qstring (*probes[i]->sole_location())
		   << ",";
      s.op->line() << " .ph=&" << probes[i]->name;
      s.op->line() << " },";
    }
  s.op->newline(-1) << "};";
  s.op->newline();


  // Emit the marker callback function
  s.op->newline();
  s.op->newline() << "static void enter_marker_probe (void *probe_data, void *call_data, const char *fmt, va_list *args) {";
  s.op->newline(1) << "struct stap_marker_probe *smp = (struct stap_marker_probe *)probe_data;";
  common_probe_entryfn_prologue (s.op, "STAP_SESSION_RUNNING", "smp->pp");
  s.op->newline() << "c->marker_name = smp->name;";
  s.op->newline() << "c->marker_format = smp->format;";
  s.op->newline() << "c->mark_va_list = args;";
  s.op->newline() << "(*smp->ph) (c);";
  s.op->newline() << "c->mark_va_list = NULL;";
  s.op->newline() << "c->data = NULL;";

  common_probe_entryfn_epilogue (s.op);
  s.op->newline(-1) << "}";

  return;
}


void
mark_derived_probe_group::emit_module_init (systemtap_session &s)
{
  if (probes.size () == 0)
    return;

  s.op->newline() << "/* init marker probes */";
  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_marker_probe *smp = &stap_marker_probes[i];";
  s.op->newline() << "probe_point = smp->pp;";
  s.op->newline() << "rc = marker_probe_register(smp->name, smp->format, enter_marker_probe, smp);";
  s.op->newline() << "if (rc) {";
  s.op->newline(1) << "for (j=i-1; j>=0; j--) {"; // partial rollback
  s.op->newline(1) << "struct stap_marker_probe *smp2 = &stap_marker_probes[j];";
  s.op->newline() << "marker_probe_unregister(smp2->name, enter_marker_probe, smp2);";
  s.op->newline(-1) << "}";
  s.op->newline() << "break;"; // don't attempt to register any more probes
  s.op->newline(-1) << "}";
  s.op->newline(-1) << "}"; // for loop
}


void
mark_derived_probe_group::emit_module_exit (systemtap_session& s)
{
  if (probes.empty())
    return;

  s.op->newline() << "/* deregister marker probes */";
  s.op->newline() << "for (i=0; i<" << probes.size() << "; i++) {";
  s.op->newline(1) << "struct stap_marker_probe *smp = &stap_marker_probes[i];";
  s.op->newline() << "marker_probe_unregister(smp->name, enter_marker_probe, smp);";
  s.op->newline(-1) << "}"; // for loop
}


struct mark_builder: public derived_probe_builder
{
private:
  bool cache_initialized;
  typedef multimap<string, string> mark_cache_t;
  typedef multimap<string, string>::const_iterator mark_cache_const_iterator_t;
  typedef pair<mark_cache_const_iterator_t, mark_cache_const_iterator_t>
    mark_cache_const_iterator_pair_t;
  mark_cache_t mark_cache;

public:
  mark_builder(): cache_initialized(false) {}

  void build_no_more (systemtap_session &s)
  {
    if (! mark_cache.empty())
      {
        if (s.verbose > 3)
          clog << "mark_builder releasing cache" << endl;
	mark_cache.clear();
      }
  }

  void build(systemtap_session & sess,
             probe * base,
             probe_point * location,
             literal_map_t const & parameters,
             vector<derived_probe *> & finished_results);
};


void
mark_builder::build(systemtap_session & sess,
		    probe * base,
		    probe_point *loc,
		    literal_map_t const & parameters,
		    vector<derived_probe *> & finished_results)
{
  string mark_str_val;
  bool has_mark_str = get_param (parameters, TOK_MARK, mark_str_val);
  string mark_format_val;
  bool has_mark_format = get_param (parameters, TOK_FORMAT, mark_format_val);
  assert (has_mark_str);
  (void) has_mark_str;

  if (! cache_initialized)
    {
      cache_initialized = true;
      string module_markers_path = sess.kernel_build_tree + "/Module.markers";
      
      ifstream module_markers;
      module_markers.open(module_markers_path.c_str(), ifstream::in);
      if (! module_markers)
        {
	  if (sess.verbose>3)
	    clog << module_markers_path << " cannot be opened: "
		 << strerror(errno) << endl;
	  return;
	}

      string name, module, format;
      do
        {
	  module_markers >> name >> module;
	  getline(module_markers, format);

	  // trim leading whitespace
	  string::size_type notwhite = format.find_first_not_of(" \t");
	  format.erase(0, notwhite);

	  // If the format is empty, make sure we add back a space
	  // character, which is what MARK_NOARGS expands to.
	  if (format.length() == 0)
	    format = " ";

	  if (sess.verbose>3)
	    clog << "'" << name << "' '" << module << "' '" << format
		 << "'" << endl;

	  if (mark_cache.count(name) > 0)
	    {
	      // If we have 2 markers with the same we've got 2 cases:
	      // different format strings or duplicate format strings.
	      // If an existing marker in the cache doesn't have the
	      // same format string, add this marker.
	      mark_cache_const_iterator_pair_t ret;
	      mark_cache_const_iterator_t it;
	      bool matching_format_string = false;

	      ret = mark_cache.equal_range(name);
	      for (it = ret.first; it != ret.second; ++it)
	        {
		  if (format == it->second)
		    {
		      matching_format_string = true;
		      break;
		    }
		}

	      if (! matching_format_string)
	        mark_cache.insert(pair<string,string>(name, format));
	  }
	  else
	    mark_cache.insert(pair<string,string>(name, format));
	}
      while (! module_markers.eof());
      module_markers.close();
    }

  // Search marker list for matching markers
  for (mark_cache_const_iterator_t it = mark_cache.begin();
       it != mark_cache.end(); it++)
    {
      // Below, "rc" has negative polarity: zero iff matching.
      int rc = fnmatch(mark_str_val.c_str(), it->first.c_str(), 0);
      if (! rc)
        {
	  bool add_result = true;

	  // Match format strings (if the user specified one)
	  if (has_mark_format && fnmatch(mark_format_val.c_str(),
					 it->second.c_str(), 0))
	    add_result = false;

	  if (add_result)
	    {
	      derived_probe *dp
		= new mark_derived_probe (sess,
					  it->first, it->second,
					  base, loc);
	      finished_results.push_back (dp);
	    }
	}
    }
}



void
register_tapset_mark(systemtap_session& s)
{
  match_node* root = s.pattern_root;
  derived_probe_builder *builder = new mark_builder();

  root = root->bind(TOK_KERNEL);
  root = root->bind_str(TOK_MARK);

  root->bind(builder);
  root->bind_str(TOK_FORMAT)->bind(builder);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
