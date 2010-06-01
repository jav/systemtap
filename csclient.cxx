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
#include <iostream>

#if HAVE_AVAHI
extern "C" {
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>

#if HAVE_NSS
#include <ssl.h>
#include <nspr.h>
#include <nss.h>
#include <pk11pub.h>
#include <prerror.h>
#include <secerr.h>
#include <sslerr.h>

#include "nsscommon.h"
#endif

#include <stdlib.h>
#include <glob.h>
}
#endif

using namespace std;

#if HAVE_AVAHI
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

    /* Called whenever a service has been resolved successfully or timed out */

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
	    info.ip_address = a;
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

    /* Called whenever a new services becomes available on the LAN or is removed from the LAN */

    switch (event) {
        case AVAHI_BROWSER_FAILURE:
	    cerr << "Avahi browse failed: "
		 << avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b)))
		 << endl;
	    avahi_simple_poll_quit(simple_poll);
	    break;

        case AVAHI_BROWSER_NEW:
            /* We ignore the returned resolver object. In the callback
               function we free it. If the server is terminated before
               the callback function is called the server will free
               the resolver for us. */
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

    /* Called whenever the client or server state changes */

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
systemtap_session::get_online_server_info (
  vector<compile_server_info> &servers
)
{
#if HAVE_AVAHI
    AvahiClient *client = NULL;
    AvahiServiceBrowser *sb = NULL;
    struct timeval tv;
    int error;
    browsing_context context;
    AvahiSimplePoll *simple_poll;

    context.servers = & servers;

    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new())) {
        cerr << "Failed to create simple poll object" << endl;
        goto fail;
    }
    context.simple_poll = simple_poll;

    /* Allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), (AvahiClientFlags)0, client_callback, & context, & error);

    /* Check whether creating the client object succeeded */
    if (!client) {
        cerr << "Failed to create Avahi client: "
	     << avahi_strerror(error)
	     << endl;
        goto fail;
    }
    context.client = client;
    
    /* Create the service browser */
    if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_stap._tcp", NULL, (AvahiLookupFlags)0, browse_callback, & context))) {
        cerr << "Failed to create Avahi service browser: "
	     << avahi_strerror(avahi_client_errno(client))
	     << endl;
        goto fail;
    }

    /* Timeout after 2 seconds.  */
    avahi_simple_poll_get(simple_poll)->timeout_new(
        avahi_simple_poll_get(simple_poll),
        avahi_elapse_time(&tv, 1000*2, 0),
        timeout_callback,
        & context);

    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);
    
fail:
    
    /* Cleanup things */
    if (sb)
        avahi_service_browser_free(sb);
    
    if (client)
        avahi_client_free(client);

    if (simple_poll)
        avahi_simple_poll_free(simple_poll);
#endif // HAVE_AVAHI
}

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
  if (rc != 0 || systemtap_session::pending_interrupts) goto done;
  rc = create_request ();
  if (rc != 0 || systemtap_session::pending_interrupts) goto done;
  rc = package_request ();
  if (rc != 0 || systemtap_session::pending_interrupts) goto done;

  // Submit it to the server.
  rc = find_and_connect_to_server ();
  if (rc != 0 || systemtap_session::pending_interrupts) goto done;

  // Unpack and process the response.
  rc = unpack_response ();
  if (rc != 0 || systemtap_session::pending_interrupts) goto done;
  rc = process_response ();

 done:
  cleanup ();

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
      clog << "Compilation using a server completed "
           << s.getmemusage()
           << TIMESPRINT
           << endl;
    }
#endif // HAVE_NSS

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

	  // Name the script in the packaged arguments.
	  add_package_arg ("script/-");
	}
      else
	{
	  // Add the script to our package. This will also name the script
	  // in the packaged arguments.
	  rc = include_file_or_directory ("script", s.script_file);
	}
    }

  // Add -I paths. Skip the default directory.
  if (s.include_arg_start != -1)
    {
      unsigned limit = s.include_path.size ();
      for (unsigned i = s.include_arg_start; i < limit; ++i)
	{
	  add_package_arg ("-I");
	  rc |= include_file_or_directory ("tapset", s.include_path[i]);
	}
    }

  // Add other options.
  add_package_args ();

  // Add the sysinfo file
  string sysinfo = "sysinfo: " + s.kernel_release + " " + s.architecture;
  write_to_file (client_tmpdir + "/sysinfo", sysinfo);

  return rc;
}

// Add the arguments specified on the command line to the server request
// package, as appropriate.
void
compile_server_client::add_package_args ()
{
  unsigned limit = s.server_args.size();
  for (unsigned i = 0; i < limit; ++i)
    add_package_arg (s.server_args[i]);

  limit = s.args.size();
  for (unsigned i = 0; i < limit; ++i)
    add_package_arg (s.args[i]);
}  

