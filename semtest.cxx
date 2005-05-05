// semantic analysis pass, beginnings of elaboration
// Copyright 2005 Red Hat Inc.
// GPL

#include "staptree.h"
#include "parse.h"
#include <iostream>


int
semantic_pass_1 (vector<stapfile*>& files)
{
  int rc = 0;

  // link up symbols to their declarations
  for (unsigned i=0; i<files.size(); i++)
    {
      stapfile* f = files[i];

      // ... on functions
      for (unsigned j=0; j<f->functions.size(); j++)
        {
          functiondecl* fn = f->functions[j];
          symresolution_info ri (fn->locals, f->globals, f->functions, fn);

          fn->body->resolve_symbols (ri);
          if (ri.num_unresolved)
            rc ++;
        }

      // ... and on probes
      for (unsigned j=0; j<f->probes.size(); j++)
        {
          probe* pn = f->probes[j];
          symresolution_info ri (pn->locals, f->globals, f->functions);

          pn->body->resolve_symbols (ri);
          if (ri.num_unresolved)
            rc ++;
        }
    }

  return rc;
}


int
semantic_pass_2 (vector<stapfile*>& files)
{
  int rc = 0;

  // next pass: type inference
  unsigned iterations = 0;
  typeresolution_info ti;
  
  ti.assert_resolvability = false;
  while (1)
    {
      iterations ++;
      // cerr << "Type resolution, iteration " << iterations << endl;
      ti.num_newly_resolved = 0;
      ti.num_still_unresolved = 0;
      
      for (unsigned i=0; i<files.size(); i++)
        {
          stapfile* f = files[i];
          
          for (unsigned j=0; j<f->functions.size(); j++)
            {
              functiondecl* fn = f->functions[j];
              ti.current_function = fn;
              fn->body->resolve_types (ti);
              if (fn->type == pe_unknown)
                ti.unresolved (fn->tok);
            }
          
          for (unsigned j=0; j<f->probes.size(); j++)
            {
              probe* pn = f->probes[j];
              ti.current_function = 0;
              pn->body->resolve_types (ti);
            }

          for (unsigned j=0; j<f->globals.size(); j++)
            {
              vardecl* gd = f->globals[j];
              if (gd->type == pe_unknown)
                ti.unresolved (gd->tok);
            }
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

  return rc;
}


int
main (int argc, char *argv [])
{
  int rc = 0;

  vector<stapfile*> files;
  if (argc == 1)
    {
      stapfile* f = parser::parse (cin);
      if (f)
        files.push_back (f);
      else
        rc ++;
    }
  else for (int i = 1; i < argc; i ++)
    {
      stapfile* f = parser::parse (argv[i]);
      if (f)
        files.push_back (f);
      else
        rc ++;
    }

  rc += semantic_pass_1 (files);
  if (rc == 0)
    rc += semantic_pass_2 (files);

  for (unsigned i=0; i<files.size(); i++)
    {
      stapfile* f = files[i];
      for (unsigned j=0; j<f->functions.size(); j++)
        {
          functiondecl* fn = f->functions[j];
          cerr << "Function ";
          fn->printsig (cerr);
          cerr << endl << "locals:" << endl;
          for (unsigned k=0; k<fn->locals.size(); k++)
            {
              vardecl* fa = fn->locals[k];
              cerr << "\t";
              fa->printsig (cerr);
              cerr << endl;
            }
          cerr << endl;
        }
      
      for (unsigned j=0; j<f->probes.size(); j++)
        {
          probe* pn = f->probes[j];
          cerr << "Probe ";
          pn->printsig (cerr);
          cerr << "locals:" << endl;
          for (unsigned k=0; k<pn->locals.size(); k++)
            {
              vardecl* fa = pn->locals[k];
              cerr << "\t";
              fa->printsig (cerr);
              cerr << endl;
            }
          cerr << endl;
        }
      
      cerr << "globals:" << endl;
      for (unsigned k=0; k<f->globals.size(); k++)
        {
          vardecl* fa = f->globals[k];
          cerr << "\t";
          fa->printsig (cerr);
          cerr << endl;
        }
      cerr << endl;
    }

  return rc;
}
