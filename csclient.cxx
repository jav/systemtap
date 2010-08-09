/*
 Compile server client functions
 Copyright (C) 2010 Red Hat Inc.

 This file is part of systemtap, and is free software.  You can
 redistribute it and/or modify it under the terms of the GNU General
 Public License (GPL); either version 2, or (at your option) any
 later version.
*/
#include "config.h"
#include "session.h"
#include "csclient.h"
#include "util.h"
#include "sys/sdt.h"

#include <sys/times.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <cassert>

extern "C" {
#include <linux/limits.h>
#include <sys/time.h>
#include <glob.h>
#include <limits.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pwd.h>
}

#if HAVE_AVAHI
extern "C" {
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>
}
#endif

#if HAVE_NSS
extern "C" {
#include <ssl.h>
#include <nspr.h>
#include <nss.h>
#include <pk11pub.h>
#include <prerror.h>
#include <secerr.h>
#include <sslerr.h>

#include "nsscommon.h"
}

static const char *server_cert_nickname = "stap-server";
#endif

#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <algorithm>

using namespace std;

int
compile_server_client::passes_0_4 ()
{
  STAP_PROBE1(stap, client__start, &s);

#if ! HAVE_NSS
  // This code will never be called, if we don't have NSS, but it must still
  // compile.
  int rc = 1; // Failure
#else
  // arguments parsed; get down to business
  if (s.verbose > 1)
    clog << "Using a compile server" << endl;

  struct tms tms_before;
  times (& tms_before);
  struct timeval tv_before;
  gettimeofday (&tv_before, NULL);

  // Create the request package.
  int rc = initialize ();
  if (rc != 0 || pending_interrupts) goto done;
  rc = create_request ();
  if (rc != 0 || pending_interrupts) goto done;
  rc = package_request ();
  if (rc != 0 || pending_interrupts) goto done;

  // Submit it to the server.
  rc = find_and_connect_to_server ();
  if (rc != 0 || pending_interrupts) goto done;

  // Unpack and process the response.
  rc = unpack_response ();
  if (rc != 0 || pending_interrupts) goto done;
  rc = process_response ();

  if (rc == 0 && s.last_pass == 4)
    {
      cout << s.module_name + ".ko";
      cout << endl;
    }

 done:
  struct tms tms_after;
  times (& tms_after);
  unsigned _sc_clk_tck = sysconf (_SC_CLK_TCK);
  struct timeval tv_after;
  gettimeofday (&tv_after, NULL);

#define TIMESPRINT "in " << \
           (tms_after.tms_cutime + tms_after.tms_utime \
            - tms_before.tms_cutime - tms_before.tms_utime) * 1000 / (_sc_clk_tck) << "usr/" \
        << (tms_after.tms_cstime + tms_after.tms_stime \
            - tms_before.tms_cstime - tms_before.tms_stime) * 1000 / (_sc_clk_tck) << "sys/" \
        << ((tv_after.tv_sec - tv_before.tv_sec) * 1000 + \
            ((long)tv_after.tv_usec - (long)tv_before.tv_usec) / 1000) << "real ms."

  // syntax errors, if any, are already printed
  if (s.verbose)
    {
      clog << "Passes: via server " << s.winning_server << " "
           << getmemusage()
           << TIMESPRINT
           << endl;
    }
#endif // HAVE_NSS

  if (rc == 0)
    {
      // Save the module, if necessary.
      if (s.last_pass == 4)
	s.save_module = true;

      // Copy module to the current directory.
      if (s.save_module && ! pending_interrupts)
	{
	  string module_src_path = s.tmpdir + "/" + s.module_name + ".ko";
	  string module_dest_path = s.module_name + ".ko";
	  copy_file (module_src_path, module_dest_path, s.verbose > 1);
	}
    }

  STAP_PROBE1(stap, client__end, &s);

  return rc;
}

// Initialize a client/server session.
int
compile_server_client::initialize ()
{
  int rc = 0;

  // Initialize session state
  argc = 0;

  // Default location for server certificates if we're not root.
  uid_t euid = geteuid ();
  if (euid != 0)
    {
      private_ssl_dbs.push_back (s.data_path + "/ssl/client");
    }

  // Additional location for all users.
  public_ssl_dbs.push_back (SYSCONFDIR "/systemtap/ssl/client");

  // Create a temporary directory to package things in.
  client_tmpdir = s.tmpdir + "/client";
  rc = create_dir (client_tmpdir.c_str ());
  if (rc != 0)
    {
      const char* e = strerror (errno);
      cerr << "ERROR: cannot create temporary directory (\""
	   << client_tmpdir << "\"): " << e
	   << endl;
    }

  return rc;
}

// Create the request package.
int
compile_server_client::create_request ()
{
  int rc = 0;

  // Add the script file or script option
  if (s.script_file != "")
    {
      if (s.script_file == "-")
	{
	  // Copy the script from stdin
	  string packaged_script_dir = client_tmpdir + "/script";
	  rc = create_dir (packaged_script_dir.c_str ());
	  if (rc != 0)
	    {
	      const char* e = strerror (errno);
	      cerr << "ERROR: cannot create temporary directory "
		   << packaged_script_dir << ": " << e
		   << endl;
	      return rc;
	    }
	  rc = ! copy_file("/dev/stdin", packaged_script_dir + "/-");
	  if (rc != 0)
	    return rc;

	  // Name the script in the packaged arguments.
	  rc = add_package_arg ("script/-");
	  if (rc != 0)
	    return rc;
	}
      else
	{
	  // Add the script to our package. This will also name the script
	  // in the packaged arguments.
	  rc = include_file_or_directory ("script", s.script_file);
	  if (rc != 0)
	    return rc;
	}
    }

  // Add -I paths. Skip the default directory.
  if (s.include_arg_start != -1)
    {
      unsigned limit = s.include_path.size ();
      for (unsigned i = s.include_arg_start; i < limit; ++i)
	{
	  rc = add_package_arg ("-I");
	  if (rc != 0)
	    return rc;
	  rc = include_file_or_directory ("tapset", s.include_path[i]);
	  if (rc != 0)
	    return rc;
	}
    }

  // Add other options.
  rc = add_package_args ();
  if (rc != 0)
    return rc;

  // Add the sysinfo file
  string sysinfo = "sysinfo: " + s.kernel_release + " " + s.architecture;
  rc = write_to_file (client_tmpdir + "/sysinfo", sysinfo);

  return rc;
}

