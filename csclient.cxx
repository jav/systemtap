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
#include "stap-probe.h"

#include <sys/times.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <algorithm>

extern "C" {
#include <linux/limits.h>
#include <sys/time.h>
#include <glob.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
#endif // HAVE_AVAHI

#if HAVE_NSS
extern "C" {
#include <ssl.h>
#include <nspr.h>
#include <nss.h>
#include <certdb.h>
#include <pk11pub.h>
#include <prerror.h>
#include <secerr.h>
#include <sslerr.h>

#include "nsscommon.h"
}
#endif // HAVE_NSS

using namespace std;

// Information about compile servers.
struct compile_server_info
{
  compile_server_info () : port (0) {}

  std::string host_name;
  std::string ip_address;
  unsigned short port;
  std::string sysinfo;
  std::string certinfo;

  bool empty () const
  {
    return host_name.empty () && ip_address.empty ();
  }

  bool operator== (const compile_server_info &that) const
  {
    // If the ip address is not set, then the host names must match, otherwise
    // the ip addresses must match.
    if (this->ip_address.empty () || that.ip_address.empty ())
      {
	if (this->host_name != that.host_name)
	  return false;
      }
    else if (this->ip_address != that.ip_address)
      return false;

    // Compare the other fields only if they have both been set.
    if (this->port != 0 && that.port != 0 && this->port != that.port)
      return false;
    if (! this->sysinfo.empty () && ! that.sysinfo.empty () &&
	this->sysinfo != that.sysinfo)
      return false;
    if (! this->certinfo.empty () && ! that.certinfo.empty () &&
	this->certinfo != that.certinfo)
      return false;
    return true;
  }
};

ostream &operator<< (ostream &s, const compile_server_info &i);

// For filtering queries.
enum compile_server_properties {
  compile_server_all        = 0x1,
  compile_server_trusted    = 0x2,
  compile_server_online     = 0x4,
  compile_server_compatible = 0x8,
  compile_server_signer     = 0x10,
  compile_server_specified  = 0x20
};

// Static functions.
static void query_server_status (systemtap_session &s, const string &status_string);

static void get_server_info (systemtap_session &s, int pmask, vector<compile_server_info> &servers);
static void get_all_server_info (systemtap_session &s, vector<compile_server_info> &servers);
static void get_default_server_info (systemtap_session &s, vector<compile_server_info> &servers);
static void get_specified_server_info (systemtap_session &s, vector<compile_server_info> &servers, bool no_default = false);
static void get_or_keep_online_server_info (systemtap_session &s, vector<compile_server_info> &servers, bool keep);
static void get_or_keep_trusted_server_info (systemtap_session &s, vector<compile_server_info> &servers, bool keep);
static void get_or_keep_signing_server_info (systemtap_session &s, vector<compile_server_info> &servers, bool keep);
static void get_or_keep_compatible_server_info (systemtap_session &s, vector<compile_server_info> &servers, bool keep);
static void keep_common_server_info (const compile_server_info &info_to_keep, vector<compile_server_info> &filtered_info);
static void keep_common_server_info (const vector<compile_server_info> &info_to_keep, vector<compile_server_info> &filtered_info);
static void keep_server_info_with_cert_and_port (systemtap_session &s, const compile_server_info &server, vector<compile_server_info> &servers);

static void add_server_info (const compile_server_info &info, vector<compile_server_info>& list);
static void add_server_info (const vector<compile_server_info> &source, vector<compile_server_info> &target);
static void merge_server_info (const compile_server_info &source, compile_server_info &target);
#if 0 // not used right now
static void merge_server_info (const compile_server_info &source, vector<compile_server_info> &target);
static void merge_server_info (const vector<compile_server_info> &source, vector <compile_server_info> &target);
#endif
static void resolve_host (systemtap_session& s, compile_server_info &server, vector<compile_server_info> &servers);

#if HAVE_NSS
static const char *server_cert_nickname = "stap-server";

static void add_server_trust (systemtap_session &s, const string &cert_db_path, const vector<compile_server_info> &server_list);
static void revoke_server_trust (systemtap_session &s, const string &cert_db_path, const vector<compile_server_info> &server_list);
static void get_server_info_from_db (systemtap_session &s, vector<compile_server_info> &servers, const string &cert_db_path);

static string
private_ssl_cert_db_path (const systemtap_session &s)
{
  return s.data_path + "/ssl/client";
}

static string
global_ssl_cert_db_path ()
{
  return SYSCONFDIR "/systemtap/ssl/client";
}

static string
signing_cert_db_path ()
{
  return SYSCONFDIR "/systemtap/staprun";
}
#endif // HAVE_NSS

int
compile_server_client::passes_0_4 ()
{
  PROBE1(stap, client__start, &s);

#if ! HAVE_NSS
  // This code will never be called, if we don't have NSS, but it must still
  // compile.
  return 1; // Failure
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
	  // Also copy the module signature, it it exists.
	  module_src_path += ".sgn";
	  if (file_exists (module_src_path))
	    {
	      module_dest_path += ".sgn";
	      copy_file(module_src_path, module_dest_path, s.verbose > 1);
	    }
	}
    }

  PROBE1(stap, client__end, &s);

  return rc;
#endif // HAVE_NSS
}

