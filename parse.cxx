// recursive descent parser for systemtap scripts
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "parse.h"
#include <iostream>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cerrno>
#include <climits>

using namespace std;

// ------------------------------------------------------------------------

parser::parser (istream& i):
  input_name ("<input>"), free_input (0), input (i, input_name),
  last_t (0), next_t (0), num_errors (0)
{ }

parser::parser (const string& fn):
  input_name (fn), free_input (new ifstream (input_name.c_str(), ios::in)),
  input (* free_input, input_name),
  last_t (0), next_t (0), num_errors (0)
{ }

parser::~parser()
{
  if (free_input) delete free_input;
}


stapfile*
parser::parse (std::istream& i)
{
  parser p (i);
  return p.parse ();
}


stapfile*
parser::parse (const std::string& n)
{
  parser p (n);
  return p.parse ();
}


ostream&
operator << (ostream& o, const token& t)
{
  o << (t.type == tok_junk ? "junk" :
        t.type == tok_identifier ? "identifier" :
        t.type == tok_operator ? "operator" :
        t.type == tok_string ? "string" :
        t.type == tok_number ? "number" :
        "unknown token");

  o << " '";
  for (unsigned i=0; i<t.content.length(); i++)
    {
      char c = t.content[i];
      o << (isprint (c) ? c : '?');
    }
  o << "'";

  o << " at " 
    << t.location.file << ":" 
    << t.location.line << ":"
    << t.location.column;

  return o;
}


void 
parser::print_error  (const parse_error &pe)
{
  cerr << "parse error: " << pe.what () << endl;

  const token* t = last_t;
  if (t)
    cerr << "\tsaw: " << *t << endl;
  else
    cerr << "\tsaw: " << input_name << " EOF" << endl;

  // XXX: make it possible to print the last input line,
  // so as to line up an arrow with the specific error column

  num_errors ++;
}


const token* 
parser::last ()
{
  return last_t;
}


const token*
parser::next ()
{
  if (! next_t)
    next_t = input.scan ();
  if (! next_t)
    throw parse_error ("unexpected end-of-file");

  last_t = next_t;
  // advance by zeroing next_t
  next_t = 0;
  return last_t;
}


const token*
parser::peek ()
{
  if (! next_t)
    next_t = input.scan ();

  // cerr << "{" << (next_t ? next_t->content : "null") << "}";

  // don't advance by zeroing next_t
  last_t = next_t;
  return next_t;
}


lexer::lexer (istream& i, const string& in):
  input (i), input_name (in), cursor_line (1), cursor_column (1)
{ }

int 
lexer::input_get ()
{
  int c = input.get();
  
  if (! input)
    return -1;
  
  // update source cursor
  if (c == '\n')
    {
      cursor_line ++;
      cursor_column = 1;
    }
  else
    cursor_column ++;

  return c;
}


token*
lexer::scan ()
{
  token* n = new token;
  n->location.file = input_name;

 skip:
  n->location.line = cursor_line;
  n->location.column = cursor_column;

  int c = input_get();
  if (c < 0)
    {
      delete n;
      return 0;
    }

  if (isspace (c))
    goto skip;

  else if (isalpha (c) || c == '$')
    {
      n->type = tok_identifier;
      n->content = (char) c;
      while (1)
	{
	  int c2 = input.peek ();
	  if (! input)
	    break;
	  if ((isalnum(c2) || c2 == '_' || c2 == '$'))
	    {
	      n->content.push_back(c2);
	      input_get ();
	    }
	  else
	    break;
	}
      return n;
    }

  else if (isdigit (c))
    {
      n->type = tok_number;
      n->content = (char) c;

      while (1)
	{
	  int c2 = input.peek ();
	  if (! input)
	    break;

          // NB: isalnum is very permissive.  We rely on strtol, called in
          // parser::parse_literal below, to confirm that the number string
          // is correctly formatted and in range.

	  if (isalnum (c2))
	    {
	      n->content.push_back (c2);
	      input_get ();
	    }
	  else
	    break;
	}
      return n;
    }

  else if (c == '\"')
    {
      n->type = tok_string;
      while (1)
	{
	  c = input_get ();

	  if (! input || c == '\n')
	    {
	      n->type = tok_junk;
	      break;
	    }
	  if (c == '\"') // closing double-quotes
	    break;
	  else if (c == '\\')
	    {
	      // XXX: handle escape sequences
	    }
	  else
	    n->content.push_back(c);
	}
      return n;
    }

  else if (ispunct (c))
    {
      int c2 = input.peek ();

      if (c == '#') // comment to end-of-line
        {
          unsigned this_line = cursor_line;
          while (input && cursor_line == this_line)
            input_get ();
          goto skip;
        }

      n->type = tok_operator;
      n->content = (char) c;

      // handle two-character operators
      if ((c == '=' && c2 == '=') ||
          (c == '+' && c2 == '+') ||
          (c == '-' && c2 == '-') ||
          (c == '|' && c2 == '|') ||
          (c == '&' && c2 == '&') ||
          (c == '<' && c2 == '<') ||
          (c == '+' && c2 == '=') ||
          (c == '-' && c2 == '=') ||
          (c == ':' && c2 == ':') ||
          (c == '-' && c2 == '>') ||
	  false) // XXX: etc.
        n->content.push_back ((char) input_get ());

      // handle three-character operator
      if (c == '<' && c2 == '<')
        {
          int c3 = input.peek ();
          if (c3 == '<')
            n->content.push_back ((char) input_get ());
        }

      return n;
    }

  else
    {
      n->type = tok_junk;
      n->content = (char) c;
      return n;
    }
}