// Add the arguments specified on the command line to the server request
// package, as appropriate.
int
compile_server_client::add_package_args ()
{
  // stap arguments to be passed to the server.
  int rc = 0;
  unsigned limit = s.server_args.size();
  for (unsigned i = 0; i < limit; ++i)
    {
      rc = add_package_arg (s.server_args[i]);
      if (rc != 0)
	return rc;
    }

  // Script arguments.
  limit = s.args.size();
  if (limit > 0) {
    rc = add_package_arg ("--");
    if (rc != 0)
      return rc;
    for (unsigned i = 0; i < limit; ++i)
      {
	rc = add_package_arg (s.args[i]);
	if (rc != 0)
	  return rc;
      }
  }
  return rc;
}  

int
compile_server_client::add_package_arg (const string &arg)
{
  int rc = 0;
  ostringstream fname;
  fname << client_tmpdir << "/argv" << ++argc;
  write_to_file (fname.str (), arg); // NB: No terminating newline
  return rc;
}

// Symbolically link the given file or directory into the client's temp
// directory under the given subdirectory.
int
compile_server_client::include_file_or_directory (
  const string &subdir, const string &path, const char *option
)
{
  // Must predeclare these because we do use 'goto done' to
  // exit from error situations.
  vector<string> components;
  string name;
  int rc;

  // Canonicalize the given path and remove the leading /.
  string rpath;
  char *cpath = canonicalize_file_name (path.c_str ());
  if (! cpath)
    {
      // It can not be canonicalized. Use the name relative to
      // the current working directory and let the server deal with it.
      char cwd[PATH_MAX];
      if (getcwd (cwd, sizeof (cwd)) == NULL)
	{
	  rpath = path;
	  rc = 1;
	  goto done;
	}
	rpath = string (cwd) + "/" + path;
    }
  else
    {
      // It can be canonicalized. Use the canonicalized name and add this
      // file or directory to the request package.
      rpath = cpath;
      free (cpath);

      // First create the requested subdirectory.
      name = client_tmpdir + "/" + subdir;
      rc = create_dir (name.c_str ());
      if (rc) goto done;

      // Now create each component of the path within the sub directory.
      assert (rpath[0] == '/');
      tokenize (rpath.substr (1), components, "/");
      assert (components.size () >= 1);
      unsigned i;
      for (i = 0; i < components.size() - 1; ++i)
	{
	  if (components[i].empty ())
	    continue; // embedded '//'
	  name += "/" + components[i];
	  rc = create_dir (name.c_str ());
	  if (rc) goto done;
	}

      // Now make a symbolic link to the actual file or directory.
      assert (i == components.size () - 1);
      name += "/" + components[i];
      rc = symlink (rpath.c_str (), name.c_str ());
      if (rc) goto done;
    }

  // Name this file or directory in the packaged arguments along with any
  // associated option.
  if (option)
    {
      rc = add_package_arg (option);
      if (rc) goto done;
    }

  rc = add_package_arg (subdir + "/" + rpath.substr (1));

 done:
  if (rc != 0)
    {
      const char* e = strerror (errno);
      cerr << "ERROR: unable to add "
	   << rpath
	   << " to temp directory as "
	   << name << ": " << e
	   << endl;
    }
  return rc;
}

// Package the client's temp directory into a form suitable for sending to the
// server.
int
compile_server_client::package_request ()
{
  // Package up the temporary directory into a zip file.
  client_zipfile = client_tmpdir + ".zip";
  string cmd = "cd " + client_tmpdir + " && zip -qr " + client_zipfile + " *";
  int rc = stap_system (s.verbose, cmd);
  return rc;
}

int
compile_server_client::find_and_connect_to_server ()
{
  // Accumulate info on the specified servers.
  vector<compile_server_info> server_list;
  get_specified_server_info (s, server_list);

  // Did we identify any potential servers?
  unsigned limit = server_list.size ();
  if (limit == 0)
    {
      cerr << "Unable to find a server" << endl;
      return 1;
    }

  // Now try each of the identified servers in turn.
  for (unsigned i = 0; i < limit; ++i)
    {
      int rc = compile_using_server (server_list[i]);
      if (rc == 0)
	{
	  s.winning_server =
	    server_list[i].host_name + string(" [") +
	    server_list[i].ip_address + string(":") +
	    lex_cast(server_list[i].port) + string("]");
	  return 0; // success!
	}
    }

  cerr << "Unable to connect to a server" << endl;
  return 1; // Failure
}

// Temporary until the stap-client-connect program goes away.
extern "C"
int
client_main (const char *hostName, unsigned short port,
	     const char* infileName, const char* outfileName);

int 
compile_server_client::compile_using_server (const compile_server_info &server)
{
  // This code will never be called if we don't have NSS, but it must still
  // compile.
#if HAVE_NSS
  // We cannot contact the server if we don't have the port number.
  if (server.port == 0)
    return 1; // Failure

  // Attempt connection using each of the available client certificate
  // databases.
  vector<string> dbs = private_ssl_dbs;
  vector<string>::iterator i = dbs.end();
  dbs.insert (i, public_ssl_dbs.begin (), public_ssl_dbs.end ());
  for (i = dbs.begin (); i != dbs.end (); ++i)
    {
      const char *cert_dir = i->c_str ();

      if (s.verbose > 1)
	clog << "Attempting SSL connection with " << server << endl
	     << "  using certificates from the database in " << cert_dir
	     << endl;
      // Call the NSPR initialization routines.
      PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

#if 0 // no client authentication for now.
      // Set our password function callback.
      PK11_SetPasswordFunc (myPasswd);
#endif

      // Initialize the NSS libraries.
      SECStatus secStatus = NSS_InitReadWrite (cert_dir);
      if (secStatus != SECSuccess)
	{
	  // Try it again, readonly.
	  secStatus = NSS_Init(cert_dir);
	  if (secStatus != SECSuccess)
	    {
	      cerr << "Error initializing NSS" << endl;
	      goto error;
	    }
	}

      // All cipher suites except RSA_NULL_MD5 are enabled by Domestic Policy.
      NSS_SetDomesticPolicy ();

      server_zipfile = s.tmpdir + "/server.zip";
      int rc = client_main (server.host_name.c_str (), server.port,
			    client_zipfile.c_str(), server_zipfile.c_str ());

      NSS_Shutdown();
      PR_Cleanup();

      if (rc == 0)
	return rc; // Success!
    }

  return 1; // Failure

 error:
  nssError ();
  NSS_Shutdown();
  PR_Cleanup();
#endif // HAVE_NSS

  return 1; // Failure
}

