// -*- C++ -*-
// Copyright (C) 2008 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "dwarf_wrappers.h"
#include "staptree.h"

#include <cstring>
#include <string>
#include <elfutils/libdwfl.h>

using std::string;

void dwfl_assert(const string& desc, int rc)
{
  if (rc == 0)
    return;
  string msg = "libdwfl failure (" + desc + "): ";
  if (rc < 0)
    msg += dwfl_errmsg (rc);
  else
    msg += std::strerror (rc);
  throw semantic_error (msg);
}

void dwarf_assert(const string& desc, int rc)
{
  if (rc == 0)
    return;
  string msg = "libdw failure (" + desc + "): ";
  if (rc < 0)
    msg += dwarf_errmsg (rc);
  else
    msg += std::strerror (rc);
  throw semantic_error (msg);
}

void dwfl_assert(const std::string& desc, bool condition)
{
    if (!condition)
        dwarf_assert(desc, -1);
}