#if HAVE_NSS
// Initialize a client/server session.
int
compile_server_client::initialize ()
{
  int rc = 0;

  // Initialize session state
  argc = 0;

  // Private location for server certificates.
  private_ssl_dbs.push_back (private_ssl_cert_db_path (s));

  // Additional public location.
  public_ssl_dbs.push_back (global_ssl_cert_db_path ());

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
  vector<compile_server_info> specified_servers;
  get_specified_server_info (s, specified_servers);

  // Examine the specified servers to make sure that each has been resolved
  // with a host name, ip address and port. If not, try to obtain this
  // information by examining online servers.
  vector<compile_server_info> server_list = specified_servers;
  for (vector<compile_server_info>::const_iterator i = specified_servers.begin ();
       i != specified_servers.end ();
       ++i)
    {
      // If we have an ip address and port number, then just use the one we've
      // been given. Otherwise, check for matching online servers and try their
      // ip addresses and ports.
      if (! i->host_name.empty () && ! i->ip_address.empty () && i->port != 0)
	add_server_info (*i, server_list);
      else
	{
	  // Obtain a list of online servers.
	  vector<compile_server_info> online_servers;
	  get_or_keep_online_server_info (s, online_servers, false/*keep*/);

	  // If no specific server (port) has been specified,
	  // then we'll need the servers to be
	  // compatible and possible trusted as signers as well.
	  if (i->port == 0)
	    {
	      get_or_keep_compatible_server_info (s, online_servers, true/*keep*/);
	      if (s.unprivileged)
		get_or_keep_signing_server_info (s, online_servers, true/*keep*/);
	    }

	  // Keep the ones (if any) which match our server.
	  keep_common_server_info (*i, online_servers);

	  // Add these servers (if any) to the server list.
	  add_server_info (online_servers, server_list);
	}
    }

  // Did we identify any potential servers?
  unsigned limit = server_list.size ();
  if (limit == 0)
    {
      cerr << "Unable to find a server" << endl;
      return 1;
    }

  // Now try each of the identified servers in turn.
  int rc = compile_using_server (server_list);
  if (rc == 0)
    return 0; // success!

  return 1; // Failure - message already generated.
}

// Temporary until the stap-client-connect program goes away.
extern "C"
int
client_main (const char *hostName, PRUint32 ip, PRUint16 port,
	     const char* infileName, const char* outfileName,
	     const char* trustNewServer);
#endif // HAVE_NSS

// Convert the given string to an ip address in host byte order.
static unsigned
stringToIpAddress (const string &s)
{
  if (s.empty ())
    return 0; // unknown

  vector<string>components;
  tokenize (s, components, ".");
  assert (components.size () >= 1);

  unsigned ip = 0;
  unsigned i;
  for (i = 0; i < components.size (); ++i)
    {
      const char *ipstr = components[i].c_str ();
      char *estr;
      errno = 0;
      unsigned a = strtoul (ipstr, & estr, 10);
      if (errno == 0 && *estr == '\0' && a <= UCHAR_MAX)
	ip = (ip << 8) + a;
      else
	return 0;
    }

  return ip;
}

int 
compile_server_client::compile_using_server (
  const vector<compile_server_info> &servers
)
{
  // This code will never be called if we don't have NSS, but it must still
  // compile.
#if HAVE_NSS
  // Make sure NSPR is initialized
  s.NSPR_init ();

  // Attempt connection using each of the available client certificate
  // databases. Assume the server certificate is invalid until proven otherwise.
  PR_SetError (SEC_ERROR_CA_CERT_INVALID, 0);
  vector<string> dbs = private_ssl_dbs;
  vector<string>::iterator i = dbs.end();
  dbs.insert (i, public_ssl_dbs.begin (), public_ssl_dbs.end ());
  int rc = 1; // assume failure
  for (i = dbs.begin (); i != dbs.end (); ++i)
    {
      // Make sure the database directory exists. It is not an error if it
      // doesn't.
      if (! file_exists (*i))
	continue;

#if 0 // no client authentication for now.
      // Set our password function callback.
      PK11_SetPasswordFunc (myPasswd);
#endif

      // Initialize the NSS libraries.
      const char *cert_dir = i->c_str ();
      SECStatus secStatus = NSS_InitReadWrite (cert_dir);
      if (secStatus != SECSuccess)
	{
	  // Try it again, readonly.
	  secStatus = NSS_Init(cert_dir);
	  if (secStatus != SECSuccess)
	    {
	      cerr << "Error initializing NSS" << endl;
	      nssError ();
	      NSS_Shutdown();
	      continue; // try next database
	    }
	}

      // All cipher suites except RSA_NULL_MD5 are enabled by Domestic Policy.
      NSS_SetDomesticPolicy ();

      server_zipfile = s.tmpdir + "/server.zip";

      // Try each server in turn.
      for (vector<compile_server_info>::const_iterator j = servers.begin ();
	   j != servers.end ();
	   ++j)
	{
	  if (s.verbose > 1)
	    clog << "Attempting SSL connection with " << *j << endl
		 << "  using certificates from the database in " << cert_dir
		 << endl;

	  // The host name defaults to the ip address, if not specified.
	  string hostName;
	  if (j->host_name.empty ())
	    {
	      assert (! j->ip_address.empty ());
	      hostName = j->ip_address;
	    }
	  else
	    hostName = j->host_name;

	  rc = client_main (hostName.c_str (),
			    stringToIpAddress (j->ip_address),
			    j->port,
			    client_zipfile.c_str(), server_zipfile.c_str (),
			    NULL/*trustNewServer_p*/);
	  if (rc == SECSuccess)
	    {
	      s.winning_server =
		hostName + string(" [") +
		j->ip_address + string(":") +
		lex_cast(j->port) + string("]");
	      break; // Success!
	    }

	  if (s.verbose > 1)
	    {
	      clog << "  Unable to connect: ";
	      nssError ();
	    }
	}
 
      NSS_Shutdown();

      if (rc == SECSuccess)
	break; // Success!
    }

  if (rc != SECSuccess)
    cerr << "Unable to connect to a server" << endl;

  return rc;
#endif // HAVE_NSS

  return 1; // Failure
}

