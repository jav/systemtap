// toy driver
// Copyright 2005 Red Hat Inc.
// GPL


#include "staptree.h"
#include "parse.h"
#include <iostream>


int main (int argc, char *argv [])
{
  int rc = 0;

  if (argc > 1)
    {
      for (int i = 1; i < argc; i ++)
        {
          parser p (argv[i]);
          stapfile* f = p.parse ();
          if (f)
            f->print (cout);
          else
            rc = 1;
        }
    }
  else
    {
      parser p (cin);
      stapfile* f = p.parse ();
      if (f)
        f->print (cout);
      else
        rc = 1;
    }

  return rc;
}
