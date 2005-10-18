// elaboration functions
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "elaborate.h"
#include "parse.h"
#include "tapsets.h"

extern "C" {
#include <sys/utsname.h>
}

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <vector>

using namespace std;


template <typename OUT, typename IN> inline OUT
lex_cast(IN const & in)
{
  stringstream ss;
  OUT out;
  if (!(ss << in && ss >> out))
    throw runtime_error("bad lexical cast");
  return out;
}


// ------------------------------------------------------------------------


derived_probe::derived_probe (probe *p):
  base (p)
{
  if (p)
    {
      this->locations = p->locations;  
      this->tok = p->tok;
      this->body = deep_copy_visitor::deep_copy(p->body);
    }
}


derived_probe::derived_probe (probe *p, probe_point *l):
  base (p)
{
  this->locations.push_back (l);
  if (p)
    {
      this->tok = p->tok;
      this->body = deep_copy_visitor::deep_copy(p->body);
    }
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



// ------------------------------------------------------------------------
// Members of match_key.

match_key::match_key(string const & n) 
  : name(n), 
    have_parameter(false), 
    parameter_type(tok_junk)
{
}

match_key::match_key(probe_point::component const & c)
  : name(c.functor),
    have_parameter(c.arg != NULL),
    parameter_type(c.arg ? c.arg->tok->type : tok_junk)
{
}

match_key &
match_key::with_number() 
{
  have_parameter = true;
  parameter_type = tok_number;
  return *this;
}

match_key &
match_key::with_string() 
{
  have_parameter = true;
  parameter_type = tok_string;
  return *this;
}

string 
match_key::str() const
{
  if (have_parameter)
    switch (parameter_type)
      {
      case tok_string: return name + "(string)";
      case tok_number: return name + "(number)";
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
  else if (loc->components[pos]->functor == "*") // wildcard?
    {
      // Recursively call derive_probes for all matches of the current
      // key position.  To do this, we have to perform almost an
      // alias-level duplication of the probe body and synthesis of
      // new probe_points.

      probe * n = new probe();
      n->tok = p->tok;
      n->body = deep_copy_visitor::deep_copy(p->body);

      // Construct N probe_point instances: one per wildcard match
      for (sub_map_iterator_t i = sub.begin(); i != sub.end(); i++)
        {
          const match_key& match = i->first;
          probe_point *loc2 = new probe_point;
          loc2->tok = loc->tok;

          // deep-copy probe point components, except for this wildcard position
          for (unsigned k=0; k<loc->components.size(); k++)
            if (pos != k)
              loc2->components.push_back(new probe_point::component(loc->components[k]->functor,
                                                                    loc->components[k]->arg));
            else
              // NB: we retain wildcard arg - foo.*(5).bar => foo.a(5).bar, foo.b(5).bar
              loc2->components.push_back(new probe_point::component(match.name,
                                                                    loc->components[k]->arg));
          n->locations.push_back (loc2);
        }

      derive_probes (s, n, results, false);
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


// ------------------------------------------------------------------------
// Alias probes
// ------------------------------------------------------------------------

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
		     std::map<std::string, literal *> const & parameters,
		     vector<derived_probe *> & finished_results)
  {
    // We're going to build a new probe and wrap it up in an
    // alias_expansion_probe so that the expansion loop recognizer it as
    // such and re-expands its expansion.
    
    probe * n = new probe();
    n->body = new block();

    // The new probe gets the location list of the alias,
    n->locations = alias->locations;
  
    // the token location of the use,
    n->tok = use->tok;
    n->body->tok = use->tok;

    // and statements representing the concatenation of the alias'
    // body with the use's. 
    //
    // NB: locals are *not* copied forward, from either alias or
    // use. The expansion should have its locals re-inferred since
    // there's concatenated code here and we only want one vardecl per
    // resulting variable.

    for (unsigned i = 0; i < alias->body->statements.size(); ++i)
      {
	statement *s = deep_copy_visitor::deep_copy(alias->body->statements[i]);
	n->body->statements.push_back(s);
      }

    for (unsigned i = 0; i < use->body->statements.size(); ++i)
      {
	statement *s = deep_copy_visitor::deep_copy(use->body->statements[i]);
	n->body->statements.push_back(s);
      }

    derive_probes (sess, n, finished_results, false);
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
              print_error (e);
              cerr << "         while: registering probe alias ";
              alias->printsig(cerr);
              cerr << endl;
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
               bool exc_outermost)
{
  for (unsigned i = 0; i < p->locations.size(); ++i)
    {
      probe_point *loc = p->locations[i];
      
      try
        {
          unsigned num_atbegin = dps.size();
          s.pattern_root->find_and_build (s, p, loc, 0, dps);
          unsigned num_atend = dps.size();
          
          if (num_atbegin == num_atend) // nothing new derived!
            throw semantic_error ("no match for probe point");
        }
      catch (const semantic_error& e)
        {
          // XXX: prefer not to print_error at every nest/unroll level
          s.print_error (e);
          cerr << "         while: resolving probe point " << *loc << endl;
          //          if (! exc_outermost)
          //            throw;
        }
    }
}



// ------------------------------------------------------------------------
//
// Map usage checks
//

struct mutated_map_collector
  : public traversing_visitor
{
  set<vardecl *> * mutated_maps;

  mutated_map_collector(set<vardecl *> * mm) 
    : mutated_maps (mm)
  {}

  void visit_arrayindex (arrayindex *e)
  {
    if (is_active_lvalue(e))
      mutated_maps->insert(e->referent);
  }
};


struct no_map_mutation_during_iteration_check
  : public traversing_visitor
{
  systemtap_session & session;
  map<functiondecl *,set<vardecl *> *> & function_mutates_maps;
  vector<vardecl *> maps_being_iterated;
  
  no_map_mutation_during_iteration_check 
  (systemtap_session & sess,
   map<functiondecl *,set<vardecl *> *> & fmm)
    : session(sess), function_mutates_maps (fmm)
  {}

  void visit_arrayindex (arrayindex *e)
  {
    if (is_active_lvalue(e))
      {
	for (unsigned i = 0; i < maps_being_iterated.size(); ++i)
	  {
	    vardecl *m = maps_being_iterated[i];
	    if (m == e->referent)
	      {
		string err = ("map '" + m->name +
			      "' modified during 'foreach' iteration");
		session.print_error (semantic_error (err, e->tok));
	      }
	  }
      }
  }

  void visit_functioncall (functioncall* e)
  {
    map<functiondecl *,set<vardecl *> *>::const_iterator i 
      = function_mutates_maps.find (e->referent);

    if (i != function_mutates_maps.end())
      {
	for (unsigned j = 0; j < maps_being_iterated.size(); ++j)
	  {
	    vardecl *m = maps_being_iterated[j];
	    if (i->second->find (m) != i->second->end())
	      {
		string err = ("function call modifies map '" + m->name +
			      "' during 'foreach' iteration");
		session.print_error (semantic_error (err, e->tok));
	      }
	  }
      }

    for (unsigned i=0; i<e->args.size(); i++)
      e->args[i]->visit (this);
  }

  void visit_foreach_loop(foreach_loop* s)
  {
    maps_being_iterated.push_back (s->base_referent);
    for (unsigned i=0; i<s->indexes.size(); i++)
      s->indexes[i]->visit (this);
    s->block->visit (this);
    maps_being_iterated.pop_back();
  }
};


static int
semantic_pass_maps (systemtap_session & sess)
{
  
  map<functiondecl *, set<vardecl *> *> fmm;
  no_map_mutation_during_iteration_check chk(sess, fmm);
  
  for (unsigned i = 0; i < sess.functions.size(); ++i)
    {
      functiondecl * fn = sess.functions[i];
      if (fn->body)
	{
	  set<vardecl *> * m = new set<vardecl *>();
	  mutated_map_collector mc (m);
	  fn->body->visit (&mc);
	  fmm[fn] = m;
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

  return sess.num_errors;
}

// ------------------------------------------------------------------------


static int semantic_pass_symbols (systemtap_session&);
static int semantic_pass_types (systemtap_session&);
static int semantic_pass_maps (systemtap_session&);



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

          // much magic happens here: probe alias expansion,
          // provider identification
          derive_probes (s, p, dps);

          for (unsigned j=0; j<dps.size(); j++)
            {
              derived_probe* dp = dps[j];
	      s.probes.push_back (dp);

              try 
                {
                  sym.current_function = 0;
                  sym.current_probe = dp;
                  dp->body->visit (& sym);
                }
              catch (const semantic_error& e)
                {
                  s.print_error (e);
                }
            }
        }
    }
  
  return s.num_errors; // all those print_error calls
}



int
semantic_pass (systemtap_session& s)
{
  int rc = 0;

  try 
    {
      s.register_library_aliases();
      register_standard_tapsets(s);
      
      rc = semantic_pass_symbols (s);
      if (rc == 0) rc = semantic_pass_types (s);
      if (rc == 0) rc = semantic_pass_maps (s);
    }
  catch (const semantic_error& e)
    {
      s.print_error (e);
    }
  
  return rc;
}


// ------------------------------------------------------------------------


systemtap_session::systemtap_session ():
  pattern_root(new match_node),
  user_file (0), op (0), up (0), num_errors (0)
{
}


void
systemtap_session::print_error (const semantic_error& e)
{
  cerr << "semantic error: " << e.what ();
  if (e.tok1 || e.tok2)
    cerr << ": ";
  if (e.tok1) cerr << *e.tok1;
  cerr << e.msg2;
  if (e.tok2) cerr << *e.tok2;
  cerr << endl;
  num_errors ++;
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

  if (e->base_referent)
    return;

  vardecl* d = find_var (e->base, e->indexes.size ());
  if (d)
    e->base_referent = d;
  else
    throw semantic_error ("unresolved global array " + e->base, e->tok);

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
        // must not happen
        throw semantic_error ("no current probe/function", e->tok);
      e->referent = v;
    }
}


void
symresolution_info::visit_arrayindex (arrayindex* e)
{
  for (unsigned i=0; i<e->indexes.size(); i++)
    e->indexes[i]->visit (this);

  if (e->referent)
    return;

  vardecl* d = find_var (e->base, e->indexes.size ());
  if (d)
    e->referent = d;
  else
    {
      // new local
      vardecl* v = new vardecl;
      v->set_arity(e->indexes.size());
      v->name = e->base;
      v->tok = e->tok;
      if (current_function)
        current_function->locals.push_back (v);
      else if (current_probe)
        current_probe->locals.push_back (v);
      else
        // must not happen
        throw semantic_error ("no current probe/function", e->tok);
      e->referent = v;
    }
}


void
symresolution_info::visit_functioncall (functioncall* e)
{
  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);

  if (e->referent)
    return;

  functiondecl* d = find_function (e->function, e->args.size ());
  if (d)
    e->referent = d;
  else
    throw semantic_error ("unresolved function call", e->tok);
}


