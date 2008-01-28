// elaboration functions
// Copyright (C) 2005-2008 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "elaborate.h"
#include "parse.h"
#include "tapsets.h"
#include "session.h"
#include "util.h"

extern "C" {
#include <sys/utsname.h>
#include <fnmatch.h>
}

#include <algorithm>
#include <fstream>
#include <map>
#include <cassert>
#include <set>
#include <vector>
#include <algorithm>
#include <iterator>


using namespace std;


// ------------------------------------------------------------------------

// Used in probe_point condition construction.  Either argument may be
// NULL; if both, return NULL too.  Resulting expression is a deep
// copy for symbol resolution purposes.
expression* add_condition (expression* a, expression* b)
{
  if (!a && !b) return 0;
  if (! a) return deep_copy_visitor::deep_copy(b);
  if (! b) return deep_copy_visitor::deep_copy(a);
  logical_and_expr la;
  la.op = "&&";
  la.left = a;
  la.right = b;
  la.tok = a->tok; // or could be b->tok
  return deep_copy_visitor::deep_copy(& la);
}

// ------------------------------------------------------------------------



derived_probe::derived_probe (probe *p):
  base (p)
{
  assert (p);
  this->locations = p->locations;
  this->tok = p->tok;
  this->privileged = p->privileged;
  this->body = deep_copy_visitor::deep_copy(p->body);
}


derived_probe::derived_probe (probe *p, probe_point *l):
  base (p)
{
  assert (p);
  this->tok = p->tok;
  this->privileged = p->privileged;
  this->body = deep_copy_visitor::deep_copy(p->body);

  assert (l);
  this->locations.push_back (l);
}


void
derived_probe::printsig (ostream& o) const
{
  probe::printsig (o);
  printsig_nested (o);
}

void
derived_probe::printsig_nested (ostream& o) const
{
  // We'd like to enclose the probe derivation chain in a /* */
  // comment delimiter.  But just printing /* base->printsig() */ is
  // not enough, since base might itself be a derived_probe.  So we,
  // er, "cleverly" encode our nesting state as a formatting flag for
  // the ostream.
  ios::fmtflags f = o.flags (ios::internal);
  if (f & ios::internal)
    {
      // already nested
      o << " <- ";
      base->printsig (o);
    }
  else
    {
      // outermost nesting
      o << " /* <- ";
      base->printsig (o);
      o << " */";
    }
  // restore flags
  (void) o.flags (f);
}


void
derived_probe::collect_derivation_chain (std::vector<derived_probe*> &probes_list)
{
  probes_list.push_back(this);
  base->collect_derivation_chain(probes_list);
}


probe_point*
derived_probe::sole_location () const
{
  if (locations.size() == 0)
    throw semantic_error ("derived_probe with no locations", this->tok);
  else if (locations.size() > 1)
    throw semantic_error ("derived_probe with too many locations", this->tok);
  else 
    return locations[0];
}



// ------------------------------------------------------------------------
// Members of derived_probe_builder

bool
derived_probe_builder::get_param (std::map<std::string, literal*> const & params,
                                  const std::string& key,
                                  std::string& value)
{
  map<string, literal *>::const_iterator i = params.find (key);
  if (i == params.end())
    return false;
  literal_string * ls = dynamic_cast<literal_string *>(i->second);
  if (!ls)
    return false;
  value = ls->value;
  return true;
}


bool
derived_probe_builder::get_param (std::map<std::string, literal*> const & params,
                                  const std::string& key,
                                  int64_t& value)
{
  map<string, literal *>::const_iterator i = params.find (key);
  if (i == params.end())
    return false;
  if (i->second == NULL)
    return false;
  literal_number * ln = dynamic_cast<literal_number *>(i->second);
  if (!ln)
    return false;
  value = ln->value;
  return true;
}


bool
derived_probe_builder::has_null_param (std::map<std::string, literal*> const & params,
                                       const std::string& key)
{
  map<string, literal *>::const_iterator i = params.find(key);
  return (i != params.end() && i->second == NULL);
}



// ------------------------------------------------------------------------
// Members of match_key.

match_key::match_key(string const & n) 
  : name(n), 
    have_parameter(false), 
    parameter_type(pe_unknown)
{
}

match_key::match_key(probe_point::component const & c)
  : name(c.functor),
    have_parameter(c.arg != NULL),
    parameter_type(c.arg ? c.arg->type : pe_unknown)
{
}

match_key &
match_key::with_number() 
{
  have_parameter = true;
  parameter_type = pe_long;
  return *this;
}

match_key &
match_key::with_string() 
{
  have_parameter = true;
  parameter_type = pe_string;
  return *this;
}

string 
match_key::str() const
{
  if (have_parameter)
    switch (parameter_type)
      {
      case pe_string: return name + "(string)";
      case pe_long: return name + "(number)";
      default: return name + "(...)";
      }
  return name;
}

bool 
match_key::operator<(match_key const & other) const
{
  return ((name < other.name)
	  
	  || (name == other.name 
	      && have_parameter < other.have_parameter)
	  
	  || (name == other.name 
	      && have_parameter == other.have_parameter 
	      && parameter_type < other.parameter_type));
}

static bool
isglob(string const & str)
{
  return(str.find('*') != str.npos);
}

bool
match_key::globmatch(match_key const & other) const
{
  const char *other_str = other.name.c_str();
  const char *name_str = name.c_str();

  return ((fnmatch(name_str, other_str, FNM_NOESCAPE) == 0)
	  && have_parameter == other.have_parameter 
	  && parameter_type == other.parameter_type);
}

// ------------------------------------------------------------------------
// Members of match_node
// ------------------------------------------------------------------------

match_node::match_node()
  : end(NULL)
{}

match_node *
match_node::bind(match_key const & k) 
{
  if (k.name == "*")
    throw semantic_error("invalid use of wildcard probe point component");

  map<match_key, match_node *>::const_iterator i = sub.find(k);
  if (i != sub.end())
    return i->second;
  match_node * n = new match_node();
  sub.insert(make_pair(k, n));
  return n;
}

void 
match_node::bind(derived_probe_builder * e)
{
  if (end)
    throw semantic_error("duplicate probe point pattern");
  end = e;
}

match_node * 
match_node::bind(string const & k)
{
  return bind(match_key(k));
}

match_node *
match_node::bind_str(string const & k)
{
  return bind(match_key(k).with_string());
}

match_node * 
match_node::bind_num(string const & k)
{
  return bind(match_key(k).with_number());
}


void
match_node::find_and_build (systemtap_session& s,
                            probe* p, probe_point *loc, unsigned pos,
                            vector<derived_probe *>& results)
{
  assert (pos <= loc->components.size());
  if (pos == loc->components.size()) // matched all probe point components so far 
    {
      derived_probe_builder *b = end; // may be 0 if only nested names are bound

      if (! b)
        {
          string alternatives;
          for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
            alternatives += string(" ") + i->first.str();

          throw semantic_error (string("probe point truncated at position ") + 
                                lex_cast<string> (pos) +
                                " (follow:" + alternatives + ")");
        }

      map<string, literal *> param_map;
      for (unsigned i=0; i<pos; i++)
        param_map[loc->components[i]->functor] = loc->components[i]->arg;
      // maybe 0

      b->build (s, p, loc, param_map, results);
    }
  else if (isglob(loc->components[pos]->functor)) // wildcard?
    {
      match_key match (* loc->components[pos]);

      // Call find_and_build for each possible match.  Ignore errors -
      // unless we don't find any match.
      unsigned int num_results = results.size();
      for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
        {
	  const match_key& subkey = i->first;
	  match_node* subnode = i->second;

	  if (match.globmatch(subkey))
	    {
	      if (s.verbose > 2)
		clog << "wildcard '" << loc->components[pos]->functor
		     << "' matched '" << subkey.name << "'" << endl;

	      // When we have a wildcard, we need to create a copy of
	      // the probe point.  Then we'll create a copy of the
	      // wildcard component, and substitute the non-wildcard
	      // functor.
	      probe_point *non_wildcard_pp = new probe_point(*loc);
	      probe_point::component *non_wildcard_component
		= new probe_point::component(*loc->components[pos]);
	      non_wildcard_component->functor = subkey.name;
	      non_wildcard_pp->components[pos] = non_wildcard_component;

              // NB: probe conditions are not attached at the wildcard
              // (component/functor) level, but at the overall
              // probe_point level.

	      // recurse (with the non-wildcard probe point)
	      try
	        {
		  subnode->find_and_build (s, p, non_wildcard_pp, pos+1,
					   results);
	        }
	      catch (const semantic_error& e)
	        {
		  // Ignore semantic_errors while expanding wildcards.
		  // If we get done and nothing was expanded, the code
		  // following the loop will complain.

		  // If this wildcard didn't match, cleanup.
		  delete non_wildcard_pp;
		  delete non_wildcard_component;
		}
	    }
	}
      if (! loc->optional && num_results == results.size())
        {
	  // We didn't find any wildcard matches (since the size of
	  // the result vector didn't change).  Throw an error.
          string alternatives;
          for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
            alternatives += string(" ") + i->first.str();
          
	  throw semantic_error(string("probe point mismatch at position ") +
			       lex_cast<string> (pos) +
			       " (alternatives:" + alternatives + ")");
	}
    }
  else 
    {
      match_key match (* loc->components[pos]);
      sub_map_iterator_t i = sub.find (match);
      if (i == sub.end()) // no match
        {
          string alternatives;
          for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
            alternatives += string(" ") + i->first.str();
          
          throw semantic_error (string("probe point mismatch at position ") + 
                                lex_cast<string> (pos) +
                                " (alternatives:" + alternatives + ")");
        }

      match_node* subnode = i->second;
      // recurse
      subnode->find_and_build (s, p, loc, pos+1, results);
    }
}