int
compile_server_client::unpack_response ()
{
  // Unzip the response package.
  server_tmpdir = s.tmpdir + "/server";
  string cmd = "unzip -qd " + server_tmpdir + " " + server_zipfile;
  int rc = stap_system (s.verbose, cmd);
  if (rc != 0)
    {
      cerr << "Unable to unzip the server reponse '" << server_zipfile << '\''
	   << endl;
    }

  // If the server's response contains a systemtap temp directory, move
  // its contents to our temp directory.
  glob_t globbuf;
  string filespec = server_tmpdir + "/stap??????";
  if (s.verbose > 2)
    clog << "Searching \"" << filespec << "\"" << endl;
  int r = glob(filespec.c_str (), 0, NULL, & globbuf);
  if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
    {
      if (globbuf.gl_pathc > 1)
	{
	  cerr << "Incorrect number of files in server response" << endl;
	  rc = 1; goto done;
	}

      assert (globbuf.gl_pathc == 1);
      string dirname = globbuf.gl_pathv[0];
      if (s.verbose > 2)
	clog << "  found " << dirname << endl;

      filespec = dirname + "/*";
      if (s.verbose > 2)
	clog << "Searching \"" << filespec << "\"" << endl;
      int r = glob(filespec.c_str (), GLOB_PERIOD, NULL, & globbuf);
      if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
	{
	  unsigned prefix_len = dirname.size () + 1;
	  for (unsigned i = 0; i < globbuf.gl_pathc; ++i)
	    {
	      string oldname = globbuf.gl_pathv[i];
	      if (oldname.substr (oldname.size () - 2) == "/." ||
		  oldname.substr (oldname.size () - 3) == "/..")
		continue;
	      string newname = s.tmpdir + "/" + oldname.substr (prefix_len);
	      if (s.verbose > 2)
		clog << "  found " << oldname
		     << " -- linking from " << newname << endl;
	      rc = symlink (oldname.c_str (), newname.c_str ());
	      if (rc != 0)
		{
		  cerr << "Unable to link '" << oldname
		       << "' to '" << newname << "': "
		       << strerror (errno) << endl;
		  goto done;
		}
	    }
	}
    }

  // Remove the output line due to the synthetic server-side -k
  cmd = "sed -i '/^Keeping temporary directory.*/ d' " +
    server_tmpdir + "/stderr";
  stap_system (s.verbose, cmd);

  // Remove the output line due to the synthetic server-side -p4
  cmd = "sed -i '/^.*\\.ko$/ d' " + server_tmpdir + "/stdout";
  stap_system (s.verbose, cmd);

 done:
  globfree (& globbuf);
  return rc;
}

int
compile_server_client::process_response ()
{
  // Pick up the results of running stap on the server.
  string filename = server_tmpdir + "/rc";
  int stap_rc;
  int rc = read_from_file (filename, stap_rc);
  if (rc != 0)
    return rc;
  rc = stap_rc;

  if (s.last_pass >= 4)
    {
      // The server should have returned a module.
      string filespec = s.tmpdir + "/*.ko";
      if (s.verbose > 2)
	clog << "Searching \"" << filespec << "\"" << endl;

      glob_t globbuf;
      int r = glob(filespec.c_str (), 0, NULL, & globbuf);
      if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
	{
	  if (globbuf.gl_pathc > 1)
	    cerr << "Incorrect number of modules in server response" << endl;
	  else
	    {
	      assert (globbuf.gl_pathc == 1);
	      string modname = globbuf.gl_pathv[0];
	      if (s.verbose > 2)
		clog << "  found " << modname << endl;

	      // If a module name was not specified by the user, then set it to
	      // be the one generated by the server.
	      if (! s.save_module)
		{
		  vector<string> components;
		  tokenize (modname, components, "/");
		  s.module_name = components.back ();
		  s.module_name.erase(s.module_name.size() - 3);
		}
	    }
	}
      else if (s.have_script)
	{
	  if (rc == 0)
	    {
	      cerr << "No module was returned by the server" << endl;
	      rc = 1;
	    }
	}
      globfree (& globbuf);
    }

  // Output stdout and stderr.
  filename = server_tmpdir + "/stderr";
  flush_to_stream (filename, cerr);

  filename = server_tmpdir + "/stdout";
  flush_to_stream (filename, cout);

  return rc;
}

int
compile_server_client::read_from_file (const string &fname, int &data)
{
  // C++ streams may not set errno in the even of a failure. However if we
  // set it to 0 before each operation and it gets set during the operation,
  // then we can use its value in order to determine what happened.
  errno = 0;
  ifstream f (fname.c_str ());
  if (! f.good ())
    {
      cerr << "Unable to open file '" << fname << "' for reading: ";
      goto error;
    }

  // Read the data;
  errno = 0;
  f >> data;
  if (f.fail ())
    {
      cerr << "Unable to read from file '" << fname << "': ";
      goto error;
    }

  // NB: not necessary to f.close ();
  return 0; // Success

 error:
  if (errno)
    cerr << strerror (errno) << endl;
  else
    cerr << "unknown error" << endl;
  return 1; // Failure
}

int
compile_server_client::write_to_file (const string &fname, const string &data)
{
  // C++ streams may not set errno in the even of a failure. However if we
  // set it to 0 before each operation and it gets set during the operation,
  // then we can use its value in order to determine what happened.
  errno = 0;
  ofstream f (fname.c_str ());
  if (! f.good ())
    {
      cerr << "Unable to open file '" << fname << "' for writing: ";
      goto error;
    }

  // Write the data;
  f << data;
  errno = 0;
  if (f.fail ())
    {
      cerr << "Unable to write to file '" << fname << "': ";
      goto error;
    }

  // NB: not necessary to f.close ();
  return 0; // Success

 error:
  if (errno)
    cerr << strerror (errno) << endl;
  else
    cerr << "unknown error" << endl;
  return 1; // Failure
}