vardecl* 
symresolution_info::find_var (const string& name, int arity)
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
      // cerr << "Type resolution, iteration " << iterations << endl;
      ti.num_newly_resolved = 0;
      ti.num_still_unresolved = 0;

      for (unsigned j=0; j<s.functions.size(); j++)
        {
          functiondecl* fn = s.functions[j];
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
          ti.t = pe_unknown;
          pn->body->visit (& ti);
        }

      for (unsigned j=0; j<s.globals.size(); j++)
        {
          vardecl* gd = s.globals[j];
          if (gd->type == pe_unknown)
            ti.unresolved (gd->tok);
        }
      
      if (ti.num_newly_resolved == 0) // converged
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
  
  return rc + s.num_errors;
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
      if (e->type == pe_unknown)
        {
          e->type = pe_long;
          resolved (e->tok, e->type);
        }
    }
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

  if (e->referent->arity > 0)
    unresolved (e->tok); // symbol resolution should not permit this
  else
    resolve_2types (e, e->referent, this, t);
}


void
typeresolution_info::visit_target_symbol (target_symbol* e)
{
  throw semantic_error("unresolved target-symbol expression", e->tok);
}


void
typeresolution_info::visit_arrayindex (arrayindex* e)
{
  assert (e->referent != 0);

  resolve_2types (e, e->referent, this, t);

  // now resolve the array indexes

  // if (e->referent->index_types.size() == 0)
  //   // redesignate referent as array
  //   e->referent->set_arity (e->indexes.size ());

  if (e->indexes.size() != e->referent->index_types.size())
    unresolved (e->tok); // symbol resolution should prevent this
  else for (unsigned i=0; i<e->indexes.size(); i++)
    {
      expression* ee = e->indexes[i];
      exp_type& ft = e->referent->index_types [i];
      t = ft;
      ee->visit (this);
      exp_type at = ee->type;

      if ((at == pe_string || at == pe_long) && ft == pe_unknown)
        {
          // propagate to formal type
          ft = at;
          resolved (e->referent->tok, ft);
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

  // XXX: but what about functions that return no value,
  // and are used only as an expression-statement for side effects?

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
typeresolution_info::visit_embeddedcode (embeddedcode* e)
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
  e->init->visit (this);
  t = pe_long;
  e->cond->visit (this);
  t = pe_unknown;
  e->incr->visit (this);  
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

  if (e->indexes.size() != e->base_referent->index_types.size())
    unresolved (e->tok); // symbol resolution should prevent this
  else for (unsigned i=0; i<e->indexes.size(); i++)
    {
      expression* ee = e->indexes[i];
      exp_type& ft = e->base_referent->index_types [i];
      t = ft;
      ee->visit (this);
      exp_type at = ee->type;

      if ((at == pe_string || at == pe_long) && ft == pe_unknown)
        {
          // propagate to formal type
          ft = at;
          resolved (e->base_referent->tok, ft);
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
  e->block->visit (this);  
}


void
typeresolution_info::visit_null_statement (null_statement* e)
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
typeresolution_info::visit_next_statement (next_statement* s)
{
}


void
typeresolution_info::visit_break_statement (break_statement* s)
{
}


void
typeresolution_info::visit_continue_statement (continue_statement* s)
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
typeresolution_info::unresolved (const token* tok)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      cerr << "error: unresolved type for ";
      if (tok)
        cerr << *tok;
      else
        cerr << "a token";
      cerr << endl;
    }
}


void
typeresolution_info::invalid (const token* tok, exp_type pe)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      cerr << "error: invalid type " << pe << " for ";
      if (tok)
        cerr << *tok;
      else
        cerr << "a token";
      cerr << endl;
    }
}


void
typeresolution_info::mismatch (const token* tok, exp_type t1, exp_type t2)
{
  num_still_unresolved ++;

  if (assert_resolvability)
    {
      cerr << "error: type mismatch for ";
      if (tok)
        cerr << *tok;
      else
        cerr << "a token";
      cerr << ": " << t1 << " vs. " << t2 << endl;
    }
}


void
typeresolution_info::resolved (const token* tok, exp_type t)
{
  num_newly_resolved ++;
  // cerr << "resolved " << *e->tok << " type " << t << endl;
}