void
match_node::build_no_more (systemtap_session& s)
{
  for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
    i->second->build_no_more (s);
  if (end) end->build_no_more (s);
}


// ------------------------------------------------------------------------
// Alias probes
// ------------------------------------------------------------------------

struct alias_derived_probe: public derived_probe
{
  alias_derived_probe (probe* base, probe_point *l): derived_probe (base, l) {}

  void upchuck () { throw semantic_error ("inappropriate", this->tok); }

  // Alias probes are immediately expanded to other derived_probe
  // types, and are not themselves emitted or listed in
  // systemtap_session.probes

  void join_group (systemtap_session&) { upchuck (); }
};


struct
alias_expansion_builder 
  : public derived_probe_builder
{
  probe_alias * alias;

  alias_expansion_builder(probe_alias * a) 
    : alias(a)
  {}

  virtual void build(systemtap_session & sess,
		     probe * use, 
		     probe_point * location,
		     std::map<std::string, literal *> const &,
		     vector<derived_probe *> & finished_results)
  {
    // We're going to build a new probe and wrap it up in an
    // alias_expansion_probe so that the expansion loop recognizes it as
    // such and re-expands its expansion.
    
    alias_derived_probe * n = new alias_derived_probe (use, location /* soon overwritten */);
    n->body = new block();

    // The new probe gets the location list of the alias (with incoming condition joined)
    n->locations = alias->locations;
    for (unsigned i=0; i<n->locations.size(); i++)
      n->locations[i]->condition = add_condition (n->locations[i]->condition,
                                                  location->condition);

    // the token location of the alias,
    n->tok = location->tok;
    n->body->tok = location->tok;

    // and statements representing the concatenation of the alias'
    // body with the use's. 
    //
    // NB: locals are *not* copied forward, from either alias or
    // use. The expansion should have its locals re-inferred since
    // there's concatenated code here and we only want one vardecl per
    // resulting variable.

    if (alias->epilogue_style)
      {
        for (unsigned i = 0; i < use->body->statements.size(); ++i)
          n->body->statements.push_back
            (deep_copy_visitor::deep_copy(use->body->statements[i]));

        for (unsigned i = 0; i < alias->body->statements.size(); ++i)
          n->body->statements.push_back
            (deep_copy_visitor::deep_copy(alias->body->statements[i]));
      }
    else
      {
        for (unsigned i = 0; i < alias->body->statements.size(); ++i)
          n->body->statements.push_back
            (deep_copy_visitor::deep_copy(alias->body->statements[i]));
        
        for (unsigned i = 0; i < use->body->statements.size(); ++i)
          n->body->statements.push_back
            (deep_copy_visitor::deep_copy(use->body->statements[i]));
      }
    
    derive_probes (sess, n, finished_results, location->optional);
  }
};


// ------------------------------------------------------------------------
// Pattern matching
// ------------------------------------------------------------------------


// Register all the aliases we've seen in library files, and the user
// file, as patterns.

void
systemtap_session::register_library_aliases()
{
  vector<stapfile*> files(library_files);
  files.push_back(user_file);

  for (unsigned f = 0; f < files.size(); ++f)
    {
      stapfile * file = files[f];
      for (unsigned a = 0; a < file->aliases.size(); ++a)
	{
	  probe_alias * alias = file->aliases[a];
          try 
            {
              for (unsigned n = 0; n < alias->alias_names.size(); ++n)
                {
                  probe_point * name = alias->alias_names[n];
                  match_node * n = pattern_root;
                  for (unsigned c = 0; c < name->components.size(); ++c)
                    {
                      probe_point::component * comp = name->components[c];
                      // XXX: alias parameters
                      if (comp->arg)
                        throw semantic_error("alias component " 
                                             + comp->functor 
                                             + " contains illegal parameter");
                      n = n->bind(comp->functor);
                    }
                  n->bind(new alias_expansion_builder(alias));
                }
            }
          catch (const semantic_error& e)
            {
              semantic_error* er = new semantic_error (e); // copy it
              stringstream msg;
              msg << e.msg2;
              msg << " while registering probe alias ";
              alias->printsig(msg);
              er->msg2 = msg.str();
              print_error (* er);
              delete er;
            }
	}
    }
}


static unsigned max_recursion = 100;

struct 
recursion_guard
{
  unsigned & i;
  recursion_guard(unsigned & i) : i(i)
    {
      if (i > max_recursion)
	throw semantic_error("recursion limit reached");
      ++i;
    }
  ~recursion_guard() 
    {
      --i;
    }
};

// The match-and-expand loop.
void
derive_probes (systemtap_session& s,
               probe *p, vector<derived_probe*>& dps,
               bool optional)
{
  for (unsigned i = 0; i < p->locations.size(); ++i)
    {
      probe_point *loc = p->locations[i];

      try
        {
          unsigned num_atbegin = dps.size();

          // Pass down optional flag from e.g. alias reference to each
          // probe_point instance.  We do this by temporarily overriding
          // the probe_point optional flag.  We could instead deep-copy
          // and set a flag on the copy permanently.
          bool old_loc_opt = loc->optional;
          loc->optional = loc->optional || optional;
          s.pattern_root->find_and_build (s, p, loc, 0, dps); // <-- actual derivation!
          loc->optional = old_loc_opt;
          unsigned num_atend = dps.size();

          if (! (loc->optional||optional) && // something required, but
              num_atbegin == num_atend) // nothing new derived!
            throw semantic_error ("no match");

          if (loc->sufficient && (num_atend > num_atbegin))
            {
              if (s.verbose > 1)
                {
                  clog << "Probe point ";
                  p->locations[i]->print(clog);
                  clog << " sufficient, skipped";
                  for (unsigned j = i+1; j < p->locations.size(); ++j)
                    {
                      clog << " ";
                      p->locations[j]->print(clog);
                    }
                  clog << endl;
                }
              break; // we need not try to derive for any other locations
            }
        }
      catch (const semantic_error& e)
        {
          // XXX: prefer not to print_error at every nest/unroll level

          semantic_error* er = new semantic_error (e); // copy it
          stringstream msg;
          msg << e.msg2;
          msg << " while resolving probe point " << *loc;
          er->msg2 = msg.str();
          s.print_error (* er);
          delete er;
        }

    }
}



// ------------------------------------------------------------------------
//
// Indexable usage checks
//

struct symbol_fetcher
  : public throwing_visitor
{
  symbol *&sym;

  symbol_fetcher (symbol *&sym)
    : sym(sym) 
  {}

  void visit_symbol (symbol* e)
  {
    sym = e;
  }

  void visit_arrayindex (arrayindex* e)
  {
    e->base->visit_indexable (this);
  }

  void throwone (const token* t)
  {
    throw semantic_error ("Expecting symbol or array index expression", t);
  }
};

symbol *
get_symbol_within_expression (expression *e)
{
  symbol *sym = NULL;
  symbol_fetcher fetcher(sym);
  e->visit (&fetcher);
  if (!sym)
    throw semantic_error("Unable to find symbol in expression", e->tok);
  return sym;
}

static symbol *
get_symbol_within_indexable (indexable *ix)
{
  symbol *array = NULL;
  hist_op *hist = NULL;
  classify_indexable(ix, array, hist);
  if (array)
    return array;
  else
    return get_symbol_within_expression (hist->stat);
}

struct mutated_var_collector
  : public traversing_visitor
{
  set<vardecl *> * mutated_vars;

  mutated_var_collector (set<vardecl *> * mm) 
    : mutated_vars (mm)
  {}

  void visit_assignment(assignment* e)
  {
    if (e->type == pe_stats && e->op == "<<<")
      {
	vardecl *vd = get_symbol_within_expression (e->left)->referent;
	if (vd)
	  mutated_vars->insert (vd);
      }
    traversing_visitor::visit_assignment(e);
  }

  void visit_arrayindex (arrayindex *e)
  {
    if (is_active_lvalue (e))
      {
	symbol *sym;
	if (e->base->is_symbol (sym))
	  mutated_vars->insert (sym->referent);
	else
	  throw semantic_error("Assignment to read-only histogram bucket", e->tok);
      }
    traversing_visitor::visit_arrayindex (e);
  }
};


struct no_var_mutation_during_iteration_check
  : public traversing_visitor
{
  systemtap_session & session;
  map<functiondecl *,set<vardecl *> *> & function_mutates_vars;
  vector<vardecl *> vars_being_iterated;
  
  no_var_mutation_during_iteration_check 
  (systemtap_session & sess,
   map<functiondecl *,set<vardecl *> *> & fmv)
    : session(sess), function_mutates_vars (fmv)
  {}

  void visit_arrayindex (arrayindex *e)
  {
    if (is_active_lvalue(e))
      {
	vardecl *vd = get_symbol_within_indexable (e->base)->referent;
	if (vd)
	  {
	    for (unsigned i = 0; i < vars_being_iterated.size(); ++i)
	      {
		vardecl *v = vars_being_iterated[i];
		if (v == vd)
		  {
		    string err = ("variable '" + v->name +
				  "' modified during 'foreach' iteration");
		    session.print_error (semantic_error (err, e->tok));
		  }
	      }
	  }
      }
    traversing_visitor::visit_arrayindex (e);
  }

  void visit_functioncall (functioncall* e)
  {
    map<functiondecl *,set<vardecl *> *>::const_iterator i 
      = function_mutates_vars.find (e->referent);

    if (i != function_mutates_vars.end())
      {
	for (unsigned j = 0; j < vars_being_iterated.size(); ++j)
	  {
	    vardecl *m = vars_being_iterated[j];
	    if (i->second->find (m) != i->second->end())
	      {
		string err = ("function call modifies var '" + m->name +
			      "' during 'foreach' iteration");
		session.print_error (semantic_error (err, e->tok));
	      }
	  }
      }

    traversing_visitor::visit_functioncall (e);
  }

  void visit_foreach_loop(foreach_loop* s)
  {
    vardecl *vd = get_symbol_within_indexable (s->base)->referent;

    if (vd)
      vars_being_iterated.push_back (vd);
    
    traversing_visitor::visit_foreach_loop (s);

    if (vd)
      vars_being_iterated.pop_back();
  }
};


