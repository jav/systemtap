// coveragedb.cxx
// Copyright (C) 2007 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "parse.h"
#include "coveragedb.h"
#include "config.h"
#include "elaborate.h"
#include "tapsets.h"
#include "session.h"
#include "util.h"

#ifdef HAVE_LIBSQLITE3

#include <iostream>
#include <sqlite3.h>
#include <cstdlib>

using namespace std;

void print_coverage_info(systemtap_session &s)
{
  // print out used probes
  clog << "---- used probes-----" << endl;
  for (unsigned i=0; i<s.probes.size(); i++) {
    // walk through the chain of probes
    vector<probe*> used_probe_list;
    s.probes[i]->collect_derivation_chain(used_probe_list);
    for (unsigned j=0; j<used_probe_list.size(); ++j) {
      for (unsigned k=0; k< used_probe_list[j]->locations.size(); ++k)
        clog << "probe: "
	     << used_probe_list[j]->locations[k]->tok->location << endl;
    }

    clog << "----" << endl;
    // for each probe print used and unused variables
    for (unsigned j=0; j<s.probes[i]->locals.size(); ++j) {
	    clog << "local: " << s.probes[i]->locals[j]->tok->location << endl;
    }
    for (unsigned j=0; j<s.probes[i]->unused_locals.size(); ++j) {
	    clog << "unused_local: "
		 << s.probes[i]->unused_locals[j]->tok->location
		 << endl;
    }
  }
  // print out unused probes
  clog << "---- unused probes----- " << endl;
  for (unsigned i=0; i<s.unused_probes.size(); i++) {
    // walk through the chain of probes
    vector<probe*> unused_probe_list;
    s.unused_probes[i]->collect_derivation_chain(unused_probe_list);
    for (unsigned j=0; j<unused_probe_list.size(); ++j) {
      for (unsigned k=0; k< unused_probe_list[j]->locations.size(); ++k)
        clog << "probe: "
	     << unused_probe_list[j]->locations[k]->tok->location << endl;
    }

  }
  // print out used functions
  clog << "---- used functions----- " << endl;
  for (unsigned i=0; i<s.functions.size(); i++) {
     clog << "function: " << s.functions[i]->tok->location
	  << " "  << s.functions[i]->name
	  << endl;
  }
  // print out unused functions
  clog << "---- unused functions----- " << endl;
  for (unsigned i=0; i<s.unused_functions.size(); i++) {
     clog << "unused_function: " << s.unused_functions[i]->tok->location
	  << " "  << s.unused_functions[i]->name
	  << endl;
  }
  // print out used globals
  clog << "---- used globals----- " << endl;
  for (unsigned i=0; i<s.globals.size(); i++) {
     clog << "globals: " << s.globals[i]->tok->location
	  << " " << s.globals[i]->name
	  << endl;
  }
  // print out unused globals
  clog << "---- unused globals----- " << endl;
  for (unsigned i=0; i<s.unused_globals.size(); i++) {
     clog << "globals: " << s.unused_globals[i]->tok->location
	  << " " << s.unused_globals[i]->name
	  << endl;
  }
}


bool
has_table(sqlite3 *db, const char * table)
{
  int rc, rows, columns;
  char *errmsg;
  char **results = NULL;

  ostringstream command;
  command << "SELECT name FROM sqlite_master "
	  << "WHERE type='table' AND name='" << table << "'";

  rc = sqlite3_get_table(db, command.str().c_str(),
			 &results, &rows, &columns, &errmsg);

  if(rc != SQLITE_OK) {
    cerr << "Error in statement: " << command << " [" << errmsg << "]."
				 << endl;
  }
  sqlite3_free_table(results);
  return (rows !=0);
}


bool
has_index(sqlite3 *db, const char * index)
{
  int rc, rows, columns;
  char *errmsg;
  char **results = NULL;

  ostringstream command;
  command << "SELECT name FROM sqlite_master "
	  << "WHERE type='index' AND name='" << index << "'";

  rc = sqlite3_get_table(db, command.str().c_str(),
			 &results, &rows, &columns, &errmsg);

  if(rc != SQLITE_OK) {
    cerr << "Error in statement: " << command << " [" << errmsg << "]."
				 << endl;
  }
  sqlite3_free_table(results);
  return (rows !=0);
}


void sql_stmt(sqlite3 *db, const char* stmt)
{
  char *errmsg;
  int   ret;

  //  cerr << "sqlite: " << stmt << endl;

  ret = sqlite3_exec(db, stmt, 0, 0, &errmsg);

  if(ret != SQLITE_OK) {
    cerr << "Error in statement: " << stmt << " [" << errmsg << "]."
				 << endl;
  }
}

void enter_element(sqlite3 *db, coverage_element &x)
{
  ostringstream command;
  command << "insert or ignore into counts values ('"
          << x.file << "', '"
          << x.line << "', '"
          << x.col  << "', '"
          << x.type << "','"
          << x.name << "', '"
          << x.parent <<"',"
          << "'0', '0')";
  sql_stmt(db, command.str().c_str());
}


void increment_element(sqlite3 *db, coverage_element &x)
{
  ostringstream command;
  // make sure value in table
  command << "insert or ignore into counts values ('"
          << x.file << "', '"
          << x.line << "', '"
          << x.col  << "', '"
          << x.type << "','"
          << x.name << "', '"
          << x.parent <<"',"
          << "'0', '0'); "
  // increment appropriate value
	  << "update counts set compiled=compiled+"
	  << x.compiled << " where ("
	  << "file=='" << x.file << "' and "
	  << "line=='" << x.line << "' and "
	  << "col=='" << x.col << "' and "
	  << "type=='" << x.type << "' and "
	  << "name=='" << x.name << "')";
  sql_stmt(db, command.str().c_str());
}