int
compile_server_client::flush_to_stream (const string &fname, ostream &o)
{
  // C++ streams may not set errno in the even of a failure. However if we
  // set it to 0 before each operation and it gets set during the operation,
  // then we can use its value in order to determine what happened.
  errno = 0;
  ifstream f (fname.c_str ());
  if (! f.good ())
    {
      cerr << "Unable to open file '" << fname << "' for reading: ";
      goto error;
    }

  // Stream the data

  // NB: o << f.rdbuf() misbehaves for some reason, appearing to close o,
  // which is unfortunate if o == cerr or cout.
  while (1)
    {
      errno = 0;
      int c = f.get();
      if (f.eof ()) return 0; // normal exit
      if (! f.good()) break;
      o.put(c);
      if (! o.good()) break;
    }

  // NB: not necessary to f.close ();

 error:
  if (errno)
    cerr << strerror (errno) << endl;
  else
    cerr << "unknown error" << endl;
  return 1; // Failure
}

// Utility Functions.
//-----------------------------------------------------------------------
std::ostream &operator<< (std::ostream &s, const compile_server_info &i)
{
  s << i.host_name;
  s << " ip=";
  if (! i.ip_address.empty ())
    s << i.ip_address;
  else
    s << "unknown";
  s << " port=";
  if (i.port != 0)
    s << i.port;
  else
    s << "unknown";
  s << " sysinfo=\"";
  if (! i.sysinfo.empty ())
    s << i.sysinfo << '"';
  else
    s << "unknown\"";
  s << " certinfo=\"";
  if (! i.certinfo.empty ())
    s << i.certinfo << '"';
  else
    s << "unknown\"";
  return s;
}

// Return the default server specification, used when none is given on the
// command line.
static string
default_server_spec (const systemtap_session &s)
{
  // If the --use-server option has been used
  //   the default is 'specified'
  // otherwise if the --unprivileged has been used
  //   the default is online,trusted,compatible,signer
  // otherwise
  //   the default is online,compatible
  //
  // Having said that,
  //   'online' and 'compatible' will only succeed if we have avahi
  //   'trusted' and 'signer' will only succeed if we have NSS
  //
  string working_string = "online,trusted,compatible";
  if (s.unprivileged)
    working_string += ",signer";
  return working_string;
}

static int
server_spec_to_pmask (const string &server_spec)
{
  // Construct a mask of the server properties that have been requested.
  // The available properties are:
  //     trusted    - servers which are trusted SSL peers.
  //	 online     - online servers.
  //     compatible - servers which compile for the current kernel release
  //	 	      and architecture.
  //     signer     - servers which are trusted module signers.
  //	 specified  - servers which have been specified using --use-server=XXX.
  //	 	      If no servers have been specified, then this is
  //		      equivalent to --list-servers=trusted,online,compatible.
  //     all        - all trusted servers, trusted module signers,
  //                  servers currently online and specified servers.
  string working_spec = server_spec;
  vector<string> properties;
  tokenize (working_spec, properties, ",");
  int pmask = 0;
  unsigned limit = properties.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      const string &property = properties[i];
      // Tolerate (and ignore) empty properties.
      if (property.empty ())
	continue;
      if (property == "all")
	{
	  pmask |= compile_server_all;
	}
      else if (property == "specified")
	{
	  pmask |= compile_server_specified;
	}
      else if (property == "trusted")
	{
	  pmask |= compile_server_trusted;
	}
      else if (property == "online")
	{
	  pmask |= compile_server_online;
	}
      else if (property == "compatible")
	{
	  pmask |= compile_server_compatible;
	}
      else if (property == "signer")
	{
	  pmask |= compile_server_signer;
	}
      else
	{
	  cerr << "Warning: unsupported compile server property: " << property
	       << endl;
	}
    }
  return pmask;
}

void
query_server_status (systemtap_session &s)
{
  unsigned limit = s.server_status_strings.size ();
  for (unsigned i = 0; i < limit; ++i)
    query_server_status (s, s.server_status_strings[i]);
}

void
query_server_status (systemtap_session &s, const string &status_string)
{
#if HAVE_NSS || HAVE_AVAHI
  // If this string is empty, then the default is "specified"
  string working_string = status_string;
  if (working_string.empty ())
    working_string = "specified";

  // If the query is "specified" and no servers have been specified
  // (i.e. --use-server not used or used once with no argument), then
  // use the default query.
  if (working_string == "specified" &&
      (s.specified_servers.empty () ||
       (s.specified_servers.size () == 1 && s.specified_servers[0].empty ())))
    working_string = default_server_spec (s);

  int pmask = server_spec_to_pmask (working_string);

  // Now obtain a list of the servers which match the criteria.
  vector<compile_server_info> servers;
  get_server_info (s, pmask, servers);

  // Print the server information
  clog << "Systemtap Compile Server Status for '" << working_string << '\''
       << endl;
  unsigned limit = servers.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      clog << servers[i] << endl;
    }
#endif // HAVE_NSS || HAVE_AVAHI
}

void
get_server_info (
  systemtap_session &s,
  int pmask,
  vector<compile_server_info> &servers
)
{
  // Get information on compile servers matching the requested criteria.
  // The order of queries is significant. Accumulating queries must go first
  // followed by accumulating/filering queries followed by filtering queries.
  // We start with an empty vector.
  // These queries accumulate server information.
  vector<compile_server_info> temp;
  if ((pmask & compile_server_all))
    {
      get_all_server_info (s, temp);
    }
  if ((pmask & compile_server_specified))
    {
      get_specified_server_info (s, temp);
    }
  // These queries filter server information if the vector is not empty and
  // accumulate it otherwise.
  if ((pmask & compile_server_online))
    {
      get_or_keep_online_server_info (s, temp);
    }
  if ((pmask & compile_server_trusted))
    {
      get_or_keep_trusted_server_info (s, temp);
    }
  if ((pmask & compile_server_signer))
    {
      get_or_keep_signing_server_info (s, temp);
    }
  // This query filters server information.
  if ((pmask & compile_server_compatible))
    {
      keep_compatible_server_info (s, temp);
    }

  // Now add the collected information to the target vector.
  merge_server_info (temp, servers);
}

