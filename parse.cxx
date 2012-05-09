// recursive descent parser for systemtap scripts
// Copyright (C) 2005-2012 Red Hat Inc.
// Copyright (C) 2006 Intel Corporation.
// Copyright (C) 2007 Bull S.A.S
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "parse.h"
#include "session.h"
#include "util.h"

#include <iostream>

#include <fstream>
#include <cctype>
#include <cstdlib>
#include <cassert>
#include <cerrno>
#include <climits>
#include <sstream>
#include <cstring>
#include <cctype>
#include <iterator>

extern "C" {
#include <fnmatch.h>
}

using namespace std;


class lexer
{
public:
  bool ate_comment; // the most recent token followed a comment
  token* scan (bool wildcard=false);
  lexer (istream&, const string&, systemtap_session&);
  void set_current_file (stapfile* f);

private:
  inline int input_get ();
  inline int input_peek (unsigned n=0);
  void input_put (const string&, const token*);
  string input_name;
  string input_contents;
  const char *input_pointer; // index into input_contents
  const char *input_end;
  unsigned cursor_suspend_count;
  unsigned cursor_suspend_line;
  unsigned cursor_suspend_column;
  unsigned cursor_line;
  unsigned cursor_column;
  systemtap_session& session;
  stapfile* current_file;
  static set<string> keywords;
};


class parser
{
public:
  parser (systemtap_session& s, istream& i, bool p);
  parser (systemtap_session& s, const string& n, bool p);
  ~parser ();

  stapfile* parse ();

private:
  typedef enum {
      PP_NONE,
      PP_KEEP_THEN,
      PP_SKIP_THEN,
      PP_KEEP_ELSE,
      PP_SKIP_ELSE,
  } pp_state_t;

  systemtap_session& session;
  string input_name;
  istream* free_input;
  lexer input;
  bool privileged;
  parse_context context;

  // preprocessing subordinate
  vector<pair<const token*, pp_state_t> > pp_state;
  const token* scan_pp (bool wildcard=false);
  const token* skip_pp ();

  // scanning state
  const token* last ();
  const token* next (bool wildcard=false);
  const token* peek (bool wildcard=false);

  const token* systemtap_v_seen;
  const token* last_t; // the last value returned by peek() or next()
  const token* next_t; // lookahead token

  // expectations
  const token* expect_known (token_type tt, string const & expected);
  const token* expect_unknown (token_type tt, string & target);
  const token* expect_unknown2 (token_type tt1, token_type tt2,
				string & target);

  // convenience forms
  const token* expect_op (string const & expected);
  const token* expect_kw (string const & expected);
  const token* expect_number (int64_t & expected);
  const token* expect_ident (string & target);
  const token* expect_ident_or_keyword (string & target);
  bool peek_op (string const & op);
  bool peek_kw (string const & kw);

  void print_error (const parse_error& pe);
  unsigned num_errors;

private: // nonterminals
  void parse_probe (vector<probe*>&, vector<probe_alias*>&);
  void parse_global (vector<vardecl*>&, vector<probe*>&);
  void parse_functiondecl (vector<functiondecl*>&);
  embeddedcode* parse_embeddedcode ();
  probe_point* parse_probe_point ();
  literal* parse_literal ();
  block* parse_stmt_block ();
  try_block* parse_try_block ();
  statement* parse_statement ();
  if_statement* parse_if_statement ();
  for_loop* parse_for_loop ();
  for_loop* parse_while_loop ();
  foreach_loop* parse_foreach_loop ();
  expr_statement* parse_expr_statement ();
  return_statement* parse_return_statement ();
  delete_statement* parse_delete_statement ();
  next_statement* parse_next_statement ();
  break_statement* parse_break_statement ();
  continue_statement* parse_continue_statement ();
  indexable* parse_indexable ();
  const token *parse_hist_op_or_bare_name (hist_op *&hop, string &name);
  target_symbol *parse_target_symbol (const token* t);
  expression* parse_entry_op (const token* t);
  expression* parse_defined_op (const token* t);
  expression* parse_expression ();
  expression* parse_assignment ();
  expression* parse_ternary ();
  expression* parse_logical_or ();
  expression* parse_logical_and ();
  expression* parse_boolean_or ();
  expression* parse_boolean_xor ();
  expression* parse_boolean_and ();
  expression* parse_array_in ();
  expression* parse_comparison ();
  expression* parse_shift ();
  expression* parse_concatenation ();
  expression* parse_additive ();
  expression* parse_multiplicative ();
  expression* parse_unary ();
  expression* parse_crement ();
  expression* parse_value ();
  expression* parse_symbol ();

  void parse_target_symbol_components (target_symbol* e);
};


// ------------------------------------------------------------------------

stapfile*
parse (systemtap_session& s, istream& i, bool pr)
{
  parser p (s, i, pr);
  return p.parse ();
}


stapfile*
parse (systemtap_session& s, const string& n, bool pr)
{
  parser p (s, n, pr);
  return p.parse ();
}

// ------------------------------------------------------------------------


parser::parser (systemtap_session& s, istream& i, bool p):
  session (s),
  input_name ("<input>"), free_input (0),
  input (i, input_name, s), privileged (p),
  context(con_unknown), systemtap_v_seen(0), last_t (0), next_t (0), num_errors (0)
{ }

parser::parser (systemtap_session& s, const string& fn, bool p):
  session (s),
  input_name (fn), free_input (new ifstream (input_name.c_str(), ios::in)),
  input (* free_input, input_name, s), privileged (p),
  context(con_unknown), systemtap_v_seen(0), last_t (0), next_t (0), num_errors (0)
{ }

parser::~parser()
{
  if (free_input) delete free_input;
}

static string
tt2str(token_type tt)
{
  switch (tt)
    {
    case tok_junk: return "junk";
    case tok_identifier: return "identifier";
    case tok_operator: return "operator";
    case tok_string: return "string";
    case tok_number: return "number";
    case tok_embedded: return "embedded-code";
    case tok_keyword: return "keyword";
    }
  return "unknown token";
}

ostream&
operator << (ostream& o, const source_loc& loc)
{
  o << loc.file->name << ":"
    << loc.line << ":"
    << loc.column;

  return o;
}

ostream&
operator << (ostream& o, const token& t)
{
  o << tt2str(t.type);

  if (t.type != tok_embedded && t.type != tok_keyword) // XXX: other types?
    {
      o << " '";
      for (unsigned i=0; i<t.content.length(); i++)
        {
          char c = t.content[i];
          o << (isprint (c) ? c : '?');
        }
      o << "'";
    }

  o << " at "
    << t.location;

  return o;
}


void
parser::print_error  (const parse_error &pe)
{
  string align_parse_error ("     ");
  cerr << _("parse error: ") << pe.what () << endl;

  if (pe.tok)
    {
      cerr << _("\tat: ") << *pe.tok << endl;
      session.print_error_source (cerr, align_parse_error, pe.tok);
    }
  else
    {
      const token* t = last_t;
      if (t)
	{
	  cerr << _("\tsaw: ") << *t << endl;
	  session.print_error_source (cerr, align_parse_error, t);
	}
      else
        cerr << _("\tsaw: ") << input_name << " EOF" << endl;
    }

  // XXX: make it possible to print the last input line,
  // so as to line up an arrow with the specific error column

  num_errors ++;
}


const token*
parser::last ()
{
  return last_t;
}



template <typename OPERAND>
bool eval_comparison (const OPERAND& lhs, const token* op, const OPERAND& rhs)
{
  if (op->type == tok_operator && op->content == "<=")
    { return lhs <= rhs; }
  else if (op->type == tok_operator && op->content == ">=")
    { return lhs >= rhs; }
  else if (op->type == tok_operator && op->content == "<")
    { return lhs < rhs; }
  else if (op->type == tok_operator && op->content == ">")
    { return lhs > rhs; }
  else if (op->type == tok_operator && op->content == "==")
    { return lhs == rhs; }
  else if (op->type == tok_operator && op->content == "!=")
    { return lhs != rhs; }
  else
    throw parse_error (_("expected comparison operator"), op);
}


// Here, we perform on-the-fly preprocessing.
// The basic form is %( CONDITION %? THEN-TOKENS %: ELSE-TOKENS %)
// where CONDITION is: kernel_v[r] COMPARISON-OP "version-string"
//                 or: arch COMPARISON-OP "arch-string"
//                 or: systemtap_v COMPARISON-OP "version-string"
//                 or: CONFIG_foo COMPARISON-OP "config-string"
//                 or: CONFIG_foo COMPARISON-OP number
//                 or: CONFIG_foo COMPARISON-OP CONFIG_bar
//                 or: "string1" COMPARISON-OP "string2"
//                 or: number1 COMPARISON-OP number2
// The %: ELSE-TOKENS part is optional.
//
// e.g. %( kernel_v > "2.5" %? "foo" %: "baz" %)
// e.g. %( arch != "i?86" %? "foo" %: "baz" %)
// e.g. %( CONFIG_foo %? "foo" %: "baz" %)
//
// Up to an entire %( ... %) expression is processed by a single call
// to this function.  Tokens included by any nested conditions are
// enqueued in a private vector.