#if HAVE_NSS
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

	      // If a uprobes.ko module was returned, then make note of it.
	      if (file_exists (s.tmpdir + "/server/uprobes.ko"))
		{
		  s.need_uprobes = true;
		  s.uprobes_path = s.tmpdir + "/server/uprobes.ko";
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
#endif // HAVE_NSS

// Utility Functions.
//-----------------------------------------------------------------------
ostream &operator<< (ostream &s, const compile_server_info &i)
{
  s << " host=";
  if (! i.host_name.empty ())
    s << i.host_name;
  else
    s << "unknown";
  s << " ip=";
  if (! i.ip_address.empty ())
    s << i.ip_address;
  else
    s << "offline";
  s << " port=";
  if (i.port != 0)
    s << i.port;
  else
    s << "offline";
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
  // Make sure NSPR is initialized
  s.NSPR_init ();

  unsigned limit = s.server_status_strings.size ();
  for (unsigned i = 0; i < limit; ++i)
    query_server_status (s, s.server_status_strings[i]);
}

static void
query_server_status (systemtap_session &s, const string &status_string)
{
  // If this string is empty, then the default is "specified"
  string working_string = status_string;
  if (working_string.empty ())
    working_string = "specified";

  // If the query is "specified" and no servers have been specified
  // (i.e. --use-server not used or used with no argument), then
  // use the default query.
  // TODO: This may not be necessary. The underlying queries should handle
  //       "specified" properly.
  if (working_string == "specified" &&
      (s.specified_servers.empty () ||
       (s.specified_servers.size () == 1 && s.specified_servers[0].empty ())))
    working_string = default_server_spec (s);

  int pmask = server_spec_to_pmask (working_string);

  // Now obtain a list of the servers which match the criteria.
  vector<compile_server_info> raw_servers;
  get_server_info (s, pmask, raw_servers);

  // Augment the listing with as much information as possible by adding
  // information from known servers.
  vector<compile_server_info> servers;
  get_all_server_info (s, servers);
  keep_common_server_info (raw_servers, servers);

  // Print the server information. Skip the empty entry at the head of the list.
  clog << "Systemtap Compile Server Status for '" << working_string << '\''
       << endl;
  bool found = false;
  unsigned limit = servers.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      assert (! servers[i].empty ());
      // Don't list servers with no cert information. They may not actually
      // exist.
      // TODO: Could try contacting the server and obtaining it cert
      if (servers[i].certinfo.empty ())
	continue;
      clog << servers[i] << endl;
      found = true;
    }
  if (! found)
    clog << "No servers found" << endl;
}

// Add or remove trust of the servers specified on the command line.
void
manage_server_trust (systemtap_session &s)
{
  // This function will never be called if we don't have NSS, but it must
  // still compile.
#if HAVE_NSS
  // Nothing to do if --trust-servers was not specified.
  if (s.server_trust_spec.empty ())
    return;

  // Break up and analyze the trust specification. Recognized components are:
  //   ssl       - trust the specified servers as ssl peers
  //   signer    - trust the specified servers as module signers
  //   revoke    - revoke the requested trust
  //   all-users - apply/revoke the requested trust for all users
  //   no-prompt - don't prompt the user for confirmation
  vector<string>components;
  tokenize (s.server_trust_spec, components, ",");
  bool ssl = false;
  bool signer = false;
  bool revoke = false;
  bool all_users = false;
  bool no_prompt = false;
  bool error = false;
  for (vector<string>::const_iterator i = components.begin ();
       i != components.end ();
       ++i)
    {
      if (*i == "ssl")
	ssl = true;
      else if (*i == "signer")
	{
	  if (geteuid () != 0)
	    {
	      cerr << "Only root can specify 'signer' on --trust-servers" << endl;
	      error = true;
	    }
	  else
	    signer = true;
	}
      else if (*i == "revoke")
	revoke = true;
      else if (*i == "all-users")
	{
	  if (geteuid () != 0)
	    {
	      cerr << "Only root can specify 'all-users' on --trust-servers" << endl;
	      error = true;
	    }
	  else
	    all_users = true;
	}
      else if (*i == "no-prompt")
	no_prompt = true;
      else
	cerr << "Warning: Unrecognized server trust specification: " << *i
	     << endl;
    }
  if (error)
    return;

  // Make sure NSPR is initialized
  s.NSPR_init ();

  // Now obtain the list of specified servers.
  vector<compile_server_info> server_list;
  get_specified_server_info (s, server_list, true/*no_default*/);

  // Did we identify any potential servers?
  unsigned limit = server_list.size ();
  if (limit == 0)
    {
      cerr << "No servers identified for trust" << endl;
      return;
    }

  // Create a string representing the request in English.
  // If neither 'ssl' or 'signer' was specified, the default is 'ssl'.
  if (! ssl && ! signer)
    ssl = true;
  ostringstream trustString;
  if (ssl)
    {
      trustString << "as an SSL peer";
      if (all_users)
	trustString << " for all users";
      else
	trustString << " for the current user";
    }
  if (signer)
    {
      if (ssl)
	trustString << " and ";
      trustString << "as a module signer for all users";
    }

  // Prompt the user to confirm what's about to happen.
  if (no_prompt)
    {
      if (revoke)
	clog << "Revoking trust ";
      else
	clog << "Adding trust ";
    }
  else
    {
      if (revoke)
	clog << "Revoke trust ";
      else
	clog << "Add trust ";
    }
  clog << "in the following servers " << trustString.str ();
  if (! no_prompt)
    clog << '?';
  clog << endl;
  for (unsigned i = 0; i < limit; ++i)
    clog << "  " << server_list[i] << endl;
  if (! no_prompt)
    {
      clog << "[y/N] " << flush;

      // Only carry out the operation if the response is "yes"
      string response;
      cin >> response;
      if (response[0] != 'y' && response [0] != 'Y')
	{
	  clog << "Server trust unchanged" << endl;
	  return;
	}
    }

  // Now add/revoke the requested trust.
  string cert_db_path;
  if (ssl)
    {
      if (all_users)
	cert_db_path = global_ssl_cert_db_path ();
      else
	cert_db_path = private_ssl_cert_db_path (s);
      if (revoke)
	revoke_server_trust (s, cert_db_path, server_list);
      else
	add_server_trust (s, cert_db_path, server_list);
    }
  if (signer)
    {
      cert_db_path = signing_cert_db_path ();
      if (revoke)
	revoke_server_trust (s, cert_db_path, server_list);
      else
	add_server_trust (s, cert_db_path, server_list);
    }
#endif // HAVE_NSS
}

