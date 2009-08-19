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
#include <sstream>
#include <string>
#include <elfutils/libdwfl.h>
#include <dwarf.h>

using namespace std;

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


static bool
dwarf_type_name(Dwarf_Die *type_die, ostringstream& o)
{
  // if we've gotten down to a basic type, then we're done
  bool done = true;
  switch (dwarf_tag(type_die))
    {
    case DW_TAG_enumeration_type:
      o << "enum ";
      break;
    case DW_TAG_structure_type:
      o << "struct ";
      break;
    case DW_TAG_union_type:
      o << "union ";
      break;
    case DW_TAG_typedef:
    case DW_TAG_base_type:
      break;
    default:
      done = false;
      break;
    }
  if (done)
    {
      // this follows gdb precedent that anonymous structs/unions
      // are displayed as "struct {...}" and "union {...}".
      o << (dwarf_diename(type_die) ?: "{...}");
      return true;
    }

  // otherwise, this die is a type modifier.

  // recurse into the referent type
  // if it can't be named, just call it "void"
  Dwarf_Attribute subtype_attr;
  Dwarf_Die subtype_die;
  if (!dwarf_attr_integrate(type_die, DW_AT_type, &subtype_attr)
      || !dwarf_formref_die(&subtype_attr, &subtype_die)
      || !dwarf_type_name(&subtype_die, o))
    o.str("void"), o.seekp(4);

  switch (dwarf_tag(type_die))
    {
    case DW_TAG_pointer_type:
      o << "*";
      break;
    case DW_TAG_array_type:
      o << "[]";
      break;
    case DW_TAG_const_type:
      o << " const";
      break;
    case DW_TAG_volatile_type:
      o << " volatile";
      break;
    default:
      return false;
    }

  // XXX HACK!  The va_list isn't usable as found in the debuginfo...
  if (o.str() == "struct __va_list_tag*")
    o.str("va_list"), o.seekp(7);

  return true;
}


bool
dwarf_type_name(Dwarf_Die *type_die, string& type_name)
{
  ostringstream o;
  bool ret = dwarf_type_name(type_die, o);
  type_name = o.str();
  return ret;
}


string
dwarf_type_name(Dwarf_Die *type_die)
{
  ostringstream o;
  return dwarf_type_name(type_die, o) ? o.str() : "<unknown>";
}


/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