// ------------------------------------------------------------------------

stapfile*
parser::parse ()
{
  stapfile* f = new stapfile;
  f->name = input_name;

  bool empty = true;

  while (1)
    {
      try
	{
	  const token* t = peek ();
	  if (! t) // nice clean EOF
	    break;

          empty = false;
	  if (t->type == tok_identifier && t->content == "probe")
	    f->probes.push_back (parse_probe ());
	  else if (t->type == tok_identifier && t->content == "global")
	    parse_global (f->globals);
	  else if (t->type == tok_identifier && t->content == "function")
	    f->functions.push_back (parse_functiondecl ());
	  else
	    throw parse_error ("expected 'probe', 'global', or 'function'");
	}
      catch (parse_error& pe)
	{
	  print_error (pe);
	  // Quietly swallow all tokens until the next '}'.
	  while (1)
	    {
	      const token* t = peek ();
	      if (! t)
		break;
	      next ();
	      if (t->type == tok_operator && t->content == "}")
		break;
	    }
	}
    }

  if (empty)
    {
      cerr << "Input file '" << input_name << "' is empty or missing." << endl;
      delete f;
      return 0;
    }
  else if (num_errors > 0)
    {
      cerr << num_errors << " parse error(s)." << endl;
      delete f;
      return 0;
    }
  
  return f;
}


probe*
parser::parse_probe ()
{
  const token* t0 = next ();
  if (! (t0->type == tok_identifier && t0->content == "probe"))
    throw parse_error ("expected 'probe'");

  probe *p = new probe;
  p->tok = t0;

  while (1)
    {
      const token *t = peek ();
      if (t && t->type == tok_identifier)
	{
	  p->locations.push_back (parse_probe_point ());

	  t = peek ();
	  if (t && t->type == tok_operator && t->content == ",")
	    {
	      next ();
	      continue;
	    }
	  else if (t && t->type == tok_operator && t->content == "{")
	    break;
	  else
            throw parse_error ("expected ',' or '{'");
          // XXX: unify logic with that in parse_symbol()
	}
      else
	throw parse_error ("expected probe point specifier");
    }
  
  p->body = parse_stmt_block ();
  
  return p;
}


block*
parser::parse_stmt_block ()
{
  block* pb = new block;

  const token* t = next ();
  if (! (t->type == tok_operator && t->content == "{"))
    throw parse_error ("expected '{'");

  pb->tok = t;

  while (1)
    {
      try
	{
	  t = peek ();
	  if (t && t->type == tok_operator && t->content == "}")
	    {
	      next ();
	      break;
	    }

          pb->statements.push_back (parse_statement ());
	}
      catch (parse_error& pe)
	{
	  print_error (pe);
	  // Quietly swallow all tokens until the next ';' or '}'.
	  while (1)
	    {
	      const token* t = peek ();
	      if (! t)
		return 0;
	      next ();
	      if (t->type == tok_operator && (t->content == "}"
                                              || t->content == ";"))
		break;
	    }
	}
    }

  return pb;
}