#if HAVE_NSS
// Issue a status message for when a server's trust is already in place.
static void
trust_already_in_place (
  const compile_server_info &server,
  const vector<compile_server_info> &server_list,
  const string cert_db_path,
  bool revoking
)
{
  // What level of trust?
  string purpose;
  if (cert_db_path == signing_cert_db_path ())
    purpose = "as a module signer for all users";
  else
    {
      purpose = "as an SSL peer";
      if (cert_db_path == global_ssl_cert_db_path ())
	purpose += " for all users";
      else
	purpose += " for the current user";
    }

  // Issue a message for each server in the list with the same certificate.
  unsigned limit = server_list.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      if (server.certinfo != server_list[i].certinfo)
	continue;
      clog << server_list[i] << " is already ";
      if (revoking)
	clog << "un";
      clog << "trusted " << purpose << endl;
    }
}

// Add the given servers to the given database of trusted servers.
static void
add_server_trust (
  systemtap_session &s,
  const string &cert_db_path,
  const vector<compile_server_info> &server_list
)
{
  // Get a list of servers already trusted. This opens the database, so do it
  // before we open it for our own purposes.
  vector<compile_server_info> already_trusted;
  get_server_info_from_db (s, already_trusted, cert_db_path);

  // Make sure the given path exists.
  if (create_dir (cert_db_path.c_str (), 0755) != 0)
    {
      cerr << "Unable to find or create the client certificate database directory "
	   << cert_db_path << ": ";
      perror ("");
      return;
    }

  // Must predeclare this because of jumps to cleanup: below.
  vector<string> processed_certs;

  // Initialize the NSS libraries -- read/write
  SECStatus secStatus = NSS_InitReadWrite (cert_db_path.c_str ());
  if (secStatus != SECSuccess)
    {
      cerr << "Error initializing NSS for " << cert_db_path << endl;
      nssError ();
      goto cleanup;
    }

  // All cipher suites except RSA_NULL_MD5 are enabled by Domestic Policy.
  NSS_SetDomesticPolicy ();

  // Iterate over the servers to become trusted. Contact each one and
  // add it to the list of trusted servers if it is not already trusted.
  // client_main will issue any error messages.
  for (vector<compile_server_info>::const_iterator server = server_list.begin();
       server != server_list.end ();
       ++server)
    {
      // Trust is based on certificates. We need only add trust in the
      // same certificate once.
      if (find (processed_certs.begin (), processed_certs.end (),
		server->certinfo) != processed_certs.end ())
	continue;
      processed_certs.push_back (server->certinfo);

      // We need not contact the server if it is already trusted.
      if (find (already_trusted.begin (), already_trusted.end (), *server) !=
	  already_trusted.end ())
	{
	  if (s.verbose > 1)
	    trust_already_in_place (*server, server_list, cert_db_path, false/*revoking*/);
	  continue;
	}
      // At a minimum we need a host name or ip_address along with a port
      // number in order to contact the server.
      if (server->empty () || server->port == 0)
	continue;
      int rc = client_main (server->host_name.c_str (),
			    stringToIpAddress (server->ip_address),
			    server->port,
			    NULL, NULL, "permanent");
      if (rc != SECSuccess)
	{
	  cerr << "Unable to connect to " << *server << endl;
	  nssError ();
	}
    }

 cleanup:
  // Shutdown NSS.
  NSS_Shutdown ();

  // Make sure the database files are readable.
  glob_t globbuf;
  string filespec = cert_db_path + "/*.db";
  if (s.verbose > 2)
    clog << "Searching \"" << filespec << "\"" << endl;
  int r = glob (filespec.c_str (), 0, NULL, & globbuf);
  if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
    {
      for (unsigned i = 0; i < globbuf.gl_pathc; ++i)
	{
	  string filename = globbuf.gl_pathv[i];
	  if (s.verbose > 2)
	    clog << "  found " << filename << endl;

	  if (chmod (filename.c_str (), 0644) != 0)
	    {
	      cerr << "Warning: Unable to change permissions on "
		   << filename << ": ";
	      perror ("");
	    }
	}
    }
}