bool eval_pp_conditional (systemtap_session& s,
                          const token* l, const token* op, const token* r)
{
  if (l->type == tok_identifier && (l->content == "kernel_v" ||
                                    l->content == "kernel_vr" || 
                                    l->content == "systemtap_v"))
    {
      if (! (r->type == tok_string))
        throw parse_error (_("expected string literal"), r);

      string target_kernel_vr = s.kernel_release;
      string target_kernel_v = s.kernel_base_release;
      string target;

      if (l->content == "kernel_v") target = target_kernel_v;
      else if (l->content == "kernel_vr") target = target_kernel_vr;
      else if (l->content == "systemtap_v") target = s.compatible;
      else assert (0);

      string query = r->content;
      bool rhs_wildcard = (strpbrk (query.c_str(), "*?[") != 0);

      // collect acceptable strverscmp results.
      int rvc_ok1, rvc_ok2;
      bool wc_ok = false;
      if (op->type == tok_operator && op->content == "<=")
        { rvc_ok1 = -1; rvc_ok2 = 0; }
      else if (op->type == tok_operator && op->content == ">=")
        { rvc_ok1 = 1; rvc_ok2 = 0; }
      else if (op->type == tok_operator && op->content == "<")
        { rvc_ok1 = -1; rvc_ok2 = -1; }
      else if (op->type == tok_operator && op->content == ">")
        { rvc_ok1 = 1; rvc_ok2 = 1; }
      else if (op->type == tok_operator && op->content == "==")
        { rvc_ok1 = 0; rvc_ok2 = 0; wc_ok = true; }
      else if (op->type == tok_operator && op->content == "!=")
        { rvc_ok1 = -1; rvc_ok2 = 1; wc_ok = true; }
      else
        throw parse_error (_("expected comparison operator"), op);

      if ((!wc_ok) && rhs_wildcard)
        throw parse_error (_("wildcard not allowed with order comparison operators"), op);

      if (rhs_wildcard)
        {
          int rvc_result = fnmatch (query.c_str(), target.c_str(),
                                    FNM_NOESCAPE); // spooky
          bool badness = (rvc_result == 0) ^ (op->content == "==");
          return !badness;
        }
      else
        {
          int rvc_result = strverscmp (target.c_str(), query.c_str());
          // normalize rvc_result
          if (rvc_result < 0) rvc_result = -1;
          if (rvc_result > 0) rvc_result = 1;
          return (rvc_result == rvc_ok1 || rvc_result == rvc_ok2);
        }
    }
  else if (l->type == tok_identifier && l->content == "arch")
    {
      string target_architecture = s.architecture;
      if (! (r->type == tok_string))
        throw parse_error (_("expected string literal"), r);
      string query_architecture = r->content;

      int nomatch = fnmatch (query_architecture.c_str(),
                             target_architecture.c_str(),
                             FNM_NOESCAPE); // still spooky

      bool result;
      if (op->type == tok_operator && op->content == "==")
        result = !nomatch;
      else if (op->type == tok_operator && op->content == "!=")
        result = nomatch;
      else
        throw parse_error (_("expected '==' or '!='"), op);

      return result;
    }
  else if (l->type == tok_identifier && startswith(l->content, "CONFIG_"))
    {
      if (r->type == tok_string)
	{
	  string lhs = s.kernel_config[l->content]; // may be empty
	  string rhs = r->content;

	  int nomatch = fnmatch (rhs.c_str(), lhs.c_str(), FNM_NOESCAPE); // still spooky

	  bool result;
	  if (op->type == tok_operator && op->content == "==")
	    result = !nomatch;
	  else if (op->type == tok_operator && op->content == "!=")
	    result = nomatch;
	  else
	    throw parse_error (_("expected '==' or '!='"), op);

	  return result;
	}
      else if (r->type == tok_number)
	{
          const char* startp = s.kernel_config[l->content].c_str ();
          char* endp = (char*) startp;
          errno = 0;
          int64_t lhs = (int64_t) strtoll (startp, & endp, 0);
          if (errno == ERANGE || errno == EINVAL || *endp != '\0')
	    throw parse_error ("Config option value not a number", l);

	  int64_t rhs = lex_cast<int64_t>(r->content);
	  return eval_comparison (lhs, op, rhs);
	}
      else if (r->type == tok_identifier
	       && startswith(r->content, "CONFIG_"))
	{
	  // First try to convert both to numbers,
	  // otherwise threat both as strings.
          const char* startp = s.kernel_config[l->content].c_str ();
          char* endp = (char*) startp;
          errno = 0;
          int64_t val = (int64_t) strtoll (startp, & endp, 0);
          if (errno != ERANGE && errno != EINVAL && *endp == '\0')
	    {
	      int64_t lhs = val;
	      startp = s.kernel_config[r->content].c_str ();
	      endp = (char*) startp;
	      errno = 0;
	      int64_t rhs = (int64_t) strtoll (startp, & endp, 0);
	      if (errno != ERANGE && errno != EINVAL && *endp == '\0')
		return eval_comparison (lhs, op, rhs);
	    }

	  string lhs = s.kernel_config[l->content];
	  string rhs = s.kernel_config[r->content];
	  return eval_comparison (lhs, op, rhs);
	}
      else
	throw parse_error (_("expected string, number literal or other CONFIG_... as right side operand"), r);
    }
  else if (l->type == tok_string && r->type == tok_string)
    {
      string lhs = l->content;
      string rhs = r->content;
      return eval_comparison (lhs, op, rhs);
      // NB: no wildcarding option here
    }
  else if (l->type == tok_number && r->type == tok_number)
    {
      int64_t lhs = lex_cast<int64_t>(l->content);
      int64_t rhs = lex_cast<int64_t>(r->content);
      return eval_comparison (lhs, op, rhs);
      // NB: no wildcarding option here
    }
  else if (l->type == tok_string && r->type == tok_number
	    && op->type == tok_operator)
    throw parse_error (_("expected string literal as right value"), r);
  else if (l->type == tok_number && r->type == tok_string
	    && op->type == tok_operator)
    throw parse_error (_("expected number literal as right value"), r);

  else
    throw parse_error (_("expected 'arch' or 'kernel_v' or 'kernel_vr' or 'CONFIG_...'\n"
		       "             or comparison between strings or integers"), l);
}


// Only tokens corresponding to the TRUE statement must be expanded
const token*
parser::scan_pp (bool wildcard)
{
  while (true)
    {
      pp_state_t pp = PP_NONE;
      if (!pp_state.empty())
        pp = pp_state.back().second;

      const token* t = 0;
      if (pp == PP_SKIP_THEN || pp == PP_SKIP_ELSE)
        t = skip_pp ();
      else
        t = input.scan (wildcard);

      if (t == 0) // EOF
        {
          if (pp != PP_NONE)
            {
              t = pp_state.back().first;
              pp_state.pop_back(); // so skip_some doesn't keep trying to close this
              //TRANSLATORS: 'conditional' meaning 'conditional preprocessing'
              throw parse_error (_("incomplete conditional at end of file"), t);
            }
          return t;
        }

      // misplaced preprocessor "then"
      if (t->type == tok_operator && t->content == "%?")
        throw parse_error (_("incomplete conditional - missing '%('"), t);

      // preprocessor "else"
      if (t->type == tok_operator && t->content == "%:")
        {
          if (pp == PP_NONE)
            throw parse_error (_("incomplete conditional - missing '%('"), t);
          if (pp == PP_KEEP_ELSE || pp == PP_SKIP_ELSE)
            throw parse_error (_("invalid conditional - duplicate '%:'"), t);

          pp_state.back().second = (pp == PP_KEEP_THEN) ?
                                   PP_SKIP_ELSE : PP_KEEP_ELSE;
          delete t;
          continue;
        }

      // preprocessor close
      if (t->type == tok_operator && t->content == "%)")
        {
          if (pp == PP_NONE)
            throw parse_error (_("incomplete conditional - missing '%('"), t);
          delete pp_state.back().first;
          delete t; //this is the closing bracket
          pp_state.pop_back();
          continue;
        }

      if (! (t->type == tok_operator && t->content == "%(")) // ordinary token
        return t;

      // We have a %( - it's time to throw a preprocessing party!

      bool result = false;
      bool and_result = true;
      const token *n = NULL;
      do {
        const token *l, *op, *r;
        l = input.scan (false);
        op = input.scan (false);
        r = input.scan (false);
        if (l == 0 || op == 0 || r == 0)
          throw parse_error (_("incomplete condition after '%('"), t);
        // NB: consider generalizing to consume all tokens until %?, and
        // passing that as a vector to an evaluator.

        // Do not evaluate the condition if we haven't expanded everything.
        // This may occur when having several recursive conditionals.
        and_result &= eval_pp_conditional (session, l, op, r);
        if(l->content=="systemtap_v")
          systemtap_v_seen=r;

        else
          delete r;

        delete l;
        delete op;
        delete n;

        n = input.scan ();
        if (n && n->type == tok_operator && n->content == "&&")
          continue;
        result |= and_result;
        and_result = true;
        if (! (n && n->type == tok_operator && n->content == "||"))
          break;
      } while (true);

      /*
      clog << "PP eval (" << *t << ") == " << result << endl;
      */

      const token *m = n;
      if (! (m && m->type == tok_operator && m->content == "%?"))
        throw parse_error (_("expected '%?' marker for conditional"), t);
      delete m; // "%?"

      pp = result ? PP_KEEP_THEN : PP_SKIP_THEN;
      pp_state.push_back (make_pair (t, pp));

      // Now loop around to look for a real token.
    }
}