statement*
parser::parse_statement ()
{
  const token* t = peek ();
  if (t && t->type == tok_operator && t->content == ";")
    {
      null_statement* n = new null_statement ();
      n->tok = next ();
      return n;
    }
  else if (t && t->type == tok_operator && t->content == "{")  
    return parse_stmt_block ();
  else if (t && t->type == tok_identifier && t->content == "if")
    return parse_if_statement ();
  /*
  else if (t && t->type == tok_identifier && t->content == "for")
    return parse_for_loop ();
  */
  else if (t && t->type == tok_identifier && t->content == "foreach")
    return parse_foreach_loop ();
  else if (t && t->type == tok_identifier && t->content == "return")
    return parse_return_statement ();
  else if (t && t->type == tok_identifier && t->content == "delete")
    return parse_delete_statement ();
  // XXX: other control constructs ("delete", "while", "do",
  // "break", "continue", "exit")
  else if (t && (t->type == tok_operator || // expressions are flexible
                 t->type == tok_identifier ||
                 t->type == tok_number ||
                 t->type == tok_string))
    return parse_expr_statement ();
  else
    throw parse_error ("expected statement");
}


void
parser::parse_global (vector <vardecl*>& globals)
{
  const token* t0 = next ();
  if (! (t0->type == tok_identifier && t0->content == "global"))
    throw parse_error ("expected 'global'");

  while (1)
    {
      const token* t = next ();
      if (! (t->type == tok_identifier))
        throw parse_error ("expected identifier");

      bool dupe = false;
      for (unsigned i=0; i<globals.size(); i++)
	if (globals[i]->name == t->content)
	  dupe = true;

      if (! dupe)
	{
	  vardecl* d = new vardecl;
	  d->name = t->content;
	  d->tok = t;
	  globals.push_back (d);
	}

      t = peek ();
      if (t && t->type == tok_operator && t->content == ",")
	{
	  next ();
	  continue;
	}
      else
	break;
    }
}


functiondecl*
parser::parse_functiondecl ()
{
  const token* t = next ();
  if (! (t->type == tok_identifier && t->content == "function"))
    throw parse_error ("expected 'function'");

  functiondecl *fd = new functiondecl ();

  t = next ();
  if (! (t->type == tok_identifier))
    throw parse_error ("expected identifier");
  fd->name = t->content;
  fd->tok = t;

  t = next ();
  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error ("expected '('");

  while (1)
    {
      t = next ();

      // permit zero-argument fuctions
      if (t->type == tok_operator && t->content == ")")
        break;
      else if (! (t->type == tok_identifier))
	throw parse_error ("expected identifier");
      vardecl* vd = new vardecl;
      vd->name = t->content;
      vd->tok = t;
      fd->formal_args.push_back (vd);

      t = next ();
      if (t->type == tok_operator && t->content == ")")
	break;
      if (t->type == tok_operator && t->content == ",")
	continue;
      else
	throw parse_error ("expected ',' or ')'");
    }

  fd->body = parse_stmt_block ();
  return fd;
}


probe_point*
parser::parse_probe_point ()
{
  probe_point* pl = new probe_point;

  // XXX: add support for probe point aliases
  // e.g.   probe   a.b = a.c = a.d = foo
  while (1)
    {
      const token* t = next ();
      if (t->type != tok_identifier)
        throw parse_error ("expected identifier");

      if (pl->tok == 0) pl->tok = t;

      probe_point::component* c = new probe_point::component;
      c->functor = t->content;
      pl->components.push_back (c);
      // NB though we still may add c->arg soon

      t = peek ();
      if (t && t->type == tok_operator 
          && (t->content == "{" || t->content == ","))
        break;
      
      if (t && t->type == tok_operator && t->content == "(")
        {
          next (); // consume "("
          c->arg = parse_literal ();

          t = next ();
          if (! (t->type == tok_operator && t->content == ")"))
            throw parse_error ("expected ')'");

          t = peek ();
          if (t && t->type == tok_operator 
              && (t->content == "{" || t->content == ","))
            break;
          else if (t && t->type == tok_operator &&
                   t->content == "(")
            throw parse_error ("unexpected '.' or ',' or '{'");
        }
      // fall through

      if (t && t->type == tok_operator && t->content == ".")
        next ();
      else
        throw parse_error ("expected '.' or ',' or '(' or '{'");
    }

  return pl;
}


