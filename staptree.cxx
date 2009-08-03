// parse tree functions
// Copyright (C) 2005-2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "parse.h"
#include "util.h"

#include <iostream>
#include <typeinfo>
#include <sstream>
#include <cassert>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstring>

using namespace std;



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
  components(comps), tok(t), optional (false), sufficient (false),
  condition (0)
{
}

// NB: shallow-copy of compoonents & condition!
probe_point::probe_point (const probe_point& pp):
  components(pp.components), tok(pp.tok), optional (pp.optional), sufficient (pp.sufficient),
  condition (pp.condition)
{
}


probe_point::probe_point ():
  tok (0), optional (false), sufficient (false), condition (0)
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
  arity (-1), maxsize(0), init(NULL)
{
}


void
vardecl::set_arity (int a)
{
  if (a < 0)
    return;

  if ((arity != a && arity >= 0) || (a == 0 && maxsize > 0))
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
  if (a == 0 && maxsize > 0)
    return false;
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


void
target_symbol::assert_no_components(const std::string& tapset)
{
  if (components.empty())
    return;

  switch (components[0].type)
    {
    case target_symbol::comp_literal_array_index:
    case target_symbol::comp_expression_array_index:
      throw semantic_error(tapset + " variable '" + base_name +
                           "' may not be used as array",
                           components[0].tok);
    case target_symbol::comp_struct_member:
      throw semantic_error(tapset + " variable '" + base_name +
                           "' may not be used as a structure",
                           components[0].tok);
    default:
      throw semantic_error ("invalid use of " + tapset +
                            " variable '" + base_name + "'",
                            components[0].tok);
    }
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


void target_symbol::component::print (ostream& o) const
{
  switch (type)
    {
    case comp_struct_member:
      o << "->" << member;
      break;
    case comp_literal_array_index:
      o << '[' << num_index << ']';
      break;
    case comp_expression_array_index:
      o << '[' << *expr_index << ']';
      break;
    }
}


std::ostream& operator << (std::ostream& o, const target_symbol::component& c)
{
  c.print (o);
  return o;
}


void target_symbol::print (ostream& o) const
{
  if (addressof)
    o << "&";
  o << base_name;
  for (unsigned i = 0; i < components.size(); ++i)
    o << components[i];
}


void cast_op::print (ostream& o) const
{
  if (addressof)
    o << "&";
  o << base_name << '(' << *operand;
  o << ", " << lex_cast_qstring (type);
  if (module.length() > 0)
    o << ", " << lex_cast_qstring (module);
  o << ')';
  for (unsigned i = 0; i < components.size(); ++i)
    o << components[i];
}


void vardecl::print (ostream& o) const
{
  o << name;
  if (maxsize > 0)
    o << "[" << maxsize << "]";
  if (arity > 0 || index_types.size() > 0)
    o << "[...]";
  if (init)
    {
      o << " = ";
      init->print(o);
    }
}


void vardecl::printsig (ostream& o) const
{
  o << name;
  if (maxsize > 0)
    o << "[" << maxsize << "]";
  o << ":" << type;
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


bool
print_format::parse_print(const std::string &name,
  bool &stream, bool &format, bool &delim, bool &newline, bool &_char)
{
  const char *n = name.c_str();

  stream = true;
  format = delim = newline = _char = false;

  if (strcmp(n, "print_char") == 0)
    {
      _char = true;
      return true;
    }

  if (*n == 's')
    {
      stream = false;
      ++n;
    }

  if (0 != strncmp(n, "print", 5))
    return false;
  n += 5;

  if (*n == 'f')
    {
      format = true;
      ++n;
    }
  else
    {
      if (*n == 'd')
        {
	  delim = true;
	  ++n;
	}

      if (*n == 'l' && *(n+1) == 'n')
        {
	  newline = true;
	  n += 2;
	}
    }

  return (*n == '\0');
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
              // See also: c_unparser::visit_literal_string and lex_cast_qstring
	      if (*j == '%')
		oss << '%';
              else if(*j == '"')
                oss << '\\';
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

	  if (i->widthtype == width_dynamic)
	    oss << '*';
	  else if (i->widthtype != width_unspecified && i->width > 0)
	    oss << i->width;

	  if (i->prectype == prec_dynamic)
	    oss << ".*";
	  else if (i->prectype != prec_unspecified && i->precision > 0)
	    oss << '.' << i->precision;

	  switch (i->type)
	    {
	    case conv_binary:
	      oss << "b";
	      break;

	    case conv_char:
	      oss << "llc";
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

	    case conv_memory:
	      oss << 'm';
	      break;

	    case conv_memory_hex:
	      oss << 'M';
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
      // Begin by parsing flags (which are optional).

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

      if (i == str.end())
	break;

      // Parse optional width
      if (*i == '*')
	{
	  curr.widthtype = width_dynamic;
	  ++i;
	}
      else if (isdigit(*i))
	{
	  curr.widthtype = width_static;
	  curr.width = 0;
	  do
	    {
	      curr.width *= 10;
	      curr.width += (*i - '0');
	      ++i;
	    }
	  while (i != str.end() && isdigit(*i));
	}

      if (i == str.end())
	break;

      // Parse optional precision
      if (*i == '.')
	{
	  ++i;
	  if (i == str.end())
	    break;
	  if (*i == '*')
	    {
	      curr.prectype = prec_dynamic;
	      ++i;
	    }
	  else if (isdigit(*i))
	    {
	      curr.prectype = prec_static;
	      curr.precision = 0;
	      do
		{
		  curr.precision *= 10;
		  curr.precision += (*i - '0');
		  ++i;
		}
	      while (i != str.end() && isdigit(*i));
	    }
	}

      if (i == str.end())
	break;

      // Parse the actual conversion specifier (bcsmdioupxXn)
      switch (*i)
	{
	  // Valid conversion types
	case 'b':
	  curr.type = conv_binary;
	  break;

	case 'c':
	  curr.type = conv_char;
	  break;

	case 's':
	  curr.type = conv_string;
	  break;

	case 'm':
	  curr.type = conv_memory;
	  break;

	case 'M':
	  curr.type = conv_memory_hex;
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
  o << tok->content << "(";
  if (print_with_format)
    o << lex_cast_qstring (raw_components);
  if (print_with_delim)
    o << lex_cast_qstring (delimiter.literal_string);
  if (hist)
    hist->print(o);
  for (vector<expression*>::const_iterator i = args.begin();
       i != args.end(); ++i)
    {
      if (i != args.begin() || print_with_format || print_with_delim)
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
      assert(params.size() == 0);
      o << "hist_log(";
      stat->print(o);
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

block::block (statement* car, statement* cdr)
{
  statements.push_back(car);
  statements.push_back(cdr);
  this->tok = car->tok;
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
  if (limit)
    {
      o << " limit ";
      limit->print (o);
    }
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
  const probe_alias *alias = get_alias ();
  if (alias)
    {
      alias->printsig (o);
      return;
    }

  for (unsigned i=0; i<locations.size(); i++)
    {
      if (i > 0) o << ",";
      locations[i]->print (o);
    }
}


void
probe::collect_derivation_chain (std::vector<probe*> &probes_list)
{
  probes_list.push_back(this);
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
  if (sufficient)
    o << "!";
  else if (optional) // sufficient implies optional
    o << "?";
  if (condition)
    o<< " if (" << *condition << ")";
}

string probe_point::str ()
{
  ostringstream o;
  for (unsigned i=0; i<components.size(); i++)
    {
      if (i>0) o << ".";
      probe_point::component* c = components[i];
      o << c->functor;
      if (c->arg)
        o << "(" << *c->arg << ")";
    }
  if (sufficient)
    o << "!";
  else if (optional) // sufficient implies optional
    o << "?";
  if (condition)
    o<< " if (" << *condition << ")";
  return o.str();
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
      if (i > 0) o << ", ";
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
target_symbol::visit_components (visitor* u)
{
  for (unsigned i = 0; i < components.size(); ++i)
    if (components[i].type == comp_expression_array_index)
      components[i].expr_index->visit (u);
}

void
target_symbol::visit_components (update_visitor* u)
{
  for (unsigned i = 0; i < components.size(); ++i)
    if (components[i].type == comp_expression_array_index)
      u->replace (components[i].expr_index);
}

void
cast_op::visit (visitor* u)
{
  u->visit_cast_op(this);
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
traversing_visitor::visit_embeddedcode (embeddedcode*)
{
}

void
traversing_visitor::visit_null_statement (null_statement*)
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

  if (s->limit)
    s->limit->visit (this);

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
traversing_visitor::visit_next_statement (next_statement*)
{
}

void
traversing_visitor::visit_break_statement (break_statement*)
{
}

void
traversing_visitor::visit_continue_statement (continue_statement*)
{
}

void
traversing_visitor::visit_literal_string (literal_string*)
{
}

void
traversing_visitor::visit_literal_number (literal_number*)
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
traversing_visitor::visit_symbol (symbol*)
{
}

void
traversing_visitor::visit_target_symbol (target_symbol* e)
{
  e->visit_components (this);
}

void
traversing_visitor::visit_cast_op (cast_op* e)
{
  e->operand->visit (this);
  e->visit_components (this);
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
varuse_collecting_visitor::visit_target_symbol (target_symbol *e)
{
  // Still-unresolved target symbol assignments get treated as
  // generating side-effects like embedded-C, to prevent premature
  // elision and later error message suppression (PR5516).  rvalue use
  // of unresolved target symbols is OTOH not considered a side-effect.

  if (is_active_lvalue (e))
    embedded_seen = true;

  functioncall_traversing_visitor::visit_target_symbol (e);
}

void
varuse_collecting_visitor::visit_cast_op (cast_op *e)
{
  // As with target_symbols, unresolved cast assignments need to preserved
  // for later error handling.
  if (is_active_lvalue (e))
    embedded_seen = true;

  functioncall_traversing_visitor::visit_cast_op (e);
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

  // We could handle initialized globals by marking them as "written".
  // However, this current visitor may be called for a function or
  // probe body, from the point of view of which this global is
  // already initialized, so not written.
  /*
  if (e->referent->init)
    written.insert (e->referent);
  */

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
  // NB: we duplicate so don't bother call
  // functioncall_traversing_visitor::visit_foreach_loop (s);

  symbol *array = NULL;
  hist_op *hist = NULL;
  classify_indexable (s->base, array, hist);
  if (array)
    array->visit(this);
  else
    hist->visit(this);

  // If the collection is sorted, imply a "write" access to the
  // array in addition to the "read" one already noted above.
  if (s->sort_direction)
    {
      symbol *array = NULL;
      hist_op *hist = NULL;
      classify_indexable (s->base, array, hist);
      if (array) this->written.insert (array->referent);
      // XXX: Can hist_op iterations be sorted?
    }

  // NB: don't forget to visit the index expressions, which are lvalues.
  for (unsigned i=0; i<s->indexes.size(); i++)
    {
      expression* last_lvalue = current_lvalue;
      current_lvalue = s->indexes[i]; // leave a mark for ::visit_symbol
      s->indexes[i]->visit (this);
      current_lvalue = last_lvalue;
    }

  if (s->limit)
    s->limit->visit (this);

  s->block->visit (this);
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

bool
varuse_collecting_visitor::side_effect_free ()
{
  return (written.empty() && !embedded_seen);
}


bool
varuse_collecting_visitor::side_effect_free_wrt (const set<vardecl*>& vars)
{
  // A looser notion of side-effect-freeness with respect to a given
  // list of variables.

  // That's useful because the written list may consist of local
  // variables of called functions.  But visible side-effects only
  // occur if the client's locals, or any globals are written-to.

  set<vardecl*> intersection;
  insert_iterator<set<vardecl*> > int_it (intersection, intersection.begin());
  set_intersection (written.begin(), written.end(),
                    vars.begin(), vars.end(),
                    int_it);

  return (intersection.empty() && !embedded_seen);
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
throwing_visitor::visit_cast_op (cast_op* e)
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
update_visitor::visit_block (block* s)
{
  for (unsigned i = 0; i < s->statements.size(); ++i)
    replace (s->statements[i]);
  provide (s);
}

void
update_visitor::visit_embeddedcode (embeddedcode* s)
{
  provide (s);
}

void
update_visitor::visit_null_statement (null_statement* s)
{
  provide (s);
}

void
update_visitor::visit_expr_statement (expr_statement* s)
{
  replace (s->value);
  provide (s);
}

void
update_visitor::visit_if_statement (if_statement* s)
{
  replace (s->condition);
  replace (s->thenblock);
  replace (s->elseblock);
  provide (s);
}

void
update_visitor::visit_for_loop (for_loop* s)
{
  replace (s->init);
  replace (s->cond);
  replace (s->incr);
  replace (s->block);
  provide (s);
}

void
update_visitor::visit_foreach_loop (foreach_loop* s)
{
  for (unsigned i = 0; i < s->indexes.size(); ++i)
    replace (s->indexes[i]);
  replace (s->base);
  replace (s->limit);
  replace (s->block);
  provide (s);
}

void
update_visitor::visit_return_statement (return_statement* s)
{
  replace (s->value);
  provide (s);
}

void
update_visitor::visit_delete_statement (delete_statement* s)
{
  replace (s->value);
  provide (s);
}

void
update_visitor::visit_next_statement (next_statement* s)
{
  provide (s);
}

void
update_visitor::visit_break_statement (break_statement* s)
{
  provide (s);
}

void
update_visitor::visit_continue_statement (continue_statement* s)
{
  provide (s);
}

void
update_visitor::visit_literal_string (literal_string* e)
{
  provide (e);
}

void
update_visitor::visit_literal_number (literal_number* e)
{
  provide (e);
}

void
update_visitor::visit_binary_expression (binary_expression* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_unary_expression (unary_expression* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_pre_crement (pre_crement* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_post_crement (post_crement* e)
{
  replace (e->operand);
  provide (e);
}


void
update_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_array_in (array_in* e)
{
  replace (e->operand);
  provide (e);
}

void
update_visitor::visit_comparison (comparison* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_concatenation (concatenation* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_ternary_expression (ternary_expression* e)
{
  replace (e->cond);
  replace (e->truevalue);
  replace (e->falsevalue);
  provide (e);
}

void
update_visitor::visit_assignment (assignment* e)
{
  replace (e->left);
  replace (e->right);
  provide (e);
}

void
update_visitor::visit_symbol (symbol* e)
{
  provide (e);
}

void
update_visitor::visit_target_symbol (target_symbol* e)
{
  e->visit_components (this);
  provide (e);
}

void
update_visitor::visit_cast_op (cast_op* e)
{
  replace (e->operand);
  e->visit_components (this);
  provide (e);
}

void
update_visitor::visit_arrayindex (arrayindex* e)
{
  replace (e->base);
  for (unsigned i = 0; i < e->indexes.size(); ++i)
    replace (e->indexes[i]);
  provide (e);
}

void
update_visitor::visit_functioncall (functioncall* e)
{
  for (unsigned i = 0; i < e->args.size(); ++i)
    replace (e->args[i]);
  provide (e);
}

void
update_visitor::visit_print_format (print_format* e)
{
  for (unsigned i = 0; i < e->args.size(); ++i)
    replace (e->args[i]);
  replace (e->hist);
  provide (e);
}

void
update_visitor::visit_stat_op (stat_op* e)
{
  replace (e->stat);
  provide (e);
}

void
update_visitor::visit_hist_op (hist_op* e)
{
  replace (e->stat);
  provide (e);
}

template <> indexable*
update_visitor::require <indexable> (indexable* src, bool clearok)
{
  indexable *dst = NULL;
  if (src != NULL)
    {
      symbol *array_src=NULL;
      hist_op *hist_src=NULL;

      classify_indexable(src, array_src, hist_src);

      if (array_src)
        dst = require (array_src);
      else
        dst = require (hist_src);
      assert(clearok || dst);
    }
  return dst;
}


// ------------------------------------------------------------------------


void
deep_copy_visitor::visit_block (block* s)
{
  update_visitor::visit_block(new block(*s));
}

void
deep_copy_visitor::visit_embeddedcode (embeddedcode* s)
{
  update_visitor::visit_embeddedcode(new embeddedcode(*s));
}

void
deep_copy_visitor::visit_null_statement (null_statement* s)
{
  update_visitor::visit_null_statement(new null_statement(*s));
}

void
deep_copy_visitor::visit_expr_statement (expr_statement* s)
{
  update_visitor::visit_expr_statement(new expr_statement(*s));
}

void
deep_copy_visitor::visit_if_statement (if_statement* s)
{
  update_visitor::visit_if_statement(new if_statement(*s));
}

void
deep_copy_visitor::visit_for_loop (for_loop* s)
{
  update_visitor::visit_for_loop(new for_loop(*s));
}

void
deep_copy_visitor::visit_foreach_loop (foreach_loop* s)
{
  update_visitor::visit_foreach_loop(new foreach_loop(*s));
}

void
deep_copy_visitor::visit_return_statement (return_statement* s)
{
  update_visitor::visit_return_statement(new return_statement(*s));
}

void
deep_copy_visitor::visit_delete_statement (delete_statement* s)
{
  update_visitor::visit_delete_statement(new delete_statement(*s));
}

void
deep_copy_visitor::visit_next_statement (next_statement* s)
{
  update_visitor::visit_next_statement(new next_statement(*s));
}

void
deep_copy_visitor::visit_break_statement (break_statement* s)
{
  update_visitor::visit_break_statement(new break_statement(*s));
}

void
deep_copy_visitor::visit_continue_statement (continue_statement* s)
{
  update_visitor::visit_continue_statement(new continue_statement(*s));
}

void
deep_copy_visitor::visit_literal_string (literal_string* e)
{
  update_visitor::visit_literal_string(new literal_string(*e));
}

void
deep_copy_visitor::visit_literal_number (literal_number* e)
{
  update_visitor::visit_literal_number(new literal_number(*e));
}

void
deep_copy_visitor::visit_binary_expression (binary_expression* e)
{
  update_visitor::visit_binary_expression(new binary_expression(*e));
}

void
deep_copy_visitor::visit_unary_expression (unary_expression* e)
{
  update_visitor::visit_unary_expression(new unary_expression(*e));
}

void
deep_copy_visitor::visit_pre_crement (pre_crement* e)
{
  update_visitor::visit_pre_crement(new pre_crement(*e));
}

void
deep_copy_visitor::visit_post_crement (post_crement* e)
{
  update_visitor::visit_post_crement(new post_crement(*e));
}


void
deep_copy_visitor::visit_logical_or_expr (logical_or_expr* e)
{
  update_visitor::visit_logical_or_expr(new logical_or_expr(*e));
}

void
deep_copy_visitor::visit_logical_and_expr (logical_and_expr* e)
{
  update_visitor::visit_logical_and_expr(new logical_and_expr(*e));
}

void
deep_copy_visitor::visit_array_in (array_in* e)
{
  update_visitor::visit_array_in(new array_in(*e));
}

void
deep_copy_visitor::visit_comparison (comparison* e)
{
  update_visitor::visit_comparison(new comparison(*e));
}

void
deep_copy_visitor::visit_concatenation (concatenation* e)
{
  update_visitor::visit_concatenation(new concatenation(*e));
}

void
deep_copy_visitor::visit_ternary_expression (ternary_expression* e)
{
  update_visitor::visit_ternary_expression(new ternary_expression(*e));
}

void
deep_copy_visitor::visit_assignment (assignment* e)
{
  update_visitor::visit_assignment(new assignment(*e));
}

void
deep_copy_visitor::visit_symbol (symbol* e)
{
  symbol* n = new symbol(*e);
  n->referent = NULL; // don't copy!
  update_visitor::visit_symbol(n);
}

void
deep_copy_visitor::visit_target_symbol (target_symbol* e)
{
  target_symbol* n = new target_symbol(*e);
  n->referent = NULL; // don't copy!
  update_visitor::visit_target_symbol(n);
}

void
deep_copy_visitor::visit_cast_op (cast_op* e)
{
  update_visitor::visit_cast_op(new cast_op(*e));
}

void
deep_copy_visitor::visit_arrayindex (arrayindex* e)
{
  update_visitor::visit_arrayindex(new arrayindex(*e));
}

void
deep_copy_visitor::visit_functioncall (functioncall* e)
{
  functioncall* n = new functioncall(*e);
  n->referent = NULL; // don't copy!
  update_visitor::visit_functioncall(n);
}

void
deep_copy_visitor::visit_print_format (print_format* e)
{
  update_visitor::visit_print_format(new print_format(*e));
}

void
deep_copy_visitor::visit_stat_op (stat_op* e)
{
  update_visitor::visit_stat_op(new stat_op(*e));
}

void
deep_copy_visitor::visit_hist_op (hist_op* e)
{
  update_visitor::visit_hist_op(new hist_op(*e));
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
