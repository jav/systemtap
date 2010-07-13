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
#endif

#include <cstdlib>
#include <cstdio>

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
  int rc;
  int servers = 0;

  unsigned limit = s.specified_servers.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      const string &server = s.specified_servers[i];

      // If the server string is empty, then work with all online, trusted and
      // compatible servers.
      if (server.empty ())
	{
	  vector<compile_server_info> default_servers;
	  int pmask = compile_server_online | compile_server_trusted |
	    compile_server_compatible;
	  get_server_info (s, pmask, default_servers);

	  // Try each server in succession until successful.
	  unsigned limit = default_servers.size ();
	  for (unsigned i = 0; i < limit; ++i)
	    {
	      ++servers;
	      rc = compile_using_server (default_servers[i]);
	      if (rc == 0)
                {
                  s.winning_server =
                    default_servers[i].host_name + string(" [") +
                    default_servers[i].ip_address + string(":") +
                    lex_cast(default_servers[i].port) + string("]");
                  return rc; // success!
                }

	    }
	  continue;
	}

      // Otherwise work with the specified server
      // XXXX: TODO
    }

  if (servers == 0)
    cerr << "Unable to find a server" << endl;
  else
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
  // Try resolve the host name if it is '.local'.
  string::size_type dot_index = server.host_name.find ('.');
  assert (dot_index != 0);
  string host = server.host_name.substr (0, dot_index);
  string domain = server.host_name.substr (dot_index + 1);

  if (domain == "local")
    domain = s.get_domain_name ();

  string host_name;
  if (host + domain == "localhost" + s.get_domain_name () ||
      host + domain == s.get_host_name () + s.get_domain_name ())
    host_name = "localhost";
  else
    host_name = host + "." + domain;

  // Attempt connection using each of the available client ceritificate
  // databases.
  vector<string> dbs = private_ssl_dbs;
  vector<string>::iterator i = dbs.end();
  dbs.insert (i, public_ssl_dbs.begin (), public_ssl_dbs.end ());
  for (i = dbs.begin (); i != dbs.end (); ++i)
    {
      const char *cert_dir = i->c_str ();

      if (s.verbose > 1)
	clog << "Attempting connection with " << server
	     << " (" << host_name << ")" << endl
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
      int rc = client_main (host_name.c_str (), server.port,
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
  if (s.verbose > 1)
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
      if (s.verbose > 1)
	clog << "  found " << dirname << endl;

      filespec = dirname + "/*";
      if (s.verbose > 1)
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
	      if (s.verbose > 1)
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
      if (s.verbose > 1)
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
	      if (s.verbose > 1)
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
  return s << i.host_name << ' '
	   << i.ip_address << ' '
	   << i.port << ' '
	   << i.sysinfo;
}

void
query_server_status (const systemtap_session &s)
{
  unsigned limit = s.server_status_strings.size ();
  for (unsigned i = 0; i < limit; ++i)
    query_server_status (s, s.server_status_strings[i]);
}

void
query_server_status (const systemtap_session &s, const string &status_string)
{
#if HAVE_NSS || HAVE_AVAHI
  // If this string is empty, then set the default.
  // If the --use-server option has been used
  //   the default is 'specified'
  // otherwise if the --unprivileged has been used
  //   the default is online,trusted,compatible,signer
  // otherwise
  //   the default is online,compatible
  //
  // Having said that,
  //   'online' is only applicable if we have avahi
  //   'trusted' and 'signer' are only applicable if we have NSS
  string working_string;
  if (status_string.empty ())
    {
      if (s.specified_servers.empty ())
	{
#if HAVE_AVAHI
	  working_string = "online,";
#endif
#if HAVE_NSS
	  //	  working_string += "trusted,"; --- not yet
	  if (s.unprivileged)
	    working_string += "signer,";
#endif
	  working_string += "compatible";
	}
      else
	working_string = "specified";
    }
  else
    working_string = status_string;

  clog << "Systemtap Compile Server Status for '" << working_string << '\''
       << endl;

  // Construct a mask of the server properties that have been requested.
  // The available properties are:
  //     trusted    - trusted servers only.
  //	 online     - online servers only.
  //     compatible - servers which compile for the current kernel release
  //	 	      and architecture.
  //     signer     - servers which are trusted module signers.
  //	 specified  - servers which have been specified using --use-server=XXX.
  //	 	      If no servers have been specified, then this is
  //		      equivalent to --list-servers=trusted,online,compatible.
  //     all        - all trusted servers, servers currently online and
  //	              specified servers.
  vector<string> properties;
  tokenize (working_string, properties, ",");
  int pmask = 0;
  unsigned limit = properties.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      const string &property = properties[i];
      if (property == "all")
	{
	  pmask |= compile_server_specified;
#if HAVE_NSS
	  pmask |= compile_server_trusted;
#endif
#if HAVE_AVAHI
	  pmask |= compile_server_online;
#endif
	}
      else if (property == "specified")
	{
	  pmask |= compile_server_specified;
	}
#if HAVE_NSS
      else if (property == "trusted")
	{
	  pmask |= compile_server_trusted;
	}
#endif
#if HAVE_AVAHI
      else if (property == "online")
	{
	  pmask |= compile_server_online;
	}
#endif
      else if (property == "compatible")
	{
	  pmask |= compile_server_compatible;
	}
#if HAVE_NSS
      else if (property == "signer")
	{
	  pmask |= compile_server_signer;
	}
#endif
      else
	{
	  cerr << "Warning: unsupported compile server property: " << property
	       << endl;
	}
    }

  // Now obtain a list of the servers which match the criteria.
  vector<compile_server_info> servers;
  get_server_info (s, pmask, servers);

  // Print the server information
  limit = servers.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      clog << servers[i] << endl;
    }
