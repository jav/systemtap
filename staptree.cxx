// parse tree functions
// Copyright (C) 2005, 2006 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "parse.h"
#include <iostream>
#include <typeinfo>
#include <sstream>
#include <cassert>

using namespace std;


// return as quoted string, with at least '"' backslash-escaped
template <typename IN> inline string
lex_cast_qstring(IN const & in)
{
  stringstream ss;
  string out, out2;
  if (!(ss << in))
    throw runtime_error("bad lexical cast");
  out = ss.str();
  out2 += '"';
  for (unsigned i=0; i<out.length(); i++)
    {
      if (out[i] == '"') // XXX others?
	out2 += '\\';
      out2 += out[i];
    }
  out2 += '"';
  return out2;
}


template <typename OUT, typename IN> inline OUT
lex_cast(IN const & in)
{
  stringstream ss;
  OUT out;
  if (!(ss << in && ss >> out))
    throw runtime_error("bad lexical cast");
  return out;
}


expression::expression ():
  type (pe_unknown), tok (0)
{
} 


expression::~expression ()
{
}


statement::statement ():
  tok (0)
{
} 


statement::~statement ()
{
}


symbol::symbol ():
  referent (0)
{
}


arrayindex::arrayindex ():
  base (0)
{
}


functioncall::functioncall ():
  referent (0)
{
}


symboldecl::symboldecl ():
  tok (0),
  type (pe_unknown)
{
} 


symboldecl::~symboldecl ()
{
}

probe_point::probe_point (std::vector<component*> const & comps,
			  const token * t):
  components(comps), tok(t), optional (false)
{
}

probe_point::probe_point ():
  tok (0), optional (false)
{
}


unsigned probe::last_probeidx = 0;
probe::probe ():
  body (0), tok (0)
{
  this->name = string ("probe_") + lex_cast<string>(last_probeidx ++);
}


probe_point::component::component ():
  arg (0)
{
}


probe_point::component::component (std::string const & f, literal * a): 
  functor(f), arg(a) 
{
}


vardecl::vardecl ():
  arity (-1)
{
}


void
vardecl::set_arity (int a)
{
  if (a < 0)
    return;

  if (arity != a && arity >= 0)
    throw semantic_error ("inconsistent arity", tok);

  if (arity != a)
    {
      arity = a;
      index_types.resize (arity);
      for (int i=0; i<arity; i++)
	index_types[i] = pe_unknown;
    }
}

bool 
vardecl::compatible_arity (int a)
{
  if (arity == -1 || a == -1)
    return true;
  return arity == a;
}


functiondecl::functiondecl ():
  body (0)
{
}


literal_number::literal_number (int64_t v)
{
  value = v;
  type = pe_long;
}


literal_string::literal_string (const string& v)
{
  value = v;
  type = pe_string;
}


ostream&
operator << (ostream& o, const exp_type& e)
{
  switch (e)
    {
    case pe_unknown: o << "unknown"; break;
    case pe_long: o << "long"; break;
    case pe_string: o << "string"; break;
    case pe_stats: o << "stats"; break;
    default: o << "???"; break;
    }
  return o;
}


// ------------------------------------------------------------------------
// parse tree printing

ostream& operator << (ostream& o, const expression& k)
{
  k.print (o);
  return o;
}


void literal_string::print (ostream& o) const
{
  o << '"';
  for (unsigned i=0; i<value.size(); i++)
    if (value[i] == '"') // or other escapeworthy characters?
      o << '\\' << '"';
    else
      o << value[i];
  o << '"';
}


void literal_number::print (ostream& o) const
{
  o << value;
}


void binary_expression::print (ostream& o) const
{
  o << "(" << *left << ") " 
    << op 
    << " (" << *right << ")";
}


void unary_expression::print (ostream& o) const
{
  o << op << '(' << *operand << ")"; 
}

void array_in::print (ostream& o) const
{
  o << "[";
  for (unsigned i=0; i<operand->indexes.size(); i++)
    {
      if (i > 0) o << ", ";
      operand->indexes[i]->print (o);
    }
  o << "] in ";
  operand->base->print_indexable (o);
}

void post_crement::print (ostream& o) const
{
  o << '(' << *operand << ")" << op; 
}


void ternary_expression::print (ostream& o) const
{
  o << "(" << *cond << ")?("
    << *truevalue << "):("
    << *falsevalue << ")"; 
}


void symbol::print (ostream& o) const
{
  o << name;
}


void target_symbol::print (std::ostream& o) const
{
  o << base_name;
  for (unsigned i = 0; i < components.size(); ++i)
    {
      switch (components[i].first)
	{
	case comp_literal_array_index:
	  o << '[' << components[i].second << ']';
	  break;
	case comp_struct_member:
	  o << "->" << components[i].second;
	  break;
	}
    }
}


void vardecl::print (ostream& o) const
{
  o << name;
  if (arity > 0 || index_types.size() > 0)
    o << "[...]";
}


void vardecl::printsig (ostream& o) const
{
  o << name << ":" << type;
  if (index_types.size() > 0)
    {
      o << " [";
      for (unsigned i=0; i<index_types.size(); i++)
        o << (i>0 ? ", " : "") << index_types[i];
      o << "]";
    }
}


void functiondecl::print (ostream& o) const
{
  o << "function " << name << " (";
  for (unsigned i=0; i<formal_args.size(); i++)
    o << (i>0 ? ", " : "") << *formal_args[i];
  o << ")" << endl;
  body->print(o);
}


