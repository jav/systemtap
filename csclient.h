// -*- C++ -*-
// Copyright (C) 2010-2011 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
#ifndef CSCLIENT_H
#define CSCLIENT_H

#if HAVE_NSS
#include "cscommon.h"

struct compile_server_info;

class compile_server_client
{
public:
  compile_server_client (systemtap_session &s) : s(s), argc(0), server_version() {}
  int passes_0_4 ();

private:
  // Client/server session methods.
  int initialize ();
  int create_request ();
  int package_request ();
  int find_and_connect_to_server ();
  int unpack_response ();
  int process_response ();

  // Client/server utility methods.
  int include_file_or_directory (
    const std::string &subdir,
    const std::string &path
  );
  int add_package_args ();
  int add_package_arg (const std::string &arg);
  int compile_using_server (const std::vector<compile_server_info> &servers);
  int add_localization_variables();

  int read_from_file (const std::string &fname, int &data);
  template <class T>
  int write_to_file (const std::string &fname, const T &data);
  int flush_to_stream (const std::string &fname, std::ostream &o);

  void show_server_compatibility () const;

  systemtap_session &s;
  std::vector<std::string> private_ssl_dbs;
  std::vector<std::string> public_ssl_dbs;
  std::string client_tmpdir;
  std::string client_zipfile;
  std::string server_tmpdir;
  std::string server_zipfile;
  unsigned argc;
  cs_protocol_version server_version;
};

// Utility functions
void query_server_status (systemtap_session &s);
void manage_server_trust (systemtap_session &s);
#endif // HAVE_NSS

#endif // CSCLIENT_H