#endif // HAVE_NSS || HAVE_AVAHI
}

void
get_server_info (
  const systemtap_session &s,
  int pmask,
  vector<compile_server_info> &servers
)
{
  // Get information on compile servers matching the requested criteria.
  // XXXX: TODO: Only compile_server_online and compile_server_compatible
  //             are currently implemented.
  if ((pmask & compile_server_online))
    {
      get_online_server_info (servers);
    }
  if ((pmask & compile_server_compatible))
    {
      keep_compatible_server_info (s, servers);
    }
}

void
keep_compatible_server_info (
  const systemtap_session &s,
  vector<compile_server_info> &servers
)
{
  // Remove entries for servers incompatible with the host environment
  // from the given list of servers.
  // A compatible server compiles for the kernel release and architecture
  // of the host environment.
  for (unsigned i = 0; i < servers.size (); /**/)
    {
      // Extract the sysinfo field from the Avahi TXT.
      string sysinfo = servers[i].sysinfo;
      size_t ix = sysinfo.find("\"sysinfo=");
      if (ix == string::npos)
	{
	  // No sysinfo field. Treat as incompatible.
	  servers.erase (servers.begin () + i);
	  continue;
	}
      sysinfo = sysinfo.substr (ix + sizeof ("\"sysinfo=") - 1);
      ix = sysinfo.find('"');
      if (ix == string::npos)
	{
	  // No end of sysinfo field. Treat as incompatible.
	  servers.erase (servers.begin () + i);
	  continue;
	}
      sysinfo = sysinfo.substr (0, ix);

      // Break the sysinfo into kernel release and arch.
      vector<string> release_arch;
      tokenize (sysinfo, release_arch, " ");
      if (release_arch.size () != 2)
	{
	  // Bad sysinfo data. Treat as incompatible.
	  servers.erase (servers.begin () + i);
	  continue;
	}
      if (release_arch[0] != s.kernel_release ||
	  release_arch[1] != s.architecture)
	{
	  // Platform mismatch.
	  servers.erase (servers.begin () + i);
	  continue;
	}
  
      // The server is compatible. Leave it in the list.
      ++i;
    }
}

#if HAVE_AVAHI
// Avahi API Callbacks.
//-----------------------------------------------------------------------
struct browsing_context {
  AvahiSimplePoll *simple_poll;
  AvahiClient *client;
  vector<compile_server_info> *servers;
};

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
	    info.ip_address = strdup (a);
	    info.port = port;
	    info.sysinfo = t;
	    info.host_name = host_name;

	    // Add this server to the list of discovered servers.
	    servers->push_back (info);

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
get_online_server_info (vector<compile_server_info> &servers)
{
#if HAVE_AVAHI
    // Initialize.
    AvahiClient *client = NULL;
    AvahiServiceBrowser *sb = NULL;
 
    // Allocate main loop object.
    AvahiSimplePoll *simple_poll;
    if (!(simple_poll = avahi_simple_poll_new())) {
        cerr << "Failed to create Avahi simple poll object" << endl;
        goto fail;
    }
    browsing_context context;
    context.simple_poll = simple_poll;
    context.servers = & servers;

    // Allocate a new Avahi client
    int error;
    client = avahi_client_new (avahi_simple_poll_get (simple_poll),
			       (AvahiClientFlags)0,
			       client_callback, & context, & error);

    // Check whether creating the client object succeeded.
    if (!client) {
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
					  browse_callback, & context))) {
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
    
fail:
    // Cleanup.
    if (sb)
        avahi_service_browser_free(sb);
    
    if (client)
        avahi_client_free(client);

    if (simple_poll)
        avahi_simple_poll_free(simple_poll);
#endif // HAVE_AVAHI
}