void functiondecl::printsig (ostream& o) const
{
  o << name << ":" << type << " (";
  for (unsigned i=0; i<formal_args.size(); i++)
    o << (i>0 ? ", " : "")
      << *formal_args[i]
      << ":"
      << formal_args[i]->type;
  o << ")";
}


void arrayindex::print (ostream& o) const
{
  base->print_indexable (o);
  o << "[";
  for (unsigned i=0; i<indexes.size(); i++)
    o << (i>0 ? ", " : "") << *indexes[i];
  o << "]";
}


void functioncall::print (ostream& o) const
{
  o << function << "(";
  for (unsigned i=0; i<args.size(); i++)
    o << (i>0 ? ", " : "") << *args[i];
  o << ")";
}  


string 
print_format::components_to_string(vector<format_component> const & components)
{
  ostringstream oss;

  for (vector<format_component>::const_iterator i = components.begin();
       i != components.end(); ++i)
    {

      assert (i->type != conv_unspecified);

      if (i->type == conv_literal)
	{
	  assert(!i->literal_string.empty());
	  for (string::const_iterator j = i->literal_string.begin();
	       j != i->literal_string.end(); ++j)
	    {
	      if (*j == '%')
		oss << '%';
	      oss << *j;
	    }
	}
      else
	{
	  oss << '%';

	  if (i->flags & static_cast<unsigned long>(fmt_flag_zeropad))
	    oss << '0';

	  if (i->flags & static_cast<unsigned long>(fmt_flag_plus))
	    oss << '+';

	  if (i->flags & static_cast<unsigned long>(fmt_flag_space))
	    oss << ' ';

	  if (i->flags & static_cast<unsigned long>(fmt_flag_left))
	    oss << '-';

	  if (i->flags & static_cast<unsigned long>(fmt_flag_special))
	    oss << '#';

	  if (i->width > 0)
	    oss << i->width;

	  if (i->precision > 0)
	    oss << '.' << i->precision;

	  switch (i->type)	
	    {
	    case conv_binary:
	      oss << "b";
	      break;

	    case conv_signed_decimal:
	      oss << "lld";
	      break;

	    case conv_unsigned_decimal:
	      oss << "llu";
	      break;

	    case conv_unsigned_octal:
	      oss << "llo";
	      break;

	    case conv_unsigned_ptr:
	      oss << "p";
	      break;

	    case conv_unsigned_uppercase_hex:
	      oss << "llX";
	      break;

	    case conv_unsigned_lowercase_hex:
	      oss << "llx";
	      break;

	    case conv_string:
	      oss << 's';
	      break;
	      
	    case conv_size:
	      oss << 'n';
	      break;

	    default:
	      break;
	    }
	}
    }
  return oss.str ();
}
 
vector<print_format::format_component> 
print_format::string_to_components(string const & str)
{
  format_component curr;
  vector<format_component> res;

  curr.clear();

  string::const_iterator i = str.begin();
  
  while (i != str.end())
    {
      if (*i != '%')
	{
	  assert (curr.type == conv_unspecified || curr.type == conv_literal);
	  curr.type = conv_literal;
	  curr.literal_string += *i;
	  ++i;
	  continue;
	}
      else if (i+1 == str.end() || *(i+1) == '%')
	{
	  assert(*i == '%');
	  // *i == '%' and *(i+1) == '%'; append only one '%' to the literal string
	  assert (curr.type == conv_unspecified || curr.type == conv_literal);
	  curr.type = conv_literal;
	  curr.literal_string += '%';
          i += 2;
	  continue;
	}
      else 
	{
	  assert(*i == '%');
	  if (curr.type != conv_unspecified)
	    {
	      // Flush any component we were previously accumulating
	      assert (curr.type == conv_literal);
	      res.push_back(curr);
	      curr.clear();
	    }
	}
      ++i;
      
      if (i == str.end())
	break;

      // Now we are definitely parsing a conversion. 
      // Begin by parsing flags (whicih are optional).

      switch (*i)
	{
	case '0':
	  curr.flags |= static_cast<unsigned long>(fmt_flag_zeropad);
	  ++i;
	  break;
	  
	case '+':
	  curr.flags |= static_cast<unsigned long>(fmt_flag_plus);
	  ++i;
	  break;
	  
	case '-':
	  curr.flags |= static_cast<unsigned long>(fmt_flag_left);
	  ++i;
	  break;
	  
	case ' ':
	  curr.flags |= static_cast<unsigned long>(fmt_flag_space);
	  ++i;
	  break;
	  
	case '#':
	  curr.flags |= static_cast<unsigned long>(fmt_flag_special);
	  ++i;
	  break;

	default:
	  break;
	}

      // Parse optional width
	  
      while (i != str.end() && isdigit(*i))
	{
	  curr.width *= 10;
	  curr.width += (*i - '0');
	  ++i;
	}

      if (i == str.end())
	break;

      // Parse optional precision
      if (*i == '.')
	{
	  ++i;
	  if (i == str.end())
	    break;
	  while (i != str.end() && isdigit(*i))
	    {
	      curr.precision *= 10;
	      curr.precision += (*i - '0');
	      ++i;
	    }
	}

      if (i == str.end())
	break;

      // Parse the actual conversion specifier (sdiouxX)
      switch (*i)
	{
	  // Valid conversion types
	case 'b':
	  curr.type = conv_binary;
	  break;
	  
	case 's':
	  curr.type = conv_string;
	  break;
	  
	case 'd':
	case 'i':
	  curr.type = conv_signed_decimal;
	  break;
	  
	case 'o':
	  curr.type = conv_unsigned_octal;
	  break;
	  
	case 'u':
	  curr.type = conv_unsigned_decimal;
	  break;
	  
	case 'p':
	  curr.type = conv_unsigned_ptr;
	  break;

	case 'X':
	  curr.type = conv_unsigned_uppercase_hex;
	  break;
	  
	case 'x':
	  curr.type = conv_unsigned_lowercase_hex;
	  break;

	case 'n':
	  curr.type = conv_size;
	  break;
	  
	default:
	  break;
	}
      
      if (curr.type == conv_unspecified)
	throw parse_error("invalid or missing conversion specifier");
      
      ++i;
      res.push_back(curr);
      curr.clear();      
    }

  // If there's a remaining partly-composed conversion, fail.
  if (!curr.is_empty())
    {
      if (curr.type == conv_literal)
	res.push_back(curr);      
      else
	throw parse_error("trailing incomplete print format conversion");
    }

  return res;
}


