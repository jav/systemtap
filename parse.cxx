// recursive descent parser for systemtap scripts
// Copyright 2005 Red Hat Inc.
// GPL

#include <iostream>
#include "staptree.h"
#include "parse.h"
#include <cctype>
#include <fstream>

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

  // cerr << "[" << next_t->content << "]" << endl;

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
      // XXX: support 0xHEX etc.
      n->type = tok_number;
      n->content = c;
      while (1)
	{
	  int c2 = input.peek ();
	  if (! input)
	    break;
	  if (isdigit(c2))
	    {
	      n->content.push_back(c2);
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
        n->content.push_back((char) input_get ());

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
	    {
	      next ();
	      f->probes.push_back (parse_probe ());
	    }
	  else if (t->type == tok_identifier && t->content == "global")
	    {
	      next ();
              parse_global (f->globals);
	    }
	  else if (t->type == tok_identifier && t->content == "function")
	    {
	      next ();
	      f->functions.push_back (parse_functiondecl ());
              // XXX: check for duplicate function decl
	    }
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
  probe *p = new probe;
  while (1)
    {
      const token *t = peek ();
      if (t && t->type == tok_identifier)
	{
          p->tok = t;
	  p->location.push_back (parse_probe_point_spec ());

	  t = peek ();
	  if (t && t->type == tok_operator && t->content == ":")
	    {
	      next ();
	      continue;
	    }
	  else if (t && t->type == tok_operator && t->content == "{")
	    break;
	  else
            throw parse_error ("expected ':' or '{'");
          // XXX: unify logic with that in parse_symbol()
	}
      else
	throw parse_error ("expected probe location specifier");
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
          // handle empty blocks
          t = peek ();
          if (t && t->type == tok_operator && t->content == "}")
            {
              next ();
              break;
            }
          
          pb->statements.push_back (parse_statement ());

          // ';' is a statement separator in awk, not a terminator.
          // Note that ';' is also a possible null statement.
          t = peek ();
          if (t && t->type == tok_operator && t->content == ";")
            {
              next ();
              continue;
            }
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
      next ();
      return new null_statement ();
    }
  else if (t && t->type == tok_operator && t->content == "{")  
    return parse_stmt_block ();
  else if (t && t->type == tok_identifier && t->content == "if")
    return parse_if_statement ();
  else if (t && t->type == tok_identifier && t->content == "return")
    return parse_return_statement ();
  else if (t && t->type == tok_identifier && t->content == "delete")
    return parse_delete_statement ();
  // XXX: other control constructs ("for", "delete", "while", "do",
  // "break", "continue", "exit", "return")
  else if (t && (t->type == tok_operator || // expressions are flexible
                 t->type == tok_identifier ||
                 t->type == tok_number ||
                 t->type == tok_string))
    {
      expr_statement *es = new expr_statement;
      es->tok = t;
      es->value = parse_expression ();
      return es;
    }
  else
    throw parse_error ("expected statement");
}


void
parser::parse_global (vector <vardecl*>& globals)
{
  while (1)
    {
      const token* t = next ();
      if (! (t->type == tok_identifier))
        throw parse_error ("expected identifier");

      vardecl* d = new vardecl;
      d->name = t->content;
      d->tok = t;
      globals.push_back (d); // XXX: check for duplicates

      t = next ();
      if (t->type == tok_operator && t->content == ";")
        break;
      else if (t->type == tok_operator && t->content == ",")
        continue;
      else
        throw parse_error ("expected ';' or ','");
    }
}


functiondecl*
parser::parse_functiondecl ()
{
  functiondecl *fd = new functiondecl ();

  const token* t = next ();
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


probe_point_spec*
parser::parse_probe_point_spec ()
{
  probe_point_spec* pl = new probe_point_spec;

  const token* t = next ();
  if (t->type != tok_identifier)
    throw parse_error ("expected identifier");
  pl->functor = t->content;
  pl->tok = t;

  t = peek ();
  if (t && t->type == tok_operator && t->content == "(")
    {
      next (); // consume "("
      pl->arg = parse_literal ();
      const token* tt = next ();
      if (! (tt->type == tok_operator && tt->content == ")"))
	throw parse_error ("expected ')'");
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
    l = new literal_number (atol (t->content.c_str ()));
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


expression*
parser::parse_expression ()
{
  return parse_assignment ();
}

// XXX: in all subsequent calls to parse_expression(),
// check whether operator priority / associativity
// suggests that a different expression subtype parser
// should be called instead


expression*
parser::parse_assignment ()
{
  expression* op1 = parse_ternary ();

  const token* t = peek ();
  // left-associative operators
  while (t && t->type == tok_operator 
      && (t->content == "=" ||
	  t->content == "<<" ||
	  t->content == "+=" ||
	  false)) // XXX: add /= etc.
    {
      if (op1->is_lvalue () == 0)
        throw parse_error ("assignment not to lvalue");
      assignment* e = new assignment;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_ternary ();
      op1 = e;
      t = peek ();
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
  expression* op1 = parse_comparison ();

  const token* t = peek ();
  if (t && t->type == tok_identifier && t->content == "in")
    {
      array_in *e = new array_in;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_symbol_plain ();
      return e;
    }
  else
    return op1;
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


symbol*
parser::parse_symbol_plain () // var only
{
  symbol *s = new symbol;
  const token* t = next ();
  if (t->type != tok_identifier)
    throw parse_error ("expected identifier");
  s->name = t->content;
  s->tok = t;
  return s;
}
