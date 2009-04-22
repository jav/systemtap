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

struct derived_probe_group;

void register_standard_tapsets(systemtap_session& sess);
std::vector<derived_probe_group*> all_session_groups(systemtap_session& s);
int dwfl_report_offline_predicate (const char* modname, const char* filename);


#endif // TAPSETS_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