literal*
parser::parse_literal ()
{
  const token* t = next ();
  literal* l;
  if (t->type == tok_string)
    l = new literal_string (t->content);
  else if (t->type == tok_number)
    {
      const char* startp = t->content.c_str ();
      char* endp = (char*) startp;

      // NB: we allow controlled overflow from LONG_MIN .. ULONG_MAX
      errno = 0;
      long long value = strtoll (startp, & endp, 0);
      if (errno == ERANGE || errno == EINVAL || *endp != '\0'
          || value > ULONG_MAX || value < LONG_MIN)
        throw parse_error ("number invalid or out of range"); 

      long value2 = (long) value;
      l = new literal_number (value2);
    }
  else
    throw parse_error ("expected literal string or number");

  l->tok = t;
  return l;
}


if_statement*
parser::parse_if_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_identifier && t->content == "if"))
    throw parse_error ("expected 'if'");
  if_statement* s = new if_statement;
  s->tok = t;

  t = next ();
  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error ("expected '('");

  s->condition = parse_expression ();

  t = next ();
  if (! (t->type == tok_operator && t->content == ")"))
    throw parse_error ("expected ')'");

  s->thenblock = parse_statement ();

  t = peek ();
  if (t && t->type == tok_identifier && t->content == "else")
    {
      next ();
      s->elseblock = parse_statement ();
    }

  return s;
}


expr_statement*
parser::parse_expr_statement ()
{
  expr_statement *es = new expr_statement;
  const token* t = peek ();
  es->tok = t;
  es->value = parse_expression ();
  return es;
}


return_statement*
parser::parse_return_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_identifier && t->content == "return"))
    throw parse_error ("expected 'return'");
  return_statement* s = new return_statement;
  s->tok = t;
  s->value = parse_expression ();
  return s;
}


delete_statement*
parser::parse_delete_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_identifier && t->content == "delete"))
    throw parse_error ("expected 'delete'");
  delete_statement* s = new delete_statement;
  s->tok = t;
  s->value = parse_expression ();
  return s;
}


for_loop*
parser::parse_for_loop ()
{
  throw parse_error ("not yet implemented");
}


foreach_loop*
parser::parse_foreach_loop ()
{
  const token* t = next ();
  if (! (t->type == tok_identifier && t->content == "foreach"))
    throw parse_error ("expected 'foreach'");
  foreach_loop* s = new foreach_loop;
  s->tok = t;

  t = next ();
  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error ("expected '('");

  // see also parse_array_in

  bool parenthesized = false;
  t = peek ();
  if (t && t->type == tok_operator && t->content == "[")
    {
      next ();
      parenthesized = true;
    }

  while (1)
    {
      t = next ();
      if (! (t->type == tok_identifier))
        throw parse_error ("expected identifier");
      symbol* sym = new symbol;
      sym->tok = t;
      sym->name = t->content;
      s->indexes.push_back (sym);

      if (parenthesized)
        {
          const token* t = peek ();
          if (t && t->type == tok_operator && t->content == ",")
            {
              next ();
              continue;
            }
          else if (t && t->type == tok_operator && t->content == "]")
            {
              next ();
              break;
            }
          else 
            throw parse_error ("expected ',' or ']'");
        }
      else
        break; // expecting only one expression
    }

  t = next ();
  if (! (t->type == tok_identifier && t->content == "in"))
    throw parse_error ("expected 'in'");

  t = next ();
  if (t->type != tok_identifier)
    throw parse_error ("expected identifier");
  s->base = t->content;

  t = next ();
  if (! (t->type == tok_operator && t->content == ")"))
    throw parse_error ("expected ')'");

  s->block = parse_statement ();
  return s;
}


expression*
parser::parse_expression ()
{
  return parse_assignment ();
}


expression*
parser::parse_assignment ()
{
  expression* op1 = parse_ternary ();

  const token* t = peek ();
  // right-associative operators
  if (t && t->type == tok_operator 
      && (t->content == "=" ||
	  t->content == "<<<" ||
	  t->content == "+=" ||
	  false)) // XXX: add /= etc.
    {
      // NB: lvalueness is checked during translation / elaboration
      assignment* e = new assignment;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_expression ();
      op1 = e;
      // XXX: map assign/accumlate operators like +=, /=
      // to ordinary assignment + nested binary_expression
    }

  return op1;
}


