// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SYSTEMTAP_GRAPHER_TIME_HXX
#define SYSTEMTAP_GRAPHER_TIME_HXX 1

#include <stdint.h>
#include <sys/time.h>

namespace systemtap
{

template<typename T>
class Singleton
{
public:
  static T& instance()
  {
    static T _instance;
    return _instance;
  }
protected:
  Singleton() {}
private:
  // Insure that singleton is constructed before main() is called.
  struct InstanceBuilder
  {
    InstanceBuilder()
    {
      instance();
    }
  };
  static InstanceBuilder _instanceBuilder;
};

template<typename T>
typename Singleton<T>::InstanceBuilder Singleton<T>::_instanceBuilder;

class Time : public Singleton<Time>
{
public:
  Time()
    : origin(0)
  {
    origin = getTime();
  }

  int64_t getTime()
  {
    timeval tval;
    gettimeofday(&tval, 0);
    int64_t now = toUs(tval);
    return now - origin;
  }

  static int64_t get()
  {
    return instance().getTime();
  }

  static int64_t getAbs()
  {
    timeval tval;
    gettimeofday(&tval, 0);
    return toUs(tval);
  }

  static int64_t toUs(const timeval& t)
  {
    int64_t result = t.tv_sec * 1000000;
    result += t.tv_usec;
    return result;
  }

  int64_t origin;
};
}
#endif
