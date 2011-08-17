// coveragedb.cxx
// Copyright (C) 2007 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef COVERAGEDB_H
#define COVERAGEDB_H

#include "session.h"
#include "staptree.h"

#include <string>


/*

tuples: file, line number, column, type of object, name
values: number of times object "pulled_in", number of times "removed",
times executed

if (compiled == 0) object never compiled
if (compiled > 0) object compiled

The following are not currently implemented.
if (executed == 0) never executed
if (executed > 0) executed


Want to make sure that the data base accurately reflects testing.
1) atomic updates, either commit all or none of information
2) only update coverage db compile info, if compile successful
3) only update coverage db execute info, if instrumentation run suscessfully


Would like to have something that looks for interesting features in db:

list which things are not compile
list which things are not exectuted

ratio of compiled/total (overall, by file, by line)
ratio of executed/total (overall, by file, by line)

*/

enum db_type {
  db_type_probe = 1,
  db_type_function = 2,
  db_type_local = 3,
  db_type_global = 4,
};

class coverage_element {
public:
  std::string file;
  int line;
  int col;
  int type;
  std::string name;
  std::string parent;
  int compiled;
  int executed;

  coverage_element():
    line(0), col(0), compiled(0), executed(0) {}

  coverage_element(source_loc &place):
    file(place.file->name), line(place.line), col(place.column),
    type(0), compiled(0), executed(0) {}
};



void print_coverage_info(systemtap_session &s);
void update_coverage_db(systemtap_session &s);

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
