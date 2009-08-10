// -*- C++ -*-
// Copyright (C) 2008 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef DWARF_WRAPPERS_H
#define DWARF_WRAPPERS_H 1

#include "config.h"

extern "C" {
#include <elfutils/libdw.h>
#ifdef HAVE_ELFUTILS_VERSION_H
#include <elfutils/version.h>
#endif
}

#include <string>

#if !defined(_ELFUTILS_PREREQ)
// make a dummy PREREQ check for elfutils < 0.138
#define _ELFUTILS_PREREQ(major, minor) (0 >= 1)
#endif

// NB: "rc == 0" means OK in this case
void dwfl_assert(const std::string& desc, int rc);

// Throw error if pointer is NULL.
template <typename T>
void dwfl_assert(const std::string& desc, T* ptr)
{
  if (!ptr)
    dwfl_assert(desc, -1);
}

// Throw error if pointer is NULL
template <typename T>
void dwfl_assert(const std::string& desc, const T* ptr)
{
  if (!ptr)
    dwfl_assert(desc, -1);
}

// Throw error if condition is false
void dwfl_assert(const std::string& desc, bool condition);

// NB: "rc == 0" means OK in this case
void dwarf_assert(const std::string& desc, int rc);

// Throw error if pointer is NULL
template <typename T>
void dwarf_assert(const std::string& desc, T* ptr)
{
  if (!ptr)
    dwarf_assert(desc, -1);
}


class dwarf_line_t
{
public:
  const Dwarf_Line* line;
  dwarf_line_t() : line(0) {}
  dwarf_line_t(const Dwarf_Line* line_) : line(line_) {}

  dwarf_line_t& operator= (const Dwarf_Line* line_)
  {
    line = (line_);
    return *this;
  }

  operator bool() const
  {
    return line != 0;
  }

  int lineno() const
  {
    int lineval;
    if (!line)
      dwarf_assert("dwarf_line_t::lineno", -1);
    dwarf_lineno(const_cast<Dwarf_Line*>(line), &lineval);
    return lineval;
  }
  Dwarf_Addr addr() const
  {
    Dwarf_Addr addrval;
    if (!line)
      dwarf_assert("dwarf_line_t::addr", -1);
    dwarf_lineaddr(const_cast<Dwarf_Line*>(line), &addrval);
    return addrval;
  }
  const char* linesrc(Dwarf_Word* mtime = 0, Dwarf_Word* length = 0)
  {
    const char* retval = dwarf_linesrc(const_cast<Dwarf_Line*>(line), mtime,
                                                               length);
    dwarf_assert("dwarf_line_t::linesrc", retval);
    return retval;
  }
};


#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