// ------------------------------------------------------------------------

struct stat_decl_collector
  : public traversing_visitor
{
  systemtap_session & session;
  
  stat_decl_collector(systemtap_session & sess)
    : session(sess)
  {}

  void visit_stat_op (stat_op* e)
  {
    symbol *sym = get_symbol_within_expression (e->stat);
    if (session.stat_decls.find(sym->name) == session.stat_decls.end())
      session.stat_decls[sym->name] = statistic_decl();
  }

  void visit_assignment (assignment* e)
  {
    if (e->op == "<<<")
      {
	symbol *sym = get_symbol_within_expression (e->left);
	if (session.stat_decls.find(sym->name) == session.stat_decls.end())
	  session.stat_decls[sym->name] = statistic_decl();
      }
    else
      traversing_visitor::visit_assignment(e);
  }

  void visit_hist_op (hist_op* e)
  {
    symbol *sym = get_symbol_within_expression (e->stat);
    statistic_decl new_stat;

    if (e->htype == hist_linear)
      {
	new_stat.type = statistic_decl::linear;
	assert (e->params.size() == 3);
	new_stat.linear_low = e->params[0];
	new_stat.linear_high = e->params[1];
	new_stat.linear_step = e->params[2];
      }
    else
      {
	assert (e->htype == hist_log);
	new_stat.type = statistic_decl::logarithmic;
	assert (e->params.size() == 0);
      }

    map<string, statistic_decl>::iterator i = session.stat_decls.find(sym->name);
    if (i == session.stat_decls.end())
      session.stat_decls[sym->name] = new_stat;
    else
      {
	statistic_decl & old_stat = i->second;
	if (!(old_stat == new_stat))
	  {
	    if (old_stat.type == statistic_decl::none)
	      i->second = new_stat;
	    else
	      {
		// FIXME: Support multiple co-declared histogram types
		semantic_error se("multiple histogram types declared on '" + sym->name + "'", 
				  e->tok);
		session.print_error (se);
	      }
	  }
      }    
  }

};

static int
semantic_pass_stats (systemtap_session & sess)
{
  stat_decl_collector sdc(sess);

  for (unsigned i = 0; i < sess.functions.size(); ++i)    
    sess.functions[i]->body->visit (&sdc);

  for (unsigned i = 0; i < sess.probes.size(); ++i)    
    sess.probes[i]->body->visit (&sdc);

  for (unsigned i = 0; i < sess.globals.size(); ++i)
    {
      vardecl *v = sess.globals[i];
      if (v->type == pe_stats)
	{
	  
	  if (sess.stat_decls.find(v->name) == sess.stat_decls.end())
	    {
	      semantic_error se("unable to infer statistic parameters for global '" + v->name + "'");
	      sess.print_error (se);
	    }
	}
    }
  
  return sess.num_errors();
}

// ------------------------------------------------------------------------

// Enforce variable-related invariants: no modification of
// a foreach()-iterated array.
static int
semantic_pass_vars (systemtap_session & sess)
{
  
  map<functiondecl *, set<vardecl *> *> fmv;
  no_var_mutation_during_iteration_check chk(sess, fmv);
  
  for (unsigned i = 0; i < sess.functions.size(); ++i)
    {
      functiondecl * fn = sess.functions[i];
      if (fn->body)
	{
	  set<vardecl *> * m = new set<vardecl *>();
	  mutated_var_collector mc (m);
	  fn->body->visit (&mc);
	  fmv[fn] = m;
	}
    }

  for (unsigned i = 0; i < sess.functions.size(); ++i)
    {
      if (sess.functions[i]->body)
	sess.functions[i]->body->visit (&chk);
    }

  for (unsigned i = 0; i < sess.probes.size(); ++i)
    {
      if (sess.probes[i]->body)
	sess.probes[i]->body->visit (&chk);
    }  

  return sess.num_errors();
}


// ------------------------------------------------------------------------

// Rewrite probe condition expressions into probe bodies.  Tricky and
// exciting business, this.  This:
//
// probe foo if (g1 || g2) { ... }
// probe bar { ... g1 ++ ... }
//
// becomes:
//
// probe begin(MAX) { if (! (g1 || g2)) %{ disable_probe_foo %} }
// probe foo { if (! (g1 || g2)) next; ... }
// probe bar { ... g1 ++ ...; 
//             if (g1 || g2) %{ enable_probe_foo %} else %{ disable_probe_foo %}
//           }
//
// XXX: As a first cut, do only the "inline probe condition" part of the
// transform.

static int
semantic_pass_conditions (systemtap_session & sess)
{
  for (unsigned i = 0; i < sess.probes.size(); ++i)
    {
      derived_probe* p = sess.probes[i];
      expression* e = p->sole_location()->condition;
      if (e)
        {
          varuse_collecting_visitor vut;
          e->visit (& vut);

          if (! vut.written.empty())
            {
              string err = ("probe condition must not modify any variables");
              sess.print_error (semantic_error (err, e->tok));
            }
          else if (vut.embedded_seen)
            {
              sess.print_error (semantic_error ("probe condition must not include impure embedded-C", e->tok));
            }

          // Add the condition expression to the front of the
          // derived_probe body.
          if_statement *ifs = new if_statement ();
          ifs->tok = e->tok;
          ifs->thenblock = new next_statement ();
          ifs->thenblock->tok = e->tok;
          ifs->elseblock = NULL;
          unary_expression *notex = new unary_expression ();
          notex->op = "!";
          notex->tok = e->tok;
          notex->operand = e;
          ifs->condition = notex;
          p->body->statements.insert (p->body->statements.begin(), ifs);
        }
    }  

  return sess.num_errors();
}


// ------------------------------------------------------------------------


static int semantic_pass_symbols (systemtap_session&);
static int semantic_pass_optimize1 (systemtap_session&);
static int semantic_pass_optimize2 (systemtap_session&);
static int semantic_pass_types (systemtap_session&);
static int semantic_pass_vars (systemtap_session&);
static int semantic_pass_stats (systemtap_session&);
static int semantic_pass_conditions (systemtap_session&);


// Link up symbols to their declarations.  Set the session's
// files/probes/functions/globals vectors from the transitively
// reached set of stapfiles in s.library_files, starting from
// s.user_file.  Perform automatic tapset inclusion and probe
// alias expansion.
static int
semantic_pass_symbols (systemtap_session& s)
{
  symresolution_info sym (s);

  // NB: s.files can grow during this iteration, so size() can
  // return gradually increasing numbers.
  s.files.push_back (s.user_file);
  for (unsigned i = 0; i < s.files.size(); i++)
    {
      stapfile* dome = s.files[i];

      // Pass 1: add globals and functions to systemtap-session master list,
      //         so the find_* functions find them

      for (unsigned i=0; i<dome->globals.size(); i++)
        s.globals.push_back (dome->globals[i]);

      for (unsigned i=0; i<dome->functions.size(); i++)
        s.functions.push_back (dome->functions[i]);

      for (unsigned i=0; i<dome->embeds.size(); i++)
        s.embeds.push_back (dome->embeds[i]);

      // Pass 2: process functions

      for (unsigned i=0; i<dome->functions.size(); i++)
        {
          functiondecl* fd = dome->functions[i];

          try 
            {
              sym.current_function = fd;
              sym.current_probe = 0;
              fd->body->visit (& sym);
            }
          catch (const semantic_error& e)
            {
              s.print_error (e);
            }
        }

      // Pass 3: derive probes and resolve any further symbols in the
      // derived results. 

      for (unsigned i=0; i<dome->probes.size(); i++)
        {
          probe* p = dome->probes [i];
          vector<derived_probe*> dps;

          // much magic happens here: probe alias expansion, wildcard
          // matching, low-level derived_probe construction.
          derive_probes (s, p, dps);

          for (unsigned j=0; j<dps.size(); j++)
            {
              derived_probe* dp = dps[j];
              s.probes.push_back (dp);
              dp->join_group (s);

              try 
                {
                  sym.current_function = 0;
                  sym.current_probe = dp;
                  dp->body->visit (& sym);

                  // Process the probe-point condition expression.
                  sym.current_function = 0;
                  sym.current_probe = 0;
                  if (dp->sole_location()->condition)
                    dp->sole_location()->condition->visit (& sym);
                }
              catch (const semantic_error& e)
                {
                  s.print_error (e);
                }
            }
        }
    }

  // Inform all derived_probe builders that we're done with
  // all resolution, so it's time to release caches.
  s.pattern_root->build_no_more (s);
  
  return s.num_errors(); // all those print_error calls
}



int
semantic_pass (systemtap_session& s)
{
  int rc = 0;

  try 
    {
      s.register_library_aliases();
      register_standard_tapsets(s);
      
      if (rc == 0) rc = semantic_pass_symbols (s);
      if (rc == 0) rc = semantic_pass_conditions (s);
      if (rc == 0 && ! s.unoptimized) rc = semantic_pass_optimize1 (s);
      if (rc == 0) rc = semantic_pass_types (s);
      if (rc == 0 && ! s.unoptimized) rc = semantic_pass_optimize2 (s);
      if (rc == 0) rc = semantic_pass_vars (s);
      if (rc == 0) rc = semantic_pass_stats (s);
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
    }
  
  return rc;
}


// ------------------------------------------------------------------------