void print_format::print (ostream& o) const
{
  string name = (string(print_to_stream ? "" : "s") 
		 + string("print") 
		 + string(print_with_format ? "f" : ""));
  o << name << "(";
  if (print_with_format)
    {
      o << lex_cast_qstring (raw_components);
    }
  if (hist)
    hist->print(o);
  for (vector<expression*>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      if (i != args.begin() || print_with_format)
	o << ", ";
      (*i)->print(o);
    }
  o << ")";
}

void stat_op::print (ostream& o) const
{
  o << '@';
  switch (ctype)
    {
    case sc_average:
      o << "avg(";
      break;

    case sc_count:
      o << "count(";
      break;

    case sc_sum:
      o << "sum(";
      break;

    case sc_min:
      o << "min(";
      break;
      
    case sc_max:
      o << "max(";
      break;
    }
  stat->print(o);
  o << ")";
}

void 
hist_op::print (ostream& o) const
{
  o << '@';
  switch (htype)
    {
    case hist_linear:
      assert(params.size() == 3);
      o << "hist_linear(";
      stat->print(o);
      for (size_t i = 0; i < params.size(); ++i)
	{
	  o << ", " << params[i];
	}
      o << ")";
      break;

    case hist_log:
      assert(params.size() == 1);
      o << "hist_log(";
      stat->print(o);
      for (size_t i = 0; i < params.size(); ++i)
	{
	  o << ", " << params[i];
	}
      o << ")";
      break;
    }
}

ostream& operator << (ostream& o, const statement& k)
{
  k.print (o);
  return o;
}


void embeddedcode::print (ostream &o) const
{
  o << "%{";
  o << code;
  o << "%}";
}

void block::print (ostream& o) const
{
  o << "{" << endl;
  for (unsigned i=0; i<statements.size(); i++)
    o << *statements [i] << endl;
  o << "}";
}


void for_loop::print (ostream& o) const
{
  o << "for (";
  if (init) init->print (o);
  o << "; ";
  cond->print (o);
  o << "; ";
  if (incr) incr->print (o);
  o << ") ";
  block->print (o);
}


void foreach_loop::print (ostream& o) const
{
  o << "foreach ([";
  for (unsigned i=0; i<indexes.size(); i++)
    {
      if (i > 0) o << ", ";
      indexes[i]->print (o);
      if (sort_direction != 0 && sort_column == i+1)
	o << (sort_direction > 0 ? "+" : "-");
    }
  o << "] in ";
  base->print_indexable (o);
  if (sort_direction != 0 && sort_column == 0)
    o << (sort_direction > 0 ? "+" : "-");
  o << ") ";
  block->print (o);
}


void null_statement::print (ostream& o) const
{
  o << ";"; 
}


void expr_statement::print (ostream& o) const
{
  o << *value;
}


void return_statement::print (ostream& o) const
{
  o << "return " << *value;
}


void delete_statement::print (ostream& o) const
{
  o << "delete " << *value;
}

void next_statement::print (ostream& o) const
{
  o << "next";
}

void break_statement::print (ostream& o) const
{
  o << "break";
}

void continue_statement::print (ostream& o) const
{
  o << "continue";
}

void if_statement::print (ostream& o) const
{
  o << "if (" << *condition << ") "
    << *thenblock << endl;
  if (elseblock)
    o << "else " << *elseblock << endl;
}


void stapfile::print (ostream& o) const
{
  o << "# file " << name << endl;

  for (unsigned i=0; i<embeds.size(); i++)
    embeds[i]->print (o);

  for (unsigned i=0; i<globals.size(); i++)
    {
      o << "global ";
      globals[i]->print (o);
      o << endl;
    }

  for (unsigned i=0; i<aliases.size(); i++)
    {
      aliases[i]->print (o);
      o << endl;
    }

  for (unsigned i=0; i<probes.size(); i++)
    {
      probes[i]->print (o);
      o << endl;
    }

  for (unsigned j = 0; j < functions.size(); j++)
    {
      functions[j]->print (o);
      o << endl;
    }
}


void probe::print (ostream& o) const
{
  o << "probe ";
  printsig (o);
  o << *body;
}


