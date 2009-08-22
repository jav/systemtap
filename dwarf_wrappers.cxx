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
dwarf_type_name(Dwarf_Die *type_die, ostream& o)
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
    case DW_TAG_class_type:
      o << "class ";
      break;
    case DW_TAG_typedef:
    case DW_TAG_base_type:
      break;

    // modifier types that require recursion first
    case DW_TAG_reference_type:
    case DW_TAG_rvalue_reference_type:
    case DW_TAG_pointer_type:
    case DW_TAG_array_type:
    case DW_TAG_const_type:
    case DW_TAG_volatile_type:
      done = false;
      break;

    // unknown tag
    default:
      return false;
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
  Dwarf_Die subtype_die_mem, *subtype_die;
  subtype_die = dwarf_attr_die(type_die, DW_AT_type, &subtype_die_mem);

  // NB: va_list is a builtin type that shows up in the debuginfo as a
  // "struct __va_list_tag*", but it has to be called only va_list.
  if (subtype_die != NULL &&
      dwarf_tag(type_die) == DW_TAG_pointer_type &&
      dwarf_tag(subtype_die) == DW_TAG_structure_type &&
      strcmp(dwarf_diename(subtype_die) ?: "", "__va_list_tag") == 0)
    {
      o << "va_list";
      return true;
    }

  // if it can't be named, just call it "void"
  if (subtype_die == NULL ||
      !dwarf_type_name(subtype_die, o))
    o << "void";

  switch (dwarf_tag(type_die))
    {
    case DW_TAG_reference_type:
      o << "&";
      break;
    case DW_TAG_rvalue_reference_type:
      o << "&&";
      break;
    case DW_TAG_pointer_type:
      o << "*";
      break;
    case DW_TAG_array_type:
      o << "[]";
      break;
    case DW_TAG_const_type:
      // NB: the debuginfo may sometimes have an extra const tag
      // on reference types, which is redundant to us.
      if (subtype_die == NULL ||
          (dwarf_tag(subtype_die) != DW_TAG_reference_type &&
           dwarf_tag(subtype_die) != DW_TAG_rvalue_reference_type))
        o << " const";
      break;
    case DW_TAG_volatile_type:
      o << " volatile";
      break;
    default:
      return false;
    }

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
