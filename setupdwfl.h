// Setup routines for creating fully populated DWFLs. Used in pass 2 and 3.
// Copyright (C) 2009 Red Hat, Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
//
#ifndef SETUP_DWFLPP_H
#define SETUP_DWFLPP_H

#include "config.h"
#include "session.h"

#include <set>
#include <string>

extern "C" {
#include <elfutils/libdwfl.h>
}

Dwfl *setup_dwfl_kernel(const std::string &name,
			unsigned *found,
			systemtap_session &s);
Dwfl *setup_dwfl_kernel(const std::set<std::string> &names,
			unsigned *found,
			systemtap_session &s);
#endif
