// toy driver
// Copyright 2005 Red Hat Inc.
// GPL

#include "staptree.h"
#include "parse.h"
#include <iostream>



expression::~expression () {} 
statement::~statement () {} 


int main (int argc, char *argv [])
{
  int rc = 0;

  if (argc > 1)
    {
      // quietly parse all listed input files
      for (int i = 1; i < argc; i ++)
        {
          parser p (argv[i]);
          stapfile* f = p.parse ();
          if (f)
            cout << "file '" << argv[i] << "' parsed ok." << endl;
          else
            rc = 1;
        }
    }
  else
    {
      // parse then print just stdin
      parser p (cin);
      stapfile* f = p.parse ();
      if (f)
        f->print (cout);
      else
        rc = 1;
    }

  return rc;
}