systemtap_session::systemtap_session ():
  // NB: pointer members must be manually initialized!
  pattern_root(new match_node),
  user_file (0),
  be_derived_probes(0), 
  dwarf_derived_probes(0), 
  uprobe_derived_probes(0), 
  timer_derived_probes(0), 
  profile_derived_probes(0), 
  mark_derived_probes(0), 
  hrtimer_derived_probes(0), 
  perfmon_derived_probes(0), 
  procfs_derived_probes(0), 
  op (0), up (0)
{
}


void
systemtap_session::print_error (const semantic_error& e)
{
  string message_str;
  stringstream message;

  message << "semantic error: " << e.what ();
  if (e.tok1 || e.tok2)
    message << ": ";
  if (e.tok1) message << *e.tok1;
  message << e.msg2;
  if (e.tok2) message << *e.tok2;
  message << endl;
  message_str = message.str();

  // Duplicate elimination
  if (seen_errors.find (message_str) == seen_errors.end())
    {
      seen_errors.insert (message_str);
      cerr << message_str;
    }

  if (e.chain)
    print_error (* e.chain);
}


// ------------------------------------------------------------------------
// semantic processing: symbol resolution


symresolution_info::symresolution_info (systemtap_session& s):
  session (s), current_function (0), current_probe (0)
{
}


void
symresolution_info::visit_block (block* e)
{
  for (unsigned i=0; i<e->statements.size(); i++)
    {
      try 
	{
	  e->statements[i]->visit (this);
	}
      catch (const semantic_error& e)
	{
	  session.print_error (e);
        }
    }
}


void
symresolution_info::visit_foreach_loop (foreach_loop* e)
{
  for (unsigned i=0; i<e->indexes.size(); i++)
    e->indexes[i]->visit (this);

  symbol *array = NULL;  
  hist_op *hist = NULL;
  classify_indexable (e->base, array, hist);

  if (array)
    {
      if (!array->referent)
	{	  
	  vardecl* d = find_var (array->name, e->indexes.size ());
	  if (d)
	    array->referent = d;
	  else
            {
              stringstream msg;
              msg << "unresolved arity-" << e->indexes.size()
                  << " global array " << array->name;
              throw semantic_error (msg.str(), e->tok);
            }
	}
    }
  else 
    {
      assert (hist);
      hist->visit (this);
    }

  if (e->limit)
    e->limit->visit (this);

  e->block->visit (this);
}


struct 
delete_statement_symresolution_info:
  public traversing_visitor
{
  symresolution_info *parent;

  delete_statement_symresolution_info (symresolution_info *p):
    parent(p)
  {}

  void visit_arrayindex (arrayindex* e)
  {
    parent->visit_arrayindex (e);
  }
  void visit_functioncall (functioncall* e)
  {
    parent->visit_functioncall (e);
  }

  void visit_symbol (symbol* e)
  {
    if (e->referent)
      return;
    
    vardecl* d = parent->find_var (e->name, -1);
    if (d)
      e->referent = d;
    else
      throw semantic_error ("unresolved array in delete statement", e->tok);
  }
};

void 
symresolution_info::visit_delete_statement (delete_statement* s)
{
  delete_statement_symresolution_info di (this);
  s->value->visit (&di);
}


void
symresolution_info::visit_symbol (symbol* e)
{
  if (e->referent)
    return;

  vardecl* d = find_var (e->name, 0);
  if (d)
    e->referent = d;
  else
    {
      // new local
      vardecl* v = new vardecl;
      v->name = e->name;
      v->tok = e->tok;
      if (current_function)
        current_function->locals.push_back (v);
      else if (current_probe)
        current_probe->locals.push_back (v);
      else
        // must be probe-condition expression
        throw semantic_error ("probe condition must not reference undeclared global", e->tok);
      e->referent = v;
    }
}


void
symresolution_info::visit_arrayindex (arrayindex* e)
{
  for (unsigned i=0; i<e->indexes.size(); i++)
    e->indexes[i]->visit (this);

  symbol *array = NULL;  
  hist_op *hist = NULL;
  classify_indexable(e->base, array, hist);

  if (array)
    {
      if (array->referent)
	return;

      vardecl* d = find_var (array->name, e->indexes.size ());
      if (d)
	array->referent = d;
      else
	{
	  // new local
	  vardecl* v = new vardecl;
	  v->set_arity(e->indexes.size());
	  v->name = array->name;
	  v->tok = array->tok;
	  if (current_function)
	    current_function->locals.push_back (v);
	  else if (current_probe)
	    current_probe->locals.push_back (v);
	  else
	    // must not happen
	    throw semantic_error ("no current probe/function", e->tok);
	  array->referent = v;
	}      
    }
  else
    {
      assert (hist);
      hist->visit (this);
    }
}


void
symresolution_info::visit_functioncall (functioncall* e)
{
  // XXX: we could relax this, if we're going to examine the
  // vartracking data recursively.  See testsuite/semko/fortytwo.stp.
  if (! (current_function || current_probe))
    {
      // must be probe-condition expression
      throw semantic_error ("probe condition must not reference function", e->tok);
    }

  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);

  if (e->referent)
    return;

  functiondecl* d = find_function (e->function, e->args.size ());
  if (d)
    e->referent = d;
  else
    {
      stringstream msg;
      msg << "unresolved arity-" << e->args.size()
          << " function";
      throw semantic_error (msg.str(), e->tok);
    }
}


vardecl* 
symresolution_info::find_var (const string& name, int arity)
{
  if (current_function || current_probe)
    {
      // search locals
      vector<vardecl*>& locals = (current_function ? 
                                  current_function->locals :
                                  current_probe->locals);
      
      
      for (unsigned i=0; i<locals.size(); i++)
        if (locals[i]->name == name 
            && locals[i]->compatible_arity(arity))
          {
            locals[i]->set_arity (arity);
            return locals[i];
          }
    }

  // search function formal parameters (for scalars)
  if (arity == 0 && current_function)
    for (unsigned i=0; i<current_function->formal_args.size(); i++)
      if (current_function->formal_args[i]->name == name)
	{
	  // NB: no need to check arity here: formal args always scalar
	  current_function->formal_args[i]->set_arity (0);
	  return current_function->formal_args[i];
	}

  // search processed globals
  for (unsigned i=0; i<session.globals.size(); i++)
    if (session.globals[i]->name == name
	&& session.globals[i]->compatible_arity(arity))  
      {
	session.globals[i]->set_arity (arity);
	return session.globals[i];
      }
  
  // search library globals
  for (unsigned i=0; i<session.library_files.size(); i++)
    {
      stapfile* f = session.library_files[i];
      for (unsigned j=0; j<f->globals.size(); j++)
        {
          vardecl* g = f->globals[j];
          if (g->name == name && g->compatible_arity (arity))
            {
	      g->set_arity (arity);
              
              // put library into the queue if not already there	    
              if (find (session.files.begin(), session.files.end(), f) 
                  == session.files.end())
                session.files.push_back (f);
              
              return g;
            }
        }
    }

  return 0;
}


functiondecl* 
symresolution_info::find_function (const string& name, unsigned arity)
{
  for (unsigned j = 0; j < session.functions.size(); j++)
    {
      functiondecl* fd = session.functions[j];
      if (fd->name == name &&
          fd->formal_args.size() == arity)
        return fd;
    }

  // search library globals
  for (unsigned i=0; i<session.library_files.size(); i++)
    {
      stapfile* f = session.library_files[i];
      for (unsigned j=0; j<f->functions.size(); j++)
        if (f->functions[j]->name == name &&
            f->functions[j]->formal_args.size() == arity)
          {
            // put library into the queue if not already there
            if (0) // session.verbose_resolution
              cerr << "      function " << name << " "
                   << "is defined from " << f->name << endl;

            if (find (session.files.begin(), session.files.end(), f) 
                == session.files.end())
              session.files.push_back (f);
            // else .. print different message?

            return f->functions[j];
          }
    }

  return 0;
}



// ------------------------------------------------------------------------
// optimization


// Do away with functiondecls that are never (transitively) called
// from probes.
void semantic_pass_opt1 (systemtap_session& s, bool& relaxed_p)
{
  functioncall_traversing_visitor ftv;
  for (unsigned i=0; i<s.probes.size(); i++)
    {
      s.probes[i]->body->visit (& ftv);
      if (s.probes[i]->sole_location()->condition)
        s.probes[i]->sole_location()->condition->visit (& ftv);
    }
  for (unsigned i=0; i<s.functions.size(); /* see below */)
    {
      if (ftv.traversed.find(s.functions[i]) == ftv.traversed.end())
        {
          if (s.functions[i]->tok->location.file == s.user_file->name && // !tapset
              ! s.suppress_warnings)
            clog << "WARNING: eliding unused function " << *s.functions[i]->tok << endl;
          else if (s.verbose>2)
            clog << "Eliding unused function " << s.functions[i]->name
                 << endl;
	  if (s.tapset_compile_coverage) {
	    s.unused_functions.push_back (s.functions[i]);
	  }
          s.functions.erase (s.functions.begin() + i);
          relaxed_p = false;
          // NB: don't increment i
        }
      else
        i++;
    }
}


// ------------------------------------------------------------------------