// Remove the given servers from the given database of trusted servers.
static void
revoke_server_trust (
  systemtap_session &s,
  const string &cert_db_path,
  const vector<compile_server_info> &server_list
)
{
  // Make sure the given path exists.
  if (! file_exists (cert_db_path))
    {
      if (s.verbose > 1)
	cerr << "Certificate database '" << cert_db_path << "' does not exist."
	     << endl;
      if (s.verbose)
	{
	  for (vector<compile_server_info>::const_iterator server = server_list.begin();
	       server != server_list.end ();
	       ++server)
	    trust_already_in_place (*server, server_list, cert_db_path, true/*revoking*/);
	}
      return;
    }

  // Must predeclare these because of jumps to cleanup: below.
  PK11SlotInfo *slot = NULL;
  CERTCertDBHandle *handle;
  PRArenaPool *tmpArena = NULL;
  CERTCertList *certs = NULL;
  CERTCertificate *db_cert;
  vector<string> processed_certs;

  // Initialize the NSS libraries -- read/write
  SECStatus secStatus = NSS_InitReadWrite (cert_db_path.c_str ());
  if (secStatus != SECSuccess)
    {
      cerr << "Error initializing NSS for " << cert_db_path << endl;
      nssError ();
      goto cleanup;
    }
  slot = PK11_GetInternalKeySlot ();
  handle = CERT_GetDefaultCertDB();

  // A memory pool to work in
  tmpArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  if (! tmpArena) 
    {
      cerr << "Out of memory:";
      nssError ();
      goto cleanup;
    }

  // Iterate over the servers to become untrusted.
  for (vector<compile_server_info>::const_iterator server = server_list.begin();
       server != server_list.end ();
       ++server)
    {
      // If the server's certificate serial number is unknown, then we can't
      // match it with one in the database.
      if (server->certinfo.empty ())
	continue;

      // Trust is based on certificates. We need only revoke trust in the same
      // certificate once.
      if (find (processed_certs.begin (), processed_certs.end (),
		server->certinfo) != processed_certs.end ())
	continue;
      processed_certs.push_back (server->certinfo);

      // Search the client-side database of trusted servers.
      db_cert = PK11_FindCertFromNickname (server_cert_nickname, NULL);
      if (! db_cert)
	{
	  // No trusted servers. Not an error, but issue a status message.
	  if (s.verbose)
	    trust_already_in_place (*server, server_list, cert_db_path, true/*revoking*/);
	  continue;
	}

      // Here, we have one cert with the desired nickname.
      // Now, we will attempt to get a list of ALL certs 
      // with the same subject name as the cert we have.  That list 
      // should contain, at a minimum, the one cert we have already found.
      // If the list of certs is empty (NULL), the libraries have failed.
      certs = CERT_CreateSubjectCertList (NULL, handle, & db_cert->derSubject,
					  PR_Now (), PR_FALSE);
      CERT_DestroyCertificate (db_cert);
      if (! certs)
	{
	  cerr << "Unable to query certificate database " << cert_db_path
	       << ": " << endl;
	  PORT_SetError (SEC_ERROR_LIBRARY_FAILURE);
	  nssError ();
	  goto cleanup;
	}

      // Find the certificate matching the one belonging to our server.
      CERTCertListNode *node;
      for (node = CERT_LIST_HEAD (certs);
	   ! CERT_LIST_END (node, certs);
	   node = CERT_LIST_NEXT (node))
	{
	  // The certificate we're working with.
	  db_cert = node->cert;

	  // Get the serial number.
	  ostringstream serialNumber;
	  serialNumber << hex << setfill('0') << right;
	  for (unsigned i = 0; i < db_cert->serialNumber.len; ++i)
	    {
	      if (i > 0)
		serialNumber << ':';
	      serialNumber << setw(2) << (unsigned)db_cert->serialNumber.data[i];
	    }

	  // Does the serial number match that of the current server?
	  if (serialNumber.str () != server->certinfo)
	    continue; // goto next certificate

	  // All is ok! Remove the certificate from the database.
	  break;
	} // Loop over certificates in the database

      // Was a certificate matching the server found?  */
      if (CERT_LIST_END (node, certs))
	{
	  // Not found. Server is already untrusted.
	  if (s.verbose)
	    trust_already_in_place (*server, server_list, cert_db_path, true/*revoking*/);
	}
      else
	{
	  secStatus = SEC_DeletePermCertificate (db_cert);
	  if (secStatus != SECSuccess)
	    {
	      cerr << "Unable to remove certificate from " << cert_db_path
		   << ": " << endl;
	      nssError ();
	    }
	}
      CERT_DestroyCertList (certs);
      certs = NULL;
    } // Loop over servers

 cleanup:
  if (certs)
    CERT_DestroyCertList (certs);
  if (slot)
    PK11_FreeSlot (slot);
  if (tmpArena)
    PORT_FreeArena (tmpArena, PR_FALSE);

  NSS_Shutdown ();
}
#endif // HAVE_NSS

static void
get_server_info (
  systemtap_session &s,
  int pmask,
  vector<compile_server_info> &servers
)
{
  // Get information on compile servers matching the requested criteria.
  // The order of queries is significant. Accumulating queries must go first
  // followed by accumulating/filtering queries.
  bool keep = false;
  if (((pmask & compile_server_all)))
    {
      get_all_server_info (s, servers);
      keep = true;
    }
  // Add the specified servers, if requested
  if ((pmask & compile_server_specified))
    {
      get_specified_server_info (s, servers);
      keep = true;
    }
  // Now filter the or accumulate the list depending on whether a query has
  // already been made.
  if ((pmask & compile_server_online))
    {
      get_or_keep_online_server_info (s, servers, keep);
      keep = true;
    }
  if ((pmask & compile_server_trusted))
    {
      get_or_keep_trusted_server_info (s, servers, keep);
      keep = true;
    }
  if ((pmask & compile_server_signer))
    {
      get_or_keep_signing_server_info (s, servers, keep);
      keep = true;
    }
  if ((pmask & compile_server_compatible))
    {
      get_or_keep_compatible_server_info (s, servers, keep);
      keep = true;
    }
}