// Get information about all online servers as well as servers trusted
// as SSL peers and servers trusted as signers.
void
get_all_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers
)
{
  // The get_or_keep_XXXX_server_info functions filter the vector
  // if it is not empty. So use an empty vector for each query.
  vector<compile_server_info> temp;
  get_or_keep_online_server_info (s, temp);
  merge_server_info (temp, servers);

  temp.clear ();
  get_or_keep_trusted_server_info (s, temp);
  merge_server_info (temp, servers);

  temp.clear ();
  get_or_keep_signing_server_info (s, temp);
  merge_server_info (temp, servers);
}

void
get_default_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers
)
{
  // We only need to obtain this once. This is a good thing(tm) since
  // obtaining this information is expensive.
  static vector<compile_server_info> default_servers;
  if (default_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      default_servers.push_back (compile_server_info ());

      // Now get the required information.
      int pmask = server_spec_to_pmask (default_server_spec (s));
      get_server_info (s, pmask, default_servers);
    }

  // Add the information, but not duplicates. Skip the empty first entry.
  unsigned limit = default_servers.size ();
  for (unsigned i = 1; i < limit; ++i)
    add_server_info (default_servers[i], servers);
}

void
get_specified_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers
)
{
  // We only need to obtain this once. This is a good thing(tm) since
  // obtaining this information is expensive.
  static vector<compile_server_info> specified_servers;
  if (specified_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      specified_servers.push_back (compile_server_info ());

      // If --use-servers was not specified at all, then return info for the
      // default server list.
      if (s.specified_servers.empty ())
	{
	  get_default_server_info (s, specified_servers);
	}
      else
	{
	  // Iterate over the specified servers. For each specification, add to
	  // the list of servers.
	  unsigned num_specified_servers = s.specified_servers.size ();
	  for (unsigned i = 0; i < num_specified_servers; ++i)
	    {
	      const string &server = s.specified_servers[i];
	      if (server.empty ())
		{
		  // No server specified. Use the default servers.
		  get_default_server_info (s, specified_servers);
		}
	      else
		{
		  // Work with the specified server
		  compile_server_info server_info;

		  // See if a port was specified (:n suffix)
		  vector<string> components;
		  tokenize (server, components, ":");
		  if (components.size () > 2)
		    {
		      cerr << "Invalid server specification: " << server
			   << endl;
		      continue;
		    }
		  if (components.size () == 2)
		    {
		      // Obtain the port number.
		      const char *pstr = components.back ().c_str ();
		      char *estr;
		      errno = 0;
		      unsigned long port = strtoul (pstr, & estr, 10);
		      if (errno == 0 && *estr == '\0' && port <= USHRT_MAX)
			server_info.port = port;
		      else
			{
			  cerr << "Invalid port number specified: "
			       << components.back ()
			       << endl;
			  continue;
			}
		    }

		  // Obtain the host name.
		  server_info.host_name = components.front ();

		  // Was a port specified?
		  if (server_info.port != 0)
		    {
		      // A specific server was specified.
		      // Resolve the server. It's not an error if it fails.
		      // Just less info gathered.
		      resolve_server (s, server_info);
		      add_server_info (server_info, specified_servers);
		    }
		  else
		    {
		      // No port was specified, so find all known servers
		      // on the specified host.
		      resolve_host_name (s, server_info.host_name);
		      vector<compile_server_info> all_servers;
		      get_all_server_info (s, all_servers);

		      // Search the list of online servers for ones matching the
		      // one specified and obtain the port numbers.
		      unsigned found = 0;
		      unsigned limit = all_servers.size ();
		      for (unsigned j = 0; j < limit; ++j)
			{
			  if (server_info == all_servers[j])
			    {
			      add_server_info (all_servers[j], specified_servers);
			      ++found;
			    }
			}
		      // Do we have a port number now?
		      if (s.verbose && found == 0)
			cerr << "No server matching " << s.specified_servers[i]
			     << " found" << endl;
		    } // No port specified
		} // Specified server.
	    } // Loop over --use-server options
	} // -- use-server specified
    } // Server information is not cached

  // Add the information, but not duplicates. Skip the empty first entry.
  unsigned limit = specified_servers.size ();
  for (unsigned i = 1; i < limit; ++i)
    add_server_info (specified_servers[i], servers);
}

void
get_or_keep_trusted_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers
)
{
  // We only need to obtain this once. This is a good thing(tm) since
  // obtaining this information is expensive.
  static vector<compile_server_info> trusted_servers;
  if (trusted_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      trusted_servers.push_back (compile_server_info ());

#if HAVE_NSS
      // Call the NSPR initialization routines
      PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

      // For non-root users, check the private database first.
      string cert_db_path;
      if (geteuid () != 0)
	{
	  cert_db_path = s.data_path + "/ssl/client";
	  get_server_info_from_db (s, trusted_servers, cert_db_path);
	}

      // For all users, check the global database.
      cert_db_path = SYSCONFDIR "/systemtap/ssl/client";
      get_server_info_from_db (s, trusted_servers, cert_db_path);

      // Terminate NSPR.
      PR_Cleanup ();
#else // ! HAVE_NSS
      // Without NSS, we can't determine whether a server is trusted.
      // Issue a warning.
      if (s.verbose)
	clog << "Unable to determine server trust as an SSL peer" << endl;
#endif // ! HAVE_NSS
    } // Server information is not cached

  if (! servers.empty ())
    {
      // Filter the existing vector by keeping the information in common with
      // the trusted_server vector.
      keep_common_server_info (trusted_servers, servers);
    }
  else
    {
      // Add the information, but not duplicates. Skip the empty first entry.
      unsigned limit = trusted_servers.size ();
      for (unsigned i = 1; i < limit; ++i)
	add_server_info (trusted_servers[i], servers);
    }
}

void
get_or_keep_signing_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers
)
{
  // We only need to obtain this once. This is a good thing(tm) since
  // obtaining this information is expensive.
  static vector<compile_server_info> signing_servers;
  if (signing_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      signing_servers.push_back (compile_server_info ());

#if HAVE_NSS
      // Call the NSPR initialization routines
      PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

      // For all users, check the global database.
      string cert_db_path = SYSCONFDIR "/systemtap/staprun";
      get_server_info_from_db (s, signing_servers, cert_db_path);

      // Terminate NSPR.
      PR_Cleanup ();
#else // ! HAVE_NSS
      // Without NSS, we can't determine whether a server is a trusted
      // signer. Issue a warning.
      if (s.verbose)
	clog << "Unable to determine server trust as a module signer" << endl;
#endif // ! HAVE_NSS
    } // Server information is not cached

  if (! servers.empty ())
    {
      // Filter the existing vector by keeping the information in common with
      // the signing_server vector.
      keep_common_server_info (signing_servers, servers);
    }
  else
    {
      // Add the information, but not duplicates. Skip the empty first entry.
      unsigned limit = signing_servers.size ();
      for (unsigned i = 1; i < limit; ++i)
	add_server_info (signing_servers[i], servers);
    }
}

