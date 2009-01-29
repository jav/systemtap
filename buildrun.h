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
int run_pass (systemtap_session& s);


#endif // BUILDRUN_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