// Do away with local & global variables that are never
// written nor read.
void semantic_pass_opt2 (systemtap_session& s, bool& relaxed_p)
{
  varuse_collecting_visitor vut;
  
  for (unsigned i=0; i<s.probes.size(); i++)
    {
      s.probes[i]->body->visit (& vut);

      if (s.probes[i]->sole_location()->condition)
        s.probes[i]->sole_location()->condition->visit (& vut);
    }

  // NB: Since varuse_collecting_visitor also traverses down
  // actually called functions, we don't need to explicitly
  // iterate over them.  Uncalled ones should have been pruned
  // in _opt1 above.
  //
  // for (unsigned i=0; i<s.functions.size(); i++)
  //   s.functions[i]->body->visit (& vut);
  
  // Now in vut.read/written, we have a mixture of all locals, globals
  
  for (unsigned i=0; i<s.probes.size(); i++)
    for (unsigned j=0; j<s.probes[i]->locals.size(); /* see below */)
      {
        vardecl* l = s.probes[i]->locals[j];

        if (vut.read.find (l) == vut.read.end() &&
            vut.written.find (l) == vut.written.end())
          {
            if (l->tok->location.file == s.user_file->name && // !tapset
                ! s.suppress_warnings)
              clog << "WARNING: eliding unused variable " << *l->tok << endl;
            else if (s.verbose>2)
              clog << "Eliding unused local variable "
                   << l->name << " in " << s.probes[i]->name << endl;
	    if (s.tapset_compile_coverage) {
	      s.probes[i]->unused_locals.push_back
		      (s.probes[i]->locals[j]);
	    }
            s.probes[i]->locals.erase(s.probes[i]->locals.begin() + j);
            relaxed_p = false;
            // don't increment j
          }
        else
          j++;
      }
  for (unsigned i=0; i<s.functions.size(); i++)
    for (unsigned j=0; j<s.functions[i]->locals.size(); /* see below */)
      {
        vardecl* l = s.functions[i]->locals[j];
        if (vut.read.find (l) == vut.read.end() &&
            vut.written.find (l) == vut.written.end())
          {
            if (l->tok->location.file == s.user_file->name && // !tapset
                ! s.suppress_warnings)
              clog << "WARNING: eliding unused variable " << *l->tok << endl;
            else if (s.verbose>2)
              clog << "Eliding unused local variable "
                   << l->name << " in function " << s.functions[i]->name
                   << endl;
	    if (s.tapset_compile_coverage) {
              s.functions[i]->unused_locals.push_back
		    (s.functions[i]->locals[j]);
	    }
            s.functions[i]->locals.erase(s.functions[i]->locals.begin() + j);
            relaxed_p = false;
            // don't increment j
          }
        else
          j++;
      }
  for (unsigned i=0; i<s.globals.size(); /* see below */)
    {
      vardecl* l = s.globals[i];
      if (vut.read.find (l) == vut.read.end() &&
          vut.written.find (l) == vut.written.end())
        {
          if (l->tok->location.file == s.user_file->name && // !tapset
              ! s.suppress_warnings)
            clog << "WARNING: eliding unused variable " << *l->tok << endl;
          else if (s.verbose>2)
            clog << "Eliding unused global variable "
                 << l->name << endl;
	  if (s.tapset_compile_coverage) {
            s.unused_globals.push_back(s.globals[i]);
	  }
          s.globals.erase(s.globals.begin() + i);
          relaxed_p = false;
          // don't increment i
        }
      else
        i++;
    }
}


// ------------------------------------------------------------------------

struct dead_assignment_remover: public traversing_visitor
{
  systemtap_session& session;
  bool& relaxed_p;
  const varuse_collecting_visitor& vut;
  expression** current_expr;

  dead_assignment_remover(systemtap_session& s, bool& r,
                          const varuse_collecting_visitor& v): 
    session(s), relaxed_p(r), vut(v), current_expr(0) {}

  void visit_expr_statement (expr_statement* s);
  // XXX: other places where an assignment may be nested should be
  // handled too (e.g., loop/if conditionals, array indexes, function
  // parameters).  Until then, they result in visit_assignment() being
  // called with null current_expr.

  void visit_assignment (assignment* e);
};


void
dead_assignment_remover::visit_expr_statement (expr_statement* s)
{
  expression** last_expr = current_expr;
  current_expr = & s->value;
  s->value->visit (this);
  s->tok = s->value->tok; // in case it was replaced
  current_expr = last_expr;
}


void
dead_assignment_remover::visit_assignment (assignment* e)
{
  symbol* left = get_symbol_within_expression (e->left);
  vardecl* leftvar = left->referent;
  if (current_expr && // see XXX above: this case represents a missed
                      // optimization opportunity
      *current_expr == e) // we're not nested any deeper than expected 
    {
      // clog << "Checking assignment to " << leftvar->name << " at " << *e->tok << endl;
      if (vut.read.find(leftvar) == vut.read.end()) // var never read?
        {
          // NB: Not so fast!  The left side could be an array whose
          // index expressions may have side-effects.  This would be
          // OK if we could replace the array assignment with a 
          // statement-expression containing all the index expressions
          // and the rvalue... but we can't.

          varuse_collecting_visitor vut;
          e->left->visit (& vut);
          if (vut.side_effect_free ()) // XXX: use _wrt() once we track focal_vars
            {
              /* PR 1119: NB: This is not necessary here.  A write-only
                 variable will also be elided soon at the next _opt2 iteration.
              if (e->left->tok->location.file == session.user_file->name && // !tapset
                  ! session.suppress_warnings)
                clog << "WARNING: eliding write-only " << *e->left->tok << endl;
              else
              */
              if (session.verbose>2)
                clog << "Eliding assignment to " << leftvar->name 
                     << " at " << *e->tok << endl;
              
              *current_expr = e->right; // goodbye assignment*
              relaxed_p = false;
            }
        }
    }
}


// Let's remove assignments to variables that are never read.  We
// rewrite "(foo = expr)" as "(expr)".  This makes foo a candidate to
// be optimized away as an unused variable, and expr a candidate to be
// removed as a side-effect-free statement expression.  Wahoo!
void semantic_pass_opt3 (systemtap_session& s, bool& relaxed_p)
{
  // Recompute the varuse data, which will probably match the opt2
  // copy of the computation, except for those totally unused
  // variables that opt2 removed.
  varuse_collecting_visitor vut;
  for (unsigned i=0; i<s.probes.size(); i++)
    s.probes[i]->body->visit (& vut); // includes reachable functions too

  dead_assignment_remover dar (s, relaxed_p, vut);
  // This instance may be reused for multiple probe/function body trims.

  for (unsigned i=0; i<s.probes.size(); i++)
    s.probes[i]->body->visit (& dar);
  for (unsigned i=0; i<s.functions.size(); i++)
    s.functions[i]->body->visit (& dar);
  // The rewrite operation is performed within the visitor.
}


// ------------------------------------------------------------------------

struct dead_stmtexpr_remover: public traversing_visitor
{
  systemtap_session& session;
  bool& relaxed_p;
  statement** current_stmt; // pointer to current stmt* being iterated
  set<vardecl*> focal_vars; // vars considered subject to side-effects

  dead_stmtexpr_remover(systemtap_session& s, bool& r): 
    session(s), relaxed_p(r), current_stmt(0) {}

  void visit_block (block *s);
  void visit_if_statement (if_statement* s);
  void visit_foreach_loop (foreach_loop *s);
  void visit_for_loop (for_loop *s);
  // XXX: and other places where stmt_expr's might be nested

  void visit_expr_statement (expr_statement *s);
};


void
dead_stmtexpr_remover::visit_block (block *s)
{
  for (unsigned i=0; i<s->statements.size(); i++)
    {
      statement** last_stmt = current_stmt;
      current_stmt = & s->statements[i];
      s->statements[i]->visit (this);
      current_stmt = last_stmt;
    }
}

void
dead_stmtexpr_remover::visit_if_statement (if_statement *s)
{
  statement** last_stmt = current_stmt;
  current_stmt = & s->thenblock;
  s->thenblock->visit (this);
  if (s->elseblock)
    {
      current_stmt = & s->elseblock;
      s->elseblock->visit (this);
    }
  current_stmt = last_stmt;
}

void
dead_stmtexpr_remover::visit_foreach_loop (foreach_loop *s)
{
  statement** last_stmt = current_stmt;
  current_stmt = & s->block;
  s->block->visit (this);
  current_stmt = last_stmt;
}

void
dead_stmtexpr_remover::visit_for_loop (for_loop *s)
{
  statement** last_stmt = current_stmt;
  current_stmt = & s->block;
  s->block->visit (this);
  current_stmt = last_stmt;
}



void
dead_stmtexpr_remover::visit_expr_statement (expr_statement *s)
{
  // Run a varuse query against the operand expression.  If it has no
  // side-effects, replace the entire statement expression by a null
  // statement.  This replacement is done by overwriting the
  // current_stmt pointer.
  //
  // Unlike many other visitors, we do *not* traverse this outermost
  // one into the expression subtrees.  There is no need - no
  // expr_statement nodes will be found there.  (Function bodies
  // need to be visited explicitly by our caller.)
  //
  // NB.  While we don't share nodes in the parse tree, let's not
  // deallocate *s anyway, just in case...

  varuse_collecting_visitor vut;
  s->value->visit (& vut);
  if (vut.side_effect_free_wrt (focal_vars) &&
      *current_stmt == s) // we're not nested any deeper than expected 
    {
      /* PR 1119: NB: this message is not a good idea here.  It can
         name some arbitrary RHS expression of an assignment.
      if (s->value->tok->location.file == session.user_file->name && // not tapset
          ! session.suppress_warnings)
        clog << "WARNING: eliding read-only " << *s->value->tok << endl;
      else 
      */
      if (session.verbose>2)
        clog << "Eliding side-effect-free expression "
             << *s->tok << endl;

      null_statement* ns = new null_statement;
      ns->tok = s->tok;
      * current_stmt = ns;
      // XXX: A null_statement carries more weight in the translator's
      // output than a nonexistent statement.  It might be nice to
      // work a little harder and completely eliminate all traces of
      // an elided statement.
      
      relaxed_p = false;
    }
}


