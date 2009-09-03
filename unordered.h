// backward-compatible unordered containers
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef UNORDERED_H
#define UNORDERED_H

#include "config.h"

#if 0 // uncomment to force the old mode
#undef HAVE_TR1_UNORDERED_MAP
#define _BACKWARD_BACKWARD_WARNING_H 1 // defeat deprecation warning
#endif

#ifdef HAVE_TR1_UNORDERED_MAP

#include <tr1/unordered_map>
using std::tr1::unordered_map;
using std::tr1::unordered_multimap;

#include <tr1/unordered_set>
using std::tr1::unordered_set;
using std::tr1::unordered_multiset;

#else

#include <ext/hash_map>
#define unordered_map __gnu_cxx::hash_map
#define unordered_multimap __gnu_cxx::hash_multimap

#include <ext/hash_set>
#define unordered_set __gnu_cxx::hash_set
#define unordered_multiset __gnu_cxx::hash_multiset

// Hack in common hash functions for strings and raw pointers
namespace __gnu_cxx
{
  template<class T> struct hash<T*> {
    size_t operator() (T* p) const
    { hash<long> h; return h(reinterpret_cast<long>(p)); }
  };
  template<> struct hash<std::string> {
    size_t operator() (std::string const& s) const
    { hash<const char*> h; return h(s.c_str()); }
  };
}

#endif

#endif // UNORDERED_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