// Skip over tokens and any errors, heeding
// only nested preprocessor starts and ends.
const token*
parser::skip_pp ()
{
  const token* t = 0;
  unsigned nesting = 0;
  do
    {
      try
        {
          t = input.scan ();
        }
      catch (const parse_error &e)
        {
          continue;
        }
      if (!t)
        break;
      if (t->type == tok_operator && t->content == "%(")
        ++nesting;
      else if (nesting && t->type == tok_operator && t->content == "%)")
        --nesting;
      else if (!nesting && t->type == tok_operator &&
               (t->content == "%:" || t->content == "%?" || t->content == "%)"))
        break;
      delete t;
    }
  while (true);
  return t;
}


const token*
parser::next (bool wildcard)
{
  if (! next_t)
    next_t = scan_pp (wildcard);
  if (! next_t)
    throw parse_error (_("unexpected end-of-file"));

  last_t = next_t;
  // advance by zeroing next_t
  next_t = 0;
  return last_t;
}


const token*
parser::peek (bool wildcard)
{
  if (! next_t)
    next_t = scan_pp (wildcard);

  // don't advance by zeroing next_t
  last_t = next_t;
  return next_t;
}


static inline bool
tok_is(token const * t, token_type tt, string const & expected)
{
  return t && t->type == tt && t->content == expected;
}


const token*
parser::expect_known (token_type tt, string const & expected)
{
  const token *t = next();
  if (! (t && t->type == tt && t->content == expected))
    throw parse_error (_F("expected '%s'", expected.c_str()));
  return t;
}


const token*
parser::expect_unknown (token_type tt, string & target)
{
  const token *t = next();
  if (!(t && t->type == tt))
    throw parse_error (_("expected ") + tt2str(tt));
  target = t->content;
  return t;
}


const token*
parser::expect_unknown2 (token_type tt1, token_type tt2, string & target)
{
  const token *t = next();
  if (!(t && (t->type == tt1 || t->type == tt2)))
    throw parse_error ("expected " + tt2str(tt1) + " or " + tt2str(tt2));
  target = t->content;
  return t;
}


const token*
parser::expect_op (std::string const & expected)
{
  return expect_known (tok_operator, expected);
}


const token*
parser::expect_kw (std::string const & expected)
{
  return expect_known (tok_keyword, expected);
}

const token*
parser::expect_number (int64_t & value)
{
  bool neg = false;
  const token *t = next();
  if (t->type == tok_operator && t->content == "-")
    {
      neg = true;
      t = next ();
    }
  if (!(t && t->type == tok_number))
    throw parse_error (_("expected number"));

  const char* startp = t->content.c_str ();
  char* endp = (char*) startp;

  // NB: we allow controlled overflow from LLONG_MIN .. ULLONG_MAX
  // Actually, this allows all the way from -ULLONG_MAX to ULLONG_MAX,
  // since the lexer only gives us positive digit strings, but we'll
  // limit it to LLONG_MIN when a '-' operator is fed into the literal.
  errno = 0;
  value = (int64_t) strtoull (startp, & endp, 0);
  if (errno == ERANGE || errno == EINVAL || *endp != '\0'
      || (neg && (unsigned long long) value > 9223372036854775808ULL)
      || (unsigned long long) value > 18446744073709551615ULL
      || value < -9223372036854775807LL-1)
    throw parse_error (_("number invalid or out of range"));

  if (neg)
    value = -value;

  return t;
}


const token*
parser::expect_ident (std::string & target)
{
  return expect_unknown (tok_identifier, target);
}


const token*
parser::expect_ident_or_keyword (std::string & target)
{
  return expect_unknown2 (tok_identifier, tok_keyword, target);
}


bool
parser::peek_op (std::string const & op)
{
  return tok_is (peek(), tok_operator, op);
}


bool
parser::peek_kw (std::string const & kw)
{
  return tok_is (peek(), tok_identifier, kw);
}



lexer::lexer (istream& input, const string& in, systemtap_session& s):
  ate_comment(false), input_name (in), input_pointer (0), input_end (0),
  cursor_suspend_count(0), cursor_suspend_line (1), cursor_suspend_column (1),
  cursor_line (1), cursor_column (1),
  session(s), current_file (0)
{
  getline(input, input_contents, '\0');

  input_pointer = input_contents.data();
  input_end = input_contents.data() + input_contents.size();

  if (keywords.empty())
    {
      // NB: adding new keywords is highly disruptive to the language,
      // in particular to existing scripts that could be suddenly
      // broken.  If done at all, it has to be s.compatible-sensitive,
      // and broadly advertised.
      keywords.insert("probe");
      keywords.insert("global");
      keywords.insert("function");
      keywords.insert("if");
      keywords.insert("else");
      keywords.insert("for");
      keywords.insert("foreach");
      keywords.insert("in");
      keywords.insert("limit");
      keywords.insert("return");
      keywords.insert("delete");
      keywords.insert("while");
      keywords.insert("break");
      keywords.insert("continue");
      keywords.insert("next");
      keywords.insert("string");
      keywords.insert("long");
      keywords.insert("try");
      keywords.insert("catch");
    }
}

set<string> lexer::keywords;

void
lexer::set_current_file (stapfile* f)
{
  current_file = f;
  if (f)
    {
      f->file_contents = input_contents;
      f->name = input_name;
    }
}

int
lexer::input_peek (unsigned n)
{
  if (input_pointer + n >= input_end)
    return -1; // EOF
  return (unsigned char)*(input_pointer + n);
}


int
lexer::input_get ()
{
  int c = input_peek();
  if (c < 0) return c; // EOF

  ++input_pointer;

  if (cursor_suspend_count)
    {
      // Track effect of input_put: preserve previous cursor/line_column
      // until all of its characters are consumed.
      if (--cursor_suspend_count == 0)
        {
          cursor_line = cursor_suspend_line;
          cursor_column = cursor_suspend_column;
        }
    }
  else
    {
      // update source cursor
      if (c == '\n')
        {
          cursor_line ++;
          cursor_column = 1;
        }
      else
        cursor_column ++;
    }

  // clog << "[" << (char)c << "]";
  return c;
}


void
lexer::input_put (const string& chars, const token* t)
{
  size_t pos = input_pointer - input_contents.data();
  // clog << "[put:" << chars << " @" << pos << "]";
  input_contents.insert (pos, chars);
  cursor_suspend_count += chars.size();
  cursor_suspend_line = cursor_line;
  cursor_suspend_column = cursor_column;
  cursor_line = t->location.line;
  cursor_column = t->location.column;
  input_pointer = input_contents.data() + pos;
  input_end = input_contents.data() + input_contents.size();
}


