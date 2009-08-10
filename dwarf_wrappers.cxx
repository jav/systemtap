// -*- C++ -*-
// Copyright (C) 2008-2009 Red Hat Inc.
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
#include <dwarf.h>

using std::string;

void dwfl_assert(const string& desc, int rc)
{
  if (rc == 0)
    return;
  string msg = "libdwfl failure (" + desc + "): ";
  if (rc < 0)
    msg += (dwfl_errmsg (rc) ?: "?");
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
        dwfl_assert(desc, -1);
}


#if !_ELFUTILS_PREREQ(0, 143)
// Elfutils prior to 0.143 didn't use attr_integrate when looking up the
// decl_file or decl_line, so the attributes would sometimes be missed.  For
// those old versions, we define custom implementations to do the integration.

const char *
dwarf_decl_file_integrate (Dwarf_Die *die)
{
  Dwarf_Attribute attr_mem;
  Dwarf_Sword idx = 0;
  if (dwarf_formsdata (dwarf_attr_integrate (die, DW_AT_decl_file, &attr_mem),
                       &idx) != 0
      || idx == 0)
    return NULL;

  Dwarf_Die cudie;
  Dwarf_Files *files = NULL;
  if (dwarf_getsrcfiles (dwarf_diecu (die, &cudie, NULL, NULL),
                         &files, NULL) != 0)
    return NULL;

  return dwarf_filesrc(files, idx, NULL, NULL);
}

int
dwarf_decl_line_integrate (Dwarf_Die *die, int *linep)
{
  Dwarf_Attribute attr_mem;
  Dwarf_Sword line;

  int res = dwarf_formsdata (dwarf_attr_integrate
                             (die, DW_AT_decl_line, &attr_mem),
                             &line);
  if (res == 0)
    *linep = line;

  return res;
}

#endif // !_ELFUTILS_PREREQ(0, 143)


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