void probe::printsig (ostream& o) const
{
  for (unsigned i=0; i<locations.size(); i++)
    {
      if (i > 0) o << "," << endl;
      locations[i]->print (o);
    }
}


void probe_point::print (ostream& o) const
{
  for (unsigned i=0; i<components.size(); i++)
    {
      if (i>0) o << ".";
      probe_point::component* c = components[i];
      o << c->functor;
      if (c->arg)
        o << "(" << *c->arg << ")";
    }
  if (optional)
    o << "?";
}


probe_alias::probe_alias(std::vector<probe_point*> const & aliases):
  probe (), alias_names (aliases)
{
}

void probe_alias::printsig (ostream& o) const
{
  for (unsigned i=0; i<alias_names.size(); i++)
    {
      o << (i>0 ? " = " : "");
      alias_names[i]->print (o);
    }
  o << " = ";
  for (unsigned i=0; i<locations.size(); i++)
    {
      o << (i>0 ? ", " : "");
      locations[i]->print (o);
    }
}


ostream& operator << (ostream& o, const probe_point& k)
{
  k.print (o);
  return o;
}


ostream& operator << (ostream& o, const symboldecl& k)
{
  k.print (o);
  return o;
}



// ------------------------------------------------------------------------
// visitors


void
block::visit (visitor* u)
{
  u->visit_block (this);
}


void
embeddedcode::visit (visitor* u)
{
  u->visit_embeddedcode (this);
}


void
for_loop::visit (visitor* u)
{
  u->visit_for_loop (this);
}

void
foreach_loop::visit (visitor* u)
{
  u->visit_foreach_loop (this);
}

void
null_statement::visit (visitor* u)
{
  u->visit_null_statement (this);
}

void
expr_statement::visit (visitor* u)
{
  u->visit_expr_statement (this);
}

void
return_statement::visit (visitor* u)
{
  u->visit_return_statement (this);
}

void
delete_statement::visit (visitor* u)
{
  u->push_active_lvalue (this->value);
  u->visit_delete_statement (this);
  u->pop_active_lvalue ();
}

void
if_statement::visit (visitor* u)
{
  u->visit_if_statement (this);
}

void
next_statement::visit (visitor* u)
{
  u->visit_next_statement (this);
}

void
break_statement::visit (visitor* u)
{
  u->visit_break_statement (this);
}

void
continue_statement::visit (visitor* u)
{
  u->visit_continue_statement (this);
}

void
literal_string::visit(visitor* u)
{
  u->visit_literal_string (this);
}

void
literal_number::visit(visitor* u)
{
  u->visit_literal_number (this);
}

void
binary_expression::visit (visitor* u)
{
  u->visit_binary_expression (this);
}

void
unary_expression::visit (visitor* u)
{
  u->visit_unary_expression (this);
}

void
pre_crement::visit (visitor* u)
{
  u->push_active_lvalue (this->operand);
  u->visit_pre_crement (this);
  u->pop_active_lvalue ();
}

void
post_crement::visit (visitor* u)
{
  u->push_active_lvalue (this->operand);
  u->visit_post_crement (this);
  u->pop_active_lvalue ();
}

void
logical_or_expr::visit (visitor* u)
{
  u->visit_logical_or_expr (this);
}

void
logical_and_expr::visit (visitor* u)
{
  u->visit_logical_and_expr (this);
}

void
array_in::visit (visitor* u)
{
  u->visit_array_in (this);
}

void
comparison::visit (visitor* u)
{
  u->visit_comparison (this);
}

void
concatenation::visit (visitor* u)
{
  u->visit_concatenation (this);
}

void
ternary_expression::visit (visitor* u)
{
  u->visit_ternary_expression (this);
}

void
assignment::visit (visitor* u)
{
  u->push_active_lvalue (this->left);
  u->visit_assignment (this);
  u->pop_active_lvalue ();
}

void
symbol::visit (visitor* u)
{
  u->visit_symbol (this);
}

void 
target_symbol::visit (visitor* u)
{
  u->visit_target_symbol(this);
}

void
arrayindex::visit (visitor* u)
{
  u->visit_arrayindex (this);
}

void
functioncall::visit (visitor* u)
{
  u->visit_functioncall (this);
}

void
print_format::visit (visitor *u)
{
  u->visit_print_format (this);
}

void
stat_op::visit (visitor *u)
{
  u->visit_stat_op (this);
}

void
hist_op::visit (visitor *u)
{
  u->visit_hist_op (this);
}

void 
indexable::print_indexable (std::ostream& o) const
{
  const symbol *sym;
  const hist_op *hist;
  classify_const_indexable(this, sym, hist);
  if (sym)
    sym->print (o);
  else
    {
      assert (hist);
      hist->print (o);
    }  
}

void 
indexable::visit_indexable (visitor* u)
{
  symbol *sym;
  hist_op *hist;
  classify_indexable(this, sym, hist);
  if (sym)
    sym->visit (u);
  else
    {
      assert (hist);
      hist->visit (u);
    }
}


bool 
indexable::is_symbol(symbol *& sym_out)
{
  sym_out = NULL;
  return false;
}

bool 
indexable::is_hist_op(hist_op *& hist_out)
{
  hist_out = NULL;
  return false;
}

bool 
indexable::is_const_symbol(const symbol *& sym_out) const
{
  sym_out = NULL;
  return false;
}

bool 
indexable::is_const_hist_op(const hist_op *& hist_out) const
{
  hist_out = NULL;
  return false;
}