token*
lexer::scan (bool wildcard)
{
  ate_comment = false; // reset for each new token
  token* n = new token;
  n->location.file = current_file;

skip:
  bool suspended = (cursor_suspend_count > 0);
  n->location.line = cursor_line;
  n->location.column = cursor_column;

  int c = input_get();
  // clog << "{" << (char)c << (char)c2 << "}";
  if (c < 0)
    {
      delete n;
      return 0;
    }

  if (isspace (c))
    goto skip;

  int c2 = input_peek ();

  // Paste command line arguments as character streams into
  // the beginning of a token.  $1..$999 go through as raw
  // characters; @1..@999 are quoted/escaped as strings.
  // $# and @# expand to the number of arguments, similarly
  // raw or quoted.
  if ((c == '$' || c == '@') && (c2 == '#'))
    {
      n->content.push_back (c);
      n->content.push_back (c2);
      input_get(); // swallow '#'
      if (suspended)
        throw parse_error (_("invalid nested substitution of command line arguments"), n);
      size_t num_args = session.args.size ();
      input_put ((c == '$') ? lex_cast (num_args) : lex_cast_qstring (num_args), n);
      n->content.clear();
      goto skip;
    }
  else if ((c == '$' || c == '@') && (isdigit (c2)))
    {
      n->content.push_back (c);
      unsigned idx = 0;
      do
        {
          input_get ();
          idx = (idx * 10) + (c2 - '0');
          n->content.push_back (c2);
          c2 = input_peek ();
        } while (c2 > 0 &&
                 isdigit (c2) &&
                 idx <= session.args.size()); // prevent overflow
      if (suspended)
        throw parse_error (_("invalid nested substitution of command line arguments"), n);
      if (idx == 0 ||
          idx-1 >= session.args.size())
        throw parse_error (_F("command line argument index %lu out of range [1-%lu]",
                              (unsigned long) idx, (unsigned long) session.args.size()),
                           n);
      const string& arg = session.args[idx-1];
      input_put ((c == '$') ? arg : lex_cast_qstring (arg), n);
      n->content.clear();
      goto skip;
    }

  else if (isalpha (c) || c == '$' || c == '@' || c == '_' ||
	   (wildcard && c == '*'))
    {
      n->type = tok_identifier;
      n->content = (char) c;
      while (isalnum (c2) || c2 == '_' || c2 == '$' ||
	     (wildcard && c2 == '*'))
	{
          input_get ();
          n->content.push_back (c2);
          c2 = input_peek ();
        }

      if (keywords.count(n->content))
        n->type = tok_keyword;

      return n;
    }

  else if (isdigit (c)) // positive literal
    {
      n->type = tok_number;
      n->content = (char) c;

      while (isalnum (c2))
	{
          // NB: isalnum is very permissive.  We rely on strtol, called in
          // parser::parse_literal below, to confirm that the number string
          // is correctly formatted and in range.

          input_get ();
          n->content.push_back (c2);
          c2 = input_peek ();
	}
      return n;
    }

  else if (c == '\"')
    {
      n->type = tok_string;
      while (1)
	{
	  c = input_get ();

	  if (c < 0 || c == '\n')
	    {
		  throw parse_error(_("Could not find matching closing quote"), n);
	    }
	  if (c == '\"') // closing double-quotes
	    break;
	  else if (c == '\\') // see also input_put
	    {
	      c = input_get ();
	      switch (c)
		{
		case 'a':
		case 'b':
		case 't':
		case 'n':
		case 'v':
		case 'f':
		case 'r':
		case '0' ... '7': // NB: need only match the first digit
		case '\\':
		  // Pass these escapes through to the string value
		  // being parsed; it will be emitted into a C literal.

		  n->content.push_back('\\');

                  // fall through
		default:
		  n->content.push_back(c);
		  break;
		}
	    }
	  else
	    n->content.push_back(c);
	}
      return n;
    }

  else if (ispunct (c))
    {
      int c3 = input_peek (1);

      // NB: if we were to recognize negative numeric literals here,
      // we'd introduce another grammar ambiguity:
      // 1-1 would be parsed as tok_number(1) and tok_number(-1)
      // instead of tok_number(1) tok_operator('-') tok_number(1)

      if (c == '#') // shell comment
        {
          unsigned this_line = cursor_line;
          do { c = input_get (); }
          while (c >= 0 && cursor_line == this_line);
          ate_comment = true;
          goto skip;
        }
      else if ((c == '/' && c2 == '/')) // C++ comment
        {
          unsigned this_line = cursor_line;
          do { c = input_get (); }
          while (c >= 0 && cursor_line == this_line);
          ate_comment = true;
          goto skip;
        }
      else if (c == '/' && c2 == '*') // C comment
	{
          (void) input_get (); // swallow '*' already in c2
          c = input_get ();
          c2 = input_get ();
          while (c2 >= 0)
            {
              if (c == '*' && c2 == '/')
                break;
              c = c2;
              c2 = input_get ();
            }
          ate_comment = true;
          goto skip;
	}
      else if (c == '%' && c2 == '{') // embedded code
        {
          n->type = tok_embedded;
          (void) input_get (); // swallow '{' already in c2
          c = input_get ();
          c2 = input_get ();
          while (c2 >= 0)
            {
              if (c == '%' && c2 == '}')
                return n;
              n->content += c;
              c = c2;
              c2 = input_get ();
            }

	      throw parse_error (_("Could not find matching '%}' to close embedded function block"), n);
        }

      // We're committed to recognizing at least the first character
      // as an operator.
      n->type = tok_operator;
      n->content = c;

      // match all valid operators, in decreasing size order
      if ((c == '<' && c2 == '<' && c3 == '<') ||
          (c == '<' && c2 == '<' && c3 == '=') ||
          (c == '>' && c2 == '>' && c3 == '='))
        {
          n->content += c2;
          n->content += c3;
          input_get (); input_get (); // swallow other two characters
        }
      else if ((c == '=' && c2 == '=') ||
               (c == '!' && c2 == '=') ||
               (c == '<' && c2 == '=') ||
               (c == '>' && c2 == '=') ||
               (c == '+' && c2 == '=') ||
               (c == '-' && c2 == '=') ||
               (c == '*' && c2 == '=') ||
               (c == '/' && c2 == '=') ||
               (c == '%' && c2 == '=') ||
               (c == '&' && c2 == '=') ||
               (c == '^' && c2 == '=') ||
               (c == '|' && c2 == '=') ||
               (c == '.' && c2 == '=') ||
               (c == '&' && c2 == '&') ||
               (c == '|' && c2 == '|') ||
               (c == '+' && c2 == '+') ||
               (c == '-' && c2 == '-') ||
               (c == '-' && c2 == '>') ||
               (c == '<' && c2 == '<') ||
               (c == '>' && c2 == '>') ||
               // preprocessor tokens
               (c == '%' && c2 == '(') ||
               (c == '%' && c2 == '?') ||
               (c == '%' && c2 == ':') ||
               (c == '%' && c2 == ')'))
        {
          n->content += c2;
          input_get (); // swallow other character
        }

      return n;
    }

  else
    {
      n->type = tok_junk;
      ostringstream s;
      s << "\\x" << hex << setw(2) << setfill('0') << c;
      n->content = s.str();
      return n;
    }
}


// ------------------------------------------------------------------------

stapfile*
parser::parse ()
{
  stapfile* f = new stapfile;
  input.set_current_file (f);

  bool empty = true;

  while (1)
    {
      try
	{
          systemtap_v_seen = 0;
	  const token* t = peek ();
	  if (! t) // nice clean EOF
	    break;

          empty = false;
	  if (t->type == tok_keyword && t->content == "probe")
	    {
	      context = con_probe;
	      parse_probe (f->probes, f->aliases);
	    }
	  else if (t->type == tok_keyword && t->content == "global")
	    {
	      context = con_global;
	      parse_global (f->globals, f->probes);
	    }
	  else if (t->type == tok_keyword && t->content == "function")
	    {
	      context = con_function;
	      parse_functiondecl (f->functions);
	    }
          else if (t->type == tok_embedded)
	    {
	      context = con_embedded;
	      f->embeds.push_back (parse_embeddedcode ());
	    }
	  else
	    {
	      context = con_unknown;
	      throw parse_error (_("expected 'probe', 'global', 'function', or '%{'"));
	    }
	}
      catch (parse_error& pe)
	{
	  print_error (pe);
          if (pe.skip_some) // for recovery
            // Quietly swallow all tokens until the next keyword we can start parsing from.
            while (1)
              try
                {
                  {
                    const token* t = peek ();
                    if (! t)
                      break;
                    if (t->type == tok_keyword && t->content == "probe") break;
                    else if (t->type == tok_keyword && t->content == "global") break;
                    else if (t->type == tok_keyword && t->content == "function") break;
                    else if (t->type == tok_embedded) break;
                    next(); // swallow it
                  }
                }
              catch (parse_error& pe2)
                {
                  // parse error during recovery ... ugh
                  print_error (pe2);
                }
        }
    }

  if (empty)
    {
      cerr << _F("Input file '%s' is empty or missing.", input_name.c_str()) << endl;
      delete f;
      f = 0;
    }
  else if (num_errors > 0)
    {
      cerr << _F(ngettext("%d parse error.", "%d parse errors.", num_errors), num_errors) << endl;
      delete f;
      f = 0;
    }

  input.set_current_file(0);
  return f;
}


