// -*- C++ -*-
// Copyright 2005 Red Hat Inc.
// GPL

#include <string>
#include <vector>
#include <map>
#include <iostream>

using namespace std;


struct source_location
{
  // source co-ordinates
  string lexeme;
  string source_file;
  unsigned source_line;
};


struct expression
{
  enum { pe_void, pe_unknown, pe_long, pe_string } type;
  source_location loc;
  virtual void print (ostream& o) = 0;
  virtual ~expression ();
};


inline ostream& operator << (ostream& o, expression& k)
{
  k.print (o);
  return o;
}

struct literal: public expression
{
};

struct literal_string: public literal
{
  string value;
  literal_string (const string& v): value (v) {}
  void print (ostream& o) { o << '"' << value << '"'; }
};

struct literal_number: public literal
{
  long value;
  literal_number (long v): value(v) {}
  void print (ostream& o) { o << value; }
};


struct binary_expression: public expression
{
  expression* left;
  string op;
  expression* right;
  void print (ostream& o) { o << '(' << *left << ")" 
                                 << op 
                                 << '(' << *right << ")"; }
};

struct unary_expression: public expression
{
  string op;
  expression* operand;
  void print (ostream& o) { o << op << '(' << *operand << ")"; }
};

struct pre_crement: public unary_expression
{
};

struct post_crement: public unary_expression
{
  void print (ostream& o) { o << '(' << *operand << ")" << op; }
                                 

};

struct logical_or_expr: public binary_expression
{
};

struct logical_and_expr: public binary_expression
{
};

struct array_in: public binary_expression
{
};

struct comparison: public binary_expression
{
};

struct concatenation: public binary_expression
{
};

struct exponentiation: public binary_expression
{
};


struct ternary_expression: public expression
{
  expression* cond;
  expression* truevalue;
  expression* falsevalue;
  void print (ostream& o) { o << "(" << *cond << ") ? ("
                                 << *truevalue << ") : ("
                                 << *falsevalue << ")"; }
};


struct symbol: public expression
{
  string name;
  void print (ostream& o) { o << name; }
};

struct arrayindex: public symbol
{
  vector<expression*> indexes;
  void print (ostream& o) 
  {
    symbol::print(o);
    o << "[";
    for (unsigned i=0; i<indexes.size(); i++)
      o << (i>0 ? ", " : "") << *indexes[i];
    o << "]";
  }  
};

struct functioncall: public symbol
{
  vector<expression*> args;
  void print (ostream& o) 
  {
    symbol::print(o);
    o << "(";
    for (unsigned i=0; i<args.size(); i++)
      o << (i>0 ? ", " : "") << *args[i];
    o << ")";
  }  
};


struct statement
{
  source_location loc;
  virtual void print (ostream& o) = 0;
  virtual ~statement ();
};


inline ostream& operator << (ostream& o, statement& k)
{
  k.print (o);
  return o;
}


struct block: public statement
{
  vector<statement*> statements;
  void print (ostream& o)
  {
    o << "{" << endl;
    for (unsigned i=0; i<statements.size(); i++)
      o << *statements [i] << ";" << endl;
    o << "}" << endl;
  }
};

struct for_loop: public statement
{
  expression* init;
  expression* cond;
  expression* incr;
  statement* block;
  void print (ostream& o)
  { o << "<for_loop>" << endl; }
};

struct null_statement: public statement
{
  void print (ostream& o)
  { o << ";"; }

};

struct assignment: public expression
{
  expression* lvalue; // XXX: consider type for lvalues; see parse_variable ()
  string op;
  expression* rvalue;

  void print (ostream& o)
  { o << *lvalue << " " << op << " " << *rvalue; }
};

struct expr_statement: public statement
{
  expression* value;  // executed for side-effects
  void print (ostream& o)
  { o << *value; }
};

struct if_statement: public statement
{
  expression* condition;
  statement* thenblock;
  statement* elseblock;
  void print (ostream& o)
  { o << "if (" << *condition << ") " << endl
      << *thenblock << endl;
  if (elseblock)
    o << "else " << *elseblock << endl; }
};

struct probe;

struct stapfile
{
  string name;
  vector<probe*> probes;
  vector<symbol*> globals;

  void print (ostream& o);
};


struct probe_point_spec // inherit from something or other?
{
  string functor;
  literal* arg;

  void print (ostream& o)
  { o << functor;
  if (arg)
    o << "(" << *arg << ")";
  }
};


struct probe
{
  // map<string,psymbol*> locals;
  vector<probe_point_spec*> location;
  block* body;

  void print (ostream& o)
  { o << "probe " << endl;
  for(unsigned i=0; i<location.size(); i++)
    {
      o << (i>0 ? ":" : "");
      location[i]->print (o);
    }
  o << endl;
  o << *body;
  }
};



inline void stapfile::print (ostream& o)
{ o << "# file " << name << endl;
  for(unsigned i=0; i<probes.size(); i++)
    {
      probes[i]->print (o);
      o << endl;
    }
  }