// Obtain information about servers from the certificates in the given database.
void
get_server_info_from_db (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  const string &cert_db_path
)
{
#if HAVE_NSS
  // Make sure the given path exists.
  if (! file_exists (cert_db_path))
    {
      if (s.verbose > 1)
	cerr << "Certificate database '" << cert_db_path << "' does not exist."
	     << endl;
      return;
    }

  // Must predeclare these because of jumps to cleanup: below.
  CERTCertList *certs = NULL;
  PK11SlotInfo *slot = NULL;
  PRArenaPool *tmpArena = NULL;
  CERTCertDBHandle *handle;
  CERTCertificate *the_cert;
  vector<compile_server_info> temp_list;

  // Initialize the NSS libraries -- readonly
  SECStatus secStatus = NSS_Init (cert_db_path.c_str ());
  if (secStatus != SECSuccess)
    {
      cerr << "Error initializing NSS for " << cert_db_path << endl;
      nssError ();
      goto cleanup;
    }

  // Search the client-side database of trusted servers.
  slot = PK11_GetInternalKeySlot ();
  handle = CERT_GetDefaultCertDB();
  the_cert = PK11_FindCertFromNickname (server_cert_nickname, NULL);
  if (! the_cert)
    {
      // No trusted servers. Not an error. Just an empty list returned.
      goto cleanup;
    }

  // Here, we have one cert with the desired nickname.
  // Now, we will attempt to get a list of ALL certs 
  // with the same subject name as the cert we have.  That list 
  // should contain, at a minimum, the one cert we have already found.
  // If the list of certs is empty (NULL), the libraries have failed.
  certs = CERT_CreateSubjectCertList (NULL, handle, & the_cert->derSubject,
				      PR_Now (), PR_FALSE);
  CERT_DestroyCertificate (the_cert);
  if (! certs)
    {
      cerr << "Unable to query client certificate database: " << endl;
      PORT_SetError (SEC_ERROR_LIBRARY_FAILURE);
      nssError ();
      goto cleanup;
    }

  // A memory pool to work in
  tmpArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  if (! tmpArena) 
    {
      cerr << "Out of memory:";
      nssError ();
      goto cleanup;
    }
  for (CERTCertListNode *node = CERT_LIST_HEAD (certs);
       ! CERT_LIST_END (node, certs);
       node = CERT_LIST_NEXT (node))
    {
      compile_server_info server_info;

      // The certificate we're working with.
      the_cert = node->cert;

      // Get the host name. It is in the alt-name extension of the
      // certificate.
      SECItem subAltName;
      subAltName.data = NULL;
      secStatus = CERT_FindCertExtension (the_cert,
					  SEC_OID_X509_SUBJECT_ALT_NAME,
					  & subAltName);
      if (secStatus != SECSuccess || ! subAltName.data)
	{
	  cerr << "Unable to find alt name extension on server certificate: " << endl;
	  nssError ();
	  continue;
	}

      // Decode the extension.
      CERTGeneralName *nameList = CERT_DecodeAltNameExtension (tmpArena, & subAltName);
      SECITEM_FreeItem(& subAltName, PR_FALSE);
      if (! nameList)
	{
	  cerr << "Unable to decode alt name extension on server certificate: " << endl;
	  nssError ();
	  continue;
	}

      // We're interested in the first alternate name.
      assert (nameList->type == certDNSName);
      server_info.host_name = string ((const char *)nameList->name.other.data,
				      nameList->name.other.len);
      // Don't free nameList. It's part of the tmpArena.

      // Get the serial number.
      ostringstream field;
      field << hex << setw(2) << setfill('0') << right;
      for (unsigned i = 0; i < the_cert->serialNumber.len; ++i)
	{
	  if (i > 0)
	    field << ':';
	  field << (unsigned)the_cert->serialNumber.data[i];
	}
      server_info.certinfo = field.str ();

      // We can't call resolve_server while NSS is active on a database
      // since it may recursively call this function for another database.
      // So, keep a temporary list of the server info discovered and then
      // make a pass over it later resolving each server.
      add_server_info (server_info, temp_list);
    }

 cleanup:
  if (certs)
    CERT_DestroyCertList (certs);
  if (slot)
    PK11_FreeSlot (slot);
  if (tmpArena)
    PORT_FreeArena (tmpArena, PR_FALSE);

  NSS_Shutdown ();

  // Resolve each server discovered in order to eliminate duplicates when
  // adding them to the target verctor.
  // This must be done after NSS_Shutdown since resolve_server can call this
  // function recursively.
  for (vector<compile_server_info>::iterator i = temp_list.begin ();
       i != temp_list.end ();
       ++i)
    {
      resolve_server (s, *i);
      add_server_info (*i, servers);
    }
#endif // HAVE_NSS
}

void
keep_compatible_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers
)
{
#if HAVE_AVAHI
  // Remove entries for servers incompatible with the host environment
  // from the given list of servers.
  // A compatible server compiles for the kernel release and architecture
  // of the host environment.
  for (unsigned i = 0; i < servers.size (); /**/)
    {
      if (servers[i].sysinfo != s.kernel_release + " " + s.architecture)
	{
	  // Platform mismatch.
	  servers.erase (servers.begin () + i);
	  continue;
	}
  
      // The server is compatible. Leave it in the list.
      ++i;
    }
#else // ! HAVE_AVAHI
  // Without Avahi, we can't obtain the target platform of the server.
  // Issue a warning.
  if (s.verbose)
    clog << "Unable to detect server compatibility" << endl;
  servers.clear ();
#endif
}

