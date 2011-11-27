// systemtap remote execution
// Copyright (C) 2010-2011 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef REMOTE_H
#define REMOTE_H

#include <string>
#include <vector>

extern "C" {
#include <poll.h>
}

#include "session.h"

class remote {
  private:
    virtual int prepare() { return 0; }
    virtual int start() = 0;
    virtual int finish() = 0;

    virtual void prepare_poll(std::vector<pollfd>&) {}
    virtual void handle_poll(std::vector<pollfd>&) {}

  protected:
    systemtap_session* s;
    std::string prefix; // stap --remote-prefix
    std::string staprun_r_arg; // PR13354 data: remote_uri()/remote_idx()

    remote(systemtap_session& s): s(&s) {}

  public:
    static remote* create(systemtap_session& s, const std::string& uri, int idx);
    static int run(const std::vector<remote*>& remotes);

    systemtap_session* get_session() { return s; }

    virtual ~remote() {}
};

#endif // REMOTE_H

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