bool 
symbol::is_symbol(symbol *& sym_out)
{
  sym_out = this;
  return true;
}

bool 
symbol::is_const_symbol(const symbol *& sym_out) const
{
  sym_out = this;
  return true;
}

const token *
symbol::get_tok() const
{
  return tok;
}

bool 
hist_op::is_hist_op(hist_op *& hist_out)
{
  hist_out = this;
  return true;
}

bool 
hist_op::is_const_hist_op(const hist_op *& hist_out) const
{
  hist_out = this;
  return true;
}

const token *
hist_op::get_tok() const
{
  return tok;
}

void
classify_indexable(indexable* ix,
		   symbol *& array_out,
		   hist_op *& hist_out) 
{
  array_out = NULL;
  hist_out = NULL;
  if (!(ix->is_symbol (array_out) || ix->is_hist_op (hist_out)))
    throw semantic_error("Expecting symbol or histogram operator", ix->get_tok());
  if (ix && !(hist_out || array_out))
    throw semantic_error("Failed to classify indexable", ix->get_tok());
}

void
classify_const_indexable(const indexable* ix,
			 const symbol *& array_out,
			 const hist_op *& hist_out) 
{
  array_out = NULL;
  hist_out = NULL;
  if (!(ix->is_const_symbol(array_out) || ix->is_const_hist_op(hist_out)))
    throw semantic_error("Expecting symbol or histogram operator", ix->get_tok());
}

// ------------------------------------------------------------------------

bool 
visitor::is_active_lvalue(expression *e)
{
  for (unsigned i = 0; i < active_lvalues.size(); ++i)
    {
      if (active_lvalues[i] == e)
	return true;
    }
  return false;
}

void 
visitor::push_active_lvalue(expression *e)
{
  active_lvalues.push_back(e);
}

void 
visitor::pop_active_lvalue()
{
  assert(!active_lvalues.empty());
  active_lvalues.pop_back();
}



// ------------------------------------------------------------------------

void
traversing_visitor::visit_block (block* s)
{
  for (unsigned i=0; i<s->statements.size(); i++)
    s->statements[i]->visit (this);
}

void
traversing_visitor::visit_embeddedcode (embeddedcode* s)
{
}

void
traversing_visitor::visit_null_statement (null_statement* s)
{
}