void semantic_pass_opt4 (systemtap_session& s, bool& relaxed_p)
{
  // Finally, let's remove some statement-expressions that have no
  // side-effect.  These should be exactly those whose private varuse
  // visitors come back with an empty "written" and "embedded" lists.
  
  dead_stmtexpr_remover duv (s, relaxed_p);
  // This instance may be reused for multiple probe/function body trims.

  for (unsigned i=0; i<s.probes.size(); i++)
    {
      duv.focal_vars.clear ();
      duv.focal_vars.insert (s.globals.begin(),
                             s.globals.end());
      duv.focal_vars.insert (s.probes[i]->locals.begin(),
                             s.probes[i]->locals.end());
      s.probes[i]->body->visit (& duv);
    }
  for (unsigned i=0; i<s.functions.size(); i++)
    {
      duv.focal_vars.clear ();
      duv.focal_vars.insert (s.functions[i]->locals.begin(),
                             s.functions[i]->locals.end());
      duv.focal_vars.insert (s.functions[i]->formal_args.begin(),
                             s.functions[i]->formal_args.end());
      duv.focal_vars.insert (s.globals.begin(),
                             s.globals.end());
      s.functions[i]->body->visit (& duv);
    }
}

struct duplicate_function_remover: public functioncall_traversing_visitor
{
  systemtap_session& s;
  map<functiondecl*, functiondecl*>& duplicate_function_map;

  duplicate_function_remover(systemtap_session& sess,
			     map<functiondecl*, functiondecl*>&dfm):
    s(sess), duplicate_function_map(dfm) {};

  void visit_functioncall (functioncall* e);
};

void
duplicate_function_remover::visit_functioncall (functioncall *e)
{
  functioncall_traversing_visitor::visit_functioncall (e);

  // If the current function call reference points to a function that
  // is a duplicate, replace it.
  if (duplicate_function_map.count(e->referent) != 0)
    {
      if (s.verbose>2)
	  clog << "Changing " << e->referent->name
	       << " reference to "
	       << duplicate_function_map[e->referent]->name
	       << " reference\n";
      e->tok = duplicate_function_map[e->referent]->tok;
      e->function = duplicate_function_map[e->referent]->name;
      e->referent = duplicate_function_map[e->referent];
    }
}

static string
get_functionsig (functiondecl* f)
{
  ostringstream s;

  // Get the "name:args body" of the function in s.  We have to
  // include the args since the function 'x1(a, b)' is different than
  // the function 'x2(b, a)' even if the bodies of the two functions
  // are exactly the same.
  f->printsig(s);
  f->body->print(s);

  // printsig puts f->name + ':' on the front.  Remove this
  // (otherwise, functions would never compare equal).
  string str = s.str().erase(0, f->name.size() + 1);

  // Return the function signature.
  return str;
}

void semantic_pass_opt5 (systemtap_session& s, bool& relaxed_p)
{
  // Walk through all the functions, looking for duplicates.
  map<string, functiondecl*> functionsig_map;
  map<functiondecl*, functiondecl*> duplicate_function_map;
  for (unsigned i=0; i < s.functions.size(); /* see below */)
    {
      string functionsig = get_functionsig(s.functions[i]);

      if (functionsig_map.count(functionsig) == 0)
	{
	  // This function is unique.  Remember it.
	  functionsig_map[functionsig] = s.functions[i];
	  i++;
	}
      else
        {
	  // This function is a duplicate.
	  duplicate_function_map[s.functions[i]]
	    = functionsig_map[functionsig];

	  // Remove the duplicate function (since we don't need it
	  // anymore).
	  s.functions.erase (s.functions.begin() + i);

	  relaxed_p = false;
          // NB: don't increment i
	}
    }

  // If we have duplicate functions, traverse down the tree, replacing
  // the appropriate function calls.
  // duplicate_function_remover::visit_functioncall() handles the
  // details of replacing the function calls.
  if (duplicate_function_map.size() != 0)
    {
      duplicate_function_remover dfr (s, duplicate_function_map);

      for (unsigned i=0; i < s.probes.size(); i++)
	s.probes[i]->body->visit(&dfr);
    }
}


static int
semantic_pass_optimize1 (systemtap_session& s)
{
  // In this pass, we attempt to rewrite probe/function bodies to
  // eliminate some blatantly unnecessary code.  This is run before
  // type inference, but after symbol resolution and derived_probe
  // creation.  We run an outer "relaxation" loop that repeats the
  // optimizations until none of them find anything to remove.

  int rc = 0;

  bool relaxed_p = false;
  while (! relaxed_p)
    {
      relaxed_p = true; // until proven otherwise

      semantic_pass_opt1 (s, relaxed_p);
      semantic_pass_opt2 (s, relaxed_p);
      semantic_pass_opt3 (s, relaxed_p);
      semantic_pass_opt4 (s, relaxed_p);
    }

  if (s.probes.size() == 0)
    throw semantic_error ("no probes found");

  return rc;
}


static int
semantic_pass_optimize2 (systemtap_session& s)
{
  // This is run after type inference.  We run an outer "relaxation"
  // loop that repeats the optimizations until none of them find
  // anything to remove.

  int rc = 0;

  bool relaxed_p = false;
  while (! relaxed_p)
    {
      relaxed_p = true; // until proven otherwise

      semantic_pass_opt5 (s, relaxed_p);
    }

  if (s.probes.size() == 0)
    throw semantic_error ("no probes found");

  return rc;
}



// ------------------------------------------------------------------------
// type resolution


static int
semantic_pass_types (systemtap_session& s)
{
  int rc = 0;

  // next pass: type inference
  unsigned iterations = 0;
  typeresolution_info ti (s);
  
  ti.assert_resolvability = false;
  // XXX: maybe convert to exception-based error signalling
  while (1)
    {
      iterations ++;
      ti.num_newly_resolved = 0;
      ti.num_still_unresolved = 0;

      for (unsigned j=0; j<s.functions.size(); j++)
        {
          functiondecl* fn = s.functions[j];
          ti.current_probe = 0;
          ti.current_function = fn;
          ti.t = pe_unknown;
          fn->body->visit (& ti);
	  // NB: we don't have to assert a known type for
	  // functions here, to permit a "void" function.
	  // The translator phase will omit the "retvalue".
	  //
          // if (fn->type == pe_unknown)
          //   ti.unresolved (fn->tok);
        }          

      for (unsigned j=0; j<s.probes.size(); j++)
        {
          derived_probe* pn = s.probes[j];
          ti.current_function = 0;
          ti.current_probe = pn;
          ti.t = pe_unknown;
          pn->body->visit (& ti);

          probe_point* pp = pn->sole_location();
          if (pp->condition)
            {
              ti.current_function = 0;
              ti.current_probe = 0;
              ti.t = pe_long; // NB: expected type
              pp->condition->visit (& ti);
            }
        }

      for (unsigned j=0; j<s.globals.size(); j++)
        {
          vardecl* gd = s.globals[j];
          if (gd->type == pe_unknown)
            ti.unresolved (gd->tok);
        }
      
      if (ti.num_newly_resolved == 0) // converged
        {
          if (ti.num_still_unresolved == 0)
            break; // successfully
          else if (! ti.assert_resolvability)
            ti.assert_resolvability = true; // last pass, with error msgs
          else
            { // unsuccessful conclusion
              rc ++;
              break;
            }
        }
    }
  
  return rc + s.num_errors();
}



typeresolution_info::typeresolution_info (systemtap_session& s):
  session(s), current_function(0), current_probe(0)
{
}


void
typeresolution_info::visit_literal_number (literal_number* e)
{
  assert (e->type == pe_long);
  if ((t == e->type) || (t == pe_unknown))
    return;

  mismatch (e->tok, e->type, t);
}


void
typeresolution_info::visit_literal_string (literal_string* e)
{
  assert (e->type == pe_string);
  if ((t == e->type) || (t == pe_unknown))
    return;

  mismatch (e->tok, e->type, t);
}


void
typeresolution_info::visit_logical_or_expr (logical_or_expr *e)
{
  visit_binary_expression (e);
}


void
typeresolution_info::visit_logical_and_expr (logical_and_expr *e)
{
  visit_binary_expression (e);
}


void
typeresolution_info::visit_comparison (comparison *e)
{
  // NB: result of any comparison is an integer!
  if (t == pe_stats || t == pe_string)
    invalid (e->tok, t);

  t = (e->right->type != pe_unknown) ? e->right->type : pe_unknown;
  e->left->visit (this);
  t = (e->left->type != pe_unknown) ? e->left->type : pe_unknown;
  e->right->visit (this);
  
  if (e->left->type != pe_unknown &&
      e->right->type != pe_unknown &&
      e->left->type != e->right->type)
    mismatch (e->tok, e->left->type, e->right->type);
  
  if (e->type == pe_unknown)
    {
      e->type = pe_long;
      resolved (e->tok, e->type);
    }
}


void
typeresolution_info::visit_concatenation (concatenation *e)
{
  if (t != pe_unknown && t != pe_string)
    invalid (e->tok, t);

  t = pe_string;
  e->left->visit (this);
  t = pe_string;
  e->right->visit (this);

  if (e->type == pe_unknown)
    {
      e->type = pe_string;
      resolved (e->tok, e->type);
    }
}