void
parser::parse_probe (std::vector<probe *> & probe_ret,
		     std::vector<probe_alias *> & alias_ret)
{
  const token* t0 = next ();
  if (! (t0->type == tok_keyword && t0->content == "probe"))
    throw parse_error (_("expected 'probe'"));

  vector<probe_point *> aliases;
  vector<probe_point *> locations;

  bool equals_ok = true;

  int epilogue_alias = 0;

  while (1)
    {
      probe_point * pp = parse_probe_point ();

      const token* t = peek ();
      if (equals_ok && t
          && t->type == tok_operator && t->content == "=")
        {
          if (pp->optional || pp->sufficient)
            throw parse_error (_("probe point alias name cannot be optional nor sufficient"), pp->components.front()->tok);
          aliases.push_back(pp);
          next ();
          continue;
        }
      else if (equals_ok && t
          && t->type == tok_operator && t->content == "+=")
        {
          if (pp->optional || pp->sufficient)
            throw parse_error (_("probe point alias name cannot be optional nor sufficient"), pp->components.front()->tok);
          aliases.push_back(pp);
          epilogue_alias = 1;
          next ();
          continue;
        }
      else if (t && t->type == tok_operator && t->content == ",")
        {
          locations.push_back(pp);
          equals_ok = false;
          next ();
          continue;
        }
      else if (t && t->type == tok_operator && t->content == "{")
        {
          locations.push_back(pp);
          break;
        }
      else
	throw parse_error (_("expected probe point specifier"));
    }

  if (aliases.empty())
    {
      probe* p = new probe;
      p->tok = t0;
      p->locations = locations;
      p->body = parse_stmt_block ();
      p->privileged = privileged;
      p->systemtap_v_conditional = systemtap_v_seen;
      probe_ret.push_back (p);
    }
  else
    {
      probe_alias* p = new probe_alias (aliases);
      if(epilogue_alias)
	p->epilogue_style = true;
      else
	p->epilogue_style = false;
      p->tok = t0;
      p->locations = locations;
      p->body = parse_stmt_block ();
      p->privileged = privileged;
      p->systemtap_v_conditional = systemtap_v_seen;
      alias_ret.push_back (p);
    }
}


embeddedcode*
parser::parse_embeddedcode ()
{
  embeddedcode* e = new embeddedcode;
  const token* t = next ();
  if (t->type != tok_embedded)
    throw parse_error (_("expected '%{'"));

  if (! privileged)
    throw parse_error (_("embedded code in unprivileged script; need stap -g"),
                       false /* don't skip tokens for parse resumption */);

  e->tok = t;
  e->code = t->content;
  return e;
}


block*
parser::parse_stmt_block ()
{
  block* pb = new block;

  const token* t = next ();
  if (! (t->type == tok_operator && t->content == "{"))
    throw parse_error (_("expected '{'"));

  pb->tok = t;

  while (1)
    {
      t = peek ();
      if (t && t->type == tok_operator && t->content == "}")
        {
          next ();
          break;
        }
      pb->statements.push_back (parse_statement ());
    }

  return pb;
}


try_block*
parser::parse_try_block ()
{
  try_block* pb = new try_block;

  pb->tok = expect_kw ("try");
  pb->try_block = parse_stmt_block();
  expect_kw ("catch");

  const token* t = peek ();
  if (t->type == tok_operator && t->content == "(")
    {
      next (); // swallow the '('

      t = next();
      if (! (t->type == tok_identifier))
        throw parse_error (_("expected identifier"));
      symbol* sym = new symbol;
      sym->tok = t;
      sym->name = t->content;
      pb->catch_error_var = sym;

      expect_op (")");
    }
  else
    pb->catch_error_var = 0;

  pb->catch_block = parse_stmt_block();

  return pb;
}



statement*
parser::parse_statement ()
{
  statement *ret;
  const token* t = peek ();
  if (t && t->type == tok_operator && t->content == ";")
    return new null_statement (next ());
  else if (t && t->type == tok_operator && t->content == "{")
    return parse_stmt_block (); // Don't squash semicolons.
  else if (t && t->type == tok_keyword && t->content == "try")
    return parse_try_block (); // Don't squash semicolons.
  else if (t && t->type == tok_keyword && t->content == "if")
    return parse_if_statement (); // Don't squash semicolons.
  else if (t && t->type == tok_keyword && t->content == "for")
    return parse_for_loop (); // Don't squash semicolons.
  else if (t && t->type == tok_keyword && t->content == "foreach")
    return parse_foreach_loop (); // Don't squash semicolons.
  else if (t && t->type == tok_keyword && t->content == "while")
    return parse_while_loop (); // Don't squash semicolons.
  else if (t && t->type == tok_keyword && t->content == "return")
    ret = parse_return_statement ();
  else if (t && t->type == tok_keyword && t->content == "delete")
    ret = parse_delete_statement ();
  else if (t && t->type == tok_keyword && t->content == "break")
    ret = parse_break_statement ();
  else if (t && t->type == tok_keyword && t->content == "continue")
    ret = parse_continue_statement ();
  else if (t && t->type == tok_keyword && t->content == "next")
    ret = parse_next_statement ();
  else if (t && (t->type == tok_operator || // expressions are flexible
                 t->type == tok_identifier ||
                 t->type == tok_number ||
                 t->type == tok_string ||
                 t->type == tok_embedded ))
    ret = parse_expr_statement ();
  // XXX: consider generally accepting tok_embedded here too
  else
    throw parse_error (_("expected statement"));

  // Squash "empty" trailing colons after any "non-block-like" statement.
  t = peek ();
  if (t && t->type == tok_operator && t->content == ";")
    {
      next (); // Silently eat trailing ; after statement
    }

  return ret;
}


void
parser::parse_global (vector <vardecl*>& globals, vector<probe*>&)
{
  const token* t0 = next ();
  if (! (t0->type == tok_keyword && t0->content == "global"))
    throw parse_error (_("expected 'global'"));

  while (1)
    {
      const token* t = next ();
      if (! (t->type == tok_identifier))
        throw parse_error (_("expected identifier"));

      for (unsigned i=0; i<globals.size(); i++)
	if (globals[i]->name == t->content)
	  throw parse_error (_("duplicate global name"));

      vardecl* d = new vardecl;
      d->name = t->content;
      d->tok = t;
      d->systemtap_v_conditional = systemtap_v_seen;
      globals.push_back (d);

      t = peek ();

      if(t && t->type == tok_operator && t->content == "%") //wrapping
        {
          d->wrap = true;
          next();
          t = peek();
        }

      if (t && t->type == tok_operator && t->content == "[") // array size
	{
	  int64_t size;
	  next ();
	  expect_number(size);
	  if (size <= 0 || size > 1000000) // arbitrary max
	    throw parse_error(_("array size out of range"));
	  d->maxsize = (int)size;
	  expect_known(tok_operator, "]");
	  t = peek ();
	}

      if (t && t->type == tok_operator && t->content == "=") // initialization
	{
	  if (!d->compatible_arity(0))
	    throw parse_error(_("only scalar globals can be initialized"));
	  d->set_arity(0, t);
	  next ();
	  d->init = parse_literal ();
	  d->type = d->init->type;
	  t = peek ();
	}

      if (t && t->type == tok_operator && t->content == ";") // termination
	  next();

      if (t && t->type == tok_operator && t->content == ",") // next global
	{
	  next ();
	  continue;
	}
      else
	break;
    }
}


void
parser::parse_functiondecl (std::vector<functiondecl*>& functions)
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "function"))
    throw parse_error (_("expected 'function'"));


  t = next ();
  if (! (t->type == tok_identifier)
      && ! (t->type == tok_keyword
	    && (t->content == "string" || t->content == "long")))
    throw parse_error (_("expected identifier"));

  for (unsigned i=0; i<functions.size(); i++)
    if (functions[i]->name == t->content)
      throw parse_error (_("duplicate function name"));

  functiondecl *fd = new functiondecl ();
  fd->name = t->content;
  fd->tok = t;

  t = next ();
  if (t->type == tok_operator && t->content == ":")
    {
      t = next ();
      if (t->type == tok_keyword && t->content == "string")
	fd->type = pe_string;
      else if (t->type == tok_keyword && t->content == "long")
	fd->type = pe_long;
      else throw parse_error (_("expected 'string' or 'long'"));

      t = next ();
    }

  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error (_("expected '('"));

  while (1)
    {
      t = next ();

      // permit zero-argument functions
      if (t->type == tok_operator && t->content == ")")
        break;
      else if (! (t->type == tok_identifier))
	throw parse_error (_("expected identifier"));
      vardecl* vd = new vardecl;
      vd->name = t->content;
      vd->tok = t;
      fd->formal_args.push_back (vd);
      fd->systemtap_v_conditional = systemtap_v_seen;

      t = next ();
      if (t->type == tok_operator && t->content == ":")
	{
	  t = next ();
	  if (t->type == tok_keyword && t->content == "string")
	    vd->type = pe_string;
	  else if (t->type == tok_keyword && t->content == "long")
	    vd->type = pe_long;
	  else throw parse_error (_("expected 'string' or 'long'"));

	  t = next ();
	}
      if (t->type == tok_operator && t->content == ")")
	break;
      if (t->type == tok_operator && t->content == ",")
	continue;
      else
	throw parse_error (_("expected ',' or ')'"));
    }

  t = peek ();
  if (t && t->type == tok_embedded)
    fd->body = parse_embeddedcode ();
  else
    fd->body = parse_stmt_block ();

  functions.push_back (fd);
}