// Get information about all online servers as well as servers trusted
// as SSL peers and servers trusted as signers.
static void
get_all_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers
)
{
  get_or_keep_online_server_info (s, servers, false/*keep*/);
  get_or_keep_trusted_server_info (s, servers, false/*keep*/);
  get_or_keep_signing_server_info (s, servers, false/*keep*/);
}

static void
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
      // Get the required information.
      // get_server_info will add an empty entry at the beginning to indicate
      // that the search has been performed, in case the search comes up empty.
      int pmask = server_spec_to_pmask (default_server_spec (s));
      get_server_info (s, pmask, default_servers);
    }

  // Add the information, but not duplicates.
  add_server_info (default_servers, servers);
}

static void
get_specified_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  bool no_default
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
	  if (! no_default)
	    get_default_server_info (s, specified_servers);
	}
      else
	{
	  // Iterate over the specified servers. For each specification, add to
	  // the list of servers.
	  unsigned num_specified_servers = s.specified_servers.size ();
	  for (unsigned i = 0; i < num_specified_servers; ++i)
	    {
	      string &server = s.specified_servers[i];
	      if (server.empty ())
		{
		  // No server specified. Use the default servers.
		  if (! no_default)
		    get_default_server_info (s, specified_servers);
		  continue;
		}

	      // Work with the specified server
	      compile_server_info server_info;

	      // See if a port was specified (:n suffix)
	      vector<string> components;
	      tokenize (server, components, ":");
	      if (components.size () > 2)
		{
		  // Treat it as a certificate serial number. The final
		  // component may still be a port number.
		  if (components.size () > 5)
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
		      // Remove the port number from the spec
		      server_info.certinfo = server.substr (0, server.find_last_of (':'));
		    }
		  else
		    server_info.certinfo = server;

		  // Look for all known servers with this serial number and
		  // (optional) port number.
		  vector<compile_server_info> known_servers;
		  get_all_server_info (s, known_servers);
		  keep_server_info_with_cert_and_port (s, server_info, known_servers);
		  // Did we find one?
		  if (known_servers.empty ())
		    {
		      if (s.verbose)
			cerr << "No server matching " << server << " found"
			     << endl;
		    }
		  else
		    add_server_info (known_servers, specified_servers);
		} // specified by cert serial number
	      else {
		// Not specified by serial number. Treat it as host name or
		// ip address and optional port number.
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

		// Obtain the host name or ip address.
		if (stringToIpAddress (components.front ()))
		  server_info.ip_address = components.front ();
		else
		  server_info.host_name = components.front ();

		// Find known servers matching the specified information.
		vector<compile_server_info> known_servers;
		get_all_server_info (s, known_servers);
		keep_common_server_info (server_info, known_servers);
		add_server_info (known_servers, specified_servers);

		// Resolve this host and add any information that is discovered.
		resolve_host (s, server_info, specified_servers);
	      }  // Not specified by cert serial number
	    } // Loop over --use-server options
	} // -- use-server specified
    } // Server information is not cached

  // Add the information, but not duplicates.
  add_server_info (specified_servers, servers);
}

static void
get_or_keep_trusted_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  bool keep
)
{
  // If we're filtering the list and it's already empty, then
  // there's nothing to do.
  if (keep && servers.empty ())
    return;

  // We only need to obtain this once. This is a good thing(tm) since
  // obtaining this information is expensive.
  static vector<compile_server_info> trusted_servers;
  if (trusted_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      trusted_servers.push_back (compile_server_info ());

#if HAVE_NSS
      // Check the private database first.
      string cert_db_path = private_ssl_cert_db_path (s);
      get_server_info_from_db (s, trusted_servers, cert_db_path);

      // Now check the global database.
      cert_db_path = global_ssl_cert_db_path ();
      get_server_info_from_db (s, trusted_servers, cert_db_path);
#else // ! HAVE_NSS
      // Without NSS, we can't determine whether a server is trusted.
      // Issue a warning.
      if (s.verbose)
	clog << "Unable to determine server trust as an SSL peer" << endl;
#endif // ! HAVE_NSS
    } // Server information is not cached

  if (keep)
    {
      // Filter the existing vector by keeping the information in common with
      // the trusted_server vector.
      keep_common_server_info (trusted_servers, servers);
    }
  else
    {
      // Add the information, but not duplicates.
      add_server_info (trusted_servers, servers);
    }
}

static void
get_or_keep_signing_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  bool keep
)
{
  // If we're filtering the list and it's already empty, then
  // there's nothing to do.
  if (keep && servers.empty ())
    return;

  // We only need to obtain this once. This is a good thing(tm) since
  // obtaining this information is expensive.
  static vector<compile_server_info> signing_servers;
  if (signing_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      signing_servers.push_back (compile_server_info ());

#if HAVE_NSS
      // For all users, check the global database.
      string cert_db_path = signing_cert_db_path ();
      get_server_info_from_db (s, signing_servers, cert_db_path);
#else // ! HAVE_NSS
      // Without NSS, we can't determine whether a server is a trusted
      // signer. Issue a warning.
      if (s.verbose)
	clog << "Unable to determine server trust as a module signer" << endl;
#endif // ! HAVE_NSS
    } // Server information is not cached

  if (keep)
    {
      // Filter the existing vector by keeping the information in common with
      // the signing_server vector.
      keep_common_server_info (signing_servers, servers);
    }
  else
    {
      // Add the information, but not duplicates.
      add_server_info (signing_servers, servers);
    }
}