void
typeresolution_info::visit_assignment (assignment *e)
{
  if (t == pe_stats)
    invalid (e->tok, t);

  if (e->op == "<<<") // stats aggregation
    {
      if (t == pe_string)
        invalid (e->tok, t);

      t = pe_stats;
      e->left->visit (this);
      t = pe_long;
      e->right->visit (this);
      if (e->type == pe_unknown ||
	  e->type == pe_stats)
        {
          e->type = pe_long;
          resolved (e->tok, e->type);
        }
    }

  else if (e->left->type == pe_stats)
    invalid (e->left->tok, e->left->type);

  else if (e->right->type == pe_stats)
    invalid (e->right->tok, e->right->type);

  else if (e->op == "+=" || // numeric only
           e->op == "-=" ||
           e->op == "*=" ||
           e->op == "/=" ||
           e->op == "%=" ||
           e->op == "&=" ||
           e->op == "^=" ||
           e->op == "|=" ||
           e->op == "<<=" ||
           e->op == ">>=" ||
           false)
    {
      visit_binary_expression (e);
    }
  else if (e->op == ".=" || // string only
           false)
    {
      if (t == pe_long || t == pe_stats)
        invalid (e->tok, t);

      t = pe_string;
      e->left->visit (this);
      t = pe_string;
      e->right->visit (this);
      if (e->type == pe_unknown)
        {
          e->type = pe_string;
          resolved (e->tok, e->type);
        }
    }
  else if (e->op == "=") // overloaded = for string & numeric operands
    {
      // logic similar to ternary_expression
      exp_type sub_type = t;

      // Infer types across the l/r values
      if (sub_type == pe_unknown && e->type != pe_unknown)
        sub_type = e->type;

      t = (sub_type != pe_unknown) ? sub_type :
        (e->right->type != pe_unknown) ? e->right->type :
        pe_unknown;
      e->left->visit (this);
      t = (sub_type != pe_unknown) ? sub_type :
        (e->left->type != pe_unknown) ? e->left->type :
        pe_unknown;
      e->right->visit (this);
      
      if ((sub_type != pe_unknown) && (e->type == pe_unknown))
        {
          e->type = sub_type;
          resolved (e->tok, e->type);
        }
      if ((sub_type == pe_unknown) && (e->left->type != pe_unknown))
        {
          e->type = e->left->type;
          resolved (e->tok, e->type);
        }

      if (e->left->type != pe_unknown &&
          e->right->type != pe_unknown &&
          e->left->type != e->right->type)
        mismatch (e->tok, e->left->type, e->right->type);

    }
  else
    throw semantic_error ("unsupported assignment operator " + e->op);
}


void
typeresolution_info::visit_binary_expression (binary_expression* e)
{
  if (t == pe_stats || t == pe_string)
    invalid (e->tok, t);

  t = pe_long;
  e->left->visit (this);
  t = pe_long;
  e->right->visit (this);

  if (e->left->type != pe_unknown &&
      e->right->type != pe_unknown &&
      e->left->type != e->right->type)
    mismatch (e->tok, e->left->type, e->right->type);
  
  if (e->type == pe_unknown)
    {
      e->type = pe_long;
      resolved (e->tok, e->type);
    }
}


void
typeresolution_info::visit_pre_crement (pre_crement *e)
{
  visit_unary_expression (e);
}


void
typeresolution_info::visit_post_crement (post_crement *e)
{
  visit_unary_expression (e);
}


void
typeresolution_info::visit_unary_expression (unary_expression* e)
{
  if (t == pe_stats || t == pe_string)
    invalid (e->tok, t);

  t = pe_long;
  e->operand->visit (this);

  if (e->type == pe_unknown)
    {
      e->type = pe_long;
      resolved (e->tok, e->type);
    }
}


void
typeresolution_info::visit_ternary_expression (ternary_expression* e)
{
  exp_type sub_type = t;

  t = pe_long;
  e->cond->visit (this);

  // Infer types across the true/false arms of the ternary expression.

  if (sub_type == pe_unknown && e->type != pe_unknown)
    sub_type = e->type;
  t = sub_type;
  e->truevalue->visit (this);
  t = sub_type;
  e->falsevalue->visit (this);

  if ((sub_type == pe_unknown) && (e->type != pe_unknown))
    ; // already resolved
  else if ((sub_type != pe_unknown) && (e->type == pe_unknown))
    {
      e->type = sub_type;
      resolved (e->tok, e->type);
    }
  else if ((sub_type == pe_unknown) && (e->truevalue->type != pe_unknown))
    {
      e->type = e->truevalue->type;
      resolved (e->tok, e->type);
    }
  else if ((sub_type == pe_unknown) && (e->falsevalue->type != pe_unknown))
    {
      e->type = e->falsevalue->type;
      resolved (e->tok, e->type);
    }
  else if (e->type != sub_type)
    mismatch (e->tok, sub_type, e->type);
}


template <class Referrer, class Referent>
void resolve_2types (Referrer* referrer, Referent* referent,
                    typeresolution_info* r, exp_type t, bool accept_unknown = false)
{
  exp_type& re_type = referrer->type;
  const token* re_tok = referrer->tok;
  exp_type& te_type = referent->type;
  const token* te_tok = referent->tok;

  if (t != pe_unknown && re_type == t && re_type == te_type)
    ; // do nothing: all three e->types in agreement
  else if (t == pe_unknown && re_type != pe_unknown && re_type == te_type)
    ; // do nothing: two known e->types in agreement
  else if (re_type != pe_unknown && te_type != pe_unknown && re_type != te_type)
    r->mismatch (re_tok, re_type, te_type);
  else if (re_type != pe_unknown && t != pe_unknown && re_type != t)
    r->mismatch (re_tok, re_type, t);
  else if (te_type != pe_unknown && t != pe_unknown && te_type != t)
    r->mismatch (te_tok, te_type, t);
  else if (re_type == pe_unknown && t != pe_unknown)
    {
      // propagate from upstream
      re_type = t;
      r->resolved (re_tok, re_type);
      // catch re_type/te_type mismatch later
    }
  else if (re_type == pe_unknown && te_type != pe_unknown)
    {
      // propagate from referent
      re_type = te_type;
      r->resolved (re_tok, re_type);
      // catch re_type/t mismatch later
    }
  else if (re_type != pe_unknown && te_type == pe_unknown)
    {
      // propagate to referent
      te_type = re_type;
      r->resolved (te_tok, te_type);
      // catch re_type/t mismatch later
    }
  else if (! accept_unknown)
    r->unresolved (re_tok);
}


void
typeresolution_info::visit_symbol (symbol* e)
{
  assert (e->referent != 0);
  resolve_2types (e, e->referent, this, t);
}


void
typeresolution_info::visit_target_symbol (target_symbol* e)
{
  // This occurs only if a target symbol was not resolved over in
  // tapset.cxx land, that error was properly suppressed, and the
  // later unused-expression-elimination pass didn't get rid of it
  // either.  So we have a target symbol that is believed to be of
  // genuine use, yet unresolved by the provider.
  if (e->saved_conversion_error)
    throw (* (e->saved_conversion_error));
  else
    throw semantic_error("unresolved target-symbol expression", e->tok);
}


void
typeresolution_info::visit_arrayindex (arrayindex* e)
{

  symbol *array = NULL;
  hist_op *hist = NULL;
  classify_indexable(e->base, array, hist);
  
  // Every hist_op has type [int]:int, that is to say, every hist_op
  // is a pseudo-one-dimensional integer array type indexed by
  // integers (bucket numbers).

  if (hist)
    {
      if (e->indexes.size() != 1)
	unresolved (e->tok);
      t = pe_long;
      e->indexes[0]->visit (this);
      if (e->indexes[0]->type != pe_long)
	unresolved (e->tok);
      hist->visit (this);
      if (e->type != pe_long)
	{
	  e->type = pe_long;
	  resolved (e->tok, pe_long);
	}
      return;
    }

  // Now we are left with "normal" map inference and index checking.

  assert (array);
  assert (array->referent != 0);
  resolve_2types (e, array->referent, this, t);

  // now resolve the array indexes

  // if (e->referent->index_types.size() == 0)
  //   // redesignate referent as array
  //   e->referent->set_arity (e->indexes.size ());

  if (e->indexes.size() != array->referent->index_types.size())
    unresolved (e->tok); // symbol resolution should prevent this
  else for (unsigned i=0; i<e->indexes.size(); i++)
    {
      expression* ee = e->indexes[i];
      exp_type& ft = array->referent->index_types [i];
      t = ft;
      ee->visit (this);
      exp_type at = ee->type;

      if ((at == pe_string || at == pe_long) && ft == pe_unknown)
        {
          // propagate to formal type
          ft = at;
          resolved (array->referent->tok, ft);
          // uses array decl as there is no token for "formal type"
        }
      if (at == pe_stats)
        invalid (ee->tok, at);
      if (ft == pe_stats)
        invalid (ee->tok, ft);
      if (at != pe_unknown && ft != pe_unknown && ft != at)
        mismatch (e->tok, at, ft);
      if (at == pe_unknown)
	  unresolved (ee->tok);
    }
}


void
typeresolution_info::visit_functioncall (functioncall* e)
{
  assert (e->referent != 0);

  resolve_2types (e, e->referent, this, t, true); // accept unknown type 

  if (e->type == pe_stats)
    invalid (e->tok, e->type);

  // now resolve the function parameters
  if (e->args.size() != e->referent->formal_args.size())
    unresolved (e->tok); // symbol resolution should prevent this
  else for (unsigned i=0; i<e->args.size(); i++)
    {
      expression* ee = e->args[i];
      exp_type& ft = e->referent->formal_args[i]->type;
      const token* fe_tok = e->referent->formal_args[i]->tok;
      t = ft;
      ee->visit (this);
      exp_type at = ee->type;
      
      if (((at == pe_string) || (at == pe_long)) && ft == pe_unknown)
        {
          // propagate to formal arg
          ft = at;
          resolved (e->referent->formal_args[i]->tok, ft);
        }
      if (at == pe_stats)
        invalid (e->tok, at);
      if (ft == pe_stats)
        invalid (fe_tok, ft);
      if (at != pe_unknown && ft != pe_unknown && ft != at)
        mismatch (e->tok, at, ft);
      if (at == pe_unknown)
        unresolved (e->tok);
    }
}


void
typeresolution_info::visit_block (block* e)
{
  for (unsigned i=0; i<e->statements.size(); i++)
    {
      try 
	{
	  t = pe_unknown;
	  e->statements[i]->visit (this);
	}
      catch (const semantic_error& e)
	{
	  session.print_error (e);
        }
    }
}


