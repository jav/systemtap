// -*- C++ -*-
// Copyright (C) 2010 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
#ifndef CSCLIENT_H
#define CSCLIENT_H

// Information about compile servers.
struct compile_server_info
{
  std::string host_name;
  std::string ip_address;
  unsigned short port;
  std::string sysinfo;
};

enum compile_server_properties {
  compile_server_trusted    = 0x1,
  compile_server_online     = 0x2,
  compile_server_compatible = 0x4,
  compile_server_signer     = 0x8,
  compile_server_specified  = 0x10
};

class compile_server_client
{
public:
  compile_server_client (systemtap_session &s) : s(s) {}
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
    const std::string &path,
    const char *option = 0
  );
  int add_package_args ();
  int add_package_arg (const std::string &arg);
  int compile_using_server (const compile_server_info &server);

  int read_from_file (const std::string &fname, int &data);
  int write_to_file (const std::string &fname, const std::string &data);
  int flush_to_stream (const std::string &fname, std::ostream &o);

  systemtap_session &s;
  std::vector<std::string> private_ssl_dbs;
  std::vector<std::string> public_ssl_dbs;
  std::string client_tmpdir;
  std::string client_zipfile;
  std::string server_tmpdir;
  std::string server_zipfile;
  unsigned argc;
};

// Utility functions
void query_server_status (const systemtap_session &s);
void query_server_status (const systemtap_session &s, const std::string &status_string);
void get_server_info (const systemtap_session &s, int pmask, std::vector<compile_server_info> &servers);
void get_online_server_info (std::vector<compile_server_info> &servers);
void keep_compatible_server_info (const systemtap_session &s, std::vector<compile_server_info> &servers);

std::ostream &operator<< (std::ostream &s, const compile_server_info &i);

#endif // CSCLIENT_H
