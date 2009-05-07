// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef TAPSETS_H
#define TAPSETS_H

#include "config.h"
#include "staptree.h"
#include "elaborate.h"

void register_standard_tapsets(systemtap_session& sess);
std::vector<derived_probe_group*> all_session_groups(systemtap_session& s);
int dwfl_report_offline_predicate (const char* modname, const char* filename);
void common_probe_entryfn_prologue (translator_output* o, std::string statestr,
				    std::string new_pp, bool overload_processing = true);
void common_probe_entryfn_epilogue (translator_output* o, bool overload_processing = true);

void register_tapset_been(systemtap_session& sess);
void register_tapset_mark(systemtap_session& sess);
void register_tapset_perfmon(systemtap_session& sess);
void register_tapset_procfs(systemtap_session& sess);
void register_tapset_timers(systemtap_session& sess);


// ------------------------------------------------------------------------
// Generic derived_probe_group: contains an ordinary vector of the
// given type.  It provides only the enrollment function.

template <class DP> struct generic_dpg: public derived_probe_group
{
protected:
  std::vector <DP*> probes;
public:
  generic_dpg () {}
  void enroll (DP* probe) { probes.push_back (probe); }
};


// ------------------------------------------------------------------------
// An update visitor that allows replacing assignments with a function call

struct var_expanding_visitor: public update_visitor
{
  static unsigned tick;
  std::stack<functioncall**> target_symbol_setter_functioncalls;

  var_expanding_visitor() {}
  void visit_assignment (assignment* e);
};

#endif // TAPSETS_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
