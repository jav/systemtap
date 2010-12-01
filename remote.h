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
    std::string arch, release;
    remote() {}

  public:
    static remote* create(systemtap_session& s, const std::string& uri);

    const std::string& get_arch() { return arch; }
    const std::string& get_release() { return release; }

    virtual ~remote() {}
    virtual int run(systemtap_session& s) = 0;
};

#endif // REMOTE_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */

