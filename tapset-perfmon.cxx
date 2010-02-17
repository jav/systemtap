// tapset for HW performance monitoring
// Copyright (C) 2005-2009 Red Hat Inc.
// Copyright (C) 2005-2007 Intel Corporation.
// Copyright (C) 2008 James.Bottomley@HansenPartnership.com
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "session.h"
#include "tapsets.h"
#include "util.h"

#include <string>

using namespace std;
using namespace __gnu_cxx;



// ------------------------------------------------------------------------
// perfmon derived probes
// ------------------------------------------------------------------------
// This is a new interface to the perfmon hw.
//

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
