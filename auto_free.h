// -*- C++ -*-
// Copyright (C) 2008 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef AUTO_FREE_H
#define AUTO_FREE_H 1
#include <cstdlib>

// Very simple auto_ptr-like class for protecting storage allocated
// with free().
class auto_free
{
public:
  auto_free(void* ptr) : _ptr(ptr) {}
  ~auto_free()
  {
    if (_ptr)
      std::free(_ptr);
  }
  void release()
  {
    _ptr = 0;
  }
private:
  // No copying allowed.
  auto_free(const auto_free& af);
  // No assignment either
  auto_free& operator=(const auto_free& rhs);
  void* _ptr;
};

// Use this to free a pointer whose value may change after the initial
// allocation e.g., be realloced.
template <typename T>
class auto_free_ref
{
public:
  typedef T pointer_type;
  auto_free_ref(pointer_type& ptr) : _ptr(ptr)
  {
  }
  ~auto_free_ref()
  {
    if (_ptr)
      std::free(_ptr);
  }
private:
  // No copying allowed.
  auto_free_ref(const auto_free_ref& af);
  // No assignment either
  auto_free_ref& operator=(const auto_free_ref& rhs);
  pointer_type& _ptr;
};
#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
