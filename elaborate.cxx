// elaboration functions
// Copyright (C) 2005-2012 Red Hat Inc.
// Copyright (C) 2008 Intel Corporation
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "elaborate.h"
#include "translate.h"
#include "parse.h"
#include "tapsets.h"
#include "session.h"
#include "util.h"
#include "task_finder.h"

extern "C" {
#include <sys/utsname.h>
#include <fnmatch.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
}

#include <algorithm>
#include <fstream>
#include <map>
#include <cassert>
#include <set>
#include <vector>
#include <algorithm>
#include <iterator>
#include <climits>


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



derived_probe::derived_probe (probe *p, probe_point *l, bool rewrite_loc):
  base (p), base_pp(l), sdt_semaphore_addr(0), session_index((unsigned)-1)
{
  assert (p);
  this->tok = p->tok;
  this->privileged = p->privileged;
  this->body = deep_copy_visitor::deep_copy(p->body);

  assert (l);
  // make a copy for subclasses which want to rewrite the location
  if (rewrite_loc)
    l = new probe_point(*l);
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
derived_probe::collect_derivation_chain (std::vector<probe*> &probes_list)
{
  probes_list.push_back(this);
  base->collect_derivation_chain(probes_list);
}


void
derived_probe::collect_derivation_pp_chain (std::vector<probe_point*> &pp_list)
{
  pp_list.push_back(base_pp);
  base->collect_derivation_pp_chain(pp_list);
}


string
derived_probe::derived_locations ()
{
  ostringstream o;
  vector<probe_point*> reference_point;
  collect_derivation_pp_chain(reference_point);
  for(unsigned i=0; i<reference_point.size(); ++i)
    o << " from: " << reference_point[i]->str(false); // no ?,!,etc
  return o.str();
}


probe_point*
derived_probe::sole_location () const
{
  if (locations.size() == 0 || locations.size() > 1)
    throw semantic_error (ngettext("derived_probe with no locations",
                                   "derived_probe with no locations",
                                   locations.size()), this->tok);
  else
    return locations[0];
}


probe_point*
derived_probe::script_location () const
{
  const probe* p = almost_basest();
  const probe_alias *a = p->get_alias();
  const vector<probe_point*>& locs = a ? a->alias_names : p->locations;
  if (locs.size() == 0 || locs.size() > 1)
    throw semantic_error (ngettext("derived_probe with no locations",
                                   "derived_probe with too many locations",
                                   locs.size()), this->tok);
  else
    return locs[0];
}


void
derived_probe::emit_privilege_assertion (translator_output* o)
{
  // Emit code which will cause compilation to fail if it is compiled in
  // unprivileged mode.
  o->newline() << "#if ! STP_PRIVILEGE_CONTAINS (STP_PRIVILEGE, STP_PR_STAPDEV) && \\";
  o->newline() << "    ! STP_PRIVILEGE_CONTAINS (STP_PRIVILEGE, STP_PR_STAPSYS)";
  o->newline() << "#error Internal Error: Probe ";
  probe::printsig (o->line());
  o->line()    << " generated in --unprivileged mode";
  o->newline() << "#endif";
}


void
derived_probe::emit_process_owner_assertion (translator_output* o)
{
  // Emit code which will abort should the current target not belong to the
  // user in unprivileged mode.
  o->newline() << "#if ! STP_PRIVILEGE_CONTAINS (STP_PRIVILEGE, STP_PR_STAPDEV) && \\";
  o->newline() << "    ! STP_PRIVILEGE_CONTAINS (STP_PRIVILEGE, STP_PR_STAPSYS)";
  o->newline(1)  << "if (! is_myproc ()) {";
  o->newline(1)  << "snprintf(c->error_buffer, sizeof(c->error_buffer),";
  o->newline()   << "         \"Internal Error: Process %d does not belong to user %d in probe %s in --unprivileged mode\",";
  o->newline()   << "         current->tgid, _stp_uid, c->probe_point);";
  o->newline()   << "c->last_error = c->error_buffer;";
  // NB: since this check occurs before probe locking, its exit should
  // not be a "goto out", which would attempt unlocking.
  o->newline()   << "return;";
  o->newline(-1) << "}";
  o->newline(-1) << "#endif";
}

void
derived_probe::print_dupe_stamp_unprivileged(ostream& o)
{
  o << _("unprivileged users: authorized") << endl;
}

void
derived_probe::print_dupe_stamp_unprivileged_process_owner(ostream& o)
{
  o << _("unprivileged users: authorized for process owner") << endl;
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


// NB: these are only used in the probe point name components, where
// only "*" is permitted.
//
// Within module("bar"), function("foo"), process("baz") strings, real
// wildcards are permitted too. See also util.h:contains_glob_chars

static bool
isglob(string const & str)
{
  return(str.find('*') != str.npos);
}

static bool
isdoubleglob(string const & str)
{
  return(str.find("**") != str.npos);
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

match_node::match_node() :
  privilege(privilege_t (pr_stapdev | pr_stapsys))
{
}

match_node *
match_node::bind(match_key const & k)
{
  if (k.name == "*")
    throw semantic_error(_("invalid use of wildcard probe point component"));

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
  ends.push_back (e);
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

match_node *
match_node::bind_privilege(privilege_t p)
{
  privilege = p;
  return this;
}

void
match_node::find_and_build (systemtap_session& s,
                            probe* p, probe_point *loc, unsigned pos,
                            vector<derived_probe *>& results)
{
  assert (pos <= loc->components.size());
  if (pos == loc->components.size()) // matched all probe point components so far
    {
      if (ends.empty())
        {
          string alternatives;
          for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
            alternatives += string(" ") + i->first.str();

          throw semantic_error (_F("probe point truncated at position %s (follow: %s)",
                                   lex_cast(pos).c_str(), alternatives.c_str()),
                                   loc->components.back()->tok);
        }

      if (! pr_contains (privilege, s.privilege))
	{
          throw semantic_error (_F("probe point is not allowed for --privilege=%s",
				   pr_name (s.privilege)),
                                loc->components.back()->tok);
	}

      map<string, literal *> param_map;
      for (unsigned i=0; i<pos; i++)
        param_map[loc->components[i]->functor] = loc->components[i]->arg;
      // maybe 0

      // Iterate over all bound builders
      for (unsigned k=0; k<ends.size(); k++) 
        {
          derived_probe_builder *b = ends[k];
          b->build (s, p, loc, param_map, results);
        }
    }
  else if (isdoubleglob(loc->components[pos]->functor)) // ** wildcard?
    {
      unsigned int num_results = results.size();

      // When faced with "foo**bar", we try "foo*bar" and "foo*.**bar"

      const probe_point::component *comp = loc->components[pos];
      const string &functor = comp->functor;
      size_t glob_start = functor.find("**");
      size_t glob_end = functor.find_first_not_of('*', glob_start);
      const string prefix = functor.substr(0, glob_start);
      const string suffix = ((glob_end != string::npos) ?
                             functor.substr(glob_end) : "");

      // Synthesize "foo*bar"
      probe_point *simple_pp = new probe_point(*loc);
      probe_point::component *simple_comp = new probe_point::component(*comp);
      simple_comp->functor = prefix + "*" + suffix;
      simple_pp->components[pos] = simple_comp;
      try
        {
          find_and_build (s, p, simple_pp, pos, results);
        }
      catch (const semantic_error& e)
        {
          // Ignore semantic_errors, but cleanup
          delete simple_pp;
          delete simple_comp;
        }

      // Synthesize "foo*.**bar"
      // NB: any component arg should attach to the latter part only
      probe_point *expanded_pp = new probe_point(*loc);
      probe_point::component *expanded_comp_pre = new probe_point::component(*comp);
      expanded_comp_pre->functor = prefix + "*";
      expanded_comp_pre->arg = NULL;
      probe_point::component *expanded_comp_post = new probe_point::component(*comp);
      expanded_comp_post->functor = "**" + suffix;
      expanded_pp->components[pos] = expanded_comp_pre;
      expanded_pp->components.insert(expanded_pp->components.begin() + pos + 1,
                                     expanded_comp_post);
      try
        {
          find_and_build (s, p, expanded_pp, pos, results);
        }
      catch (const semantic_error& e)
        {
          // Ignore semantic_errors, but cleanup
          delete expanded_pp;
          delete expanded_comp_pre;
          delete expanded_comp_post;
        }

      if (! loc->optional && num_results == results.size())
        {
          // We didn't find any wildcard matches (since the size of
          // the result vector didn't change).  Throw an error.
          string alternatives;
          for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
            alternatives += string(" ") + i->first.str();

          throw semantic_error(_F("probe point mismatch at position %s (alternatives: %s)",
                                  lex_cast(pos).c_str(), alternatives.c_str()), comp->tok);
        }
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

          assert_no_interrupts();

	  if (match.globmatch(subkey))
	    {
	      if (s.verbose > 2)
                clog << _F("wildcard '%s' matched '%s'",
                           loc->components[pos]->functor.c_str(),
                           subkey.name.c_str()) << endl;

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

          throw semantic_error(_F("probe point mismatch at position %s %s didn't find any wildcard matches",
                                  lex_cast(pos).c_str(),
                                  (alternatives == "" ? "" : _(" (alternatives: ") +
                                  alternatives + ")").c_str()), loc->components[pos]->tok);
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

          throw semantic_error(_F("probe point mismatch at position %s %s", lex_cast(pos).c_str(),
                                  (alternatives == "" ? "" : (_(" (alternatives:") + alternatives +
                                  ")").c_str())), loc->components[pos]->tok);
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
  for (unsigned k=0; k<ends.size(); k++) 
    {
      derived_probe_builder *b = ends[k];
      b->build_no_more (s);
    }
}

void
match_node::dump (systemtap_session &s, const string &name)
{
  // Dump this node, if it is complete.
  for (unsigned k=0; k<ends.size(); k++)
    {
      // Don't print aliases at all (for now) until we can figure out how to determine whether
      // the probes they resolve to are ok in unprivileged mode.
      if (ends[k]->is_alias ())
	continue;

      // In unprivileged mode, don't show the probes which are not allowed for unprivileged
      // users.
      if (pr_contains (privilege, s.privilege))
	{
	  cout << name << endl;
	  break; // we need only print one instance.
	}
    }

  // Recursively dump the children of this node
  string dot;
  if (! name.empty ())
    dot = ".";
  for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
    {
      i->second->dump (s, name + dot + i->first.str());
    }
}


// ------------------------------------------------------------------------
// Alias probes
// ------------------------------------------------------------------------

struct alias_derived_probe: public derived_probe
{
  alias_derived_probe (probe* base, probe_point *l, const probe_alias *a):
    derived_probe (base, l), alias(a) {}

  void upchuck () { throw semantic_error (_("inappropriate"), this->tok); }

  // Alias probes are immediately expanded to other derived_probe
  // types, and are not themselves emitted or listed in
  // systemtap_session.probes

  void join_group (systemtap_session&) { upchuck (); }

  virtual const probe_alias *get_alias () const { return alias; }

private:
  const probe_alias *alias; // Used to check for recursion
};

probe*
probe::create_alias(probe_point* l, probe_point* a)
{
  vector<probe_point*> aliases(1, a);
  probe_alias* p = new probe_alias(aliases);
  p->tok = tok;
  p->locations.push_back(l);
  p->body = body;
  p->privileged = privileged;
  p->epilogue_style = false;
  return new alias_derived_probe(this, l, p);
}


void
alias_expansion_builder::build(systemtap_session & sess,
			       probe * use,
			       probe_point * location,
			       std::map<std::string, literal *> const &,
			       vector<derived_probe *> & finished_results)
{
  // Don't build the alias expansion if infinite recursion is detected.
  if (checkForRecursiveExpansion (use)) {
    stringstream msg;
    msg << _F("Recursive loop in alias expansion of %s at %s",
              lex_cast(*location).c_str(), lex_cast(location->components.front()->tok->location).c_str());
    // semantic_errors thrown here are ignored.
    sess.print_error (semantic_error (msg.str()));
    return;
  }

  // We're going to build a new probe and wrap it up in an
  // alias_expansion_probe so that the expansion loop recognizes it as
  // such and re-expands its expansion.

  alias_derived_probe * n = new alias_derived_probe (use, location /* soon overwritten */, this->alias);
  n->body = new block();

  // The new probe gets a deep copy of the location list of
  // the alias (with incoming condition joined)
  n->locations.clear();
  for (unsigned i=0; i<alias->locations.size(); i++)
    {
      probe_point *pp = new probe_point(*alias->locations[i]);
      pp->condition = add_condition (pp->condition, location->condition);
      n->locations.push_back(pp);
    }

  // the token location of the alias,
  n->tok = location->components.front()->tok;

  // and statements representing the concatenation of the alias'
  // body with the use's.
  //
  // NB: locals are *not* copied forward, from either alias or
  // use. The expansion should have its locals re-inferred since
  // there's concatenated code here and we only want one vardecl per
  // resulting variable.

  if (alias->epilogue_style)
    n->body = new block (use->body, alias->body);
  else
    n->body = new block (alias->body, use->body);

  unsigned old_num_results = finished_results.size();
  derive_probes (sess, n, finished_results, location->optional);

  // Check whether we resolved something. If so, put the
  // whole library into the queue if not already there.
  if (finished_results.size() > old_num_results)
    {
      stapfile *f = alias->tok->location.file;
      if (find (sess.files.begin(), sess.files.end(), f)
	  == sess.files.end())
	sess.files.push_back (f);
    }
}

bool
alias_expansion_builder::checkForRecursiveExpansion (probe *use)
{
  // Collect the derivation chain of this probe.
  vector<probe*>derivations;
  use->collect_derivation_chain (derivations);

  // Check all probe points in the alias expansion against the currently-being-expanded probe point
  // of each of the probes in the derivation chain, looking for a match. This
  // indicates infinite recursion.
  // The first element of the derivation chain will be the derived_probe representing 'use', so
  // start the search with the second element.
  assert (derivations.size() > 0);
  assert (derivations[0] == use);
  for (unsigned d = 1; d < derivations.size(); ++d) {
    if (use->get_alias() == derivations[d]->get_alias())
      return true; // recursion detected
  }
  return false;
}


// ------------------------------------------------------------------------
// Pattern matching
// ------------------------------------------------------------------------

static unsigned max_recursion = 100;

struct
recursion_guard
{
  unsigned & i;
  recursion_guard(unsigned & i) : i(i)
    {
      if (i > max_recursion)
	throw semantic_error(_("recursion limit reached"));
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
      assert_no_interrupts();

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
          try
	    {
	      s.pattern_root->find_and_build (s, p, loc, 0, dps); // <-- actual derivation!
	    }
          catch (const semantic_error& e)
	    {
              if (!loc->optional)
                throw semantic_error(e);
              else /* tolerate failure for optional probe */
	        continue;
	    }

          loc->optional = old_loc_opt;
          unsigned num_atend = dps.size();

          if (! (loc->optional||optional) && // something required, but
              num_atbegin == num_atend) // nothing new derived!
            throw semantic_error (_("no match"));

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
	  //only output in listing if -vv is supplied
          if (!s.listing_mode || (s.listing_mode && s.verbose > 1))
            {
              // XXX: prefer not to print_error at every nest/unroll level
              semantic_error* er = new semantic_error (_("while resolving probe point"),
                                                       loc->components[0]->tok);
              er->chain = & e;
              s.print_error (* er);
              delete er;
            }
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

  symbol_fetcher (symbol *&sym): sym(sym)
  {}

  void visit_symbol (symbol* e)
  {
    sym = e;
  }

  void visit_target_symbol (target_symbol* e)
  {
    sym = e;
  }

  void visit_arrayindex (arrayindex* e)
  {
    e->base->visit_indexable (this);
  }

  void visit_cast_op (cast_op* e)
  {
    sym = e;
  }

  void throwone (const token* t)
  {
    throw semantic_error (_("Expecting symbol or array index expression"), t);
  }
};

symbol *
get_symbol_within_expression (expression *e)
{
  symbol *sym = NULL;
  symbol_fetcher fetcher(sym);
  e->visit (&fetcher);
  return sym; // NB: may be null!
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
	  throw semantic_error(_("Assignment to read-only histogram bucket"), e->tok);
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
                    string err = _F("variable '%s' modified during 'foreach' iteration",
                                    v->name.c_str());
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
                string err = _F("function call modifies var '%s' during 'foreach' iteration",
                                m->name.c_str());
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
		semantic_error se(_F("multiple histogram types declared on '%s'", sym->name.c_str()), e->tok);
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

  for (map<string,functiondecl*>::iterator it = sess.functions.begin(); it != sess.functions.end(); it++)
    it->second->body->visit (&sdc);

  for (unsigned i = 0; i < sess.probes.size(); ++i)
    sess.probes[i]->body->visit (&sdc);

  for (unsigned i = 0; i < sess.globals.size(); ++i)
    {
      vardecl *v = sess.globals[i];
      if (v->type == pe_stats)
	{

	  if (sess.stat_decls.find(v->name) == sess.stat_decls.end())
	    {
              semantic_error se(_F("unable to infer statistic parameters for global '%s'", v->name.c_str()));
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

  for (map<string,functiondecl*>::iterator it = sess.functions.begin(); it != sess.functions.end(); it++)
    {
      functiondecl * fn = it->second;
      if (fn->body)
	{
	  set<vardecl *> * m = new set<vardecl *>();
	  mutated_var_collector mc (m);
	  fn->body->visit (&mc);
	  fmv[fn] = m;
	}
    }

  for (map<string,functiondecl*>::iterator it = sess.functions.begin(); it != sess.functions.end(); it++)
    {
      functiondecl * fn = it->second;
      if (fn->body) fn->body->visit (&chk);
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
          varuse_collecting_visitor vut(sess);
          e->visit (& vut);

          if (! vut.written.empty())
            {
              string err = (_("probe condition must not modify any variables"));
              sess.print_error (semantic_error (err, e->tok));
            }
          else if (vut.embedded_seen)
            {
              sess.print_error (semantic_error (_("probe condition must not include impure embedded-C"), e->tok));
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
          p->body = new block (ifs, p->body);
        }
    }

  return sess.num_errors();
}

// ------------------------------------------------------------------------


// Simple visitor that just goes through all embedded code blocks that
// are available at the end  all the optimizations to register any
// relevant pragmas or other indicators found, so that session flags can
// be set that can be inspected at translation time to trigger any
// necessary initialization of code needed by the embedded code functions.

// This is only for pragmas that don't have any other side-effect than
// needing some initialization at module init time. Currently handles
// /* pragma:vma */ /* pragma:unwind */ /* pragma:symbol */

// /* pragma:uprobes */ is handled during the typeresolution_info pass.
// /* pure */, /* unprivileged */. /* myproc-unprivileged */ and /* guru */
// are handled by the varuse_collecting_visitor.

struct embeddedcode_info: public functioncall_traversing_visitor
{
protected:
  systemtap_session& session;

public:
  embeddedcode_info (systemtap_session& s): session(s) { }

  void visit_embeddedcode (embeddedcode* c)
  {
    if (! vma_tracker_enabled(session)
	&& c->code.find("/* pragma:vma */") != string::npos)
      {
	enable_vma_tracker(session);
	if (session.verbose > 2)
          clog << _F("Turning on task_finder vma_tracker, pragma:vma found in %s",
                     current_function->name.c_str()) << endl;
      }

    if (! session.need_unwind
	&& c->code.find("/* pragma:unwind */") != string::npos)
      {
	if (session.verbose > 2)
	  clog << _F("Turning on unwind support, pragma:unwind found in %s",
		    current_function->name.c_str()) << endl;
	session.need_unwind = true;
      }

    if (! session.need_symbols
	&& c->code.find("/* pragma:symbols */") != string::npos)
      {
	if (session.verbose > 2)
	  clog << _F("Turning on symbol data collecting, pragma:symbols found in %s",
		    current_function->name.c_str()) << endl;
	session.need_symbols = true;
      }
  }
};

void embeddedcode_info_pass (systemtap_session& s)
{
  embeddedcode_info eci (s);
  for (unsigned i=0; i<s.probes.size(); i++)
    s.probes[i]->body->visit (& eci);
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
      assert_no_interrupts();
      stapfile* dome = s.files[i];

      // Pass 1: add globals and functions to systemtap-session master list,
      //         so the find_* functions find them
      //
      // NB: tapset global/function definitions may duplicate or conflict
      // with those already in s.globals/functions.  We need to deconflict
      // here.

      for (unsigned i=0; i<dome->globals.size(); i++)
        {
          vardecl* g = dome->globals[i];
          for (unsigned j=0; j<s.globals.size(); j++)
            {
              vardecl* g2 = s.globals[j];
              if (g->name == g2->name)
                {
                  s.print_error (semantic_error (_("conflicting global variables"), 
                                                 g->tok, g2->tok));
                }
            }
          s.globals.push_back (g);
        }

      for (unsigned i=0; i<dome->functions.size(); i++)
        {
          functiondecl* f = dome->functions[i];
          functiondecl* f2 = s.functions[f->name];
          if (f2 && f != f2)
            {
              s.print_error (semantic_error (_("conflicting functions"), 
                                             f->tok, f2->tok));
            }
          s.functions[f->name] = f;
        }

      // NB: embeds don't conflict with each other
      for (unsigned i=0; i<dome->embeds.size(); i++)
        s.embeds.push_back (dome->embeds[i]);

      // Pass 2: derive probes and resolve any further symbols in the
      // derived results.

      for (unsigned i=0; i<dome->probes.size(); i++)
        {
          assert_no_interrupts();
          probe* p = dome->probes [i];
          vector<derived_probe*> dps;

          // much magic happens here: probe alias expansion, wildcard
          // matching, low-level derived_probe construction.
          derive_probes (s, p, dps);

          for (unsigned j=0; j<dps.size(); j++)
            {
              assert_no_interrupts();
              derived_probe* dp = dps[j];
              s.probes.push_back (dp);
              dp->join_group (s);

              try
                {
                  for (unsigned k=0; k<s.code_filters.size(); k++)
                    s.code_filters[k]->replace (dp->body);

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

      // Pass 3: process functions

      for (unsigned i=0; i<dome->functions.size(); i++)
        {
          assert_no_interrupts();
          functiondecl* fd = dome->functions[i];

          try
            {
              for (unsigned j=0; j<s.code_filters.size(); j++)
                s.code_filters[j]->replace (fd->body);

              sym.current_function = fd;
              sym.current_probe = 0;
              fd->body->visit (& sym);
            }
          catch (const semantic_error& e)
            {
              s.print_error (e);
            }
        }
    }

  // Inform all derived_probe builders that we're done with
  // all resolution, so it's time to release caches.
  s.pattern_root->build_no_more (s);

  if(s.systemtap_v_check){ 
    for(unsigned i=0;i<s.globals.size();i++){
      if(s.globals[i]->systemtap_v_conditional)
        s.print_warning(_("This global uses tapset constructs that are dependent on systemtap version"), s.globals[i]->tok);
    }

    for(map<string, functiondecl*>::const_iterator i=s.functions.begin();i != s.functions.end();++i){
      if(i->second->systemtap_v_conditional)
        s.print_warning(_("This function uses tapset constructs that are dependent on systemtap version"), i->second->tok);
    }

    for(unsigned i=0;i<s.probes.size();i++){
      vector<probe*> sysvc;
      s.probes[i]->collect_derivation_chain(sysvc);
      for(unsigned j=0;j<sysvc.size();j++){
        if(sysvc[j]->systemtap_v_conditional)
          s.print_warning(_("This probe uses tapset constructs that are dependent on systemtap version"), sysvc[j]->tok);
        if(sysvc[j]->get_alias() && sysvc[j]->get_alias()->systemtap_v_conditional)
          s.print_warning(_("This alias uses tapset constructs that are dependent on systemtap version"), sysvc[j]->get_alias()->tok);
      }
    }
  }

  return s.num_errors(); // all those print_error calls
}



// Keep unread global variables for probe end value display.
void add_global_var_display (systemtap_session& s)
{
  // Don't generate synthetic end probes when in listings mode;
  // it would clutter up the list of probe points with "end ...".
  if (s.listing_mode) return;

  varuse_collecting_visitor vut(s);

  for (unsigned i=0; i<s.probes.size(); i++)
    {
      s.probes[i]->body->visit (& vut);

      if (s.probes[i]->sole_location()->condition)
	s.probes[i]->sole_location()->condition->visit (& vut);
    }

  for (unsigned g=0; g < s.globals.size(); g++)
    {
      vardecl* l = s.globals[g];
      if ((vut.read.find (l) != vut.read.end()
           && vut.used.find (l) != vut.used.end())
          || vut.written.find (l) == vut.written.end())
	continue;

      // Don't generate synthetic end probes for unread globals
      // declared only within tapsets. (RHBZ 468139), but rather
      // only within the end-user script.

      bool tapset_global = false;
      for (size_t m=0; m < s.library_files.size(); m++)
	{
	  for (size_t n=0; n < s.library_files[m]->globals.size(); n++)
	    {
	      if (l->name == s.library_files[m]->globals[n]->name)
		{tapset_global = true; break;}
	    }
	}
      if (tapset_global)
	continue;

      probe_point::component* c = new probe_point::component("end");
      probe_point* pl = new probe_point;
      pl->components.push_back (c);

      vector<derived_probe*> dps;
      block *b = new block;
      b->tok = l->tok;

      probe* p = new probe;
      p->tok = l->tok;
      p->locations.push_back (pl);

      // Create a symbol
      symbol* g_sym = new symbol;
      g_sym->name = l->name;
      g_sym->tok = l->tok;
      g_sym->type = l->type;
      g_sym->referent = l;

      token* print_tok = new token(*l->tok);
      print_tok->type = tok_identifier;
      print_tok->content = "printf";

      print_format* pf = print_format::create(print_tok);
      pf->raw_components += l->name;

      if (l->index_types.size() == 0) // Scalar
	{
	  if (l->type == pe_stats)
	    if (strverscmp(s.compatible.c_str(), "1.4") >= 0)
	      pf->raw_components += " @count=%#d @min=%#d @max=%#d @sum=%#d @avg=%#d\\n";
	    	    else
	      pf->raw_components += " @count=%#x @min=%#x @max=%#x @sum=%#x @avg=%#x\\n";
	  else if (l->type == pe_string)
	    pf->raw_components += "=\"%#s\"\\n";
	  else
	    pf->raw_components += "=%#x\\n";
	  pf->components = print_format::string_to_components(pf->raw_components);
	  expr_statement* feb = new expr_statement;
	  feb->value = pf;
	  feb->tok = print_tok;
	  if (l->type == pe_stats)
	    {
	      struct stat_op* so [5];
	      const stat_component_type stypes[] = {sc_count, sc_min, sc_max, sc_sum, sc_average};

	      for (unsigned si = 0;
		   si < (sizeof(so)/sizeof(struct stat_op*));
		   si++)
		{
		  so[si]= new stat_op;
		  so[si]->ctype = stypes[si];
		  so[si]->type = pe_long;
		  so[si]->stat = g_sym;
		  so[si]->tok = l->tok;
		  pf->args.push_back(so[si]);
		}
	    }
	  else
	    pf->args.push_back(g_sym);

	  /* PR7053: Checking empty aggregate for global variable */
	  if (l->type == pe_stats) {
              stat_op *so= new stat_op;
              so->ctype = sc_count;
              so->type = pe_long;
              so->stat = g_sym;
              so->tok = l->tok;
              comparison *be = new comparison;
              be->op = ">";
              be->tok = l->tok;
              be->left = so;
              be->right = new literal_number(0);

              /* Create printf @count=0x0 in else block */
              print_format* pf_0 = print_format::create(print_tok);
              pf_0->raw_components += l->name;
              pf_0->raw_components += " @count=0x0\\n";
              pf_0->components = print_format::string_to_components(pf_0->raw_components);
              expr_statement* feb_else = new expr_statement;
              feb_else->value = pf_0;
              feb_else->tok = print_tok;
              if_statement *ifs = new if_statement;
              ifs->tok = l->tok;
              ifs->condition = be;
              ifs->thenblock = feb ;
              ifs->elseblock = feb_else;
              b->statements.push_back(ifs);
	    }
	  else /* other non-stat cases */
	    b->statements.push_back(feb);
	}
      else			// Array
	{
	  int idx_count = l->index_types.size();
	  symbol* idx_sym[idx_count];
	  vardecl* idx_v[idx_count];
	  // Create a foreach loop
	  foreach_loop* fe = new foreach_loop;
	  fe->sort_direction = -1; // imply decreasing sort on value
	  fe->sort_column = 0;     // as in   foreach ([a,b,c] in array-) { }
	  fe->value = NULL;
	  fe->limit = NULL;
	  fe->tok = l->tok;

	  // Create indices for the foreach loop
	  for (int i=0; i < idx_count; i++)
	    {
	      char *idx_name;
	      if (asprintf (&idx_name, "idx%d", i) < 0) {
               delete pf;
               delete b;
               delete p;
               delete g_sym;
               delete fe;
               return;
	      }
	      idx_sym[i] = new symbol;
	      idx_sym[i]->name = idx_name;
	      idx_sym[i]->tok = l->tok;
	      idx_v[i] = new vardecl;
	      idx_v[i]->name = idx_name;
	      idx_v[i]->type = l->index_types[i];
	      idx_v[i]->tok = l->tok;
	      idx_sym[i]->referent = idx_v[i];
	      fe->indexes.push_back (idx_sym[i]);
	    }

	  // Create a printf for the foreach loop
	  pf->raw_components += "[";
	  for (int i=0; i < idx_count; i++)
	    {
	      if (i > 0)
		pf->raw_components += ",";
	      if (l->index_types[i] == pe_string)
		pf->raw_components += "\"%#s\"";
	      else
		pf->raw_components += "%#d";
	    }
	  pf->raw_components += "]";
	  if (l->type == pe_stats)
	    if (strverscmp(s.compatible.c_str(), "1.4") >= 0)
	      pf->raw_components += " @count=%#d @min=%#d @max=%#d @sum=%#d @avg=%#d\\n";
	    else
	      pf->raw_components += " @count=%#x @min=%#x @max=%#x @sum=%#x @avg=%#x\\n";
	  else if (l->type == pe_string)
	    pf->raw_components += "=\"%#s\"\\n";
	  else
	    pf->raw_components += "=%#x\\n";

	  // Create an index for the array
	  struct arrayindex* ai = new arrayindex;
	  ai->tok = l->tok;
	  ai->base = g_sym;

	  for (int i=0; i < idx_count; i++)
	    {
	      ai->indexes.push_back (idx_sym[i]);
	      pf->args.push_back(idx_sym[i]);
	    }
	  if (l->type == pe_stats)
	    {
	      struct stat_op* so [5];
	      const stat_component_type stypes[] = {sc_count, sc_min, sc_max, sc_sum, sc_average};

	      ai->type = pe_stats;
	      for (unsigned si = 0;
		   si < (sizeof(so)/sizeof(struct stat_op*));
		   si++)
		{
		  so[si]= new stat_op;
		  so[si]->ctype = stypes[si];
		  so[si]->type = pe_long;
		  so[si]->stat = ai;
		  so[si]->tok = l->tok;
		  pf->args.push_back(so[si]);
		}
	    }
	  else
	    {
	      // Create value for the foreach loop
	      fe->value = new symbol;
	      fe->value->name = "val";
	      fe->value->tok = l->tok;
	      pf->args.push_back(fe->value);
	    }

	  pf->components = print_format::string_to_components(pf->raw_components);
	  expr_statement* feb = new expr_statement;
	  feb->value = pf;
	  feb->tok = l->tok;
	  fe->base = g_sym;
	  fe->block = (statement*)feb;
	  b->statements.push_back(fe);
	}

      // Add created probe
      p->body = b;
      derive_probes (s, p, dps);
      for (unsigned i = 0; i < dps.size(); i++)
	{
	  derived_probe* dp = dps[i];
	  s.probes.push_back (dp);
	  dp->join_group (s);
	}
      // Repopulate symbol and type info
      symresolution_info sym (s);
      sym.current_function = 0;
      sym.current_probe = dps[0];
      dps[0]->body->visit (& sym);

      semantic_pass_types(s);
      // Mark that variable is read
      vut.read.insert (l);
    }
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
      if (rc == 0) rc = semantic_pass_optimize1 (s);
      if (rc == 0) rc = semantic_pass_types (s);
      if (rc == 0) add_global_var_display (s);
      if (rc == 0) rc = semantic_pass_optimize2 (s);
      if (rc == 0) rc = semantic_pass_vars (s);
      if (rc == 0) rc = semantic_pass_stats (s);
      if (rc == 0) embeddedcode_info_pass (s);

      if (s.num_errors() == 0 && s.probes.size() == 0 && !s.listing_mode)
        throw semantic_error (_("no probes found"));
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
      rc ++;
    }

  // PR11443
  if (s.listing_mode && s.probes.size() == 0)
    rc ++;

  return rc;
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
	  vardecl* d = find_var (array->name, e->indexes.size (), array->tok);
	  if (d)
	    array->referent = d;
	  else
	    {
	      stringstream msg;
              msg << _F("unresolved arity-%zu global array %s, missing global declaration?",
                        e->indexes.size(), array->name.c_str());
	      throw semantic_error (msg.str(), e->tok);
	    }
	}
    }
  else
    {
      assert (hist);
      hist->visit (this);
    }

  if (e->value)
    e->value->visit (this);

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

    vardecl* d = parent->find_var (e->name, -1, e->tok);
    if (d)
      e->referent = d;
    else
      throw semantic_error (_("unresolved array in delete statement"), e->tok);
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

  vardecl* d = find_var (e->name, 0, e->tok);
  if (d)
    e->referent = d;
  else
    {
      // new local
      vardecl* v = new vardecl;
      v->name = e->name;
      v->tok = e->tok;
      v->set_arity(0, e->tok);
      if (current_function)
        current_function->locals.push_back (v);
      else if (current_probe)
        current_probe->locals.push_back (v);
      else
        // must be probe-condition expression
        throw semantic_error (_("probe condition must not reference undeclared global"), e->tok);
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

      vardecl* d = find_var (array->name, e->indexes.size (), array->tok);
      if (d)
	array->referent = d;
      else
	{
	  stringstream msg;
          msg << _F("unresolved arity-%zu global array %s, missing global declaration?",
                    e->indexes.size(), array->name.c_str());
	  throw semantic_error (msg.str(), e->tok);
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
      throw semantic_error (_("probe condition must not reference function"), e->tok);
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
      msg << _F("unresolved arity-%zu function", e->args.size());
      throw semantic_error (msg.str(), e->tok);
    }
}

/*find_var will return an argument other than zero if the name matches the var
 * name ie, if the current local name matches the name passed to find_var*/
vardecl*
symresolution_info::find_var (const string& name, int arity, const token* tok)
{
  if (current_function || current_probe)
    {
      // search locals
      vector<vardecl*>& locals = (current_function ?
                                  current_function->locals :
                                  current_probe->locals);


      for (unsigned i=0; i<locals.size(); i++)
        if (locals[i]->name == name)
          {
            locals[i]->set_arity (arity, tok);
            return locals[i];
          }
    }

  // search function formal parameters (for scalars)
  if (arity == 0 && current_function)
    for (unsigned i=0; i<current_function->formal_args.size(); i++)
      if (current_function->formal_args[i]->name == name)
	{
	  // NB: no need to check arity here: formal args always scalar
	  current_function->formal_args[i]->set_arity (0, tok);
	  return current_function->formal_args[i];
	}

  // search processed globals
  for (unsigned i=0; i<session.globals.size(); i++)
    if (session.globals[i]->name == name)
      {
        if (! session.suppress_warnings)
          {
            vardecl* v = session.globals[i];
            // clog << "resolved " << *tok << " to global " << *v->tok << endl;
            if (v->tok->location.file != tok->location.file)
              {
                session.print_warning (_F("cross-file global variable reference to %s from",
                                          lex_cast(*v->tok).c_str()), tok);
              }
          }
        session.globals[i]->set_arity (arity, tok);
        return session.globals[i];
      }

  // search library globals
  for (unsigned i=0; i<session.library_files.size(); i++)
    {
      stapfile* f = session.library_files[i];
      for (unsigned j=0; j<f->globals.size(); j++)
        {
          vardecl* g = f->globals[j];
          if (g->name == name)
            {
	      g->set_arity (arity, tok);

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
  // the common path
  if (session.functions.find(name) != session.functions.end())
    {
      functiondecl* fd = session.functions[name];
      assert (fd->name == name);
      if (fd->formal_args.size() == arity)
        return fd;

      session.print_warning (_F("mismatched arity-%zu function found", fd->formal_args.size()),
                             fd->tok);
      // and some semantic_error will shortly follow
    }

  // search library functions
  for (unsigned i=0; i<session.library_files.size(); i++)
    {
      stapfile* f = session.library_files[i];
      for (unsigned j=0; j<f->functions.size(); j++)
        if (f->functions[j]->name == name &&
            f->functions[j]->formal_args.size() == arity)
          {
            // put library into the queue if not already there
            if (0) // session.verbose_resolution
              cerr << _F("      function %s is defined from %s",
                         name.c_str(), f->name.c_str()) << endl;

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
  vector<functiondecl*> new_unused_functions;
  for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
    {
      functiondecl* fd = it->second;
      if (ftv.traversed.find(fd) == ftv.traversed.end())
        {
          if (fd->tok->location.file->name == s.user_file->name && ! fd->synthetic)// !tapset
            s.print_warning (_F("Eliding unused function '%s'", fd->name.c_str()), fd->tok);
          // s.functions.erase (it); // NB: can't, since we're already iterating upon it
          new_unused_functions.push_back (fd);
          relaxed_p = false;
        }
    }
  for (unsigned i=0; i<new_unused_functions.size(); i++)
    {
      map<string,functiondecl*>::iterator where = s.functions.find (new_unused_functions[i]->name);
      assert (where != s.functions.end());
      s.functions.erase (where);
      if (s.tapset_compile_coverage)
        s.unused_functions.push_back (new_unused_functions[i]);
    }
}


// ------------------------------------------------------------------------

// Do away with local & global variables that are never
// written nor read.
void semantic_pass_opt2 (systemtap_session& s, bool& relaxed_p, unsigned iterations)
{
  varuse_collecting_visitor vut(s);

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

        // skip over "special" locals
        if (l->synthetic) { j++; continue; }

        if (vut.read.find (l) == vut.read.end() &&
            vut.written.find (l) == vut.written.end())
          {
            if (l->tok->location.file->name == s.user_file->name) // !tapset
              s.print_warning (_F("Eliding unused variable '%s'", l->name.c_str()), l->tok);
	    if (s.tapset_compile_coverage) {
	      s.probes[i]->unused_locals.push_back
		      (s.probes[i]->locals[j]);
	    }
            s.probes[i]->locals.erase(s.probes[i]->locals.begin() + j);
            relaxed_p = false;
            // don't increment j
          }
        else
          {
            if (vut.written.find (l) == vut.written.end())
              if (iterations == 0 && ! s.suppress_warnings)
		  {
		    stringstream o;
		    vector<vardecl*>::iterator it;
		    for (it = s.probes[i]->locals.begin(); it != s.probes[i]->locals.end(); it++)
		      if (l->name != (*it)->name)
			o << " " <<  (*it)->name;
		    for (it = s.globals.begin(); it != s.globals.end(); it++)
		      if (l->name != (*it)->name)
			o << " " <<  (*it)->name;

                    s.print_warning (_F("never-assigned local variable '%s' %s",
                                     l->name.c_str(), (o.str() == "" ? "" :
                                     (_("(alternatives:") + o.str() + ")")).c_str()), l->tok);
		  }
            j++;
          }
      }

  for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
    {
      functiondecl *fd = it->second;
      for (unsigned j=0; j<fd->locals.size(); /* see below */)
        {
          vardecl* l = fd->locals[j];
          if (vut.read.find (l) == vut.read.end() &&
              vut.written.find (l) == vut.written.end())
            {
              if (l->tok->location.file->name == s.user_file->name) // !tapset
                s.print_warning (_F("Eliding unused variable '%s'", l->name.c_str()), l->tok);
              if (s.tapset_compile_coverage) {
                fd->unused_locals.push_back (fd->locals[j]);
              }
              fd->locals.erase(fd->locals.begin() + j);
              relaxed_p = false;
              // don't increment j
            }
          else
            {
              if (vut.written.find (l) == vut.written.end())
                if (iterations == 0 && ! s.suppress_warnings)
                  {
                    stringstream o;
                    vector<vardecl*>::iterator it;
                    for (it = fd->formal_args.begin() ;
                         it != fd->formal_args.end(); it++)
                      if (l->name != (*it)->name)
                        o << " " << (*it)->name;
                    for (it = fd->locals.begin(); it != fd->locals.end(); it++)
                      if (l->name != (*it)->name)
                        o << " " << (*it)->name;
                    for (it = s.globals.begin(); it != s.globals.end(); it++)
                      if (l->name != (*it)->name)
                        o << " " << (*it)->name;

                    s.print_warning (_F("never-assigned local variable '%s' %s",
                                        l->name.c_str(), (o.str() == "" ? "" :
                                        (_("(alternatives:") + o.str() + ")")).c_str()), l->tok);
                  }

              j++;
            }
        }
    }
  for (unsigned i=0; i<s.globals.size(); /* see below */)
    {
      vardecl* l = s.globals[i];
      if (vut.read.find (l) == vut.read.end() &&
          vut.written.find (l) == vut.written.end())
        {
          if (l->tok->location.file->name == s.user_file->name) // !tapset
            s.print_warning (_F("Eliding unused variable '%s'", l->name.c_str()), l->tok);
	  if (s.tapset_compile_coverage) {
	    s.unused_globals.push_back(s.globals[i]);
	  }
	  s.globals.erase(s.globals.begin() + i);
	  relaxed_p = false;
	  // don't increment i
        }
      else
        {
          if (vut.written.find (l) == vut.written.end() && ! l->init) // no initializer
            if (iterations == 0 && ! s.suppress_warnings)
              {
                stringstream o;
                vector<vardecl*>::iterator it;
                for (it = s.globals.begin(); it != s.globals.end(); it++)
                  if (l->name != (*it)->name)
                    o << " " << (*it)->name;

                s.print_warning (_F("never assigned global variable '%s' %s", l->name.c_str(),
                                   (o.str() == "" ? "" : (_("(alternatives:") + o.str() + ")")).c_str()), l->tok);
              }

          i++;
        }
    }
}


// ------------------------------------------------------------------------

struct dead_assignment_remover: public update_visitor
{
  systemtap_session& session;
  bool& relaxed_p;
  const varuse_collecting_visitor& vut;

  dead_assignment_remover(systemtap_session& s, bool& r,
                          const varuse_collecting_visitor& v):
    session(s), relaxed_p(r), vut(v) {}

  void visit_assignment (assignment* e);
  void visit_try_block (try_block *s);
};


void
dead_assignment_remover::visit_assignment (assignment* e)
{
  replace (e->left);
  replace (e->right);

  symbol* left = get_symbol_within_expression (e->left);
  vardecl* leftvar = left->referent; // NB: may be 0 for unresolved $target
  if (leftvar) // not unresolved $target, so intended sideeffect may be elided
    {
      if (vut.read.find(leftvar) == vut.read.end()) // var never read?
        {
          // NB: Not so fast!  The left side could be an array whose
          // index expressions may have side-effects.  This would be
          // OK if we could replace the array assignment with a
          // statement-expression containing all the index expressions
          // and the rvalue... but we can't.
	  // Another possibility is that we have an unread global variable
	  // which are kept for probe end value display.

	  bool is_global = false;
	  vector<vardecl*>::iterator it;
	  for (it = session.globals.begin(); it != session.globals.end(); it++)
	    if (leftvar->name == (*it)->name)
	      {
		is_global = true;
		break;
	      }

          varuse_collecting_visitor lvut(session);
          e->left->visit (& lvut);
          if (lvut.side_effect_free () && !is_global // XXX: use _wrt() once we track focal_vars
              && !leftvar->synthetic) // don't elide assignment to synthetic $context variables
            {
              /* PR 1119: NB: This is not necessary here.  A write-only
                 variable will also be elided soon at the next _opt2 iteration.
              if (e->left->tok->location.file->name == session.user_file->name) // !tapset
                session.print_warning("eliding write-only ", *e->left->tok);
              else
              */
              if (e->left->tok->location.file->name == session.user_file->name) // !tapset
                session.print_warning(_F("Eliding assignment to %s at %s", leftvar->name.c_str(), lex_cast(*e->tok).c_str()));
              provide (e->right); // goodbye assignment*
              relaxed_p = false;
              return;
            }
        }
    }
  provide (e);
}


void
dead_assignment_remover::visit_try_block (try_block *s)
{
  replace (s->try_block);
  if (s->catch_error_var)
    {
      vardecl* errvar = s->catch_error_var->referent;
      if (vut.read.find(errvar) == vut.read.end()) // never read?
        {
          if (session.verbose>2)
            clog << _F("Eliding unused error string catcher %s at %s",
                      errvar->name.c_str(), lex_cast(*s->tok).c_str()) << endl;
          s->catch_error_var = 0;
        }
    }
  replace (s->catch_block);
  provide (s);
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
  varuse_collecting_visitor vut(s);
  for (unsigned i=0; i<s.probes.size(); i++)
    s.probes[i]->body->visit (& vut); // includes reachable functions too

  dead_assignment_remover dar (s, relaxed_p, vut);
  // This instance may be reused for multiple probe/function body trims.

  for (unsigned i=0; i<s.probes.size(); i++)
    dar.replace (s.probes[i]->body);
  for (map<string,functiondecl*>::iterator it = s.functions.begin();
       it != s.functions.end(); it++)
    dar.replace (it->second->body);
  // The rewrite operation is performed within the visitor.

  // XXX: we could also zap write-only globals here
}


// ------------------------------------------------------------------------

struct dead_stmtexpr_remover: public update_visitor
{
  systemtap_session& session;
  bool& relaxed_p;
  set<vardecl*> focal_vars; // vars considered subject to side-effects

  dead_stmtexpr_remover(systemtap_session& s, bool& r):
    session(s), relaxed_p(r) {}

  void visit_block (block *s);
  void visit_try_block (try_block *s);
  void visit_null_statement (null_statement *s);
  void visit_if_statement (if_statement* s);
  void visit_foreach_loop (foreach_loop *s);
  void visit_for_loop (for_loop *s);
  // XXX: and other places where stmt_expr's might be nested

  void visit_expr_statement (expr_statement *s);
};


void
dead_stmtexpr_remover::visit_null_statement (null_statement *s)
{
  // easy!
  if (session.verbose>2)
    clog << _("Eliding side-effect-free null statement ") << *s->tok << endl;
  s = 0;
  provide (s);
}


void
dead_stmtexpr_remover::visit_block (block *s)
{
  vector<statement*> new_stmts;
  for (unsigned i=0; i<s->statements.size(); i++ )
    {
      statement* new_stmt = require (s->statements[i], true);
      if (new_stmt != 0)
        {
          // flatten nested blocks into this one
          block *b = dynamic_cast<block *>(new_stmt);
          if (b)
            {
              if (session.verbose>2)
                clog << _("Flattening nested block ") << *b->tok << endl;
              new_stmts.insert(new_stmts.end(),
                  b->statements.begin(), b->statements.end());
              relaxed_p = false;
            }
          else
            new_stmts.push_back (new_stmt);
        }
    }
  if (new_stmts.size() == 0)
    {
      if (session.verbose>2)
        clog << _("Eliding side-effect-free empty block ") << *s->tok << endl;
      s = 0;
    }
  else if (new_stmts.size() == 1)
    {
      if (session.verbose>2)
        clog << _("Eliding side-effect-free singleton block ") << *s->tok << endl;
      provide (new_stmts[0]);
      return;
    }
  else
    s->statements = new_stmts;
  provide (s);
}


void
dead_stmtexpr_remover::visit_try_block (try_block *s)
{
  replace (s->try_block, true);
  replace (s->catch_block, true); // null catch{} is ok and useful
  if (s->try_block == 0)
    {
      if (session.verbose>2)
        clog << _("Eliding empty try {} block ") << *s->tok << endl;
      s = 0;
    }
  provide (s);
}


void
dead_stmtexpr_remover::visit_if_statement (if_statement *s)
{
  replace (s->thenblock, true);
  replace (s->elseblock, true);

  if (s->thenblock == 0)
    {
      if (s->elseblock == 0)
        {
          // We may be able to elide this statement, if the condition
          // expression is side-effect-free.
          varuse_collecting_visitor vct(session);
          s->condition->visit(& vct);
          if (vct.side_effect_free ())
            {
              if (session.verbose>2)
                clog << _("Eliding side-effect-free if statement ")
                     << *s->tok << endl;
              s = 0; // yeah, baby
            }
          else
            {
              // We can still turn it into a simple expr_statement though...
              if (session.verbose>2)
                clog << _("Creating simple evaluation from if statement ")
                     << *s->tok << endl;
              expr_statement *es = new expr_statement;
              es->value = s->condition;
              es->tok = es->value->tok;
              provide (es);
              return;
            }
        }
      else
        {
          // For an else without a then, we can invert the condition logic to
          // avoid having a null statement in the thenblock
          if (session.verbose>2)
            clog << _("Inverting the condition of if statement ")
                 << *s->tok << endl;
          unary_expression *ue = new unary_expression;
          ue->operand = s->condition;
          ue->tok = ue->operand->tok;
          ue->op = "!";
          s->condition = ue;
          s->thenblock = s->elseblock;
          s->elseblock = 0;
        }
    }
  provide (s);
}

void
dead_stmtexpr_remover::visit_foreach_loop (foreach_loop *s)
{
  replace (s->block, true);

  if (s->block == 0)
    {
      // XXX what if s->limit has side effects?
      // XXX what about s->indexes or s->value used outside the loop?
      if(session.verbose > 2)
        clog << _("Eliding side-effect-free foreach statement ") << *s->tok << endl;
      s = 0; // yeah, baby
    }
  provide (s);
}

void
dead_stmtexpr_remover::visit_for_loop (for_loop *s)
{
  replace (s->block, true);

  if (s->block == 0)
    {
      // We may be able to elide this statement, if the condition
      // expression is side-effect-free.
      varuse_collecting_visitor vct(session);
      if (s->init) s->init->visit(& vct);
      s->cond->visit(& vct);
      if (s->incr) s->incr->visit(& vct);
      if (vct.side_effect_free ())
        {
          if (session.verbose>2)
            clog << _("Eliding side-effect-free for statement ") << *s->tok << endl;
          s = 0; // yeah, baby
        }
      else
        {
          // Can't elide this whole statement; put a null in there.
          s->block = new null_statement(s->tok);
        }
    }
  provide (s);
}



void
dead_stmtexpr_remover::visit_expr_statement (expr_statement *s)
{
  // Run a varuse query against the operand expression.  If it has no
  // side-effects, replace the entire statement expression by a null
  // statement with the provide() call.
  //
  // Unlike many other visitors, we do *not* traverse this outermost
  // one into the expression subtrees.  There is no need - no
  // expr_statement nodes will be found there.  (Function bodies
  // need to be visited explicitly by our caller.)
  //
  // NB.  While we don't share nodes in the parse tree, let's not
  // deallocate *s anyway, just in case...

  varuse_collecting_visitor vut(session);
  s->value->visit (& vut);

  if (vut.side_effect_free_wrt (focal_vars))
    {
      /* PR 1119: NB: this message is not a good idea here.  It can
         name some arbitrary RHS expression of an assignment.
      if (s->value->tok->location.file->name == session.user_file->name) // not tapset
        session.print_warning("eliding never-assigned ", *s->value->tok);
      else
      */
      if (s->value->tok->location.file->name == session.user_file->name) // not tapset
        session.print_warning("Eliding side-effect-free expression ", s->tok);

      // NB: this 0 pointer is invalid to leave around for any length of
      // time, but the parent parse tree objects above handle it.
      s = 0;
      relaxed_p = false;
    }
  provide (s);
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
      assert_no_interrupts();

      derived_probe* p = s.probes[i];

      duv.focal_vars.clear ();
      duv.focal_vars.insert (s.globals.begin(),
                             s.globals.end());
      duv.focal_vars.insert (p->locals.begin(),
                             p->locals.end());

      duv.replace (p->body, true);
      if (p->body == 0)
        {
          if (! s.timing) // PR10070
            s.print_warning (_F("side-effect-free probe '%s'", p->name.c_str()), p->tok);

          p->body = new null_statement(p->tok);

          // XXX: possible duplicate warnings; see below
        }
    }
  for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
    {
      assert_no_interrupts();

      functiondecl* fn = it->second;
      duv.focal_vars.clear ();
      duv.focal_vars.insert (fn->locals.begin(),
                             fn->locals.end());
      duv.focal_vars.insert (fn->formal_args.begin(),
                             fn->formal_args.end());
      duv.focal_vars.insert (s.globals.begin(),
                             s.globals.end());

      duv.replace (fn->body, true);
      if (fn->body == 0)
        {
          s.print_warning (_F("side-effect-free function '%s'", fn->name.c_str()), fn->tok);

          fn->body = new null_statement(fn->tok);

          // XXX: the next iteration of the outer optimization loop may
          // take this new null_statement away again, and thus give us a
          // fresh warning.  It would be better if this fixup was performed
          // only after the relaxation iterations.
          // XXX: or else see bug #6469.
        }
    }
}


// ------------------------------------------------------------------------

// The goal of this visitor is to reduce top-level expressions in void context
// into separate statements that evaluate each subcomponent of the expression.
// The dead-statement-remover can later remove some parts if they have no side
// effects.
//
// All expressions must be overridden here so we never visit their subexpressions
// accidentally.  Thus, the only visited expressions should be value of an
// expr_statement.
//
// For an expression to replace its expr_statement with something else, it will
// let the new statement provide(), and then provide(0) for itself.  The
// expr_statement will take this as a sign that it's been replaced.
struct void_statement_reducer: public update_visitor
{
  systemtap_session& session;
  bool& relaxed_p;
  set<vardecl*> focal_vars; // vars considered subject to side-effects

  void_statement_reducer(systemtap_session& s, bool& r):
    session(s), relaxed_p(r) {}

  void visit_expr_statement (expr_statement* s);

  // expressions in conditional / loop controls are definitely a side effect,
  // but still recurse into the child statements
  void visit_if_statement (if_statement* s);
  void visit_for_loop (for_loop* s);
  void visit_foreach_loop (foreach_loop* s);

  // these expressions get rewritten into their statement equivalents
  void visit_logical_or_expr (logical_or_expr* e);
  void visit_logical_and_expr (logical_and_expr* e);
  void visit_ternary_expression (ternary_expression* e);

  // all of these can be reduced into simpler statements
  void visit_binary_expression (binary_expression* e);
  void visit_unary_expression (unary_expression* e);
  void visit_comparison (comparison* e);
  void visit_concatenation (concatenation* e);
  void visit_functioncall (functioncall* e);
  void visit_print_format (print_format* e);
  void visit_target_symbol (target_symbol* e);
  void visit_cast_op (cast_op* e);
  void visit_defined_op (defined_op* e);

  // these are a bit hairy to grok due to the intricacies of indexables and
  // stats, so I'm chickening out and skipping them...
  void visit_array_in (array_in* e) { provide (e); }
  void visit_arrayindex (arrayindex* e) { provide (e); }
  void visit_stat_op (stat_op* e) { provide (e); }
  void visit_hist_op (hist_op* e) { provide (e); }

  // these can't be reduced because they always have an effect
  void visit_return_statement (return_statement* s) { provide (s); }
  void visit_delete_statement (delete_statement* s) { provide (s); }
  void visit_pre_crement (pre_crement* e) { provide (e); }
  void visit_post_crement (post_crement* e) { provide (e); }
  void visit_assignment (assignment* e) { provide (e); }
};


void
void_statement_reducer::visit_expr_statement (expr_statement* s)
{
  replace (s->value, true);

  // if the expression provides 0, that's our signal that a new
  // statement has been provided, so we shouldn't provide this one.
  if (s->value != 0)
    provide(s);
}

void
void_statement_reducer::visit_if_statement (if_statement* s)
{
  // s->condition is never void
  replace (s->thenblock);
  replace (s->elseblock);
  provide (s);
}

void
void_statement_reducer::visit_for_loop (for_loop* s)
{
  // s->init/cond/incr are never void
  replace (s->block);
  provide (s);
}

void
void_statement_reducer::visit_foreach_loop (foreach_loop* s)
{
  // s->indexes/base/value/limit are never void
  replace (s->block);
  provide (s);
}

void
void_statement_reducer::visit_logical_or_expr (logical_or_expr* e)
{
  // In void context, the evaluation of "a || b" is exactly like
  // "if (!a) b", so let's do that instead.

  if (session.verbose>2)
    clog << _("Creating if statement from unused logical-or ")
         << *e->tok << endl;

  if_statement *is = new if_statement;
  is->tok = e->tok;
  is->elseblock = 0;

  unary_expression *ue = new unary_expression;
  ue->operand = e->left;
  ue->tok = e->tok;
  ue->op = "!";
  is->condition = ue;

  expr_statement *es = new expr_statement;
  es->value = e->right;
  es->tok = es->value->tok;
  is->thenblock = es;

  is->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}

void
void_statement_reducer::visit_logical_and_expr (logical_and_expr* e)
{
  // In void context, the evaluation of "a && b" is exactly like
  // "if (a) b", so let's do that instead.

  if (session.verbose>2)
    clog << _("Creating if statement from unused logical-and ")
         << *e->tok << endl;

  if_statement *is = new if_statement;
  is->tok = e->tok;
  is->elseblock = 0;
  is->condition = e->left;

  expr_statement *es = new expr_statement;
  es->value = e->right;
  es->tok = es->value->tok;
  is->thenblock = es;

  is->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}

void
void_statement_reducer::visit_ternary_expression (ternary_expression* e)
{
  // In void context, the evaluation of "a ? b : c" is exactly like
  // "if (a) b else c", so let's do that instead.

  if (session.verbose>2)
    clog << _("Creating if statement from unused ternary expression ")
         << *e->tok << endl;

  if_statement *is = new if_statement;
  is->tok = e->tok;
  is->condition = e->cond;

  expr_statement *es = new expr_statement;
  es->value = e->truevalue;
  es->tok = es->value->tok;
  is->thenblock = es;

  es = new expr_statement;
  es->value = e->falsevalue;
  es->tok = es->value->tok;
  is->elseblock = es;

  is->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}

void
void_statement_reducer::visit_binary_expression (binary_expression* e)
{
  // When the result of a binary operation isn't needed, it's just as good to
  // evaluate the operands as sequential statements in a block.

  if (session.verbose>2)
    clog << _("Eliding unused binary ") << *e->tok << endl;

  block *b = new block;
  b->tok = e->tok;

  expr_statement *es = new expr_statement;
  es->value = e->left;
  es->tok = es->value->tok;
  b->statements.push_back(es);

  es = new expr_statement;
  es->value = e->right;
  es->tok = es->value->tok;
  b->statements.push_back(es);

  b->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}

void
void_statement_reducer::visit_unary_expression (unary_expression* e)
{
  // When the result of a unary operation isn't needed, it's just as good to
  // evaluate the operand directly

  if (session.verbose>2)
    clog << _("Eliding unused unary ") << *e->tok << endl;

  relaxed_p = false;
  e->operand->visit(this);
}

void
void_statement_reducer::visit_comparison (comparison* e)
{
  visit_binary_expression(e);
}

void
void_statement_reducer::visit_concatenation (concatenation* e)
{
  visit_binary_expression(e);
}

void
void_statement_reducer::visit_functioncall (functioncall* e)
{
  // If a function call is pure and its result ignored, we can elide the call
  // and just evaluate the arguments in sequence

  if (!e->args.size())
    {
      provide (e);
      return;
    }

  varuse_collecting_visitor vut(session);
  vut.traversed.insert (e->referent);
  vut.current_function = e->referent;
  e->referent->body->visit (& vut);
  if (!vut.side_effect_free_wrt (focal_vars))
    {
      provide (e);
      return;
    }

  if (session.verbose>2)
    clog << _("Eliding side-effect-free function call ") << *e->tok << endl;

  block *b = new block;
  b->tok = e->tok;

  for (unsigned i=0; i<e->args.size(); i++ )
    {
      expr_statement *es = new expr_statement;
      es->value = e->args[i];
      es->tok = es->value->tok;
      b->statements.push_back(es);
    }

  b->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}

void
void_statement_reducer::visit_print_format (print_format* e)
{
  // When an sprint's return value is ignored, we can simply evaluate the
  // arguments in sequence

  if (e->print_to_stream || !e->args.size())
    {
      provide (e);
      return;
    }

  if (session.verbose>2)
    clog << _("Eliding unused print ") << *e->tok << endl;

  block *b = new block;
  b->tok = e->tok;

  for (unsigned i=0; i<e->args.size(); i++ )
    {
      expr_statement *es = new expr_statement;
      es->value = e->args[i];
      es->tok = es->value->tok;
      b->statements.push_back(es);
    }

  b->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}

void
void_statement_reducer::visit_target_symbol (target_symbol* e)
{
  // When target_symbol isn't needed, it's just as good to
  // evaluate any array indexes directly

  block *b = new block;
  b->tok = e->tok;

  for (unsigned i=0; i<e->components.size(); i++ )
    {
      if (e->components[i].type != target_symbol::comp_expression_array_index)
        continue;

      expr_statement *es = new expr_statement;
      es->value = e->components[i].expr_index;
      es->tok = es->value->tok;
      b->statements.push_back(es);
    }

  if (b->statements.empty())
    {
      delete b;
      provide (e);
      return;
    }

  if (session.verbose>2)
    clog << _("Eliding unused target symbol ") << *e->tok << endl;

  b->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}

void
void_statement_reducer::visit_cast_op (cast_op* e)
{
  // When the result of a cast operation isn't needed, it's just as good to
  // evaluate the operand and any array indexes directly

  block *b = new block;
  b->tok = e->tok;

  expr_statement *es = new expr_statement;
  es->value = e->operand;
  es->tok = es->value->tok;
  b->statements.push_back(es);

  for (unsigned i=0; i<e->components.size(); i++ )
    {
      if (e->components[i].type != target_symbol::comp_expression_array_index)
        continue;

      es = new expr_statement;
      es->value = e->components[i].expr_index;
      es->tok = es->value->tok;
      b->statements.push_back(es);
    }

  if (session.verbose>2)
    clog << _("Eliding unused typecast ") << *e->tok << endl;

  b->visit(this);
  relaxed_p = false;
  e = 0;
  provide (e);
}


void
void_statement_reducer::visit_defined_op (defined_op* e)
{
  // When the result of a @defined operation isn't needed, just elide
  // it entirely.  Its operand $expression must already be
  // side-effect-free.

  if (session.verbose>2)
    clog << _("Eliding unused check ") << *e->tok << endl;

  relaxed_p = false;
  e = 0;
  provide (e);
}



void semantic_pass_opt5 (systemtap_session& s, bool& relaxed_p)
{
  // Let's simplify statements with unused computed values.

  void_statement_reducer vuv (s, relaxed_p);
  // This instance may be reused for multiple probe/function body trims.

  vuv.focal_vars.insert (s.globals.begin(), s.globals.end());

  for (unsigned i=0; i<s.probes.size(); i++)
    vuv.replace (s.probes[i]->body);
  for (map<string,functiondecl*>::iterator it = s.functions.begin();
       it != s.functions.end(); it++)
    vuv.replace (it->second->body);
}


struct const_folder: public update_visitor
{
  systemtap_session& session;
  bool& relaxed_p;

  const_folder(systemtap_session& s, bool& r):
    session(s), relaxed_p(r), last_number(0), last_string(0) {}

  literal_number* last_number;
  literal_number* get_number(expression*& e);
  void visit_literal_number (literal_number* e);

  literal_string* last_string;
  literal_string* get_string(expression*& e);
  void visit_literal_string (literal_string* e);

  void get_literal(expression*& e, literal_number*& n, literal_string*& s);

  void visit_if_statement (if_statement* s);
  void visit_for_loop (for_loop* s);
  void visit_foreach_loop (foreach_loop* s);
  void visit_binary_expression (binary_expression* e);
  void visit_unary_expression (unary_expression* e);
  void visit_logical_or_expr (logical_or_expr* e);
  void visit_logical_and_expr (logical_and_expr* e);
  void visit_comparison (comparison* e);
  void visit_concatenation (concatenation* e);
  void visit_ternary_expression (ternary_expression* e);
  void visit_defined_op (defined_op* e);
  void visit_target_symbol (target_symbol* e);
};

void
const_folder::get_literal(expression*& e,
                          literal_number*& n,
                          literal_string*& s)
{
  replace (e);
  n = (e == last_number) ? last_number : NULL;
  s = (e == last_string) ? last_string : NULL;
}

literal_number*
const_folder::get_number(expression*& e)
{
  replace (e);
  return (e == last_number) ? last_number : NULL;
}

void
const_folder::visit_literal_number (literal_number* e)
{
  last_number = e;
  provide (e);
}

literal_string*
const_folder::get_string(expression*& e)
{
  replace (e);
  return (e == last_string) ? last_string : NULL;
}

void
const_folder::visit_literal_string (literal_string* e)
{
  last_string = e;
  provide (e);
}

void
const_folder::visit_if_statement (if_statement* s)
{
  literal_number* cond = get_number (s->condition);
  if (!cond)
    {
      replace (s->thenblock);
      replace (s->elseblock);
      provide (s);
    }
  else
    {
      if (session.verbose>2)
        clog << _F("Collapsing constant-%" PRIi64 " if-statement %s",
                   cond->value, lex_cast(*s->tok).c_str()) << endl;
      relaxed_p = false;

      statement* n = cond->value ? s->thenblock : s->elseblock;
      if (n)
        n->visit (this);
      else
        provide (new null_statement (s->tok));
    }
}

void
const_folder::visit_for_loop (for_loop* s)
{
  literal_number* cond = get_number (s->cond);
  if (!cond || cond->value)
    {
      replace (s->init);
      replace (s->incr);
      replace (s->block);
      provide (s);
    }
  else
    {
      if (session.verbose>2)
        clog << _("Collapsing constantly-false for-loop ") << *s->tok << endl;
      relaxed_p = false;

      if (s->init)
        s->init->visit (this);
      else
        provide (new null_statement (s->tok));
    }
}

void
const_folder::visit_foreach_loop (foreach_loop* s)
{
  literal_number* limit = get_number (s->limit);
  if (!limit || limit->value > 0)
    {
      for (unsigned i = 0; i < s->indexes.size(); ++i)
        replace (s->indexes[i]);
      replace (s->base);
      replace (s->value);
      replace (s->block);
      provide (s);
    }
  else
    {
      if (session.verbose>2)
        clog << _("Collapsing constantly-limited foreach-loop ") << *s->tok << endl;
      relaxed_p = false;

      provide (new null_statement (s->tok));
    }
}

void
const_folder::visit_binary_expression (binary_expression* e)
{
  int64_t value;
  literal_number* left = get_number (e->left);
  literal_number* right = get_number (e->right);

  if (right && !right->value && (e->op == "/" || e->op == "%"))
    {
      // Give divide-by-zero a chance to be optimized out elsewhere,
      // and if not it will be a runtime error anyway...
      provide (e);
      return;
    }

  if (left && right)
    {
      if (e->op == "+")
        value = left->value + right->value;
      else if (e->op == "-")
        value = left->value - right->value;
      else if (e->op == "*")
        value = left->value * right->value;
      else if (e->op == "&")
        value = left->value & right->value;
      else if (e->op == "|")
        value = left->value | right->value;
      else if (e->op == "^")
        value = left->value ^ right->value;
      else if (e->op == ">>")
        value = left->value >> max(min(right->value, (int64_t)64), (int64_t)0);
      else if (e->op == "<<")
        value = left->value << max(min(right->value, (int64_t)64), (int64_t)0);
      else if (e->op == "/")
        value = (left->value == LLONG_MIN && right->value == -1) ? LLONG_MIN :
                left->value / right->value;
      else if (e->op == "%")
        value = (left->value == LLONG_MIN && right->value == -1) ? 0 :
                left->value % right->value;
      else
        throw semantic_error (_("unsupported binary operator ") + e->op);
    }

  else if ((left && ((left->value == 0 && (e->op == "*" || e->op == "&" ||
                                           e->op == ">>" || e->op == "<<" )) ||
                     (left->value ==-1 && (e->op == "|" || e->op == ">>"))))
           ||
           (right && ((right->value == 0 && (e->op == "*" || e->op == "&")) ||
                      (right->value == 1 && (e->op == "%")) ||
                      (right->value ==-1 && (e->op == "%" || e->op == "|")))))
    {
      expression* other = left ? e->right : e->left;
      varuse_collecting_visitor vu(session);
      other->visit(&vu);
      if (!vu.side_effect_free())
        {
          provide (e);
          return;
        }

      if (left)
        value = left->value;
      else if (e->op == "%")
        value = 0;
      else
        value = right->value;
    }

  else if ((left && ((left->value == 0 && (e->op == "+" || e->op == "|" ||
                                           e->op == "^")) ||
                     (left->value == 1 && (e->op == "*")) ||
                     (left->value ==-1 && (e->op == "&"))))
           ||
           (right && ((right->value == 0 && (e->op == "+" || e->op == "-" ||
                                             e->op == "|" || e->op == "^")) ||
                      (right->value == 1 && (e->op == "*" || e->op == "/")) ||
                      (right->value ==-1 && (e->op == "&")) ||
                      (right->value <= 0 && (e->op == ">>" || e->op == "<<")))))
    {
      if (session.verbose>2)
        clog << _("Collapsing constant-identity binary operator ") << *e->tok << endl;
      relaxed_p = false;

      provide (left ? e->right : e->left);
      return;
    }

  else
    {
      provide (e);
      return;
    }

  if (session.verbose>2)
    clog << _F("Collapsing constant-%" PRIi64 " binary operator %s",
               value, lex_cast(*e->tok).c_str()) << endl;
  relaxed_p = false;

  literal_number* n = new literal_number(value);
  n->tok = e->tok;
  n->visit (this);
}

void
const_folder::visit_unary_expression (unary_expression* e)
{
  literal_number* operand = get_number (e->operand);
  if (!operand)
    provide (e);
  else
    {
      if (session.verbose>2)
        clog << _("Collapsing constant unary ") << *e->tok << endl;
      relaxed_p = false;

      literal_number* n = new literal_number (*operand);
      n->tok = e->tok;
      if (e->op == "+")
        ; // nothing to do
      else if (e->op == "-")
        n->value = -n->value;
      else if (e->op == "!")
        n->value = !n->value;
      else if (e->op == "~")
        n->value = ~n->value;
      else
        throw semantic_error (_("unsupported unary operator ") + e->op);
      n->visit (this);
    }
}

void
const_folder::visit_logical_or_expr (logical_or_expr* e)
{
  int64_t value;
  literal_number* left = get_number (e->left);
  literal_number* right = get_number (e->right);

  if (left && right)
    value = left->value || right->value;

  else if ((left && left->value) || (right && right->value))
    {
      // If the const is on the left, we get to short-circuit the right
      // immediately.  Otherwise, we can only eliminate the LHS if it's pure.
      if (right)
        {
          varuse_collecting_visitor vu(session);
          e->left->visit(&vu);
          if (!vu.side_effect_free())
            {
              provide (e);
              return;
            }
        }

      value = 1;
    }

  // We might also get rid of useless "0||x" and "x||0", except it does
  // normalize x to 0 or 1.  We could change it to "!!x", but it's not clear
  // that this would gain us much.

  else
    {
      provide (e);
      return;
    }

  if (session.verbose>2)
    clog << _("Collapsing constant logical-OR ") << *e->tok << endl;
  relaxed_p = false;

  literal_number* n = new literal_number(value);
  n->tok = e->tok;
  n->visit (this);
}

void
const_folder::visit_logical_and_expr (logical_and_expr* e)
{
  int64_t value;
  literal_number* left = get_number (e->left);
  literal_number* right = get_number (e->right);

  if (left && right)
    value = left->value && right->value;

  else if ((left && !left->value) || (right && !right->value))
    {
      // If the const is on the left, we get to short-circuit the right
      // immediately.  Otherwise, we can only eliminate the LHS if it's pure.
      if (right)
        {
          varuse_collecting_visitor vu(session);
          e->left->visit(&vu);
          if (!vu.side_effect_free())
            {
              provide (e);
              return;
            }
        }

      value = 0;
    }

  // We might also get rid of useless "1&&x" and "x&&1", except it does
  // normalize x to 0 or 1.  We could change it to "!!x", but it's not clear
  // that this would gain us much.

  else
    {
      provide (e);
      return;
    }

  if (session.verbose>2)
    clog << _("Collapsing constant logical-AND ") << *e->tok << endl;
  relaxed_p = false;

  literal_number* n = new literal_number(value);
  n->tok = e->tok;
  n->visit (this);
}

void
const_folder::visit_comparison (comparison* e)
{
  int comp;

  literal_number *left_num, *right_num;
  literal_string *left_str, *right_str;
  get_literal(e->left, left_num, left_str);
  get_literal(e->right, right_num, right_str);

  if (left_str && right_str)
    comp = left_str->value.compare(right_str->value);

  else if (left_num && right_num)
    comp = left_num->value < right_num->value ? -1 :
           left_num->value > right_num->value ? 1 : 0;

  else if ((left_num && ((left_num->value == LLONG_MIN &&
                          (e->op == "<=" || e->op == ">")) ||
                         (left_num->value == LLONG_MAX &&
                          (e->op == ">=" || e->op == "<"))))
           ||
           (right_num && ((right_num->value == LLONG_MIN &&
                            (e->op == ">=" || e->op == "<")) ||
                           (right_num->value == LLONG_MAX &&
                            (e->op == "<=" || e->op == ">")))))
    {
      expression* other = left_num ? e->right : e->left;
      varuse_collecting_visitor vu(session);
      other->visit(&vu);
      if (!vu.side_effect_free())
        provide (e);
      else
        {
          if (session.verbose>2)
            clog << _("Collapsing constant-boundary comparison ") << *e->tok << endl;
          relaxed_p = false;

          // ops <= and >= are true, < and > are false
          literal_number* n = new literal_number( e->op.length() == 2 );
          n->tok = e->tok;
          n->visit (this);
        }
      return;
    }

  else
    {
      provide (e);
      return;
    }

  if (session.verbose>2)
    clog << _("Collapsing constant comparison ") << *e->tok << endl;
  relaxed_p = false;

  int64_t value;
  if (e->op == "==")
    value = comp == 0;
  else if (e->op == "!=")
    value = comp != 0;
  else if (e->op == "<")
    value = comp < 0;
  else if (e->op == ">")
    value = comp > 0;
  else if (e->op == "<=")
    value = comp <= 0;
  else if (e->op == ">=")
    value = comp >= 0;
  else
    throw semantic_error (_("unsupported comparison operator ") + e->op);

  literal_number* n = new literal_number(value);
  n->tok = e->tok;
  n->visit (this);
}

void
const_folder::visit_concatenation (concatenation* e)
{
  literal_string* left = get_string (e->left);
  literal_string* right = get_string (e->right);

  if (left && right)
    {
      if (session.verbose>2)
        clog << _("Collapsing constant concatenation ") << *e->tok << endl;
      relaxed_p = false;

      literal_string* n = new literal_string (*left);
      n->tok = e->tok;
      n->value.append(right->value);
      n->visit (this);
    }
  else if ((left && left->value.empty()) ||
           (right && right->value.empty()))
    {
      if (session.verbose>2)
        clog << _("Collapsing identity concatenation ") << *e->tok << endl;
      relaxed_p = false;
      provide(left ? e->right : e->left);
    }
  else
    provide (e);
}

void
const_folder::visit_ternary_expression (ternary_expression* e)
{
  literal_number* cond = get_number (e->cond);
  if (!cond)
    {
      replace (e->truevalue);
      replace (e->falsevalue);
      provide (e);
    }
  else
    {
      if (session.verbose>2)
        clog << _F("Collapsing constant-%" PRIi64 " ternary %s",
                   cond->value, lex_cast(*e->tok).c_str()) << endl;
      relaxed_p = false;

      expression* n = cond->value ? e->truevalue : e->falsevalue;
      n->visit (this);
    }
}

void
const_folder::visit_defined_op (defined_op* e)
{
  // If a @defined makes it this far, then it is, de facto, undefined.

  if (session.verbose>2)
    clog << _("Collapsing untouched @defined check ") << *e->tok << endl;
  relaxed_p = false;

  literal_number* n = new literal_number (0);
  n->tok = e->tok;
  n->visit (this);
}

void
const_folder::visit_target_symbol (target_symbol* e)
{
  if (session.skip_badvars)
    {
      // Upon user request for ignoring context, the symbol is replaced
      // with a literal 0 and a warning message displayed
      // XXX this ignores possible side-effects, e.g. in array indexes
      literal_number* ln_zero = new literal_number (0);
      ln_zero->tok = e->tok;
      provide (ln_zero);
      session.print_warning (_("Bad $context variable being substituted with literal 0"),
                               e->tok);
      relaxed_p = false;
    }
  else
    update_visitor::visit_target_symbol (e);
}

static void semantic_pass_const_fold (systemtap_session& s, bool& relaxed_p)
{
  // Let's simplify statements with constant values.

  const_folder cf (s, relaxed_p);
  // This instance may be reused for multiple probe/function body trims.

  for (unsigned i=0; i<s.probes.size(); i++)
    cf.replace (s.probes[i]->body);
  for (map<string,functiondecl*>::iterator it = s.functions.begin();
       it != s.functions.end(); it++)
    cf.replace (it->second->body);
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
          clog << _F("Changing %s reference to %s reference\n",
                     e->referent->name.c_str(), duplicate_function_map[e->referent]->name.c_str());
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

void semantic_pass_opt6 (systemtap_session& s, bool& relaxed_p)
{
  // Walk through all the functions, looking for duplicates.
  map<string, functiondecl*> functionsig_map;
  map<functiondecl*, functiondecl*> duplicate_function_map;


  vector<functiondecl*> newly_zapped_functions;
  for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
    {
      functiondecl *fd = it->second;
      string functionsig = get_functionsig(fd);

      if (functionsig_map.count(functionsig) == 0)
	{
	  // This function is unique.  Remember it.
	  functionsig_map[functionsig] = fd;
	}
      else
        {
	  // This function is a duplicate.
	  duplicate_function_map[fd] = functionsig_map[functionsig];
          newly_zapped_functions.push_back (fd);
	  relaxed_p = false;
	}
    }
  for (unsigned i=0; i<newly_zapped_functions.size(); i++)
    {
      map<string,functiondecl*>::iterator where = s.functions.find (newly_zapped_functions[i]->name);
      assert (where != s.functions.end());
      s.functions.erase (where);
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

  // Save the old value of suppress_warnings, as we will be changing
  // it below.
  save_and_restore<bool> suppress_warnings(& s.suppress_warnings);

  bool relaxed_p = false;
  unsigned iterations = 0;
  while (! relaxed_p)
    {
      assert_no_interrupts();

      relaxed_p = true; // until proven otherwise

      // If the verbosity is high enough, always print warnings (overrides -w),
      // or if not, always suppress warnings for every itteration after the first.
      if(s.verbose > 2)
        s.suppress_warnings = false;
      else if (iterations > 0)
        s.suppress_warnings = true;

      if (!s.unoptimized)
        {
          semantic_pass_opt1 (s, relaxed_p);
          semantic_pass_opt2 (s, relaxed_p, iterations); // produce some warnings only on iteration=0
          semantic_pass_opt3 (s, relaxed_p);
          semantic_pass_opt4 (s, relaxed_p);
          semantic_pass_opt5 (s, relaxed_p);
        }

      // For listing mode, we need const-folding regardless of optimization so
      // that @defined expressions can be properly resolved.  PR11360
      // We also want it in case variables are used in if/case expressions,
      // so enable always.  PR11366
      semantic_pass_const_fold (s, relaxed_p);

      iterations ++;
    }

  return rc;
}


static int
semantic_pass_optimize2 (systemtap_session& s)
{
  // This is run after type inference.  We run an outer "relaxation"
  // loop that repeats the optimizations until none of them find
  // anything to remove.

  int rc = 0;

  // Save the old value of suppress_warnings, as we will be changing
  // it below.
  save_and_restore<bool> suppress_warnings(& s.suppress_warnings);

  bool relaxed_p = false;
  unsigned iterations = 0;
  while (! relaxed_p)
    {
      assert_no_interrupts();
      relaxed_p = true; // until proven otherwise

      // If the verbosity is high enough, always print warnings (overrides -w),
      // or if not, always suppress warnings for every itteration after the first.
      if(s.verbose > 2)
        s.suppress_warnings = false;
      else if (iterations > 0)
        s.suppress_warnings = true;

      if (!s.unoptimized)
        semantic_pass_opt6 (s, relaxed_p);

      iterations++;
    }

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
      assert_no_interrupts();

      iterations ++;
      ti.num_newly_resolved = 0;
      ti.num_still_unresolved = 0;

  for (map<string,functiondecl*>::iterator it = s.functions.begin(); it != s.functions.end(); it++)
        {
          assert_no_interrupts();

          functiondecl* fd = it->second;
          ti.current_probe = 0;
          ti.current_function = fd;
          ti.t = pe_unknown;
          fd->body->visit (& ti);
	  // NB: we don't have to assert a known type for
	  // functions here, to permit a "void" function.
	  // The translator phase will omit the "retvalue".
	  //
          // if (fd->type == pe_unknown)
          //   ti.unresolved (fd->tok);
          for (unsigned i=0; i < fd->locals.size(); ++i)
            ti.check_local (fd->locals[i]);
        }

      for (unsigned j=0; j<s.probes.size(); j++)
        {
          assert_no_interrupts();

          derived_probe* pn = s.probes[j];
          ti.current_function = 0;
          ti.current_probe = pn;
          ti.t = pe_unknown;
          pn->body->visit (& ti);
          for (unsigned i=0; i < pn->locals.size(); ++i)
            ti.check_local (pn->locals[i]);

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
          if(gd->arity == 0 && gd->wrap == true)
            {
              throw semantic_error (_("wrapping not supported for scalars"), gd->tok);
            }
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
  session(s), num_newly_resolved(0), num_still_unresolved(0),
  assert_resolvability(false), current_function(0), current_probe(0),
  t(pe_unknown)
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
    throw semantic_error (_("unsupported assignment operator ") + e->op);
}


void
typeresolution_info::visit_embedded_expr (embedded_expr *e)
{
  if (e->type == pe_unknown)
    {
      if (e->code.find ("/* string */") != string::npos)
        e->type = pe_string;
      else // if (e->code.find ("/* long */") != string::npos)
        e->type = pe_long;

      resolved (e->tok, e->type);
    }
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

  if (session.verbose > 2)
    {
      clog << _("Resolution problem with ");
      if (current_function)
        {
          clog << "function " << current_function->name << endl;
          current_function->body->print (clog);
          clog << endl;
        }
      else if (current_probe)
        {
          clog << "probe " << *current_probe->sole_location() << endl;
          current_probe->body->print (clog);
          clog << endl;
        }
      else
        //TRANSLATORS: simply saying not an issue with a probe or function
        clog << _("other") << endl;
    }

  if (e->saved_conversion_error)
    throw (* (e->saved_conversion_error));
  else
    throw semantic_error(_("unresolved target-symbol expression"), e->tok);
}


void
typeresolution_info::visit_defined_op (defined_op* e)
{
  throw semantic_error(_("unexpected @defined"), e->tok);
}


void
typeresolution_info::visit_entry_op (entry_op* e)
{
  throw semantic_error(_("@entry is only valid in .return probes"), e->tok);
}


void
typeresolution_info::visit_cast_op (cast_op* e)
{
  // Like target_symbol, a cast_op shouldn't survive this far
  // unless it was not resolved and its value is really needed.
  if (e->saved_conversion_error)
    throw (* (e->saved_conversion_error));
  else
    throw semantic_error(_F("type definition '%s' not found in '%s'",
                            e->type_name.c_str(), e->module.c_str()), e->tok);
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
typeresolution_info::visit_try_block (try_block* e)
{
  if (e->try_block)
    e->try_block->visit (this);
  if (e->catch_error_var)
    {
      t = pe_string;
      e->catch_error_var->visit (this);
    }
  if (e->catch_block)
    e->catch_block->visit (this);
}


void
typeresolution_info::visit_embeddedcode (embeddedcode* s)
{
  // PR11573.  If we have survived thus far with a piece of embedded
  // code that requires uprobes, we need to track this.
  //
  // This is an odd place for this check, as opposed
  // to a separate 'optimization' pass, or c_unparser::visit_embeddedcode
  // over yonder in pass 3.  However, we want to do it during pass 2 so
  // that cached sessions also get the uprobes treatment.
  if (!session.need_uprobes && s->code.find("/* pragma:uprobes */") != string::npos)
    {
      if (session.verbose > 2)
        clog << _("Activating uprobes support because /* pragma:uprobes */ seen.") << endl;
      session.need_uprobes = true;
    }
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

  exp_type wanted_value = pe_unknown;
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
      wanted_value = pe_long;
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
      t = pe_unknown;
      array->visit (this);
      wanted_value = array->type;
    }

  if (e->value)
    {
      if (wanted_value == pe_stats)
        invalid(e->value->tok, wanted_value);
      else if (wanted_value != pe_unknown)
        check_arg_type(wanted_value, e->value);
      else
        {
          t = pe_unknown;
          e->value->visit (this);
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
    throwing_visitor (_("invalid operand of delete expression")),
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

      unsigned expected_num_args = 0;
      std::vector<print_format::format_component> components;
      for (size_t i = 0; i < e->components.size(); ++i)
	{
	  if (e->components[i].type == print_format::conv_unspecified)
	    throw semantic_error (_("Unspecified conversion in print operator format string"),
				  e->tok);
	  else if (e->components[i].type == print_format::conv_literal)
	    continue;
	  components.push_back(e->components[i]);
	  ++expected_num_args;
	  if (e->components[i].widthtype == print_format::width_dynamic)
	    ++expected_num_args;
	  if (e->components[i].prectype == print_format::prec_dynamic)
	    ++expected_num_args;
	}

      // Then we check that the number of conversions and the number
      // of args agree.

      if (expected_num_args != e->args.size())
	throw semantic_error (_("Wrong number of args to formatted print operator"),
			      e->tok);

      // Then we check that the types of the conversions match the types
      // of the args.
      unsigned argno = 0;
      for (size_t i = 0; i < components.size(); ++i)
	{
	  // Check the dynamic width, if specified
	  if (components[i].widthtype == print_format::width_dynamic)
	    {
	      check_arg_type (pe_long, e->args[argno]);
	      ++argno;
	    }

	  // Check the dynamic precision, if specified
	  if (components[i].prectype == print_format::prec_dynamic)
	    {
	      check_arg_type (pe_long, e->args[argno]);
	      ++argno;
	    }

	  exp_type wanted = pe_unknown;

	  switch (components[i].type)
	    {
	    case print_format::conv_unspecified:
	    case print_format::conv_literal:
	      assert (false);
	      break;

	    case print_format::conv_pointer:
	    case print_format::conv_number:
	    case print_format::conv_binary:
	    case print_format::conv_char:
	    case print_format::conv_memory:
	    case print_format::conv_memory_hex:
	      wanted = pe_long;
	      break;

	    case print_format::conv_string:
	      wanted = pe_string;
	      break;
	    }

	  assert (wanted != pe_unknown);
	  check_arg_type (wanted, e->args[argno]);
	  ++argno;
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
typeresolution_info::check_arg_type (exp_type wanted, expression* arg)
{
  t = wanted;
  arg->visit (this);

  if (arg->type == pe_unknown)
    {
      arg->type = wanted;
      resolved (arg->tok, wanted);
    }
  else if (arg->type != wanted)
    {
      mismatch (arg->tok, arg->type, wanted);
    }
}


void
typeresolution_info::check_local (vardecl* v)
{
  if (v->arity != 0)
    {
      num_still_unresolved ++;
      if (assert_resolvability)
        session.print_error
          (semantic_error (_("array locals not supported, missing global declaration? "), v->tok));
    }

  if (v->type == pe_unknown)
    unresolved (v->tok);
  else if (v->type == pe_stats)
    {
      num_still_unresolved ++;
      if (assert_resolvability)
        session.print_error
          (semantic_error (_("stat locals not supported, missing global declaration? "), v->tok));
    }
  else if (!(v->type == pe_long || v->type == pe_string))
    invalid (v->tok, v->type);
}


void
typeresolution_info::unresolved (const token* tok)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      stringstream msg;
      msg << _("unresolved type ");
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
      if (tok && tok->type == tok_operator)
        msg << _("invalid operator");
      else
        msg << _("invalid type ") << pe;
      session.print_error (semantic_error (msg.str(), tok));
    }
}


void
typeresolution_info::mismatch (const token* tok, exp_type t1, exp_type t2)
{
  bool tok_resolved = false;
  size_t i;
  semantic_error* err1 = 0;
  num_still_unresolved ++;

  //BZ 9719: for improving type mismatch messages, a semantic error is
  //generated with the token where type was first resolved. All such 
  //resolved tokens, stored in a vector, are matched against their 
  //content. If an error for the matching token hasn't been printed out
  //already, it is and the token pushed in another printed_toks vector

  if (assert_resolvability)
    {
      stringstream msg;
      for (i=0; i<resolved_toks.size(); i++)
	{
	  if (resolved_toks[i]->content == tok->content)
	    {
	      tok_resolved = true;
	      break;
	    }
	}
      if (!tok_resolved)
	{
          msg << _F("type mismatch (%s vs. %s)",
                    lex_cast(t1).c_str(), lex_cast(t2).c_str());
	}
      else
	{
	  bool tok_printed = false;
	  for (size_t j=0; j<printed_toks.size(); j++)
	    {
	      if (printed_toks[j] == resolved_toks[i])
		{
		  tok_printed = true;
		  break;
		}
	    }
          msg << _F("type mismatch (%s vs. %s)",
                    lex_cast(t1).c_str(), lex_cast(t2).c_str());
	  if (!tok_printed)
	    {
	      //error for possible mismatch in the earlier resolved token
	      printed_toks.push_back (resolved_toks[i]);
	      stringstream type_msg;
              type_msg << _F("type was first inferred here (%s)", lex_cast(t2).c_str());
	      err1 = new semantic_error (type_msg.str(), resolved_toks[i]);
	    }
	}
      semantic_error err (msg.str(), tok);
      err.chain = err1;
      session.print_error (err);
    }
}


void
typeresolution_info::resolved (const token* tok, exp_type)
{
  resolved_toks.push_back (tok);
  num_newly_resolved ++;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
