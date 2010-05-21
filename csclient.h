// -*- C++ -*-
// Copyright (C) 2010 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
#ifndef CSCLIENT_H
#define CSCLIENT_H

struct systemtap_session;

class compile_server_client
{
public:
  compile_server_client (systemtap_session &s) : s(s) {}
  int passes_0_4 ();

private:
  systemtap_session &s;
};

#endif // CSCLIENT_H
