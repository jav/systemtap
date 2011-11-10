// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef BUILDRUN_H
#define BUILDRUN_H

#include "elaborate.h"

int compile_pass (systemtap_session& s);
int uprobes_pass (systemtap_session& s);

std::vector<std::string> make_run_command (systemtap_session& s,
                                           const std::string& remotedir="",
                                           const std::string& version=VERSION);

std::map<std::string,std::string> make_tracequeries(systemtap_session& s, const std::map<std::string,std::string>& contents);
int make_typequery(systemtap_session& s, std::string& module);

#endif // BUILDRUN_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