void
typeresolution_info::visit_embeddedcode (embeddedcode*)
{
}


void
typeresolution_info::visit_if_statement (if_statement* e)
{
  t = pe_long;
  e->condition->visit (this);

  t = pe_unknown;
  e->thenblock->visit (this);

  if (e->elseblock)
    {
      t = pe_unknown;
      e->elseblock->visit (this);
    }
}


void
typeresolution_info::visit_for_loop (for_loop* e)
{
  t = pe_unknown;
  if (e->init) e->init->visit (this);
  t = pe_long;
  e->cond->visit (this);
  t = pe_unknown;
  if (e->incr) e->incr->visit (this);  
  t = pe_unknown;
  e->block->visit (this);  
}


void
typeresolution_info::visit_foreach_loop (foreach_loop* e)
{
  // See also visit_arrayindex.
  // This is different in that, being a statement, we can't assign
  // a type to the outer array, only propagate to/from the indexes

  // if (e->referent->index_types.size() == 0)
  //   // redesignate referent as array
  //   e->referent->set_arity (e->indexes.size ());

  symbol *array = NULL;
  hist_op *hist = NULL;
  classify_indexable(e->base, array, hist);

  if (hist)
    {      
      if (e->indexes.size() != 1)
	unresolved (e->tok);
      t = pe_long;
      e->indexes[0]->visit (this);
      if (e->indexes[0]->type != pe_long)
	unresolved (e->tok);
      hist->visit (this);
    }
  else
    {
      assert (array);  
      if (e->indexes.size() != array->referent->index_types.size())
	unresolved (e->tok); // symbol resolution should prevent this
      else for (unsigned i=0; i<e->indexes.size(); i++)
	{
	  expression* ee = e->indexes[i];
	  exp_type& ft = array->referent->index_types [i];
	  t = ft;
	  ee->visit (this);
	  exp_type at = ee->type;
	  
	  if ((at == pe_string || at == pe_long) && ft == pe_unknown)
	    {
	      // propagate to formal type
	      ft = at;
	      resolved (array->referent->tok, ft);
	      // uses array decl as there is no token for "formal type"
	    }
	  if (at == pe_stats)
	    invalid (ee->tok, at);
	  if (ft == pe_stats)
	    invalid (ee->tok, ft);
	  if (at != pe_unknown && ft != pe_unknown && ft != at)
	    mismatch (e->tok, at, ft);
	  if (at == pe_unknown)
	    unresolved (ee->tok);
	}
    }

  if (e->limit)
    {
      t = pe_long;
      e->limit->visit (this);
    }

  t = pe_unknown;
  e->block->visit (this);  
}


void
typeresolution_info::visit_null_statement (null_statement*)
{
}


void
typeresolution_info::visit_expr_statement (expr_statement* e)
{
  t = pe_unknown;
  e->value->visit (this);
}


struct delete_statement_typeresolution_info: 
  public throwing_visitor
{
  typeresolution_info *parent;
  delete_statement_typeresolution_info (typeresolution_info *p):
    throwing_visitor ("invalid operand of delete expression"),
    parent (p)
  {}

  void visit_arrayindex (arrayindex* e)
  {
    parent->visit_arrayindex (e);
  }
  
  void visit_symbol (symbol* e)
  {
    exp_type ignored = pe_unknown;
    assert (e->referent != 0);    
    resolve_2types (e, e->referent, parent, ignored);
  }
};


void
typeresolution_info::visit_delete_statement (delete_statement* e)
{
  delete_statement_typeresolution_info di (this);
  t = pe_unknown;
  e->value->visit (&di);
}


void
typeresolution_info::visit_next_statement (next_statement*)
{
}


void
typeresolution_info::visit_break_statement (break_statement*)
{
}


void
typeresolution_info::visit_continue_statement (continue_statement*)
{
}


void
typeresolution_info::visit_array_in (array_in* e)
{
  // all unary operators only work on numerics
  exp_type t1 = t;
  t = pe_unknown; // array value can be anything
  e->operand->visit (this);

  if (t1 == pe_unknown && e->type != pe_unknown)
    ; // already resolved
  else if (t1 == pe_string || t1 == pe_stats)
    mismatch (e->tok, t1, pe_long);
  else if (e->type == pe_unknown)
    {
      e->type = pe_long;
      resolved (e->tok, e->type);
    }
}


void
typeresolution_info::visit_return_statement (return_statement* e)
{
  // This is like symbol, where the referent is
  // the return value of the function.

  // translation pass will print error 
  if (current_function == 0)
    return;

  exp_type& e_type = current_function->type;
  t = current_function->type;
  e->value->visit (this);

  if (e_type != pe_unknown && e->value->type != pe_unknown
      && e_type != e->value->type)
    mismatch (current_function->tok, e_type, e->value->type);
  if (e_type == pe_unknown && 
      (e->value->type == pe_long || e->value->type == pe_string))
    {
      // propagate non-statistics from value
      e_type = e->value->type;
      resolved (current_function->tok, e->value->type);
    }
  if (e->value->type == pe_stats)
    invalid (e->value->tok, e->value->type);
}

void 
typeresolution_info::visit_print_format (print_format* e)
{
  size_t unresolved_args = 0;

  if (e->hist)
    {
      e->hist->visit(this);
    }

  else if (e->print_with_format)
    {
      // If there's a format string, we can do both inference *and*
      // checking.

      // First we extract the subsequence of formatting components
      // which are conversions (not just literal string components)

      std::vector<print_format::format_component> components;
      for (size_t i = 0; i < e->components.size(); ++i)
	{
	  if (e->components[i].type == print_format::conv_unspecified)
	    throw semantic_error ("Unspecified conversion in print operator format string",
				  e->tok);
	  else if (e->components[i].type == print_format::conv_literal 
		   || e->components[i].type == print_format::conv_size)
	    continue;
	  components.push_back(e->components[i]);
	}

      // Then we check that the number of conversions and the number
      // of args agree.

      if (components.size() != e->args.size())
	throw semantic_error ("Wrong number of args to formatted print operator",
			      e->tok);

      // Then we check that the types of the conversions match the types
      // of the args.
      for (size_t i = 0; i < components.size(); ++i)
	{
	  exp_type wanted = pe_unknown;

	  switch (components[i].type)
	    {
	    case print_format::conv_unspecified:
	    case print_format::conv_literal:
	    case print_format::conv_size:
	      assert (false);
	      break;

	    case print_format::conv_signed_decimal:
	    case print_format::conv_unsigned_decimal:
	    case print_format::conv_unsigned_octal:
	    case print_format::conv_unsigned_ptr:
	    case print_format::conv_unsigned_uppercase_hex:
	    case print_format::conv_unsigned_lowercase_hex:
	    case print_format::conv_binary:
	      wanted = pe_long;
	      break;

	    case print_format::conv_string:
	      wanted = pe_string;
	      break;
	    }

	  assert (wanted != pe_unknown);

	  t = wanted;
	  e->args[i]->visit (this);

	  if (e->args[i]->type == pe_unknown)
	    {
	      e->args[i]->type = wanted;
	      resolved (e->args[i]->tok, wanted);
	    }
	  else if (e->args[i]->type != wanted)
	    {
	      mismatch (e->args[i]->tok, e->args[i]->type, wanted);
	    }
	}
    }
  else
    {
      // Without a format string, the best we can do is require that
      // each argument resolve to a concrete type.
      for (size_t i = 0; i < e->args.size(); ++i)
	{
	  t = pe_unknown;
	  e->args[i]->visit (this);
	  if (e->args[i]->type == pe_unknown)
	    {
	      unresolved (e->args[i]->tok);
	      ++unresolved_args;
	    }
	}
    }
  
  if (unresolved_args == 0)
    {
      if (e->type == pe_unknown)
	{
	  if (e->print_to_stream)
	    e->type = pe_long;
	  else
	    e->type = pe_string;      
	  resolved (e->tok, e->type);
	}
    }
  else
    {
      e->type = pe_unknown;
      unresolved (e->tok);
    }
}


void 
typeresolution_info::visit_stat_op (stat_op* e)
{
  t = pe_stats;
  e->stat->visit (this);
  if (e->type == pe_unknown)
    {
      e->type = pe_long;
      resolved (e->tok, e->type);
    }
  else if (e->type != pe_long)
    mismatch (e->tok, e->type, pe_long);
}

void 
typeresolution_info::visit_hist_op (hist_op* e)
{
  t = pe_stats;
  e->stat->visit (this);
}


void
typeresolution_info::unresolved (const token* tok)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      stringstream msg;
      string nm = (current_function ? current_function->name :
                   current_probe ? current_probe->name :
                   "probe condition");
      msg << nm + " with unresolved type";
      session.print_error (semantic_error (msg.str(), tok));
    }
}


void
typeresolution_info::invalid (const token* tok, exp_type pe)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      stringstream msg;
      string nm = (current_function ? current_function->name :
                   current_probe ? current_probe->name :
                   "probe condition");
      if (tok && tok->type == tok_operator)
        msg << nm + " uses invalid operator";
      else
        msg << nm + " with invalid type " << pe;
      session.print_error (semantic_error (msg.str(), tok));
    }
}


void
typeresolution_info::mismatch (const token* tok, exp_type t1, exp_type t2)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      stringstream msg;
      string nm = (current_function ? current_function->name :
                   current_probe ? current_probe->name :
                   "probe condition");
      msg << nm + " with type mismatch (" << t1 << " vs. " << t2 << ")";
      session.print_error (semantic_error (msg.str(), tok));
    }
}


void
typeresolution_info::resolved (const token*, exp_type)
{
  num_newly_resolved ++;
}