#if HAVE_NSS
// Obtain information about servers from the certificates in the given database.
static void
get_server_info_from_db (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  const string &cert_db_path
)
{
  // Make sure the given path exists.
  if (! file_exists (cert_db_path))
    {
      if (s.verbose > 1)
	cerr << "Certificate database '" << cert_db_path << "' does not exist."
	     << endl;
      return;
    }

  // Must predeclare these because of jumps to cleanup: below.
  PK11SlotInfo *slot = NULL;
  CERTCertDBHandle *handle;
  PRArenaPool *tmpArena = NULL;
  CERTCertList *certs = NULL;
  CERTCertificate *db_cert;

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
  db_cert = PK11_FindCertFromNickname (server_cert_nickname, NULL);
  if (! db_cert)
    {
      // No trusted servers. Not an error. Just an empty list returned.
      goto cleanup;
    }

  // Here, we have one cert with the desired nickname.
  // Now, we will attempt to get a list of ALL certs 
  // with the same subject name as the cert we have.  That list 
  // should contain, at a minimum, the one cert we have already found.
  // If the list of certs is empty (NULL), the libraries have failed.
  certs = CERT_CreateSubjectCertList (NULL, handle, & db_cert->derSubject,
				      PR_Now (), PR_FALSE);
  CERT_DestroyCertificate (db_cert);
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
      db_cert = node->cert;

      // Get the host name. It is in the alt-name extension of the
      // certificate.
      SECItem subAltName;
      subAltName.data = NULL;
      secStatus = CERT_FindCertExtension (db_cert,
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
      field << hex << setfill('0') << right;
      for (unsigned i = 0; i < db_cert->serialNumber.len; ++i)
	{
	  if (i > 0)
	    field << ':';
	  field << setw(2) << (unsigned)db_cert->serialNumber.data[i];
	}
      server_info.certinfo = field.str ();

      // Our results will at a minimum contain this server.
      add_server_info (server_info, servers);

      // Augment the list by querying all online servers and keeping the ones
      // with the same cert serial number.
      vector<compile_server_info> online_servers;
      get_or_keep_online_server_info (s, online_servers, false/*keep*/);
      keep_server_info_with_cert_and_port (s, server_info, online_servers);
      add_server_info (online_servers, servers);
    }

 cleanup:
  if (certs)
    CERT_DestroyCertList (certs);
  if (slot)
    PK11_FreeSlot (slot);
  if (tmpArena)
    PORT_FreeArena (tmpArena, PR_FALSE);

  NSS_Shutdown ();
}
#endif // HAVE_NSS