expression*
parser::parse_ternary ()
{
  expression* op1 = parse_logical_or ();

  const token* t = peek ();
  if (t && t->type == tok_operator && t->content == "?")
    {
      ternary_expression* e = new ternary_expression;
      e->tok = t;
      e->cond = op1;
      next ();
      e->truevalue = parse_expression (); // XXX

      t = next ();
      if (! (t->type == tok_operator && t->content == ":"))
        throw parse_error ("expected ':'");

      e->falsevalue = parse_expression (); // XXX
      return e;
    }
  else
    return op1;
}


expression*
parser::parse_logical_or ()
{
  expression* op1 = parse_logical_and ();
  
  const token* t = peek ();
  while (t && t->type == tok_operator && t->content == "||")
    {
      logical_or_expr* e = new logical_or_expr;
      e->tok = t;
      e->op = t->content;
      e->left = op1;
      next ();
      e->right = parse_logical_and ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_logical_and ()
{
  expression* op1 = parse_array_in ();

  const token* t = peek ();
  while (t && t->type == tok_operator && t->content == "&&")
    {
      logical_and_expr *e = new logical_and_expr;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_array_in ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_array_in ()
{
  // This is a very tricky case.  All these are legit expressions:
  // "a in b"  "a+0 in b" "[a,b] in c" "[c,(d+0)] in b"
  vector<expression*> indexes;
  bool parenthesized = false;

  const token* t = peek ();
  if (t && t->type == tok_operator && t->content == "[")
    {
      next ();
      parenthesized = true;
    }

  while (1)
    {
      expression* op1 = parse_comparison ();
      indexes.push_back (op1);

      if (parenthesized)
        {
          const token* t = peek ();
          if (t && t->type == tok_operator && t->content == ",")
            {
              next ();
              continue;
            }
          else if (t && t->type == tok_operator && t->content == "]")
            {
              next ();
              break;
            }
          else 
            throw parse_error ("expected ',' or ']'");
        }
      else
        break; // expecting only one expression
    }

  t = peek ();
  if (t && t->type == tok_identifier && t->content == "in")
    {
      array_in *e = new array_in;
      e->tok = t;
      next (); // swallow "in"

      arrayindex* a = new arrayindex;
      a->indexes = indexes;

      t = next ();
      if (t->type != tok_identifier)
        throw parse_error ("expected identifier");
      a->tok = t;
      a->base = t->content;

      e->operand = a;
      return e;
    }
  else if (indexes.size() == 1) // no "in" - need one expression only
    return indexes[0];
  else
    throw parse_error ("unexpected comma-separated expression list");
}


expression*
parser::parse_comparison ()
{
  expression* op1 = parse_concatenation ();

  const token* t = peek ();
  while (t && t->type == tok_operator 
      && (t->content == ">" || t->content == "==")) // xxx: more
    {
      comparison* e = new comparison;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_concatenation ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_concatenation ()
{
  expression* op1 = parse_additive ();

  const token* t = peek ();
  // XXX: the actual awk string-concatenation operator is *whitespace*.
  // I don't know how to easily to model that here.
  while (t && t->type == tok_operator && t->content == ".")
    {
      concatenation* e = new concatenation;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_additive ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_additive ()
{
  expression* op1 = parse_multiplicative ();

  const token* t = peek ();
  while (t && t->type == tok_operator 
      && (t->content == "+" || t->content == "-"))
    {
      binary_expression* e = new binary_expression;
      e->op = t->content;
      e->left = op1;
      e->tok = t;
      next ();
      e->right = parse_multiplicative ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_multiplicative ()
{
  expression* op1 = parse_unary ();

  const token* t = peek ();
  while (t && t->type == tok_operator 
      && (t->content == "*" || t->content == "/" || t->content == "%"))
    {
      binary_expression* e = new binary_expression;
      e->op = t->content;
      e->left = op1;
      e->tok = t;
      next ();
      e->right = parse_unary ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_unary ()
{
  const token* t = peek ();
  if (t && t->type == tok_operator 
      && (t->content == "+" || t->content == "-" || t->content == "!"))
    {
      unary_expression* e = new unary_expression;
      e->op = t->content;
      e->tok = t;
      next ();
      e->operand = parse_expression ();
      return e;
    }
  else
    return parse_exponentiation ();
}


expression*
parser::parse_exponentiation ()
{
  expression* op1 = parse_crement ();

  const token* t = peek ();
  // right associative: no loop
  if (t && t->type == tok_operator 
      && (t->content == "^" || t->content == "**"))
    {
      exponentiation* e = new exponentiation;
      e->op = t->content;
      e->left = op1;
      e->tok = t;
      next ();
      e->right = parse_expression ();
      op1 = e;
    }

  return op1;
}


expression*
parser::parse_crement () // as in "increment" / "decrement"
{
  const token* t = peek ();
  if (t && t->type == tok_operator 
      && (t->content == "++" || t->content == "--"))
    {
      pre_crement* e = new pre_crement;
      e->op = t->content;
      e->tok = t;
      next ();
      e->operand = parse_value ();
      return e;
    }

  // post-crement or non-crement
  expression *op1 = parse_value ();
  
  t = peek ();
  if (t && t->type == tok_operator 
      && (t->content == "++" || t->content == "--"))
    {
      post_crement* e = new post_crement;
      e->op = t->content;
      e->tok = t;
      next ();
      e->operand = op1;
      return e;
    }
  else
    return op1;
}


expression*
parser::parse_value ()
{
  const token* t = peek ();
  if (! t)
    throw parse_error ("expected value");

  if (t->type == tok_operator && t->content == "(")
    {
      next ();
      expression* e = parse_expression ();
      t = next ();
      if (! (t->type == tok_operator && t->content == ")"))
        throw parse_error ("expected ')'");
      return e;
    }
  else if (t->type == tok_identifier)
    return parse_symbol ();
  else
    return parse_literal ();
}


// var, var[index], func(parms), thread->var, process->var
expression*
parser::parse_symbol () 
{
  const token* t = next ();
  if (t->type != tok_identifier)
    throw parse_error ("expected identifier");
  const token* t2 = t;
  string name = t->content;
  
  t = peek ();
  if (t && t->type == tok_operator && t->content == "->")
    {
      // shorthand for process- or thread-specific array element
      // map "thread->VAR" to "VAR[$tid]",
      // and "process->VAR" to "VAR[$pid]"
      symbol* sym = new symbol;
      if (name == "thread")
        sym->name = "$tid";
      else if (name == "process") 
        sym->name = "$pid";
      else 
        throw parse_error ("expected 'thread->' or 'process->'");
      struct token* t2prime = new token (*t2);
      t2prime->content = sym->name;
      sym->tok = t2prime;

      next (); // swallow "->"
      t = next ();
      if (! (t->type == tok_identifier))
        throw parse_error ("expected identifier");

      struct arrayindex* ai = new arrayindex;
      ai->tok = t;
      ai->base = t->content;
      ai->indexes.push_back (sym);
      return ai;
    }
  else if (t && t->type == tok_operator && t->content == "[") // array
    {
      next ();
      struct arrayindex* ai = new arrayindex;
      ai->tok = t2;
      ai->base = name;
      while (1)
        {
          ai->indexes.push_back (parse_expression ());
          t = next ();
          if (t->type == tok_operator && t->content == "]")
            break;
          if (t->type == tok_operator && t->content == ",")
            continue;
          else
            throw parse_error ("expected ',' or ']'");
        }
      return ai;
    }
  else if (t && t->type == tok_operator && t->content == "(") // function call
    {
      next ();
      struct functioncall* f = new functioncall;
      f->tok = t2;
      f->function = name;
      // Allow empty actual parameter list
      const token* t3 = peek ();
      if (t3 && t3->type == tok_operator && t3->content == ")")
	{
	  next ();
	  return f;
	}
      while (1)
	{
	  f->args.push_back (parse_expression ());
	  t = next ();
	  if (t->type == tok_operator && t->content == ")")
	    break;
	  if (t->type == tok_operator && t->content == ",")
	    continue;
	  else
	    throw parse_error ("expected ',' or ')'");
	}
      return f;
    }
  else
    {
      symbol* sym = new symbol;
      sym->name = name;
      sym->tok = t2;
      return sym;
    }
}