probe_point*
parser::parse_probe_point ()
{
  probe_point* pl = new probe_point;

  while (1)
    {
      const token* t = next (true); // wildcard scanning here
      if (! (t->type == tok_identifier
	     // we must allow ".return" and ".function", which are keywords
	     || t->type == tok_keyword))
        throw parse_error (_("expected identifier or '*'"));


      probe_point::component* c = new probe_point::component;
      c->functor = t->content;
      c->tok = t;
      pl->components.push_back (c);
      // NB we may add c->arg soon

      t = peek ();

      // consume optional parameter
      if (t && t->type == tok_operator && t->content == "(")
        {
          next (); // consume "("
          c->arg = parse_literal ();

          t = next ();
          if (! (t->type == tok_operator && t->content == ")"))
            throw parse_error (_("expected ')'"));

          t = peek ();
        }

      if (t && t->type == tok_operator && t->content == ".")
        {
          next ();
          continue;
        }

      // We only fall through here at the end of 	a probe point (past
      // all the dotted/parametrized components).

      if (t && t->type == tok_operator &&
          (t->content == "?" || t->content == "!"))
        {
          pl->optional = true;
          if (t->content == "!") pl->sufficient = true;
          // NB: sufficient implies optional
          next ();
          t = peek ();
          // fall through
        }

      if (t && t->type == tok_keyword && t->content == "if")
        {
          next ();
          t = peek ();
          if (t && ! (t->type == tok_operator && t->content == "("))
            throw parse_error (_("expected '('"));
          next ();

          pl->condition = parse_expression ();

          t = peek ();
          if (t && ! (t->type == tok_operator && t->content == ")"))
            throw parse_error (_("expected ')'"));
          next ();
          t = peek ();
          // fall through
        }

      if (t && t->type == tok_operator
          && (t->content == "{" || t->content == "," ||
              t->content == "=" || t->content == "+=" ))
        break;

      throw parse_error (_("expected one of '. , ( ? ! { = +='"));
    }

  return pl;
}


literal*
parser::parse_literal ()
{
  const token* t = next ();
  literal* l;
  if (t->type == tok_string)
    {
      literal_string *ls = new literal_string (t->content);

      // PR11208: check if the next token is also a string literal; auto-concatenate it
      // This is complicated to the extent that we need to skip intermediate whitespace.
      // XXX: but not comments
      const token *n = peek();
      while (n != NULL && n->type == tok_string && !input.ate_comment)
        {
          ls->value.append(next()->content); // consume and append the token
          n = peek();
        }
      l = ls;
    }
  else
    {
      bool neg = false;
      if (t->type == tok_operator && t->content == "-")
	{
	  neg = true;
	  t = next ();
	}

      if (t->type == tok_number)
	{
	  const char* startp = t->content.c_str ();
	  char* endp = (char*) startp;

	  // NB: we allow controlled overflow from LLONG_MIN .. ULLONG_MAX
	  // Actually, this allows all the way from -ULLONG_MAX to ULLONG_MAX,
	  // since the lexer only gives us positive digit strings, but we'll
	  // limit it to LLONG_MIN when a '-' operator is fed into the literal.
	  errno = 0;
	  long long value = (long long) strtoull (startp, & endp, 0);
	  if (errno == ERANGE || errno == EINVAL || *endp != '\0'
	      || (neg && (unsigned long long) value > 9223372036854775808ULL)
	      || (unsigned long long) value > 18446744073709551615ULL
	      || value < -9223372036854775807LL-1)
	    throw parse_error (_("number invalid or out of range"));

	  if (neg)
	    value = -value;

	  l = new literal_number (value);
	}
      else
	throw parse_error (_("expected literal string or number"));
    }

  l->tok = t;
  return l;
}


if_statement*
parser::parse_if_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "if"))
    throw parse_error (_("expected 'if'"));
  if_statement* s = new if_statement;
  s->tok = t;

  t = next ();
  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error (_("expected '('"));

  s->condition = parse_expression ();

  t = next ();
  if (! (t->type == tok_operator && t->content == ")"))
    throw parse_error (_("expected ')'"));

  s->thenblock = parse_statement ();

  t = peek ();
  if (t && t->type == tok_keyword && t->content == "else")
    {
      next ();
      s->elseblock = parse_statement ();
    }
  else
    s->elseblock = 0; // in case not otherwise initialized

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
  if (! (t->type == tok_keyword && t->content == "return"))
    throw parse_error (_("expected 'return'"));
  if (context != con_function)
    throw parse_error (_("found 'return' not in function context"));
  return_statement* s = new return_statement;
  s->tok = t;
  s->value = parse_expression ();
  return s;
}


delete_statement*
parser::parse_delete_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "delete"))
    throw parse_error (_("expected 'delete'"));
  delete_statement* s = new delete_statement;
  s->tok = t;
  s->value = parse_expression ();
  return s;
}


next_statement*
parser::parse_next_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "next"))
    throw parse_error (_("expected 'next'"));
  if (context != con_probe)
    throw parse_error (_("found 'next' not in probe context"));
  next_statement* s = new next_statement;
  s->tok = t;
  return s;
}


break_statement*
parser::parse_break_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "break"))
    throw parse_error (_("expected 'break'"));
  break_statement* s = new break_statement;
  s->tok = t;
  return s;
}


continue_statement*
parser::parse_continue_statement ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "continue"))
    throw parse_error (_("expected 'continue'"));
  continue_statement* s = new continue_statement;
  s->tok = t;
  return s;
}


for_loop*
parser::parse_for_loop ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "for"))
    throw parse_error (_("expected 'for'"));
  for_loop* s = new for_loop;
  s->tok = t;

  t = next ();
  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error (_("expected '('"));

  // initializer + ";"
  t = peek ();
  if (t && t->type == tok_operator && t->content == ";")
    {
      s->init = 0;
      next ();
    }
  else
    {
      s->init = parse_expr_statement ();
      t = next ();
      if (! (t->type == tok_operator && t->content == ";"))
	throw parse_error (_("expected ';'"));
    }

  // condition + ";"
  t = peek ();
  if (t && t->type == tok_operator && t->content == ";")
    {
      literal_number* l = new literal_number(1);
      s->cond = l;
      s->cond->tok = next ();
    }
  else
    {
      s->cond = parse_expression ();
      t = next ();
      if (! (t->type == tok_operator && t->content == ";"))
	throw parse_error (_("expected ';'"));
    }

  // increment + ")"
  t = peek ();
  if (t && t->type == tok_operator && t->content == ")")
    {
      s->incr = 0;
      next ();
    }
  else
    {
      s->incr = parse_expr_statement ();
      t = next ();
      if (! (t->type == tok_operator && t->content == ")"))
	throw parse_error (_("expected ')'"));
    }

  // block
  s->block = parse_statement ();

  return s;
}


for_loop*
parser::parse_while_loop ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "while"))
    throw parse_error (_("expected 'while'"));
  for_loop* s = new for_loop;
  s->tok = t;

  t = next ();
  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error (_("expected '('"));

  // dummy init and incr fields
  s->init = 0;
  s->incr = 0;

  // condition
  s->cond = parse_expression ();

  t = next ();
  if (! (t->type == tok_operator && t->content == ")"))
    throw parse_error (_("expected ')'"));

  // block
  s->block = parse_statement ();

  return s;
}