void
traversing_visitor::visit_expr_statement (expr_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_if_statement (if_statement* s)
{
  s->condition->visit (this);
  s->thenblock->visit (this);
  if (s->elseblock)
    s->elseblock->visit (this);
}

void
traversing_visitor::visit_for_loop (for_loop* s)
{
  if (s->init) s->init->visit (this);
  s->cond->visit (this);
  if (s->incr) s->incr->visit (this);
  s->block->visit (this);
}

void
traversing_visitor::visit_foreach_loop (foreach_loop* s)
{
  symbol *array = NULL;  
  hist_op *hist = NULL;
  classify_indexable (s->base, array, hist);
  if (array)
    array->visit(this);
  else
    hist->visit(this);

  for (unsigned i=0; i<s->indexes.size(); i++)
    s->indexes[i]->visit (this);
  s->block->visit (this);
}

void
traversing_visitor::visit_return_statement (return_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_delete_statement (delete_statement* s)
{
  s->value->visit (this);
}

void
traversing_visitor::visit_next_statement (next_statement* s)
{
}

void
traversing_visitor::visit_break_statement (break_statement* s)
{
}

void
traversing_visitor::visit_continue_statement (continue_statement* s)
{
}

void
traversing_visitor::visit_literal_string (literal_string* e)
{
}

void
traversing_visitor::visit_literal_number (literal_number* e)
{
}

void
traversing_visitor::visit_binary_expression (binary_expression* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_unary_expression (unary_expression* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_pre_crement (pre_crement* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_post_crement (post_crement* e)
{
  e->operand->visit (this);
}


void
traversing_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_array_in (array_in* e)
{
  e->operand->visit (this);
}

void
traversing_visitor::visit_comparison (comparison* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_concatenation (concatenation* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_ternary_expression (ternary_expression* e)
{
  e->cond->visit (this);
  e->truevalue->visit (this);
  e->falsevalue->visit (this);
}

void
traversing_visitor::visit_assignment (assignment* e)
{
  e->left->visit (this);
  e->right->visit (this);
}

void
traversing_visitor::visit_symbol (symbol* e)
{
}

void
traversing_visitor::visit_target_symbol (target_symbol* e)
{
}

void
traversing_visitor::visit_arrayindex (arrayindex* e)
{
  for (unsigned i=0; i<e->indexes.size(); i++)
    e->indexes[i]->visit (this);

  symbol *array = NULL;
  hist_op *hist = NULL;
  classify_indexable(e->base, array, hist);
  if (array)
    return array->visit(this);
  else
    return hist->visit(this);
}

void
traversing_visitor::visit_functioncall (functioncall* e)
{
  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);
}

void
traversing_visitor::visit_print_format (print_format* e)
{
  for (unsigned i=0; i<e->args.size(); i++)
    e->args[i]->visit (this);
  if (e->hist)
    e->hist->visit(this);
}

void
traversing_visitor::visit_stat_op (stat_op* e)
{
  e->stat->visit (this);
}

void
traversing_visitor::visit_hist_op (hist_op* e)
{
  e->stat->visit (this);
}


void
functioncall_traversing_visitor::visit_functioncall (functioncall* e) 
{
  traversing_visitor::visit_functioncall (e);

  // prevent infinite recursion
  if (traversed.find (e->referent) == traversed.end ())
    {
      traversed.insert (e->referent);
      // recurse
      functiondecl* last_current_function = current_function;
      current_function = e->referent;
      e->referent->body->visit (this);
      current_function = last_current_function;
    }
}


void
varuse_collecting_visitor::visit_embeddedcode (embeddedcode *s)
{
  // We want to elide embedded-C functions when possible.  For
  // example, each $target variable access is expanded to an
  // embedded-C function call.  Yet, for safety reasons, we should
  // presume that embedded-C functions have intentional side-effects.
  //
  // To tell these two types of functions apart, we apply a
  // Kludge(tm): we look for a magic string within the function body.
  // $target variables as rvalues will have this; lvalues won't.
  // Also, explicit side-effect-free tapset functions will have this.
  
  assert (current_function); // only they get embedded code
  if (s->code.find ("/* pure */") != string::npos)
    return;

  embedded_seen = true;
}


void
varuse_collecting_visitor::visit_print_format (print_format* e)
{
  // NB: Instead of being top-level statements, "print" and "printf"
  // are implemented as statement-expressions containing a
  // print_format.  They have side-effects, but not via the
  // embedded-code detection method above. 
  //
  // But sprint and sprintf don't have side-effects.

  if (e->print_to_stream)
    embedded_seen = true; // a proxy for "has unknown side-effects"

  functioncall_traversing_visitor::visit_print_format (e);
}


void
varuse_collecting_visitor::visit_assignment (assignment *e)
{
  if (e->op == "=" || e->op == "<<<") // pure writes
    {
      expression* last_lvalue = current_lvalue;
      current_lvalue = e->left; // leave a mark for ::visit_symbol
      functioncall_traversing_visitor::visit_assignment (e);
      current_lvalue = last_lvalue;
    }
  else // read-modify-writes
    {
      expression* last_lrvalue = current_lrvalue;
      current_lrvalue = e->left; // leave a mark for ::visit_symbol
      functioncall_traversing_visitor::visit_assignment (e);
      current_lrvalue = last_lrvalue;
    }
}

void
varuse_collecting_visitor::visit_symbol (symbol *e)
{
  if (e->referent == 0)
    throw semantic_error ("symbol without referent", e->tok);

  if (current_lvalue == e || current_lrvalue == e)
    {
      written.insert (e->referent);
      // clog << "write ";
    }
  if (current_lvalue != e || current_lrvalue == e)
    {
      read.insert (e->referent);
      // clog << "read ";
    }
  // clog << *e->tok << endl;
}

// NB: stat_op need not be overridden, since it will get to
// visit_symbol and only as a possible rvalue.


void
varuse_collecting_visitor::visit_arrayindex (arrayindex *e)
{
  // Hooking this callback is necessary because of the hacky
  // statistics representation.  For the expression "i[4] = 5", the
  // incoming lvalue will point to this arrayindex.  However, the
  // symbol corresponding to the "i[4]" is multiply inherited with
  // arrayindex.  If the symbol base part of this object is not at
  // offset 0, then static_cast<symbol*>(e) may result in a different
  // address, and not match lvalue by number when we recurse that way.
  // So we explicitly override the incoming lvalue/lrvalue values to
  // point at the embedded objects' actual base addresses.

  expression* last_lrvalue = current_lrvalue;
  expression* last_lvalue = current_lvalue;

  symbol *array = NULL;
  hist_op *hist = NULL;
  classify_indexable(e->base, array, hist);

  if (array)
    {
      if (current_lrvalue == e) current_lrvalue = array;
      if (current_lvalue == e) current_lvalue = array;
      functioncall_traversing_visitor::visit_arrayindex (e);
    }
  else // if (hist)
    {
      if (current_lrvalue == e) current_lrvalue = hist->stat;
      if (current_lvalue == e) current_lvalue = hist->stat;
      functioncall_traversing_visitor::visit_arrayindex (e);
    }

  current_lrvalue = last_lrvalue;
  current_lvalue = last_lvalue;
}


void
varuse_collecting_visitor::visit_pre_crement (pre_crement *e)
{
  expression* last_lrvalue = current_lrvalue;
  current_lrvalue = e->operand; // leave a mark for ::visit_symbol
  functioncall_traversing_visitor::visit_pre_crement (e);
  current_lrvalue = last_lrvalue;
}

void
varuse_collecting_visitor::visit_post_crement (post_crement *e)
{
  expression* last_lrvalue = current_lrvalue;
  current_lrvalue = e->operand; // leave a mark for ::visit_symbol
  functioncall_traversing_visitor::visit_post_crement (e);
  current_lrvalue = last_lrvalue;
}

void
varuse_collecting_visitor::visit_foreach_loop (foreach_loop* s)
{
  functioncall_traversing_visitor::visit_foreach_loop (s);
  // If the collection is sorted, imply a "write" access to the
  // array in addition to the "read" one already noted in the
  // base class call above.
  if (s->sort_direction)
    {
      symbol *array = NULL;  
      hist_op *hist = NULL;
      classify_indexable (s->base, array, hist);
      if (array) this->written.insert (array->referent);
      // XXX: Can hist_op iterations be sorted?
    }
}


void
varuse_collecting_visitor::visit_delete_statement (delete_statement* s)
{
  // Ideally, this would be treated like an assignment: a plain write
  // to the underlying value ("lvalue").  XXX: However, the
  // optimization pass is not smart enough to remove an unneeded
  // "delete" yet, so we pose more like a *crement ("lrvalue").  This
  // should protect the underlying value from optimizional mischief.
  expression* last_lrvalue = current_lrvalue;
  current_lrvalue = s->value; // leave a mark for ::visit_symbol
  functioncall_traversing_visitor::visit_delete_statement (s);
  current_lrvalue = last_lrvalue;
}


// ------------------------------------------------------------------------


throwing_visitor::throwing_visitor (const std::string& m): msg (m) {}
throwing_visitor::throwing_visitor (): msg ("invalid element") {}


void
throwing_visitor::throwone (const token* t)
{
  throw semantic_error (msg, t);
}

void
throwing_visitor::visit_block (block* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_embeddedcode (embeddedcode* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_null_statement (null_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_expr_statement (expr_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_if_statement (if_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_for_loop (for_loop* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_foreach_loop (foreach_loop* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_return_statement (return_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_delete_statement (delete_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_next_statement (next_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_break_statement (break_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_continue_statement (continue_statement* s)
{
  throwone (s->tok);
}

void
throwing_visitor::visit_literal_string (literal_string* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_literal_number (literal_number* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_binary_expression (binary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_unary_expression (unary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_pre_crement (pre_crement* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_post_crement (post_crement* e)
{
  throwone (e->tok);
}


void
throwing_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_array_in (array_in* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_comparison (comparison* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_concatenation (concatenation* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_ternary_expression (ternary_expression* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_assignment (assignment* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_symbol (symbol* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_target_symbol (target_symbol* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_arrayindex (arrayindex* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_functioncall (functioncall* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_print_format (print_format* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_stat_op (stat_op* e)
{
  throwone (e->tok);
}

void
throwing_visitor::visit_hist_op (hist_op* e)
{
  throwone (e->tok);
}


// ------------------------------------------------------------------------


void
deep_copy_visitor::visit_block (block* s)
{
  block* n = new block;
  n->tok = s->tok;
  for (unsigned i = 0; i < s->statements.size(); ++i)
    {
      statement* ns;
      require <statement*> (this, &ns, s->statements[i]);
      n->statements.push_back(ns);
    }
  provide <block*> (this, n);
}

void
deep_copy_visitor::visit_embeddedcode (embeddedcode* s)
{
  embeddedcode* n = new embeddedcode;
  n->tok = s->tok;
  n->code = s->code;
  provide <embeddedcode*> (this, n);
}

void
deep_copy_visitor::visit_null_statement (null_statement* s)
{
  null_statement* n = new null_statement;
  n->tok = s->tok;
  provide <null_statement*> (this, n);
}

void
deep_copy_visitor::visit_expr_statement (expr_statement* s)
{
  expr_statement* n = new expr_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->value), s->value);
  provide <expr_statement*> (this, n);
}

void
deep_copy_visitor::visit_if_statement (if_statement* s)
{
  if_statement* n = new if_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->condition), s->condition);
  require <statement*> (this, &(n->thenblock), s->thenblock);
  require <statement*> (this, &(n->elseblock), s->elseblock);
  provide <if_statement*> (this, n);
}

void
deep_copy_visitor::visit_for_loop (for_loop* s)
{
  for_loop* n = new for_loop;
  n->tok = s->tok;
  require <expr_statement*> (this, &(n->init), s->init);
  require <expression*> (this, &(n->cond), s->cond);
  require <expr_statement*> (this, &(n->incr), s->incr);
  require <statement*> (this, &(n->block), s->block);  
  provide <for_loop*> (this, n);
}

void
deep_copy_visitor::visit_foreach_loop (foreach_loop* s)
{
  foreach_loop* n = new foreach_loop;
  n->tok = s->tok;
  for (unsigned i = 0; i < s->indexes.size(); ++i)
    {
      symbol* sym;
      require <symbol*> (this, &sym, s->indexes[i]);
      n->indexes.push_back(sym);
    }

  require <indexable*> (this, &(n->base), s->base);

  n->sort_direction = s->sort_direction;
  n->sort_column = s->sort_column;

  require <statement*> (this, &(n->block), s->block);
  provide <foreach_loop*> (this, n);
}

void
deep_copy_visitor::visit_return_statement (return_statement* s)
{
  return_statement* n = new return_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->value), s->value);
  provide <return_statement*> (this, n);
}

void
deep_copy_visitor::visit_delete_statement (delete_statement* s)
{
  delete_statement* n = new delete_statement;
  n->tok = s->tok;
  require <expression*> (this, &(n->value), s->value);
  provide <delete_statement*> (this, n);
}

void
deep_copy_visitor::visit_next_statement (next_statement* s)
{
  next_statement* n = new next_statement;
  n->tok = s->tok;
  provide <next_statement*> (this, n);
}

void
deep_copy_visitor::visit_break_statement (break_statement* s)
{
  break_statement* n = new break_statement;
  n->tok = s->tok;
  provide <break_statement*> (this, n);
}

void
deep_copy_visitor::visit_continue_statement (continue_statement* s)
{
  continue_statement* n = new continue_statement;
  n->tok = s->tok;
  provide <continue_statement*> (this, n);
}

void
deep_copy_visitor::visit_literal_string (literal_string* e)
{
  literal_string* n = new literal_string(e->value);
  n->tok = e->tok;
  provide <literal_string*> (this, n);
}

void
deep_copy_visitor::visit_literal_number (literal_number* e)
{
  literal_number* n = new literal_number(e->value);
  n->tok = e->tok;
  provide <literal_number*> (this, n);
}

void
deep_copy_visitor::visit_binary_expression (binary_expression* e)
{
  binary_expression* n = new binary_expression;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <binary_expression*> (this, n);
}

void
deep_copy_visitor::visit_unary_expression (unary_expression* e)
{
  unary_expression* n = new unary_expression;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->operand), e->operand);
  provide <unary_expression*> (this, n);
}

void
deep_copy_visitor::visit_pre_crement (pre_crement* e)
{
  pre_crement* n = new pre_crement;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->operand), e->operand);
  provide <pre_crement*> (this, n);
}

void
deep_copy_visitor::visit_post_crement (post_crement* e)
{
  post_crement* n = new post_crement;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->operand), e->operand);
  provide <post_crement*> (this, n);
}


void
deep_copy_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  logical_or_expr* n = new logical_or_expr;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <logical_or_expr*> (this, n);
}

void
deep_copy_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  logical_and_expr* n = new logical_and_expr;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <logical_and_expr*> (this, n);
}

void
deep_copy_visitor::visit_array_in (array_in* e)
{
  array_in* n = new array_in;
  n->tok = e->tok;
  require <arrayindex*> (this, &(n->operand), e->operand);
  provide <array_in*> (this, n);
}

void
deep_copy_visitor::visit_comparison (comparison* e)
{
  comparison* n = new comparison;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <comparison*> (this, n);
}

void
deep_copy_visitor::visit_concatenation (concatenation* e)
{
  concatenation* n = new concatenation;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <concatenation*> (this, n);
}

void
deep_copy_visitor::visit_ternary_expression (ternary_expression* e)
{
  ternary_expression* n = new ternary_expression;
  n->tok = e->tok;
  require <expression*> (this, &(n->cond), e->cond);
  require <expression*> (this, &(n->truevalue), e->truevalue);
  require <expression*> (this, &(n->falsevalue), e->falsevalue);
  provide <ternary_expression*> (this, n);
}

void
deep_copy_visitor::visit_assignment (assignment* e)
{
  assignment* n = new assignment;
  n->op = e->op;
  n->tok = e->tok;
  require <expression*> (this, &(n->left), e->left);
  require <expression*> (this, &(n->right), e->right);
  provide <assignment*> (this, n);
}

void
deep_copy_visitor::visit_symbol (symbol* e)
{
  symbol* n = new symbol;
  n->tok = e->tok;
  n->name = e->name;
  n->referent = NULL;
  provide <symbol*> (this, n);
}

void
deep_copy_visitor::visit_target_symbol (target_symbol* e)
{
  target_symbol* n = new target_symbol;
  n->tok = e->tok;
  n->base_name = e->base_name;
  n->components = e->components;
  provide <target_symbol*> (this, n);
}

void
deep_copy_visitor::visit_arrayindex (arrayindex* e)
{
  arrayindex* n = new arrayindex;
  n->tok = e->tok;

  require <indexable*> (this, &(n->base), e->base);

  for (unsigned i = 0; i < e->indexes.size(); ++i)
    {
      expression* ne;
      require <expression*> (this, &ne, e->indexes[i]);
      n->indexes.push_back(ne);
    }
  provide <arrayindex*> (this, n);
}

void
deep_copy_visitor::visit_functioncall (functioncall* e)
{
  functioncall* n = new functioncall;
  n->tok = e->tok;
  n->function = e->function;
  n->referent = NULL;
  for (unsigned i = 0; i < e->args.size(); ++i)
    {
      expression* na;
      require <expression*> (this, &na, e->args[i]);
      n->args.push_back(na);
    }
  provide <functioncall*> (this, n);
}

void
deep_copy_visitor::visit_print_format (print_format* e)
{
  print_format* n = new print_format;
  n->tok = e->tok;
  n->print_with_format = e->print_with_format;
  n->print_to_stream = e->print_to_stream;
  n->raw_components = e->raw_components;
  n->components = e->components;
  for (unsigned i = 0; i < e->args.size(); ++i)
    {
      expression* na;
      require <expression*> (this, &na, e->args[i]);
      n->args.push_back(na);
    }
  if (e->hist)
    require <hist_op*> (this, &n->hist, e->hist);
  provide <print_format*> (this, n);
}

void
deep_copy_visitor::visit_stat_op (stat_op* e)
{
  stat_op* n = new stat_op;
  n->tok = e->tok;
  n->ctype = e->ctype;
  require <expression*> (this, &(n->stat), e->stat);
  provide <stat_op*> (this, n);
}

void
deep_copy_visitor::visit_hist_op (hist_op* e)
{
  hist_op* n = new hist_op;
  n->tok = e->tok;
  n->htype = e->htype;
  n->params = e->params;
  require <expression*> (this, &(n->stat), e->stat);
  provide <hist_op*> (this, n);
}

block* 
deep_copy_visitor::deep_copy (block* b)
{
  block* n;
  deep_copy_visitor v;
  require <block*> (&v, &n, b);
  return n;
}

statement* 
deep_copy_visitor::deep_copy (statement* s)
{
  statement* n;
  deep_copy_visitor v;
  require <statement*> (&v, &n, s);
  return n;
}