// Attempt to obtain complete information about this server from the local
// name service and/or from online server discovery.
int resolve_server (systemtap_session& s, compile_server_info &server_info)
{
  // Attempt to resolve the host name and ip.
  int status = resolve_host_name (s, server_info.host_name,
				  & server_info.ip_address);
  if (status != 0)
    return status; // message already issued.

  // See if we can discover the target of this server.
  if (server_info.sysinfo.empty ())
    {
      vector<compile_server_info> online_servers;
      get_or_keep_online_server_info (s, online_servers);
      merge_server_info (online_servers, server_info);
    }

  // See if we can discover the certificate of this server. Look in
  // the databases of trusted peers and trusted signers.
  if (server_info.certinfo.empty ())
    {
      vector<compile_server_info> trusted_servers;
      get_or_keep_trusted_server_info (s, trusted_servers);
      merge_server_info (trusted_servers, server_info);
    }
  if (server_info.certinfo.empty ())
    {
      vector<compile_server_info> signing_servers;
      get_or_keep_signing_server_info (s, signing_servers);
      merge_server_info (signing_servers, server_info);
    }

  return 0;
}

// Obtain the canonical host name and, if requested, ip address of a host.
int resolve_host_name (
  systemtap_session& s,
  string &host_name,
  string *ip_address
)
{
  struct addrinfo hints;
  memset(& hints, 0, sizeof (hints));
  hints.ai_family = AF_INET; // AF_UNSPEC or AF_INET6 to force version
  struct addrinfo *res;
  int status = getaddrinfo(host_name.c_str(), NULL, & hints, & res);
  if (status != 0)
    goto error;

  // Obtain the ip address and canonical name of the resolved server.
  assert (res);
  for (const struct addrinfo *p = res; p != NULL; p = p->ai_next)
    {
      if (p->ai_family != AF_INET)
	continue; // Not an IPv4 address

      // Get the canonical name.
      char hbuf[NI_MAXHOST];
      status = getnameinfo (p->ai_addr, sizeof (*p->ai_addr),
			    hbuf, sizeof (hbuf), NULL, 0,
			    NI_NAMEREQD | NI_IDN);
      if (status != 0)
	continue;
      host_name = hbuf;

      // Get the ip address, if requested
      if (ip_address)
	{
	  // get the pointer to the address itself,
	  struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
	  void *addr = & ipv4->sin_addr;

	  // convert the IP to a string.
	  char ipstr[INET_ADDRSTRLEN];
	  inet_ntop(p->ai_family, addr, ipstr, sizeof (ipstr));
	  *ip_address = ipstr;
	}

      break; // Use the info from the first IPv4 result.
    }
  freeaddrinfo(res); // free the linked list

  if (status != 0)
    goto error;

  return 0;

 error:
  if (s.verbose > 1)
    {
      clog << "Unable to resolve host name " << host_name
	   << ": " << gai_strerror(status)
	   << endl;
    }
  return status;
}

#if HAVE_AVAHI
// Avahi API Callbacks.
//-----------------------------------------------------------------------
struct browsing_context {
  AvahiSimplePoll *simple_poll;
  AvahiClient *client;
  vector<compile_server_info> *servers;
};

static string
extract_field_from_avahi_txt (const string &label, const string &txt)
{
  // Extract the requested field from the Avahi TXT.
  string prefix = "\"" + label;
  size_t ix = txt.find (prefix);
  if (ix == string::npos)
    {
      // Label not found.
      return "";
    }

  // This is the start of the field.
  string field = txt.substr (ix + prefix.size ());

  // Find the end of the field.
  ix = field.find('"');
  if (ix != string::npos)
    field = field.substr (0, ix);

  return field;
}

extern "C"
void resolve_callback(
    AvahiServiceResolver *r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    AVAHI_GCC_UNUSED void* userdata) {

    assert(r);
    const browsing_context *context = (browsing_context *)userdata;
    vector<compile_server_info> *servers = context->servers;

    // Called whenever a service has been resolved successfully or timed out.

    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
	  cerr << "Failed to resolve service '" << name
	       << "' of type '" << type
	       << "' in domain '" << domain
	       << "': " << avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r)))
	       << endl;
            break;

        case AVAHI_RESOLVER_FOUND: {
            char a[AVAHI_ADDRESS_STR_MAX], *t;
            avahi_address_snprint(a, sizeof(a), address);
            t = avahi_string_list_to_string(txt);

	    // Save the information of interest.
	    compile_server_info info;
	    info.host_name = host_name;
	    info.ip_address = strdup (a);
	    info.port = port;
	    info.sysinfo = extract_field_from_avahi_txt ("sysinfo=", t);
	    info.certinfo = extract_field_from_avahi_txt ("certinfo=", t);

	    // Add this server to the list of discovered servers.
	    add_server_info (info, *servers);

            avahi_free(t);
        }
    }

    avahi_service_resolver_free(r);
}

extern "C"
void browse_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    void* userdata) {
    
    browsing_context *context = (browsing_context *)userdata;
    AvahiClient *c = context->client;
    AvahiSimplePoll *simple_poll = context->simple_poll;
    assert(b);

    // Called whenever a new services becomes available on the LAN or is removed from the LAN.

    switch (event) {
        case AVAHI_BROWSER_FAILURE:
	    cerr << "Avahi browse failed: "
		 << avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b)))
		 << endl;
	    avahi_simple_poll_quit(simple_poll);
	    break;

        case AVAHI_BROWSER_NEW:
	    // We ignore the returned resolver object. In the callback
	    // function we free it. If the server is terminated before
	    // the callback function is called the server will free
	    // the resolver for us.
            if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain,
					     AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0, resolve_callback, context))) {
	      cerr << "Failed to resolve service '" << name
		   << "': " << avahi_strerror(avahi_client_errno(c))
		   << endl;
	    }
            break;

        case AVAHI_BROWSER_REMOVE:
        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            break;
    }
}

extern "C"
void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
    assert(c);
    browsing_context *context = (browsing_context *)userdata;
    AvahiSimplePoll *simple_poll = context->simple_poll;

    // Called whenever the client or server state changes.

    if (state == AVAHI_CLIENT_FAILURE) {
        cerr << "Avahi Server connection failure: "
	     << avahi_strerror(avahi_client_errno(c))
	     << endl;
        avahi_simple_poll_quit(simple_poll);
    }
}