// Symbolically link the given file or directory into the client's temp
// directory under the given subdirectory.
int
compile_server_client::include_file_or_directory (
  const string &subdir, const string &path, const char *option
)
{
  char *cpath = 0;
  string rpath;
  vector<string> components;

  // First ensure that the requested subdirectory exists.
  string name = client_tmpdir + "/" + subdir;
  int rc = create_dir (name.c_str ());
  if (rc) goto done;

  // Now canonicalize the given path and remove the leading /.
  cpath = canonicalize_file_name (path.c_str ());
  if (! cpath)
    {
      rc = 1;
      goto done;
    }
  rpath = cpath + 1;

  // Now ensure that each component of the path exists.
  tokenize (rpath, components, "/");
  assert (components.size () >= 1);
  unsigned i;
  for (i = 0; i < components.size() - 1; ++i)
    {
      name += "/" + components[i];
      rc = create_dir (name.c_str ());
      if (rc) goto done;
    }

  // Now make a symbolic link to the actual file or directory.
  assert (i == components.size () - 1);
  name += "/" + components[i];
  rc = symlink (cpath, name.c_str ());

  // Name this file or directory in the packaged arguments along with any
  // associated option.
  if (option)
    add_package_arg (option);
  add_package_arg (subdir + "/" + rpath);

 done:
  if (cpath)
    free (cpath);

  if (rc != 0)
    {
      const char* e = strerror (errno);
      cerr << "ERROR: unable to add " << cpath << " to temp directory as "
	   << name << ": " << e
	   << endl;
    }

  return rc;
}

void
compile_server_client::add_package_arg (const string &arg)
{
  ostringstream fname;
  fname << client_tmpdir << "/argv" << ++argc;
  write_to_file (fname.str (), arg); // NB: No terminating newline
}

void
compile_server_client::write_to_file (const string &fname, const string &data)
{
  ofstream f (fname.c_str ());
  f << data; // NB: No terminating newline
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
	  s.get_server_info (pmask, default_servers);

	  // Try each server in succession until successful.
	  unsigned limit = default_servers.size ();
	  for (unsigned i = 0; i < limit; ++i)
	    {
	      ++servers;
	      rc = compile_using_server (default_servers[i]);
	      if (rc == 0)
		return rc; // success!
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

// Temporary
extern "C"
int
client_main (const char *hostName, unsigned short port,
	     const char* infileName, const char* outfileName);

int 
compile_server_client::compile_using_server (const compile_server_info &server)
{
  // This code will never be called, if we don't have NSS, but it must still
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
      /* Call the NSPR initialization routines */
      PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

#if 0 /* no client authentication for now. */
      /* Set our password function callback. */
      PK11_SetPasswordFunc (myPasswd);
#endif

      /* Initialize the NSS libraries. */
      SECStatus secStatus = NSS_InitReadWrite (cert_dir);
      if (secStatus != SECSuccess)
	{
	  /* Try it again, readonly.  */
	  secStatus = NSS_Init(cert_dir);
	  if (secStatus != SECSuccess)
	    {
	      cerr << "Error initializing NSS" << endl;
	      goto error;
	    }
	}

      /* All cipher suites except RSA_NULL_MD5 are enabled by Domestic Policy. */
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
  server_tmpdir = s.tmpdir + "/server";
  string cmd = "unzip -qd " + server_tmpdir + " " + server_zipfile;
  int rc = stap_system (s.verbose, cmd);

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
	  goto error;
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
	  errno = 0;
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
	      symlink (oldname.c_str (), newname.c_str ());
	      if (errno != 0)
		goto error;
	    }
	}
    }

  if (s.keep_tmpdir)
    {
      // Remove the output line due to the synthetic server-side -k
      cmd = "sed -i '/^Keeping temporary directory.*/ d' " +
	server_tmpdir + "/stderr";
      stap_system (s.verbose, cmd);
    }

  if (s.last_pass == 5)
    {
      // Remove the output line due to the synthetic server-side -p4
      cmd = "sed -i '/^.*\\.ko$/ d' " + server_tmpdir + "/stdout";
      stap_system (s.verbose, cmd);
    }

  goto done;

 error:
    rc = 1;
 done:
  globfree (& globbuf);
  return rc;
}

int
compile_server_client::process_response ()
{
  // Pick up the results of running stap on the server.
  int rc;
  ifstream fp_in;
  string filename = server_tmpdir + "/rc";
  fp_in.open(filename.c_str (), ios::in);
  fp_in >> rc;
  fp_in.close();

  glob_t globbuf;
  string cmd;
  if (s.last_pass >= 4)
    {
      // The server should have returned a module.
      string filespec = s.tmpdir + "/*.ko";
      if (s.verbose > 1)
	clog << "Searching \"" << filespec << "\"" << endl;
      int r = glob(filespec.c_str (), 0, NULL, & globbuf);
      if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
	{
	  if (globbuf.gl_pathc > 1)
	    {
	      cerr << "Incorrect number of modules in server response" << endl;
	      goto done;
	    }
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
      else if (s.have_script)
	{
	  if (rc == 0)
	    {
	      cerr << "No module was returned by the server" << endl;
	      goto done;
	    }
	}
    }

done:
  if (s.last_pass >= 4)
    globfree (& globbuf);

  // Output stdout and stderr as directed
  filename = server_tmpdir + "/stderr";
  fp_in.open(filename.c_str (), ios::in);
  cerr << fp_in.rdbuf ();
  fp_in.close();

  filename = server_tmpdir + "/stdout";
  fp_in.open(filename.c_str (), ios::in);
  cout << fp_in.rdbuf ();
  fp_in.close();

  return rc;
}