static void
get_or_keep_compatible_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  bool keep
)
{
#if HAVE_AVAHI
  // If we're filtering the list and it's already empty, then
  // there's nothing to do.
  if (keep && servers.empty ())
    return;

  // Remove entries for servers incompatible with the host environment
  // from the given list of servers.
  // A compatible server compiles for the kernel release and architecture
  // of the host environment.
  //
  // Compatibility can only be determined for online servers. So, augment
  // and filter the information we have with information for online servers.
  vector<compile_server_info> online_servers;
  get_or_keep_online_server_info (s, online_servers, false/*keep*/);
  if (keep)
    keep_common_server_info (online_servers, servers);
  else
    add_server_info (online_servers, servers);

  // Now look to see which ones are compatible.
  // The vector can change size as we go, so be careful!!
  for (unsigned i = 0; i < servers.size (); /**/)
    {
      // Retain empty entries.
      assert (! servers[i].empty ());

      // Check the target of the server.
      if (servers[i].sysinfo != s.kernel_release + " " + s.architecture)
	{
	  // Target platform mismatch.
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
  if (keep)
    servers.clear ();
#endif
}

static void
keep_server_info_with_cert_and_port (
  systemtap_session &s,
  const compile_server_info &server,
  vector<compile_server_info> &servers
)
{
  assert (! server.certinfo.empty ());

  // Search the list of servers for ones matching the
  // serial number specified.
  // The vector can change size as we go, so be careful!!
  for (unsigned i = 0; i < servers.size (); /**/)
    {
      // Retain empty entries.
      if (servers[i].empty ())
	{
	  ++i;
	  continue;
	}
      if (servers[i].certinfo == server.certinfo &&
	  (servers[i].port == 0 || server.port == 0 ||
	   servers[i].port == server.port))
	{
	  // If the server is not online, then use the specified
	  // port, if any.
	  if (servers[i].port == 0)
	    servers[i].port = server.port;
	  ++i;
	  continue;
	}
      // The item does not match. Delete it.
      servers.erase (servers.begin () + i);
    }
}

// Obtain missing host name or ip address, if any.
static void
resolve_host (
  systemtap_session& s,
  compile_server_info &server,
  vector<compile_server_info> &resolved_servers
)
{
  // Either the host name or the ip address or both are already set.
  const char *lookup_name;
  if (! server.host_name.empty ())
    {
      // Use the host name to do the lookup.
      lookup_name = server.host_name.c_str ();
    }
  else
    {
      // Use the ip address to do the lookup.
      // getaddrinfo works on both host names and ip addresses.
      assert (! server.ip_address.empty ());
      lookup_name = server.ip_address.c_str ();
    }

  // Resolve the server. 
  struct addrinfo hints;
  memset(& hints, 0, sizeof (hints));
  hints.ai_family = AF_INET; // AF_UNSPEC or AF_INET6 to force version
  struct addrinfo *addr_info = NULL;
  int status = getaddrinfo (lookup_name, NULL, & hints, & addr_info);

  // Failure to resolve will result in an appropriate error later if other
  // methods fail.
  if (status != 0)
    goto cleanup;
  assert (addr_info);

  // Loop over the results collecting information.
  for (const struct addrinfo *ai = addr_info; ai != NULL; ai = ai->ai_next)
    {
      if (ai->ai_family != AF_INET)
	continue; // Not an IPv4 address

      // Start with the info we were given.
      compile_server_info new_server = server;

      // Obtain the ip address.
      // Start with the pointer to the address itself,
      struct sockaddr_in *ipv4 = (struct sockaddr_in *)ai->ai_addr;
      void *addr = & ipv4->sin_addr;

      // convert the IP to a string.
      char ipstr[INET_ADDRSTRLEN];
      inet_ntop (ai->ai_family, addr, ipstr, sizeof (ipstr));
      new_server.ip_address = ipstr;

      // Try to obtain a host name.
      char hbuf[NI_MAXHOST];
      status = getnameinfo (ai->ai_addr, sizeof (*ai->ai_addr),
			    hbuf, sizeof (hbuf), NULL, 0,
			    NI_NAMEREQD | NI_IDN);
      if (status == 0)
	new_server.host_name = hbuf;

      // Don't resolve to localhost or localhost.localdomain, unless that's
      // what was asked for.
      if ((new_server.host_name == "localhost" ||
	   new_server.host_name == "localhost.localdomain") &&
	  new_server.host_name != server.host_name)
	continue;

      // Add the new resolved server to the list.
      add_server_info (new_server, resolved_servers);
    }

 cleanup:
  if (addr_info)
    freeaddrinfo (addr_info); // free the linked list
  else
    {
      // At a minimum, return the information we were given.
      add_server_info (server, resolved_servers);
    }

  return;
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

static void
get_or_keep_online_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  bool keep
)
{
  // If we're filtering the list and it's already empty, then
  // there's nothing to do.
  if (keep && servers.empty ())
    return;

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

	  // Add it to the list of servers, unless it is duplicate.
	  resolve_host (s, raw_server, online_servers);
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

  if (keep)
    {
      // Filter the existing vector by keeping the information in common with
      // the online_server vector.
      keep_common_server_info (online_servers, servers);
    }
  else
    {
      // Add the information, but not duplicates.
      add_server_info (online_servers, servers);
    }
}

// Add server info to a list, avoiding duplicates. Merge information from
// two duplicate items.
static void
add_server_info (
  const compile_server_info &info, vector<compile_server_info>& target
)
{
  if (info.empty ())
    return;

  bool found = false;
  for (vector<compile_server_info>::iterator i = target.begin ();
       i != target.end ();
       ++i)
    {
      if (info == *i)
	{
	  // Duplicate. Merge the two items.
	  merge_server_info (info, *i);
	  found = true;
	}
    }
  if (! found)
    target.push_back (info);
}

// Add server info from one vector to another.
static void
add_server_info (
  const vector<compile_server_info> &source,
  vector<compile_server_info> &target
)
{
  for (vector<compile_server_info>::const_iterator i = source.begin ();
       i != source.end ();
       ++i)
    {
      add_server_info (*i, target);
    }
}

// Filter the vector by keeping information in common with the item.
static void
keep_common_server_info (
  const	compile_server_info &info_to_keep,
  vector<compile_server_info> &filtered_info
)
{
  assert (! info_to_keep.empty ());

  // The vector may change size as we go. Be careful!!
  for (unsigned i = 0; i < filtered_info.size (); /**/)
    {
      // Retain empty entries.
      if (filtered_info[i].empty ())
	{
	  ++i;
	  continue;
	}
      if (info_to_keep == filtered_info[i])
	{
	  merge_server_info (info_to_keep, filtered_info[i]);
	  ++i;
	  continue;
	}
      // The item does not match. Delete it.
      filtered_info.erase (filtered_info.begin () + i);
      continue;
    }
}

// Filter the second vector by keeping information in common with the first
// vector.
static void
keep_common_server_info (
  const	vector<compile_server_info> &info_to_keep,
  vector<compile_server_info> &filtered_info
)
{
  // The vector may change size as we go. Be careful!!
  for (unsigned i = 0; i < filtered_info.size (); /**/)
    {
      // Retain empty entries.
      if (filtered_info[i].empty ())
	{
	  ++i;
	  continue;
	}
      bool found = false;
      for (unsigned j = 0; j < info_to_keep.size (); ++j)
	{
	  if (filtered_info[i] == info_to_keep[j])
	    {
	      merge_server_info (info_to_keep[j], filtered_info[i]);
	      found = true;
	    }
	}

      // If the item was not found. Delete it. Otherwise, advance to the next
      // item.
      if (found)
	++i;
      else
	filtered_info.erase (filtered_info.begin () + i);
    }
}

// Merge two compile server info items.
static void
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

#if 0 // not used right now
// Merge compile server info from one item into a vector.
static void
merge_server_info (
  const compile_server_info &source,
  vector<compile_server_info> &target
)
{
  for (vector<compile_server_info>::iterator i = target.begin ();
      i != target.end ();
      ++i)
    {
      if (source == *i)
	merge_server_info (source, *i);
    }
}

// Merge compile server from one vector into another.
static void
merge_server_info (
  const vector<compile_server_info> &source,
  vector <compile_server_info> &target
)
{
  for (vector<compile_server_info>::const_iterator i = source.begin ();
      i != source.end ();
      ++i)
    merge_server_info (*i, target);
}
#endif