extern "C"
void timeout_callback(AVAHI_GCC_UNUSED AvahiTimeout *e, AVAHI_GCC_UNUSED void *userdata) {
  browsing_context *context = (browsing_context *)userdata;
  AvahiSimplePoll *simple_poll = context->simple_poll;
  avahi_simple_poll_quit(simple_poll);
}
#endif // HAVE_AVAHI

void
get_or_keep_online_server_info (systemtap_session &s,
				vector<compile_server_info> &servers)
{
  // We only need to obtain this once. This is a good thing(tm) since
  // obtaining this information is expensive.
  static vector<compile_server_info> online_servers;
  if (online_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      online_servers.push_back (compile_server_info ());
#if HAVE_AVAHI
      // Must predeclare these due to jumping on error to fail:
      unsigned limit;
      vector<compile_server_info> raw_servers;

      // Initialize.
      AvahiClient *client = NULL;
      AvahiServiceBrowser *sb = NULL;
 
      // Allocate main loop object.
      AvahiSimplePoll *simple_poll;
      if (!(simple_poll = avahi_simple_poll_new()))
	{
	  cerr << "Failed to create Avahi simple poll object" << endl;
	  goto fail;
	}
      browsing_context context;
      context.simple_poll = simple_poll;
      context.servers = & raw_servers;

      // Allocate a new Avahi client
      int error;
      client = avahi_client_new (avahi_simple_poll_get (simple_poll),
				 (AvahiClientFlags)0,
				 client_callback, & context, & error);

      // Check whether creating the client object succeeded.
      if (! client)
	{
	  cerr << "Failed to create Avahi client: "
	       << avahi_strerror(error)
	       << endl;
	  goto fail;
	}
      context.client = client;
    
      // Create the service browser.
      if (!(sb = avahi_service_browser_new (client, AVAHI_IF_UNSPEC,
					    AVAHI_PROTO_UNSPEC, "_stap._tcp",
					    NULL, (AvahiLookupFlags)0,
					    browse_callback, & context)))
	{
	  cerr << "Failed to create Avahi service browser: "
	       << avahi_strerror(avahi_client_errno(client))
	       << endl;
	  goto fail;
	}

      // Timeout after 2 seconds.
      struct timeval tv;
      avahi_simple_poll_get(simple_poll)->timeout_new(
        avahi_simple_poll_get(simple_poll),
	avahi_elapse_time(&tv, 1000*2, 0),
	timeout_callback,
	& context);

      // Run the main loop.
      avahi_simple_poll_loop(simple_poll);

      // Resolve each server discovered and eliminate duplicates.
      limit = raw_servers.size ();
      for (unsigned i = 0; i < limit; ++i)
	{
	  compile_server_info &raw_server = raw_servers[i];

	  // Delete the domain, if it is '.local'
	  string &host_name = raw_server.host_name;
	  string::size_type dot_index = host_name.find ('.');
	  assert (dot_index != 0);
	  string domain = host_name.substr (dot_index + 1);
	  if (domain == "local")
	    host_name = host_name.substr (0, dot_index);

	  // Now resolve the server (name, ip address, etc)
	  // Not an error if it fails. Just less info gathered.
	  resolve_server (s, raw_server);

	  // Add it to the list of servers, unless it is duplicate.
	  add_server_info (raw_server, online_servers);
	}

    fail:
      // Cleanup.
      if (sb)
        avahi_service_browser_free(sb);
    
      if (client)
        avahi_client_free(client);

      if (simple_poll)
        avahi_simple_poll_free(simple_poll);
#else // ! HAVE_AVAHI
      // Without Avahi, we can't detect online servers. Issue a warning.
      if (s.verbose)
	clog << "Unable to detect online servers" << endl;
#endif // ! HAVE_AVAHI
    } // Server information is not cached.

  if (! servers.empty ())
    {
      // Filter the existing vector by keeping the information in common with
      // the online_server vector.
      keep_common_server_info (online_servers, servers);
    }
  else
    {
      // Add the information, but not duplicates. Skip the empty first entry.
      unsigned limit = online_servers.size ();
      for (unsigned i = 1; i < limit; ++i)
	add_server_info (online_servers[i], servers);
    }
}

// Add server info to a list, avoiding duplicates. Merge information from
// two duplicate items.
void
add_server_info (
  const compile_server_info &info, vector<compile_server_info>& list
)
{
  vector<compile_server_info>::iterator s = 
    find (list.begin (), list.end (), info);
  if (s != list.end ())
    {
      // Duplicate. Merge the two items.
      merge_server_info (info, *s);
      return;
    }
  list.push_back (info);
}

// Filter the second vector by keeping information in common with the first
// vector.
void
keep_common_server_info (
  const	vector<compile_server_info> &info_to_keep,
  vector<compile_server_info> &filtered_info
)
{
  for (unsigned i = 0; i < filtered_info.size (); /**/)
    {
      const vector<compile_server_info>::const_iterator j =
	find (info_to_keep.begin (), info_to_keep.end (), filtered_info[i]);
      // Was the item found?
      if (j != info_to_keep.end ())
	{
	  // This item was found in the info to be kept. Keep it and merge
	  // the information.
	  merge_server_info (*j, filtered_info[i]);
	  ++i;
	  continue;
	}

      // The item was not found. Delete it.
      filtered_info.erase (filtered_info.begin () + i);
    }
}

// Merge two compile server info items.
void
merge_server_info (
  const compile_server_info &source,
  compile_server_info &target
)
{
  if (target.host_name.empty ())
    target.host_name = source.host_name;
  if (target.ip_address.empty ())
    target.ip_address = source.ip_address;
  if (target.port == 0)
    target.port = source.port;
  if (target.sysinfo.empty ())
    target.sysinfo = source.sysinfo;
  if (target.certinfo.empty ())
    target.certinfo = source.certinfo;
}

// Merge two compile server info vectors.
void
merge_server_info (
  const vector<compile_server_info> &source,
  vector<compile_server_info> &target
)
{
  for (vector<compile_server_info>::const_iterator i = source.begin ();
       i != source.end ();
       ++i)
    add_server_info (*i, target);
}

// Merge info from a compile server info vector into a compile
// server info item.
void
merge_server_info (
  const vector<compile_server_info> &source,
  compile_server_info &target
)
{
  for (vector<compile_server_info>::const_iterator i = source.begin ();
       i != source.end ();
       ++i)
    {
      if (*i == target)
	{
	  merge_server_info (*i, target);
	  break;
	}
    }
}
