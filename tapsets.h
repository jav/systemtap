#ifndef TAPSETS_H
#define TAPSETS_H

// -*- C++ -*-
// Copyright (C) 2005 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "config.h"
#include "staptree.h"
#include "elaborate.h"


void 
register_standard_tapsets(systemtap_session & sess);


#endif // TAPSETS_H
