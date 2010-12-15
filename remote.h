// systemtap remote execution
// Copyright (C) 2010 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef REMOTE_H
#define REMOTE_H

#include <string>

#include "session.h"

class remote {
  protected:
    systemtap_session* s;

    remote(systemtap_session& s): s(&s) {}

  public:
    static remote* create(systemtap_session& s, const std::string& uri);

    systemtap_session* get_session() { return s; }

    virtual ~remote() {}
    virtual int start() = 0;
    virtual int finish() = 0;
};

#endif // REMOTE_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