foreach_loop*
parser::parse_foreach_loop ()
{
  const token* t = next ();
  if (! (t->type == tok_keyword && t->content == "foreach"))
    throw parse_error (_("expected 'foreach'"));
  foreach_loop* s = new foreach_loop;
  s->tok = t;
  s->sort_direction = 0;
  s->value = NULL;
  s->limit = NULL;

  t = next ();
  if (! (t->type == tok_operator && t->content == "("))
    throw parse_error (_("expected '('"));

  symbol* lookahead_sym = NULL;
  int lookahead_sort = 0;

  t = peek ();
  if (t && t->type == tok_identifier)
    {
      next ();
      lookahead_sym = new symbol;
      lookahead_sym->tok = t;
      lookahead_sym->name = t->content;

      t = peek ();
      if (t && t->type == tok_operator &&
	  (t->content == "+" || t->content == "-"))
	{
	  next ();
	  lookahead_sort = (t->content == "+") ? 1 : -1;
	}

      t = peek ();
      if (t && t->type == tok_operator && t->content == "=")
	{
	  next ();
	  s->value = lookahead_sym;
	  if (lookahead_sort)
	    {
	      s->sort_direction = lookahead_sort;
	      s->sort_column = 0;
	    }
	  lookahead_sym = NULL;
	}
    }

  // see also parse_array_in

  bool parenthesized = false;
  t = peek ();
  if (!lookahead_sym && t && t->type == tok_operator && t->content == "[")
    {
      next ();
      parenthesized = true;
    }

  if (lookahead_sym)
    {
      s->indexes.push_back (lookahead_sym);
      if (lookahead_sort)
	{
	  s->sort_direction = lookahead_sort;
	  s->sort_column = 1;
	}
      lookahead_sym = NULL;
    }
  else while (1)
    {
      t = next ();
      if (! (t->type == tok_identifier))
        throw parse_error (_("expected identifier"));
      symbol* sym = new symbol;
      sym->tok = t;
      sym->name = t->content;
      s->indexes.push_back (sym);

      t = peek ();
      if (t && t->type == tok_operator &&
	  (t->content == "+" || t->content == "-"))
	{
	  if (s->sort_direction)
	    throw parse_error (_("multiple sort directives"));
	  s->sort_direction = (t->content == "+") ? 1 : -1;
	  s->sort_column = s->indexes.size();
	  next();
	}

      if (parenthesized)
        {
          t = peek ();
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
            throw parse_error (_("expected ',' or ']'"));
        }
      else
        break; // expecting only one expression
    }

  t = next ();
  if (! (t->type == tok_keyword && t->content == "in"))
    throw parse_error (_("expected 'in'"));

  s->base = parse_indexable();

  t = peek ();
  if (t && t->type == tok_operator &&
      (t->content == "+" || t->content == "-"))
    {
      if (s->sort_direction)
	throw parse_error (_("multiple sort directives"));
      s->sort_direction = (t->content == "+") ? 1 : -1;
      s->sort_column = 0;
      next();
    }

  t = peek ();
  if (tok_is(t, tok_keyword, "limit"))
    {
      next ();				// get past the "limit"
      s->limit = parse_expression ();
    }

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
	  t->content == "-=" ||
	  t->content == "*=" ||
	  t->content == "/=" ||
	  t->content == "%=" ||
	  t->content == "<<=" ||
	  t->content == ">>=" ||
	  t->content == "&=" ||
	  t->content == "^=" ||
	  t->content == "|=" ||
	  t->content == ".=" ||
	  false))
    {
      // NB: lvalueness is checked during elaboration / translation
      assignment* e = new assignment;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_expression ();
      op1 = e;
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
        throw parse_error (_("expected ':'"));

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
  expression* op1 = parse_boolean_or ();

  const token* t = peek ();
  while (t && t->type == tok_operator && t->content == "&&")
    {
      logical_and_expr *e = new logical_and_expr;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_boolean_or ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_boolean_or ()
{
  expression* op1 = parse_boolean_xor ();

  const token* t = peek ();
  while (t && t->type == tok_operator && t->content == "|")
    {
      binary_expression* e = new binary_expression;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_boolean_xor ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_boolean_xor ()
{
  expression* op1 = parse_boolean_and ();

  const token* t = peek ();
  while (t && t->type == tok_operator && t->content == "^")
    {
      binary_expression* e = new binary_expression;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_boolean_and ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_boolean_and ()
{
  expression* op1 = parse_array_in ();

  const token* t = peek ();
  while (t && t->type == tok_operator && t->content == "&")
    {
      binary_expression* e = new binary_expression;
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
            throw parse_error (_("expected ',' or ']'"));
        }
      else
        break; // expecting only one expression
    }

  t = peek ();
  if (t && t->type == tok_keyword && t->content == "in")
    {
      array_in *e = new array_in;
      e->tok = t;
      next (); // swallow "in"

      arrayindex* a = new arrayindex;
      a->indexes = indexes;
      a->base = parse_indexable();
      a->tok = a->base->get_tok();
      e->operand = a;
      return e;
    }
  else if (indexes.size() == 1) // no "in" - need one expression only
    return indexes[0];
  else
    throw parse_error (_("unexpected comma-separated expression list"));
}


expression*
parser::parse_comparison ()
{
  expression* op1 = parse_shift ();

  const token* t = peek ();
  while (t && t->type == tok_operator
      && (t->content == ">" ||
          t->content == "<" ||
          t->content == "==" ||
          t->content == "!=" ||
          t->content == "<=" ||
          t->content == ">="))
    {
      comparison* e = new comparison;
      e->left = op1;
      e->op = t->content;
      e->tok = t;
      next ();
      e->right = parse_shift ();
      op1 = e;
      t = peek ();
    }

  return op1;
}


expression*
parser::parse_shift ()
{
  expression* op1 = parse_concatenation ();

  const token* t = peek ();
  while (t && t->type == tok_operator &&
         (t->content == "<<" || t->content == ">>"))
    {
      binary_expression* e = new binary_expression;
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
      && (t->content == "+" ||
          t->content == "-" ||
          t->content == "!" ||
          t->content == "~" ||
          false))
    {
      unary_expression* e = new unary_expression;
      e->op = t->content;
      e->tok = t;
      next ();
      e->operand = parse_unary ();
      return e;
    }
  else
    return parse_crement ();
}


expression*
parser::parse_crement () // as in "increment" / "decrement"
{
  // NB: Ideally, we'd parse only a symbol as an operand to the
  // *crement operators, instead of a general expression value.  We'd
  // need more complex lookahead code to tell apart the postfix cases.
  // So we just punt, and leave it to pass-3 to signal errors on
  // cases like "4++".

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
    throw parse_error (_("expected value"));

  if (t->type == tok_embedded)
    {
      next ();
      if (! privileged)
        throw parse_error (_("embedded expression code in unprivileged script; need stap -g"), false);

      embedded_expr *e = new embedded_expr;
      e->tok = t;
      e->code = t->content;
      return e;
    }

  if (t->type == tok_operator && t->content == "(")
    {
      next ();
      expression* e = parse_expression ();
      t = next ();
      if (! (t->type == tok_operator && t->content == ")"))
        throw parse_error (_("expected ')'"));
      return e;
    }
  else if (t->type == tok_operator && t->content == "&")
    {
      next ();
      return parse_target_symbol (t);
    }
  else if (t->type == tok_identifier)
    return parse_symbol ();
  else
    return parse_literal ();
}


const token *
parser::parse_hist_op_or_bare_name (hist_op *&hop, string &name)
{
  hop = NULL;
  const token* t = expect_ident (name);
  if (name == "@hist_linear" || name == "@hist_log")
    {
      hop = new hist_op;
      if (name == "@hist_linear")
	hop->htype = hist_linear;
      else if (name == "@hist_log")
	hop->htype = hist_log;
      hop->tok = t;
      expect_op("(");
      hop->stat = parse_expression ();
      int64_t tnum;
      if (hop->htype == hist_linear)
	{
	  for (size_t i = 0; i < 3; ++i)
	    {
	      expect_op (",");
	      expect_number (tnum);
	      hop->params.push_back (tnum);
	    }
	}
      expect_op(")");
    }
  return t;
}


indexable*
parser::parse_indexable ()
{
  hist_op *hop = NULL;
  string name;
  const token *tok = parse_hist_op_or_bare_name(hop, name);
  if (hop)
    return hop;
  else
    {
      symbol* sym = new symbol;
      sym->name = name;
      sym->tok = tok;
      return sym;
    }
}


// var, indexable[index], func(parms), printf("...", ...), $var,r
// @cast, @defined, @entry, @var, $var->member, @stat_op(stat)
expression* parser::parse_symbol ()
{
  hist_op *hop = NULL;
  symbol *sym = NULL;
  string name;
  const token *t = parse_hist_op_or_bare_name(hop, name);

  if (!hop)
    {
      // If we didn't get a hist_op, then we did get an identifier. We can
      // now scrutinize this identifier for the various magic forms of identifier
      // (printf, @stat_op, and $var...)

      if (name == "@cast"
	  || name == "@var"
	  || (name.size() > 0 && name[0] == '$'))
        return parse_target_symbol (t);

      // NB: PR11343: @defined() is not incompatible with earlier versions
      // of stap, so no need to check session.compatible for 1.2
      if (name == "@defined")
        return parse_defined_op (t);

      if (name == "@entry")
        return parse_entry_op (t);

      if (name.size() > 0 && name[0] == '@')
	{
	  stat_op *sop = new stat_op;
	  if (name == "@avg")
	    sop->ctype = sc_average;
	  else if (name == "@count")
	    sop->ctype = sc_count;
	  else if (name == "@sum")
	    sop->ctype = sc_sum;
	  else if (name == "@min")
	    sop->ctype = sc_min;
	  else if (name == "@max")
	    sop->ctype = sc_max;
	  else
	    throw parse_error(_("unknown operator ") + name);
	  expect_op("(");
	  sop->tok = t;
	  sop->stat = parse_expression ();
	  expect_op(")");
	  return sop;
	}

      else if (print_format *fmt = print_format::create(t))
	{
	  expect_op("(");
	  if ((name == "print" || name == "println" ||
	       name == "sprint" || name == "sprintln") &&
	      (peek_kw("@hist_linear") || peek_kw("@hist_log")))
	    {
	      // We have a special case where we recognize
	      // print(@hist_foo(bar)) as a magic print-the-histogram
	      // construct. This is sort of gross but it avoids
	      // promoting histogram references to typeful
	      // expressions.

	      hop = NULL;
	      t = parse_hist_op_or_bare_name(hop, name);
	      assert(hop);

	      // It is, sadly, possible that even while parsing a
	      // hist_op, we *mis-guessed* and the user wishes to
	      // print(@hist_op(foo)[bucket]), a scalar. In that case
	      // we must parse the arrayindex and print an expression.
	      //
	      // XXX: This still fails if the arrayindex is part of a
	      // larger expression.  To really handle everything, we'd
	      // need to push back all the hist tokens start over.

	      if (!peek_op ("["))
		fmt->hist = hop;
	      else
		{
		  // This is simplified version of the
		  // multi-array-index parser below, because we can
		  // only ever have one index on a histogram anyways.
		  expect_op("[");
		  struct arrayindex* ai = new arrayindex;
		  ai->tok = t;
		  ai->base = hop;
		  ai->indexes.push_back (parse_expression ());
		  expect_op("]");
		  fmt->args.push_back(ai);

		  // Consume any subsequent arguments.
		  while (!peek_op (")"))
		    {
		      expect_op(",");
		      expression *e = parse_expression ();
		      fmt->args.push_back(e);
		    }
		}
	    }
	  else
	    {
	      int min_args = 0;
	      if (fmt->print_with_format)
		{
		  // Consume and convert a format string. Agreement between the
		  // format string and the arguments is postponed to the
		  // typechecking phase.
		  string tmp;
		  expect_unknown (tok_string, tmp);
		  fmt->raw_components = tmp;
		  fmt->components = print_format::string_to_components (tmp);
		}
	      else if (fmt->print_with_delim)
		{
		  // Consume a delimiter to separate arguments.
		  fmt->delimiter.clear();
		  fmt->delimiter.type = print_format::conv_literal;
		  expect_unknown (tok_string, fmt->delimiter.literal_string);
		  min_args = 2;
		}
	      else
		{
		  // If we are not printing with a format string, we must have
		  // at least one argument (of any type).
		  expression *e = parse_expression ();
		  fmt->args.push_back(e);
		}

	      // Consume any subsequent arguments.
	      while (min_args || !peek_op (")"))
		{
		  expect_op(",");
		  expression *e = parse_expression ();
		  fmt->args.push_back(e);
		  if (min_args)
		    --min_args;
		}
	    }
	  expect_op(")");
	  return fmt;
	}

      else if (peek_op ("(")) // function call
	{
	  next ();
	  struct functioncall* f = new functioncall;
	  f->tok = t;
	  f->function = name;
	  // Allow empty actual parameter list
	  if (peek_op (")"))
	    {
	      next ();
	      return f;
	    }
	  while (1)
	    {
	      f->args.push_back (parse_expression ());
	      if (peek_op (")"))
		{
		  next();
		  break;
		}
	      else if (peek_op (","))
		{
		  next();
		  continue;
		}
	      else
		throw parse_error (_("expected ',' or ')'"));
	    }
	  return f;
	}

      else
	{
	  sym = new symbol;
	  sym->name = name;
	  sym->tok = t;
	}
    }

  // By now, either we had a hist_op in the first place, or else
  // we had a plain word and it was converted to a symbol.

  assert (!hop != !sym); // logical XOR

  // All that remains is to check for array indexing

  if (peek_op ("[")) // array
    {
      next ();
      struct arrayindex* ai = new arrayindex;
      ai->tok = t;

      if (hop)
	ai->base = hop;
      else
	ai->base = sym;

      while (1)
        {
          ai->indexes.push_back (parse_expression ());
          if (peek_op ("]"))
            {
	      next();
	      break;
	    }
          else if (peek_op (","))
	    {
	      next();
	      continue;
	    }
          else
            throw parse_error (_("expected ',' or ']'"));
        }
      return ai;
    }

  // If we got to here, we *should* have a symbol; if we have
  // a hist_op on its own, it doesn't count as an expression,
  // so we throw a parse error.

  if (hop)
    throw parse_error(_("base histogram operator where expression expected"), t);

  return sym;
}


// Parse a @cast or $var.  Given head token has already been consumed.
target_symbol* parser::parse_target_symbol (const token* t)
{
  bool addressof = false;
  if (t->type == tok_operator && t->content == "&")
    {
      addressof = true;
      t = next ();
    }

  if (t->type == tok_identifier && t->content == "@cast")
    {
      cast_op *cop = new cast_op;
      cop->tok = t;
      cop->name = t->content;
      expect_op("(");
      cop->operand = parse_expression ();
      expect_op(",");
      expect_unknown(tok_string, cop->type_name);
      if (peek_op (","))
        {
          next();
          expect_unknown(tok_string, cop->module);
        }
      expect_op(")");
      parse_target_symbol_components(cop);
      cop->addressof = addressof;
      return cop;
    }

  if (t->type == tok_identifier && t->content[0]=='$')
    {
      // target_symbol time
      target_symbol *tsym = new target_symbol;
      tsym->tok = t;
      tsym->name = t->content;
      tsym->target_name = "";
      tsym->cu_name = "";
      parse_target_symbol_components(tsym);
      tsym->addressof = addressof;
      return tsym;
    }

  if (t->type == tok_identifier && t->content == "@var")
    {
      target_symbol *tsym = new target_symbol;
      tsym->tok = t;
      tsym->name = t->content;
      expect_op("(");
      expect_unknown(tok_string, tsym->target_name);
      size_t found_at = tsym->target_name.find("@");
      if (found_at != string::npos)
	tsym->cu_name = tsym->target_name.substr(found_at + 1);
      else
	tsym->cu_name = "";
      expect_op(")");
      parse_target_symbol_components(tsym);
      tsym->addressof = addressof;
      return tsym;
    }

  throw parse_error (_("expected @cast, @var or $var"));
}


// Parse a @defined().  Given head token has already been consumed.
expression* parser::parse_defined_op (const token* t)
{
  defined_op* dop = new defined_op;
  dop->tok = t;
  expect_op("(");
  // no need for parse_hist_op... etc., as @defined takes only target_symbols as its operand.
  const token* tt = next ();
  dop->operand = parse_target_symbol (tt);
  expect_op(")");
  return dop;
}


// Parse a @entry().  Given head token has already been consumed.
expression* parser::parse_entry_op (const token* t)
{
  entry_op* eop = new entry_op;
  eop->tok = t;
  expect_op("(");
  eop->operand = parse_expression ();
  expect_op(")");
  return eop;
}



void
parser::parse_target_symbol_components (target_symbol* e)
{
  bool pprint = false;

  // check for pretty-print in the form $foo$
  string &base = e->name;
  size_t pprint_pos = base.find_last_not_of('$');
  if (0 < pprint_pos && pprint_pos < base.length() - 1)
    {
      string pprint_val = base.substr(pprint_pos + 1);
      base.erase(pprint_pos + 1);
      e->components.push_back (target_symbol::component(e->tok, pprint_val, true));
      pprint = true;
    }

  while (!pprint)
    {
      if (peek_op ("->"))
        {
          const token* t = next();
          string member;
          expect_ident_or_keyword (member);

          // check for pretty-print in the form $foo->$ or $foo->bar$
          pprint_pos = member.find_last_not_of('$');
          string pprint_val;
          if (pprint_pos == string::npos || pprint_pos < member.length() - 1)
            {
              pprint_val = member.substr(pprint_pos + 1);
              member.erase(pprint_pos + 1);
              pprint = true;
            }

          if (!member.empty())
            e->components.push_back (target_symbol::component(t, member));
          if (pprint)
            e->components.push_back (target_symbol::component(t, pprint_val, true));
        }
      else if (peek_op ("["))
        {
          const token* t = next();
          expression* index = parse_expression();
          literal_number* ln = dynamic_cast<literal_number*>(index);
          if (ln)
            e->components.push_back (target_symbol::component(t, ln->value));
          else
            e->components.push_back (target_symbol::component(t, index));
          expect_op ("]");
        }
      else
        break;
    }

  if (!pprint)
    {
      // check for pretty-print in the form $foo $
      // i.e. as a separate token, esp. for $foo[i]$ and @cast(...)$
      const token* t = peek();
      if (t->type == tok_identifier &&
          t->content.find_first_not_of('$') == string::npos)
        {
          t = next();
          e->components.push_back (target_symbol::component(t, t->content, true));
          pprint = true;
        }
    }

  if (pprint && (peek_op ("->") || peek_op("[")))
    throw parse_error(_("-> and [ are not accepted for a pretty-printing variable"));
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