void
sql_update_used_probes(sqlite3 *db, systemtap_session &s)
{
  // update database used probes
  for (unsigned i=0; i<s.probes.size(); i++) {
    // walk through the chain of probes
    vector<probe*> used_probe_list;
    s.probes[i]->collect_derivation_chain(used_probe_list);
    for (unsigned j=0; j<used_probe_list.size(); ++j) {
	    for (unsigned k=0; k< used_probe_list[j]->locations.size(); ++k){
		    struct source_loc place = used_probe_list[j]->locations[k]->tok->location;
		    coverage_element x(place);

		    x.type = db_type_probe;
		    x.name = used_probe_list[j]->locations[k]->str();
		    x.compiled = 1;
		    increment_element(db, x);
	    }
    }

    // for each probe update used and unused variables
    for (unsigned j=0; j<s.probes[i]->locals.size(); ++j) {
	    struct source_loc place = s.probes[i]->locals[j]->tok->location;
	    coverage_element x(place);

	    x.type = db_type_local;
	    x.name = s.probes[i]->locals[j]->tok->content;
	    x.compiled = 1;
	    increment_element(db, x);
    }
    for (unsigned j=0; j<s.probes[i]->unused_locals.size(); ++j) {
	    struct source_loc place = s.probes[i]->unused_locals[j]->tok->location;
	    coverage_element x(place);

	    x.type = db_type_local;
	    x.name = s.probes[i]->unused_locals[j]->tok->content;
	    x.compiled = 0;
	    increment_element(db, x);
    }
  }
}


void
sql_update_unused_probes(sqlite3 *db, systemtap_session &s)
{
  // update database unused probes
  for (unsigned i=0; i<s.unused_probes.size(); i++) {
    // walk through the chain of probes
    vector<probe*> unused_probe_list;
    s.unused_probes[i]->collect_derivation_chain(unused_probe_list);
    for (unsigned j=0; j<unused_probe_list.size(); ++j) {
	    for (unsigned k=0; k< unused_probe_list[j]->locations.size(); ++k) {

	      struct source_loc place = unused_probe_list[j]->locations[k]->tok->location;
	      coverage_element x(place);

	      x.type = db_type_probe;
	      x.name = unused_probe_list[j]->locations[k]->str();
	      x.compiled = 0;
	      increment_element(db, x);
	    }
    }
  }
}


void
sql_update_used_functions(sqlite3 *db, systemtap_session &s)
{
  // update db used functions
  for (unsigned i=0; i<s.functions.size(); i++) {
    struct source_loc place = s.functions[i]->tok->location;
    coverage_element x(place);

    x.type = db_type_function;
    x.name = s.functions[i]->name;
    x.compiled = 1;
    increment_element(db, x);
  }
}


void
sql_update_unused_functions(sqlite3 *db, systemtap_session &s)
{
  // update db unused functions
  for (unsigned i=0; i<s.unused_functions.size(); i++) {
    struct source_loc place = s.unused_functions[i]->tok->location;
    coverage_element x(place);

    x.type = db_type_function;
    x.name = s.unused_functions[i]->name;
    x.compiled = 0;
    increment_element(db, x);
  }
}


void
sql_update_used_globals(sqlite3 *db, systemtap_session &s)
{
  // update db used globals
  for (unsigned i=0; i<s.globals.size(); i++) {
    struct source_loc place = s.globals[i]->tok->location;
    coverage_element x(place);

    x.type = db_type_global;
    x.name = s.globals[i]->name;
    x.compiled = 1;
    increment_element(db, x);
  }
}


void
sql_update_unused_globals(sqlite3 *db, systemtap_session &s)
{
  // update db unused globals
  for (unsigned i=0; i<s.unused_globals.size(); i++) {
    struct source_loc place = s.unused_globals[i]->tok->location;
    coverage_element x(place);

    x.type = db_type_global;
    x.name = s.unused_globals[i]->name;
    x.compiled = 0;
    increment_element(db, x);
  }
}

void update_coverage_db(systemtap_session &s)
{
  sqlite3 *db;
  int rc;

  string filename(s.data_path + "/" + s.kernel_release + ".db");

  rc = sqlite3_open(filename.c_str(), &db);
  if( rc ){
    cerr << "Can't open database: " << sqlite3_errmsg(db) << endl;
    sqlite3_close(db);
    exit(EXIT_FAILURE);
  }

  // lock the database
  sql_stmt(db, "begin");

  string create_table("create table counts ("
                      "file text, line integer, col integer, "
                      "type text, name text, parent text, "
                      "compiled integer, executed integer)");
  string create_index("create unique index tokens on counts (file, line, col, "
		      "type, name)");

  // make sure the table is there
  if (!has_table(db, "counts"))
    sql_stmt(db, create_table.c_str());

  // make sure the index is there
  if (!has_index(db, "tokens"))
    sql_stmt(db, create_index.c_str());

  sql_update_used_probes(db, s);
  sql_update_unused_probes(db, s);
  sql_update_used_functions(db, s);
  sql_update_unused_functions(db, s);
  sql_update_used_globals(db, s);
  sql_update_unused_globals(db, s);

  // unlock the database and close database
  sql_stmt(db, "commit");

  sqlite3_close(db);
}

#endif /* HAVE_LIBSQLITE3 */
