/*
  SSL server program listens on a port, accepts client connection, reads
  the data into a temporary file, calls the systemtap translator and
  then transmits the resulting file back to the client.

  Copyright (C) 2011-2012 Red Hat Inc.

  This file is part of systemtap, and is free software.  You can
  redistribute it and/or modify it under the terms of the GNU General Public
  License as published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

#include <fstream>
#include <string>
#include <cerrno>
#include <cassert>
#include <climits>
#include <iostream>
#include <map>

extern "C" {
#include <unistd.h>
#include <getopt.h>
#include <wordexp.h>
#include <glob.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <pwd.h>

#include <nspr.h>
#include <ssl.h>
#include <nss.h>
#include <keyhi.h>
#include <regex.h>

#if HAVE_AVAHI
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#endif
}

#include "util.h"
#include "nsscommon.h"
#include "cscommon.h"
#include "cmdline.h"

using namespace std;

static void cleanup ();
static PRStatus spawn_and_wait (const vector<string> &argv,
                                const char* fd0, const char* fd1, const char* fd2,
				const char *pwd, bool setrlimits = false, const vector<string>& envVec = vector<string> ());

/* getopt variables */
extern int optind;

/* File scope statics. Set during argument parsing and initialization. */
static cs_protocol_version client_version;
static bool set_rlimits;
static bool use_db_password;
static int port;
static string cert_db_path;
static string stap_options;
static string uname_r;
static string arch;
static string cert_serial_number;
static string B_options;
static string I_options;
static string R_option;
static string D_options;
static bool   keep_temp;

// Used to save our resource limits for these categories and impose smaller
// limits on the translator while servicing a request.
static struct rlimit our_RLIMIT_FSIZE;
static struct rlimit our_RLIMIT_STACK;
static struct rlimit our_RLIMIT_CPU;
static struct rlimit our_RLIMIT_NPROC;
static struct rlimit our_RLIMIT_AS;

static struct rlimit translator_RLIMIT_FSIZE;
static struct rlimit translator_RLIMIT_STACK;
static struct rlimit translator_RLIMIT_CPU;
static struct rlimit translator_RLIMIT_NPROC;
static struct rlimit translator_RLIMIT_AS;

static string stapstderr;

// Message handling.
// Server_error messages are printed to stderr and logged, if requested.
static void
server_error (const string &msg, int logit = true)
{
  cerr << msg << endl << flush;
  // Log it, but avoid repeated messages to the terminal.
  if (logit && log_ok ())
    log (msg);
}

// client_error messages are treated as server errors and also printed to the client's stderr.
static void
client_error (const string &msg)
{
  server_error (msg);
  if (! stapstderr.empty ())
    {
      ofstream errfile;
      errfile.open (stapstderr.c_str (), ios_base::app);
      if (! errfile.good ())
	server_error (_F("Could not open client stderr file %s: %s", stapstderr.c_str (),
			 strerror (errno)));
      else
	errfile << "Server: " << msg << endl;
      // NB: No need to close errfile
    }
}

// Messages from the nss common code are treated as server errors.
extern "C"
void
nsscommon_error (const char *msg, int logit)
{
  server_error (msg, logit);
}

// Fatal errors are treated as server errors but also result in termination
// of the server.
static void
fatal (const string &msg)
{
  server_error (msg);
  cleanup ();
  exit (1);
}

// Argument handling
static void
process_a (const string &arg)
{
  arch = arg;
  stap_options += " -a " + arg;
}

static void
process_r (const string &arg)
{
  if (arg[0] == '/') // fully specified path
    uname_r = kernel_release_from_build_tree (arg);
  else
    uname_r = arg;
  stap_options += " -r " + arg; // Pass the argument to stap directly.
}

static void
process_log (const char *arg)
{
  start_log (arg);
}

static void
parse_options (int argc, char **argv)
{
  // Examine the command line. This is the command line for us (stap-serverd) not the command
  // line for spawned stap instances.
  optind = 1;
  while (true)
    {
      int long_opt = 0;
      char *num_endptr;
#define LONG_OPT_PORT 1
#define LONG_OPT_SSL 2
#define LONG_OPT_LOG 3
      static struct option long_options[] = {
        { "port", 1, & long_opt, LONG_OPT_PORT },
        { "ssl", 1, & long_opt, LONG_OPT_SSL },
        { "log", 1, & long_opt, LONG_OPT_LOG },
        { NULL, 0, NULL, 0 }
      };
      int grc = getopt_long (argc, argv, "a:B:D:I:kPr:R:", long_options, NULL);
      if (grc < 0)
        break;
      switch (grc)
        {
        case 'a':
	  process_a (optarg);
	  break;
	case 'B':
	  B_options += string (" -") + (char)grc + optarg;
	  stap_options += string (" -") + (char)grc + optarg;
	  break;
	case 'D':
	  D_options += string (" -") + (char)grc + optarg;
	  stap_options += string (" -") + (char)grc + optarg;
	  break;
	case 'I':
	  I_options += string (" -") + (char)grc + optarg;
	  stap_options += string (" -") + (char)grc + optarg;
	  break;
	case 'k':
	  keep_temp = true;
	  break;
	case 'P':
	  use_db_password = true;
	  break;
	case 'r':
	  process_r (optarg);
	  break;
	case 'R':
	  R_option = string (" -") + (char)grc + optarg;
	  stap_options += string (" -") + (char)grc + optarg;
	  break;
	case '?':
	  // Invalid/unrecognized option given. Message has already been issued.
	  break;
        default:
          // Reached when one added a getopt option but not a corresponding switch/case:
          if (optarg)
	    server_error (_F("%s: unhandled option '%c %s'", argv[0], (char)grc, optarg));
          else
	    server_error (_F("%s: unhandled option '%c'", argv[0], (char)grc));
	  break;
        case 0:
          switch (long_opt)
            {
            case LONG_OPT_PORT:
	      port = (int) strtoul (optarg, &num_endptr, 10);
	      break;
            case LONG_OPT_SSL:
	      cert_db_path = optarg;
	      break;
            case LONG_OPT_LOG:
	      process_log (optarg);
	      break;
            default:
	      if (optarg)
		server_error (_F("%s: unhandled option '--%s=%s'", argv[0],
				    long_options[long_opt - 1].name, optarg));
	      else
		server_error (_F("%s: unhandled option '--%s'", argv[0],
				    long_options[long_opt - 1].name));
            }
          break;
        }
    }

  for (int i = optind; i < argc; i++)
    server_error (_F("%s: unrecognized argument '%s'", argv[0], argv[i]));
}

static string
server_cert_file ()
{
  return server_cert_db_path () + "/stap.cert";
}

// Signal handling. When an interrupt is received, kill any spawned processes
// and exit.
extern "C"
void
handle_interrupt (int sig)
{
  // If one of the resource limits that we set for the translator was exceeded, then we can
  // continue, as long as it wasn't our own limit that was exceeded.
  int rc;
  struct rlimit rl;
  switch (sig)
    {
    case SIGXFSZ:
      rc = getrlimit (RLIMIT_FSIZE, & rl);
      if (rc == 0 && rl.rlim_cur < our_RLIMIT_FSIZE.rlim_cur)
	return;
      break;
    case SIGXCPU:
      rc = getrlimit (RLIMIT_CPU, & rl);
      if (rc == 0 && rl.rlim_cur < our_RLIMIT_CPU.rlim_cur)
	return;
      break;
    default:
      break;
    }

  // Otherwise, it's game over.
  log (_F("Received signal %d, exiting", sig));
  kill_stap_spawn (sig);
  cleanup ();
  exit (0);
}

static void
setup_signals (sighandler_t handler)
{
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handler;
  sigemptyset (&sa.sa_mask);
  if (handler != SIG_IGN)
    {
      sigaddset (&sa.sa_mask, SIGHUP);
      sigaddset (&sa.sa_mask, SIGPIPE);
      sigaddset (&sa.sa_mask, SIGINT);
      sigaddset (&sa.sa_mask, SIGTERM);
      sigaddset (&sa.sa_mask, SIGTTIN);
      sigaddset (&sa.sa_mask, SIGTTOU);
      sigaddset (&sa.sa_mask, SIGXFSZ);
      sigaddset (&sa.sa_mask, SIGXCPU);
    }
  sa.sa_flags = SA_RESTART;

  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGPIPE, &sa, NULL);
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGTTIN, &sa, NULL);
  sigaction (SIGTTOU, &sa, NULL);
  sigaction (SIGXFSZ, &sa, NULL);
  sigaction (SIGXCPU, &sa, NULL);
}

#if HAVE_AVAHI
static AvahiEntryGroup *avahi_group = NULL;
static AvahiThreadedPoll *avahi_threaded_poll = NULL;
static char *avahi_service_name = NULL;
static AvahiClient *avahi_client = 0;

static void create_services (AvahiClient *c);

static void
entry_group_callback (
  AvahiEntryGroup *g,
  AvahiEntryGroupState state,
  AVAHI_GCC_UNUSED void *userdata
) {
  assert(g == avahi_group || avahi_group == NULL);
  avahi_group = g;

  // Called whenever the entry group state changes.
  switch (state)
    {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
      // The entry group has been established successfully.
      log (_F("Service '%s' successfully established.", avahi_service_name));
      break;

    case AVAHI_ENTRY_GROUP_COLLISION: {
      char *n;
      // A service name collision with a remote service.
      // happened. Let's pick a new name.
      n = avahi_alternative_service_name (avahi_service_name);
      avahi_free (avahi_service_name);
      avahi_service_name = n;
      server_error (_F("Avahi service name collision, renaming service to '%s'", avahi_service_name));

      // And recreate the services.
      create_services (avahi_entry_group_get_client (g));
      break;
    }

    case AVAHI_ENTRY_GROUP_FAILURE:
      server_error (_F("Avahi entry group failure: %s",
		  avahi_strerror (avahi_client_errno (avahi_entry_group_get_client (g)))));
      // Some kind of failure happened while we were registering our services.
      avahi_threaded_poll_stop (avahi_threaded_poll);
      break;

    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
      break;
    }
}

static void
create_services (AvahiClient *c) {
  assert (c);

  // If this is the first time we're called, let's create a new
  // entry group if necessary.
  if (! avahi_group)
    if (! (avahi_group = avahi_entry_group_new (c, entry_group_callback, NULL)))
      {
	server_error (_F("avahi_entry_group_new () failed: %s",
		    avahi_strerror (avahi_client_errno (c))));
	  goto fail;
      }

  // If the group is empty (either because it was just created, or
  // because it was reset previously, add our entries.
  if (avahi_entry_group_is_empty (avahi_group))
    {
      log (_F("Adding Avahi service '%s'", avahi_service_name));

      // Create the txt tags that will be registered with our service.
      string sysinfo = "sysinfo=" + uname_r + " " + arch;
      string certinfo = "certinfo=" + cert_serial_number;
      string version = string ("version=") + CURRENT_CS_PROTOCOL_VERSION;;
      string optinfo = "optinfo=";
      string separator;
      // These option strings already have a leading space.
      if (! R_option.empty ())
	{
	  optinfo += R_option.substr(1);
	  separator = " ";
	}
      if (! B_options.empty ())
	{
	  optinfo += separator + B_options.substr(1);
	  separator = " ";
	}
      if (! D_options.empty ())
	{
	  optinfo += separator + D_options.substr(1);
	  separator = " ";
	}
      if (! I_options.empty ())
	optinfo += separator + I_options.substr(1);

      // We will now our service to the entry group. Only services with the
      // same name should be put in the same entry group.
      int ret;
      if ((ret = avahi_entry_group_add_service (avahi_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
						(AvahiPublishFlags)0,
						avahi_service_name, "_stap._tcp", NULL, NULL, port,
						sysinfo.c_str (), optinfo.c_str (),
						version.c_str (), certinfo.c_str (), NULL)) < 0)
	{
	  if (ret == AVAHI_ERR_COLLISION)
	    goto collision;

	  server_error (_F("Failed to add _stap._tcp service: %s", avahi_strerror (ret)));
	  goto fail;
	}

      // Tell the server to register the service.
      if ((ret = avahi_entry_group_commit (avahi_group)) < 0)
	{
	  server_error (_F("Failed to commit avahi entry group: %s", avahi_strerror (ret)));
	  goto fail;
	}
    }
  return;

 collision:
  // A service name collision with a local service happened. Let's
  // pick a new name.
  char *n;
  n = avahi_alternative_service_name (avahi_service_name);
  avahi_free(avahi_service_name);
  avahi_service_name = n;
  server_error (_F("Avahi service name collision, renaming service to '%s'", avahi_service_name));
  avahi_entry_group_reset (avahi_group);
  create_services (c);
  return;

 fail:
  avahi_entry_group_reset (avahi_group);
  avahi_threaded_poll_stop (avahi_threaded_poll);
}

static void
client_callback (AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata)
{
  assert(c);

  // Called whenever the client or server state changes.
  switch (state)
    {
    case AVAHI_CLIENT_S_RUNNING:
      // The server has startup successfully and registered its host
      // name on the network, so it's time to create our services.
      create_services (c);
      break;

    case AVAHI_CLIENT_FAILURE:
      server_error (_F("Avahi client failure: %s", avahi_strerror (avahi_client_errno (c))));
      avahi_threaded_poll_stop (avahi_threaded_poll);
      break;

    case AVAHI_CLIENT_S_COLLISION:
      // Let's drop our registered services. When the server is back
      // in AVAHI_SERVER_RUNNING state we will register them
      // again with the new host name.
      // Fall through ...
    case AVAHI_CLIENT_S_REGISTERING:
      // The server records are now being established. This
      // might be caused by a host name change. We need to wait
      // for our own records to register until the host name is
      // properly esatblished.
      if (avahi_group)
	avahi_entry_group_reset (avahi_group);
      break;

    case AVAHI_CLIENT_CONNECTING:
      break;
    }
}
 
static void
avahi_cleanup () {
  if (avahi_service_name)
    log (_F("Removing Avahi service '%s'", avahi_service_name));

  // Stop the avahi client, if it's running
  if (avahi_threaded_poll)
    avahi_threaded_poll_stop (avahi_threaded_poll);

  // Clean up the avahi objects. The order of freeing these is significant.
  if (avahi_group) {
    avahi_entry_group_reset (avahi_group);
    avahi_entry_group_free (avahi_group);
    avahi_group = 0;
  }
  if (avahi_client) {
    avahi_client_free (avahi_client);
    avahi_client = 0;
  }
  if (avahi_threaded_poll) {
    avahi_threaded_poll_free (avahi_threaded_poll);
    avahi_threaded_poll = 0;
  }
  if (avahi_service_name) {
    avahi_free (avahi_service_name);
    avahi_service_name = 0;
  }
}

// The entry point for the avahi client thread.
static void
avahi_publish_service (CERTCertificate *cert)
{
  cert_serial_number = get_cert_serial_number (cert);

  string buf = "Systemtap Compile Server, pid=" + lex_cast (getpid ());
  avahi_service_name = avahi_strdup (buf.c_str ());

  // Allocate main loop object.
  if (! (avahi_threaded_poll = avahi_threaded_poll_new ()))
    {
      server_error (_("Failed to create avahi threaded poll object."));
      return;
    }

  // Always allocate a new client.
  int error;
  avahi_client = avahi_client_new (avahi_threaded_poll_get (avahi_threaded_poll),
				   (AvahiClientFlags)0,
				   client_callback, NULL, & error);
  // Check wether creating the client object succeeded.
  if (! avahi_client)
    {
      server_error (_F("Failed to create avahi client: %s", avahi_strerror(error)));
      return;
    }

  // Run the main loop.
  avahi_threaded_poll_start (avahi_threaded_poll);

  return;
}
#endif // HAVE_AVAHI

static void
advertise_presence (CERTCertificate *cert __attribute ((unused)))
{
#if HAVE_AVAHI
  avahi_publish_service (cert);
#else
  server_error (_("Unable to advertise presence on the network. Avahi is not available"));
#endif
}

static void
unadvertise_presence ()
{
#if HAVE_AVAHI
  avahi_cleanup ();
#endif
}

static void
initialize (int argc, char **argv) {
  setup_signals (& handle_interrupt);

  // Seed the random number generator. Used to generate noise used during key generation.
  srand (time (NULL));

  // Initial values.
  client_version = "1.0"; // Assumed until discovered otherwise
  use_db_password = false;
  port = 0;
  keep_temp = false;
  struct utsname utsname;
  uname (& utsname);
  uname_r = utsname.release;
  arch = normalize_machine (utsname.machine);

  // Parse the arguments. This also starts the server log, if any, and should be done before
  // any messages are issued.
  parse_options (argc, argv);

  // PR11197: security prophylactics.
  // 1) Reject use as root, except via a special environment variable.
  if (! getenv ("STAP_PR11197_OVERRIDE")) {
    if (geteuid () == 0)
      fatal ("For security reasons, invocation of stap-serverd as root is not supported.");
  }
  // 2) resource limits should be set if the user is the 'stap-server' daemon.
  struct passwd *pw = getpwuid (geteuid ());
  if (! pw)
    fatal (_F("Unable to determine effective user name: %s", strerror (errno)));
  string username = pw->pw_name;
  if (username == "stap-server") {
    // First obtain the current limits.
    int rc = getrlimit (RLIMIT_FSIZE, & our_RLIMIT_FSIZE);
    rc |= getrlimit (RLIMIT_STACK, & our_RLIMIT_STACK);
    rc |= getrlimit (RLIMIT_CPU,   & our_RLIMIT_CPU);
    rc |= getrlimit (RLIMIT_NPROC, & our_RLIMIT_NPROC);
    rc |= getrlimit (RLIMIT_AS,    & our_RLIMIT_AS);
    if (rc != 0)
      fatal (_F("Unable to obtain current resource limits: %s", strerror (errno)));

    // Now establish limits for the translator. Make sure these limits do not exceed the current
    // limits.
    #define TRANSLATOR_LIMIT(category, limit) \
      do { \
	translator_RLIMIT_##category = our_RLIMIT_##category; \
	if (translator_RLIMIT_##category.rlim_cur > (limit)) \
	  translator_RLIMIT_##category.rlim_cur = (limit); \
      } while (0);
    TRANSLATOR_LIMIT (FSIZE, 50000 * 1024);
    TRANSLATOR_LIMIT (STACK, 1000 * 1024);
    TRANSLATOR_LIMIT (CPU, 60);
    TRANSLATOR_LIMIT (NPROC, 20);
    TRANSLATOR_LIMIT (AS, 500000 * 1024);
    set_rlimits = true;
    #undef TRANSLATOR_LIMIT
  }
  else
    set_rlimits = false;

  pid_t pid = getpid ();
  log (_F("===== compile server pid %d starting as %s =====", pid, username.c_str ()));

  // Where is the ssl certificate/key database?
  if (cert_db_path.empty ())
    cert_db_path = server_cert_db_path ();

  // Make sure NSPR is initialized. Must be done before NSS is initialized
  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
  /* Set the cert database password callback. */
  PK11_SetPasswordFunc (nssPasswordCallback);
}

static void
cleanup ()
{
  unadvertise_presence ();
  end_log ();
}

/* Function:  readDataFromSocket()
 *
 * Purpose:  Read data from the socket into a temporary file.
 *
 */
static PRInt32
readDataFromSocket(PRFileDesc *sslSocket, const char *requestFileName)
{
  PRFileDesc *local_file_fd = 0;
  PRInt32     numBytesExpected;
  PRInt32     numBytesRead;
  PRInt32     numBytesWritten;
  PRInt32     totalBytes = 0;
#define READ_BUFFER_SIZE 4096
  char        buffer[READ_BUFFER_SIZE];

  /* Read the number of bytes to be received.  */
  /* XXX: impose a limit to prevent disk space consumption DoS */
  numBytesRead = PR_Read (sslSocket, & numBytesExpected, sizeof (numBytesExpected));
  if (numBytesRead == 0) /* EOF */
    {
      server_error (_("Error reading size of request file"));
      goto done;
    }
  if (numBytesRead < 0)
    {
      server_error (_("Error in PR_Read"));
      nssError ();
      goto done;
    }

  /* Convert numBytesExpected from network byte order to host byte order.  */
  numBytesExpected = ntohl (numBytesExpected);

  /* If 0 bytes are expected, then we were contacted only to obtain our certificate.
     There is no client request. */
  if (numBytesExpected == 0)
    return 0;

  /* Open the output file.  */
  local_file_fd = PR_Open(requestFileName, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
			  PR_IRUSR | PR_IWUSR);
  if (local_file_fd == NULL)
    {
      server_error (_F("Could not open output file %s", requestFileName));
      nssError ();
      return -1;
    }

  /* Read until EOF or until the expected number of bytes has been read. */
  for (totalBytes = 0; totalBytes < numBytesExpected; totalBytes += numBytesRead)
    {
      numBytesRead = PR_Read(sslSocket, buffer, READ_BUFFER_SIZE);
      if (numBytesRead == 0)
	break;	/* EOF */
      if (numBytesRead < 0)
	{
	  server_error (_("Error in PR_Read"));
	  nssError ();
	  goto done;
	}

      /* Write to the request file. */
      numBytesWritten = PR_Write(local_file_fd, buffer, numBytesRead);
      if (numBytesWritten < 0 || (numBytesWritten != numBytesRead))
        {
          server_error (_F("Could not write to output file %s", requestFileName));
	  nssError ();
	  goto done;
        }
    }

  if (totalBytes != numBytesExpected)
    {
      server_error (_F("Expected %d bytes, got %d while reading client request from socket",
			  numBytesExpected, totalBytes));
      goto done;
    }

 done:
  if (local_file_fd)
    PR_Close (local_file_fd);
  return totalBytes;
}

/* Function:  setupSSLSocket()
 *
 * Purpose:  Configure a socket for SSL.
 *
 *
 */
static PRFileDesc * 
setupSSLSocket (PRFileDesc *tcpSocket, CERTCertificate *cert, SECKEYPrivateKey *privKey)
{
  PRFileDesc *sslSocket;
  SSLKEAType  certKEA;
  SECStatus   secStatus;

  /* Inport the socket into SSL.  */
  sslSocket = SSL_ImportFD (NULL, tcpSocket);
  if (sslSocket == NULL)
    {
      server_error (_("Could not import socket into SSL"));
      nssError ();
      return NULL;
    }
   
  /* Set the appropriate flags. */
  secStatus = SSL_OptionSet (sslSocket, SSL_SECURITY, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error setting SSL security for socket"));
      nssError ();
      return NULL;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_SERVER, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error setting handshake as server for socket"));
      nssError ();
      return NULL;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_REQUEST_CERTIFICATE, PR_FALSE);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error setting SSL client authentication mode for socket"));
      nssError ();
      return NULL;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_REQUIRE_CERTIFICATE, PR_FALSE);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error setting SSL client authentication mode for socket"));
      nssError ();
      return NULL;
    }

  /* Set the appropriate callback routines. */
#if 0 /* use the default */
  secStatus = SSL_AuthCertificateHook (sslSocket, myAuthCertificate, CERT_GetDefaultCertDB());
  if (secStatus != SECSuccess)
    {
      nssError ();
      server_error (_("Error in SSL_AuthCertificateHook"));
      return NULL;
    }
#endif
#if 0 /* Use the default */
  secStatus = SSL_BadCertHook(sslSocket, (SSLBadCertHandler)myBadCertHandler, &certErr);
  if (secStatus != SECSuccess)
    {
      nssError ();
      server_error (_("Error in SSL_BadCertHook"));
      return NULL;
    }
#endif
#if 0 /* no handshake callback */
  secStatus = SSL_HandshakeCallback(sslSocket, myHandshakeCallback, NULL);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error in SSL_HandshakeCallback"));
      nssError ();
      return NULL;
    }
#endif

  certKEA = NSS_FindCertKEAType (cert);

  secStatus = SSL_ConfigSecureServer (sslSocket, cert, privKey, certKEA);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error configuring SSL server"));
      nssError ();
      return NULL;
    }

  return sslSocket;
}

#if 0 /* No client authentication (for now) and not authenticating after each transaction.  */
/* Function:  authenticateSocket()
 *
 * Purpose:  Perform client authentication on the socket.
 *
 */
static SECStatus
authenticateSocket (PRFileDesc *sslSocket, PRBool requireCert)
{
  CERTCertificate *cert;
  SECStatus secStatus;

  /* Returns NULL if client authentication is not enabled or if the
   * client had no certificate. */
  cert = SSL_PeerCertificate(sslSocket);
  if (cert)
    {
      /* Client had a certificate, so authentication is through. */
      CERT_DestroyCertificate(cert);
      return SECSuccess;
    }

  /* Request client to authenticate itself. */
  secStatus = SSL_OptionSet(sslSocket, SSL_REQUEST_CERTIFICATE, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error in SSL_OptionSet:SSL_REQUEST_CERTIFICATE"));
      nssError ();
      return SECFailure;
    }

  /* If desired, require client to authenticate itself.  Note
   * SSL_REQUEST_CERTIFICATE must also be on, as above.  */
  secStatus = SSL_OptionSet(sslSocket, SSL_REQUIRE_CERTIFICATE, requireCert);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error in SSL_OptionSet:SSL_REQUIRE_CERTIFICATE"));
      nssError ();
      return SECFailure;
    }

  /* Having changed socket configuration parameters, redo handshake. */
  secStatus = SSL_ReHandshake(sslSocket, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error in SSL_ReHandshake"));
      nssError ();
      return SECFailure;
    }

  /* Force the handshake to complete before moving on. */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error in SSL_ForceHandshake"));
      nssError ();
      return SECFailure;
    }

  return SECSuccess;
}
#endif /* No client authentication and not authenticating after each transaction.  */

/* Function:  writeDataToSocket
 *
 * Purpose:  Write the server's response back to the socket.
 *
 */
static SECStatus
writeDataToSocket(PRFileDesc *sslSocket, const char *responseFileName)
{
  PRFileDesc *local_file_fd = PR_Open (responseFileName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      server_error (_F("Could not open input file %s", responseFileName));
      nssError ();
      return SECFailure;
    }

  /* Transmit the local file across the socket.
   */
  int numBytes = PR_TransmitFile (sslSocket, local_file_fd, 
				  NULL, 0,
				  PR_TRANSMITFILE_KEEP_OPEN,
				  PR_INTERVAL_NO_TIMEOUT);

  /* Error in transmission. */
  SECStatus secStatus = SECSuccess;
  if (numBytes < 0)
    {
      server_error (_("Error writing response to socket"));
      nssError ();
      secStatus = SECFailure;
    }

  PR_Close (local_file_fd);
  return secStatus;
}

static void
get_stap_locale (const string &staplang, vector<string> &envVec)
{
  // If the client version is < 1.6, then no file containing environment
  // variables defining the locale has been passed.
  if (client_version < "1.6")
    return;

  /* Go through each line of the file, verify it, then add it to the vector */
  ifstream langfile;
  langfile.open(staplang.c_str());
  if (!langfile.is_open())
    {
      // Not fatal. Proceed with the environment we have.
      server_error(_F("Unable to open file %s for reading: %s", staplang.c_str(),
			 strerror (errno)));
      return;
    }

  /* Unpackage internationalization variables and verify their contents */
  map<string, string> envMap; /* To temporarily store the entire array of strings */
  string line;
  const set<string> &locVars = localization_variables();

  /* Copy the global environ variable into the map */
   if(environ != NULL)
     {
      for (unsigned i=0; environ[i]; i++)
        {
          string line = (string)environ[i];

          /* Find the first '=' sign */
          size_t pos = line.find("=");

          /* Make sure it found an '=' sign */
          if(pos != string::npos)
            /* Everything before the '=' sign is the key, and everything after is the value. */ 
            envMap[line.substr(0, pos)] = line.substr(pos+1); 
        }
     }

  /* Create regular expression objects to verify lines read from file. Should not allow
     spaces, ctrl characters, etc */
  regex_t checkre;
  if ((regcomp(&checkre, "^[a-zA-Z0-9@_.=-]*$", REG_EXTENDED | REG_NOSUB) != 0))
    {
      // Not fatal. Proceed with the environment we have.
      server_error(_F("Error in regcomp: %s", strerror (errno)));
      return;
    }

  while (1)
    {
      getline(langfile, line);
      if (!langfile.good())
	break;

      /* Extract key and value from the line. Note: value may contain "=". */
      string key;
      string value;
      size_t pos;
      pos = line.find("=");
      if (pos == string::npos)
        {
          client_error(_F("Localization key=value line '%s' cannot be parsed", line.c_str()));
	  continue;
        }
      key = line.substr(0, pos);
      pos++;
      value = line.substr(pos);

      /* Make sure the key is found in the localization variables global set */
      if (locVars.find(key) == locVars.end())
	{
	  // Not fatal. Just ignore it.
	  client_error(_F("Localization key '%s' not found in global list", key.c_str()));
	  continue;
	}

      /* Make sure the value does not contain illegal characters */
      if ((regexec(&checkre, value.c_str(), (size_t) 0, NULL, 0) != 0))
	{
	  // Not fatal. Just ignore it.
	  client_error(_F("Localization value '%s' contains illegal characters", value.c_str()));
	  continue;
	}

      /* All is good, copy line into envMap, replacing if already there */
      envMap[key] = value;
    }

  if (!langfile.eof())
    {
      // Not fatal. Proceed with what we have.
      server_error(_F("Error reading file %s: %s", staplang.c_str(), strerror (errno)));
    }

  regfree(&checkre);

  /* Copy map into vector */
  for (map<string, string>::iterator it = envMap.begin(); it != envMap.end(); it++)
    envVec.push_back(it->first + "=" + it->second);
}

// Filter paths prefixed with the server's home directory from the given file.
//
static void
filter_response_file (const string &file_name, const string &responseDirName)
{
  vector<string> cmd;

  // Filter the server's home directory name
  cmd.clear();
  cmd.push_back ("sed");
  cmd.push_back ("-i");
  cmd.push_back (string ("s,") + get_home_directory () + ",<server>,g");
  cmd.push_back (file_name);
  stap_system (0, cmd);

  // Filter the server's response directory name
  cmd.clear();
  cmd.push_back ("sed");
  cmd.push_back ("-i");
  cmd.push_back (string ("s,") + responseDirName + ",<server>,g");
  cmd.push_back (file_name);
  stap_system (0, cmd);
}

static privilege_t
getRequestedPrivilege (const vector<string> &stapargv)
{
  // The purpose of this function is to find the --privilege or --unprivileged option specified
  // by the user on the client side. We need to parse the command line completely, but we can
  // exit when we find the first --privilege or --unprivileged option, since stap does not allow
  // multiple privilege levels to specified on the same command line.
  //
  // Note that we need not do any options consistency checking since our spawned stap instance
  // will do that.
  //
  // Create an argv/argc for use by getopt_long.
  int argc = stapargv.size();
  char ** argv = new char *[argc + 1];
  for (unsigned i = 0; i < stapargv.size(); ++i)
    argv[i] = (char *)stapargv[i].c_str();
  argv[argc] = NULL;

  privilege_t privilege = pr_highest; // Until specified otherwise.
  optind = 1;
  while (true)
    {
      // We need only allow getopt to parse the options until we find a
      // --privilege or --unprivileged option.
      int grc = getopt_long (argc, argv, STAP_SHORT_OPTIONS, stap_long_options, NULL);
      if (grc < 0)
        break;
      switch (grc)
        {
	default:
	  // We can ignore all short options
	  break;
        case 0:
          switch (stap_long_opt)
            {
	    default:
	      // We can ignore all options other than --privilege and --unprivileged.
	      break;
	    case LONG_OPT_PRIVILEGE:
	      if (strcmp (optarg, "stapdev") == 0)
		privilege = pr_stapdev;
	      else if (strcmp (optarg, "stapsys") == 0)
		privilege = pr_stapsys;
	      else if (strcmp (optarg, "stapusr") == 0)
		privilege = pr_stapusr;
	      else
		{
		  server_error (_F("Invalid argument '%s' for --privilege", optarg));
		  privilege = pr_highest;
		}
	      // We have discovered the client side --privilege option. We can exit now since
	      // stap only tolerates one privilege setting option.
	      goto done; // break 2 switches and a loop
	    case LONG_OPT_UNPRIVILEGED:
	      privilege = pr_unprivileged;
	      // We have discovered the client side --unprivileged option. We can exit now since
	      // stap only tolerates one privilege setting option.
	      goto done; // break 2 switches and a loop
	    }
	}
    }
 done:
  delete[] argv;
  return privilege;
}

/* Run the translator on the data in the request directory, and produce output
   in the given output directory. */
static void
handleRequest (const string &requestDirName, const string &responseDirName)
{
  vector<string> stapargv;
  int rc;
  wordexp_t words;
  unsigned u;
  unsigned i;
  FILE* f;

  // Save the server version. Do this early, so the client knows what version of the server
  // it is dealing with, even if the request is not fully completed.
  string stapversion = responseDirName + "/version";
  f = fopen (stapversion.c_str (), "w");
  if (f) 
    {
      fputs (CURRENT_CS_PROTOCOL_VERSION, f);
      fclose(f);
    }
  else
    server_error (_F("Unable to open client version file %s", stapversion.c_str ()));

  // Get the client version. The default version is already set. Use it if we fail here.
  string filename = requestDirName + "/version";
  if (file_exists (filename))
    read_from_file (filename, client_version);
  log (_F("Client version is %s", client_version.v));

  // The name of the translator executable.
  stapargv.push_back ((char *)(getenv ("SYSTEMTAP_STAP") ?: STAP_PREFIX "/bin/stap"));

  /* Transcribe stap_options.  We use plain wordexp(3), since these
     options are coming from the local trusted user, so malicious
     content is not a concern. */
  // TODO: Use tokenize here.
  rc = wordexp (stap_options.c_str (), & words, WRDE_NOCMD|WRDE_UNDEF);
  if (rc)
    {
      server_error (_("Cannot parse stap options"));
      return;
    }

  for (u=0; u<words.we_wordc; u++)
    stapargv.push_back (words.we_wordv[u]);

  /* Process the saved command line arguments.  Avoid quoting/unquoting errors by
     transcribing literally. */
  string new_staptmpdir = responseDirName + "/stap000000";
  rc = mkdir(new_staptmpdir.c_str(), 0700);
  if (rc)
    server_error(_F("Could not create temporary directory %s", new_staptmpdir.c_str()));

  stapargv.push_back("--tmpdir=" + new_staptmpdir);

  stapargv.push_back ("--client-options");
  for (i=1 ; ; i++)
    {
      char stapargfile[PATH_MAX];
      FILE* argfile;
      struct stat st;
      char *arg;

      snprintf (stapargfile, PATH_MAX, "%s/argv%d", requestDirName.c_str (), i);

      rc = stat(stapargfile, & st);
      if (rc) break;

      arg = (char *)malloc (st.st_size+1);
      if (!arg)
        {
          server_error (_("Out of memory"));
          return;
        }

      argfile = fopen(stapargfile, "r");
      if (! argfile)
        {
          free(arg);
          server_error (_F("Error opening %s: %s", stapargfile, strerror (errno)));
          return;
        }

      rc = fread(arg, 1, st.st_size, argfile);
      if (rc != st.st_size)
        {
          free(arg);
          fclose(argfile);
          server_error (_F("Error reading %s: %s", stapargfile, strerror (errno)));
          return;
        }

      arg[st.st_size] = '\0';
      stapargv.push_back (arg);
      free (arg);
      fclose (argfile);
    }

  string stapstdout = responseDirName + "/stdout";

  // NB: Before, when we did not fully parse the client's command line using getopt_long,
  // we used to insert a --privilege=XXX option here in case some other argument was mistaken
  // for a --privilege or --unprivileged option by our spawned stap. Since we now parse
  // the client's command line using getopt_long and share the getopt_long options
  // string and table with stap, this is no longer necessary. stap will parse the
  // command line identically to the way we have parsed it and will discover the same
  // privilege-setting option.

  // Environment variables (possibly empty) to be passed to spawn_and_wait().
  string staplang = requestDirName + "/locale";
  vector<string> envVec;
  get_stap_locale (staplang, envVec);

  /* All ready, let's run the translator! */
  rc = spawn_and_wait(stapargv, "/dev/null", stapstdout.c_str (), stapstderr.c_str (),
		      requestDirName.c_str (), set_rlimits, envVec);

  /* Save the RC */
  string staprc = responseDirName + "/rc";
  f = fopen(staprc.c_str (), "w");
  if (f) 
    {
      /* best effort basis */
      fprintf(f, "%d", rc);
      fclose(f);
    }

  // In unprivileged modes, if we have a module built, we need to sign the sucker.
  privilege_t privilege = getRequestedPrivilege (stapargv);
  if (pr_contains (privilege, pr_stapusr) || pr_contains (privilege, pr_stapsys))
    {
      glob_t globber;
      char pattern[PATH_MAX];
      snprintf (pattern, PATH_MAX, "%s/*.ko", new_staptmpdir.c_str());
      rc = glob (pattern, GLOB_ERR, NULL, &globber);
      if (rc)
        server_error (_F("Unable to find a module in %s", new_staptmpdir.c_str()));
      else if (globber.gl_pathc != 1)
        server_error (_F("Too many modules (%zu) in %s", globber.gl_pathc, new_staptmpdir.c_str()));
      else
        {
         sign_file (cert_db_path, server_cert_nickname(),
                    globber.gl_pathv[0], string(globber.gl_pathv[0]) + ".sgn");
        }
    }

  /* If uprobes.ko is required, it will have been built or cache-copied into
   * the temp directory.  We need to pack it into the response where the client
   * can find it, and sign, if necessary, for unprivileged users.
   */
  string uprobes_ko = new_staptmpdir + "/uprobes/uprobes.ko";
  if (get_file_size(uprobes_ko) > 0)
    {
      /* uprobes.ko is required.
       *
       * It's already underneath the stap tmpdir, but older stap clients
       * don't know to look for it there, so, for these clients, we end up packing uprobes twice
       * into the zip.  We could move instead of symlink.
       */
      string uprobes_response;
      if (client_version < "1.6")
	{
	  uprobes_response = (string)responseDirName + "/uprobes.ko";
	  rc = symlink(uprobes_ko.c_str(), uprobes_response.c_str());
	  if (rc != 0)
	    server_error (_F("Could not link to %s from %s",
			     uprobes_ko.c_str(), uprobes_response.c_str()));
	}
      else
	uprobes_response = uprobes_ko;

      /* In unprivileged mode, we need a signature on uprobes as well. */
      if (! pr_contains (privilege, pr_stapdev))
        {
          sign_file (cert_db_path, server_cert_nickname(),
                     uprobes_response, uprobes_response + ".sgn");
        }

    }

  /* Free up all the arg string copies.  Note that the first few were alloc'd
     by wordexp(), which wordfree() frees; others were hand-set to literal strings. */
  wordfree (& words);

  // Filter paths prefixed with the server's home directory from the stdout and stderr
  // files in the response.
  filter_response_file (stapstdout, responseDirName);
  filter_response_file (stapstderr, responseDirName);

  /* Sorry about the inconvenience.  C string/file processing is such a pleasure. */
}


/* A front end for stap_spawn that handles stdin, stdout, stderr, switches to a working
   directory and returns overall success or failure. */
static PRStatus
spawn_and_wait (const vector<string> &argv,
		const char* fd0, const char* fd1, const char* fd2,
		const char *pwd,  bool setrlimits, const vector<string>& envVec)
{ 
  pid_t pid;
  int rc;
  posix_spawn_file_actions_t actions;
  int dotfd = -1;

#define CHECKRC(msg) do { if (rc) { server_error (_(msg)); return PR_FAILURE; } } while (0)

  rc = posix_spawn_file_actions_init (& actions);
  CHECKRC ("Error in spawn file actions ctor");
  if (fd0) {
    rc = posix_spawn_file_actions_addopen(& actions, 0, fd0, O_RDONLY, 0600);
    CHECKRC ("Error in spawn file actions fd0");
  }
  if (fd1) {
    rc = posix_spawn_file_actions_addopen(& actions, 1, fd1, O_WRONLY|O_CREAT, 0600);
    CHECKRC ("Error in spawn file actions fd1");
  }
  if (fd2) { 
    // Use append mode for stderr because it gets written to in other places in the server.
    rc = posix_spawn_file_actions_addopen(& actions, 2, fd2, O_WRONLY|O_APPEND|O_CREAT, 0600);
    CHECKRC ("Error in spawn file actions fd2");
  }

  /* change temporarily to a directory if requested */
  if (pwd)
    {
      dotfd = open (".", O_RDONLY);
      if (dotfd < 0)
        {
          server_error (_("Error in spawn getcwd"));
          return PR_FAILURE;
        }

      rc = chdir (pwd);
      if (rc)
        {
          close(dotfd);
          server_error(_("Error in spawn chdir"));
          return PR_FAILURE;
        }
    }

  // Set resource limits, if requested, in order to prevent
  // DOS. spawn_and_wait ultimately uses posix_spawp which behaves like
  // fork (according to the posix_spawnbp man page), so the limits we set here will be
  // respected (according to the setrlimit man page).
  rc = 0;
  if (setrlimits) {
    rc = setrlimit (RLIMIT_FSIZE, & translator_RLIMIT_FSIZE);
    rc |= setrlimit (RLIMIT_STACK, & translator_RLIMIT_STACK);
    rc |= setrlimit (RLIMIT_CPU,   & translator_RLIMIT_CPU);
    rc |= setrlimit (RLIMIT_NPROC, & translator_RLIMIT_NPROC);
    rc |= setrlimit (RLIMIT_AS,    & translator_RLIMIT_AS);
  }
  if (rc == 0)
    {
      pid = stap_spawn (0, argv, & actions, envVec);
      /* NB: don't react to pid==-1 right away; need to chdir back first. */
    }
  else {
    server_error (_F("Unable to set resource limits for %s: %s",
			argv[0].c_str (), strerror (errno)));
    pid = -1;
  }
  if (set_rlimits) {
    int rrlrc = setrlimit (RLIMIT_FSIZE, & our_RLIMIT_FSIZE);
    rrlrc |= setrlimit (RLIMIT_STACK, & our_RLIMIT_STACK);
    rrlrc |= setrlimit (RLIMIT_CPU,   & our_RLIMIT_CPU);
    rrlrc |= setrlimit (RLIMIT_NPROC, & our_RLIMIT_NPROC);
    rrlrc |= setrlimit (RLIMIT_AS,    & our_RLIMIT_AS);
    if (rrlrc != 0)
      log (_F("Unable to restore resource limits after %s: %s",
	      argv[0].c_str (), strerror (errno)));
  }

  if (pwd && dotfd >= 0)
    {
      int subrc;
      subrc = fchdir (dotfd);
      subrc |= close (dotfd);
      if (subrc) 
        server_error (_("Error in spawn unchdir"));
    }

  if (pid == -1)
    {
      server_error (_F("Error in spawn: %s", strerror (errno)));
      return PR_FAILURE;
    }

  rc = stap_waitpid (0, pid);
  if (rc == -1)
    {
      server_error (_("Error in waitpid"));
      return PR_FAILURE;
    }

  rc = posix_spawn_file_actions_destroy (&actions);
  CHECKRC ("Error in spawn file actions dtor");

  return PR_SUCCESS;
#undef CHECKRC
}

/* Function:  int handle_connection()
 *
 * Purpose: Handle a connection to a socket.  Copy in request zip
 * file, process it, copy out response.  Temporary directories are
 * created & destroyed here.
 */
static SECStatus
handle_connection (PRFileDesc *tcpSocket, CERTCertificate *cert, SECKEYPrivateKey *privKey)
{
  PRFileDesc *       sslSocket = NULL;
  SECStatus          secStatus = SECFailure;
  int                rc;
  char              *rc1;
  char               tmpdir[PATH_MAX];
  char               requestFileName[PATH_MAX];
  char               requestDirName[PATH_MAX];
  char               responseDirName[PATH_MAX];
  char               responseFileName[PATH_MAX];
  vector<string>     argv;
  PRInt32            bytesRead;

  tmpdir[0]='\0'; /* prevent cleanup-time /bin/rm of uninitialized directory */

#if 0 // already done on the listenSocket
  /* Make sure the socket is blocking. */
  PRSocketOptionData socketOption;
  socketOption.option             = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;
  PR_SetSocketOption (tcpSocket, &socketOption);
#endif
  secStatus = SECFailure;
  sslSocket = setupSSLSocket (tcpSocket, cert, privKey);
  if (sslSocket == NULL)
    {
      // Message already issued.
      goto cleanup;
    }

  secStatus = SSL_ResetHandshake(sslSocket, /* asServer */ PR_TRUE);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error resetting SSL handshake"));
      nssError ();
      goto cleanup;
    }

#if 0 // The client authenticates the server, so the client initiates the handshake
  /* Force the handshake to complete before moving on. */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    {
      server_error (_("Error forcing SSL handshake"));
      nssError ();
      goto cleanup;
    }
#endif

  secStatus = SECFailure;
  snprintf(tmpdir, PATH_MAX, "%s/stap-server.XXXXXX", getenv("TMPDIR") ?: "/tmp");
  rc1 = mkdtemp(tmpdir);
  if (! rc1)
    {
      server_error (_F("Could not create temporary directory %s: %s", tmpdir, strerror(errno)));
      tmpdir[0]=0; /* prevent /bin/rm */
      goto cleanup;
    }

  /* Create a temporary files names and directories.  */
  snprintf (requestFileName, PATH_MAX, "%s/request.zip", tmpdir);

  snprintf (requestDirName, PATH_MAX, "%s/request", tmpdir);
  rc = mkdir(requestDirName, 0700);
  if (rc)
    {
      server_error (_F("Could not create temporary directory %s: %s", requestDirName, strerror (errno)));
      goto cleanup;
    }

  snprintf (responseDirName, PATH_MAX, "%s/response", tmpdir);
  rc = mkdir(responseDirName, 0700);
  if (rc)
    {
      server_error (_F("Could not create temporary directory %s: %s", responseDirName, strerror (errno)));
      goto cleanup;
    }
  // Set this early, since it gets used for errors to be returned to the client.
  stapstderr = string(responseDirName) + "/stderr";

  snprintf (responseFileName, PATH_MAX, "%s/response.zip", tmpdir);

  /* Read data from the socket.
   * If the user is requesting/requiring authentication, authenticate
   * the socket.  */
  bytesRead = readDataFromSocket(sslSocket, requestFileName);
  if (bytesRead < 0) // Error
    goto cleanup;
  if (bytesRead == 0) // No request -- not an error
    {
      secStatus = SECSuccess;
      goto cleanup;
    }

#if 0 /* Don't authenticate after each transaction */
  if (REQUEST_CERT_ALL)
    {
      secStatus = authenticateSocket(sslSocket);
      if (secStatus != SECSuccess)
	goto cleanup;
    }
#endif

  /* Unzip the request. */
  secStatus = SECFailure;
  argv.push_back ("unzip");
  argv.push_back ("-q");
  argv.push_back ("-d");
  argv.push_back (requestDirName);
  argv.push_back (requestFileName);
  rc = stap_system (0, argv);
  if (rc != 0)
    {
      server_error (_("Unable to extract client request"));
      goto cleanup;
    }

  /* Handle the request zip file.  An error therein should still result
     in a response zip file (containing stderr etc.) so we don't have to
     have a result code here.  */
  handleRequest(requestDirName, responseDirName);

  /* Zip the response. */
  argv.clear ();
  argv.push_back ("zip");
  argv.push_back ("-q");
  argv.push_back ("-r");
  argv.push_back (responseFileName);
  argv.push_back (".");
  rc = spawn_and_wait (argv, NULL, NULL, NULL, responseDirName);
  if (rc != PR_SUCCESS)
    {
      server_error (_("Unable to compress server response"));
      goto cleanup;
    }
  
  secStatus = writeDataToSocket (sslSocket, responseFileName);

cleanup:
  if (sslSocket)
    if (PR_Close (sslSocket) != PR_SUCCESS)
      {
	server_error (_("Error closing ssl socket"));
	nssError ();
      }

  if (tmpdir[0]) 
    {
      // Remove the whole tmpdir and all that lies beneath, unless -k was specified.
      if (keep_temp) 
	log (_F("Keeping temporary directory %s", tmpdir));
      else
	{
	  argv.clear ();
	  argv.push_back ("rm");
	  argv.push_back ("-r");
	  argv.push_back (tmpdir);
	  rc = stap_system (0, argv);
	  if (rc != 0)
	    server_error (_("Error in tmpdir cleanup"));
	}
    }

  return secStatus;
}

/* Function:  int accept_connection()
 *
 * Purpose:  Accept a connection to the socket.
 *
 */
static SECStatus
accept_connections (PRFileDesc *listenSocket, CERTCertificate *cert)
{
  PRNetAddr   addr;
  PRFileDesc *tcpSocket;
  SECStatus   secStatus;
  CERTCertDBHandle *dbHandle;

  dbHandle = CERT_GetDefaultCertDB ();

  // cert_db_path gets passed to nssPasswordCallback.
  SECKEYPrivateKey *privKey = PK11_FindKeyByAnyCert (cert, (void*)cert_db_path.c_str ());
  if (privKey == NULL)
    {
      server_error (_("Unable to obtain certificate private key"));
      nssError ();
      return SECFailure;
    }

  while (PR_TRUE)
    {
      /* Accept a connection to the socket. */
      tcpSocket = PR_Accept (listenSocket, &addr, PR_INTERVAL_NO_TIMEOUT);
      if (tcpSocket == NULL)
	{
	  server_error (_("Error accepting client connection"));
	  break;
	}

      /* Log the accepted connection.  */
      log (_F("Accepted connection from %d.%d.%d.%d:%d",
	      (addr.inet.ip      ) & 0xff,
	      (addr.inet.ip >>  8) & 0xff,
	      (addr.inet.ip >> 16) & 0xff,
	      (addr.inet.ip >> 24) & 0xff,
	      addr.inet.port));

      /* XXX: alarm() or somesuch to set a timeout. */
      /* XXX: fork() or somesuch to handle concurrent requests. */

      /* Accepted the connection, now handle it. */
      secStatus = handle_connection (tcpSocket, cert, privKey);
      if (secStatus != SECSuccess)
	server_error (_("Error processing client request"));

      // Log the end of the request.
      log (_F("Request from %d.%d.%d.%d:%d complete",
	      (addr.inet.ip      ) & 0xff,
	      (addr.inet.ip >>  8) & 0xff,
	      (addr.inet.ip >> 16) & 0xff,
	      (addr.inet.ip >> 24) & 0xff,
	      addr.inet.port));

      // If our certificate is no longer valid (e.g. has expired), then exit.
      secStatus = CERT_VerifyCertNow (dbHandle, cert, PR_TRUE/*checkSig*/,
				      certUsageSSLServer, NULL/*wincx*/);
      if (secStatus != SECSuccess)
	{
	  // Not an error. Exit the loop so a new cert can be generated.
	  break;
	}
    }

  SECKEY_DestroyPrivateKey (privKey);
  return SECSuccess;
}

/* Function:  void server_main()
 *
 * Purpose:  This is the server's main function.  It configures a socket
 *			 and listens to it.
 *
 */
static SECStatus
server_main (PRFileDesc *listenSocket)
{
  // Initialize NSS.
  SECStatus secStatus = nssInit (cert_db_path.c_str ());
  if (secStatus != SECSuccess)
    {
      // Message already issued.
      return secStatus;
    }

  // Preinitialized here due to jumps to the label 'done'.
  CERTCertificate *cert = NULL;
  bool serverCacheConfigured = false;

  // Enable cipher suites which are allowed by U.S. export regulations.
  // NB: The NSS docs say that SSL_ClearSessionCache is required for the new settings to take
  // effect, however, calling it puts NSS in a state where it will not shut down cleanly.
  // We need to be able to shut down NSS cleanly if we are to generate a new certificate when
  // ours expires. It should be noted however, thet SSL_ClearSessionCache only clears the
  // client cache, and we are a server.
  secStatus = NSS_SetExportPolicy ();
  //      SSL_ClearSessionCache ();
  if (secStatus != SECSuccess)
    {
      server_error (_("Unable to set NSS export policy"));
      nssError ();
      goto done;
    }

  // Configure the SSL session cache for a single process server with the default settings.
  secStatus = SSL_ConfigServerSessionIDCache (0, 0, 0, NULL);
  if (secStatus != SECSuccess)
    {
      server_error (_("Unable to configure SSL server session ID cache"));
      nssError ();
      goto done;
    }
  serverCacheConfigured = true;

  /* Get own certificate. */
  cert = PK11_FindCertFromNickname (server_cert_nickname (), NULL);
  if (cert == NULL)
    {
      server_error (_F("Unable to find our certificate in the database at %s", 
			  cert_db_path.c_str ()));
      nssError ();
      goto done;
    }

  // Tell the world that we're listening.
  advertise_presence (cert);

  /* Handle connections to the socket. */
  secStatus = accept_connections (listenSocket, cert);

  // Tell the world we're no longer listening.
  unadvertise_presence ();

 done:
  // Clean up
  if (cert)
    CERT_DestroyCertificate (cert);

  // Shutdown NSS
  if (serverCacheConfigured && SSL_ShutdownServerSessionIDCache () != SECSuccess)
    {
      server_error (_("Unable to shut down server session ID cache"));
      nssError ();
    }
  nssCleanup (cert_db_path.c_str ());

  return secStatus;
}

static void
listen ()
{
  // Create a new socket.
  PRFileDesc *listenSocket = PR_NewTCPSocket ();
  if (listenSocket == NULL)
    {
      server_error (_("Error creating socket"));
      nssError ();
      return;
    }

  // Set socket to be blocking - on some platforms the default is nonblocking.
  PRSocketOptionData socketOption;
  socketOption.option = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;
  PRStatus prStatus = PR_SetSocketOption (listenSocket, & socketOption);
  if (prStatus != PR_SUCCESS)
    {
      server_error (_("Error setting socket properties"));
      nssError ();
      goto done;
    }

  // Allow the socket address to be reused, in case we want the same port across a
  // 'service stap-server restart'
  socketOption.option = PR_SockOpt_Reuseaddr;
  socketOption.value.reuse_addr = PR_TRUE;
  prStatus = PR_SetSocketOption (listenSocket, & socketOption);
  if (prStatus != PR_SUCCESS)
    {
      server_error (_("Error setting socket properties"));
      nssError ();
      goto done;
    }

  // Configure the network connection.
  PRNetAddr addr;
  addr.inet.family = PR_AF_INET;
  addr.inet.ip	   = PR_INADDR_ANY;

  // Bind the socket to an address. Retry if the selected port is busy.
  for (;;)
    {
      addr.inet.port = PR_htons (port);

      /* Bind the address to the listener socket. */
      prStatus = PR_Bind (listenSocket, & addr);
      if (prStatus == PR_SUCCESS)
	break;

      // If the selected port is busy. Try another.
      PRErrorCode errorNumber = PR_GetError ();
      switch (errorNumber)
	{
	case PR_ADDRESS_NOT_AVAILABLE_ERROR:
	  server_error (_F("Network port %d is unavailable. Trying another port", port));
	  port = 0; // Will automatically select an available port
	  continue;
	case PR_ADDRESS_IN_USE_ERROR:
	  server_error (_F("Network port %d is busy. Trying another port", port));
	  port = 0; // Will automatically select an available port
	  continue;
	default:
	  server_error (_("Error setting socket address"));
	  nssError ();
	  goto done;
	}
    }

  // Query the socket for the port that was assigned.
  prStatus = PR_GetSockName (listenSocket, &addr);
  if (prStatus != PR_SUCCESS)
    {
      server_error (_("Unable to obtain socket address"));
      nssError ();
      goto done;
    }
  port = PR_ntohs (addr.inet.port);
  log (_F("Using network port %d", port));

  // Listen for connection on the socket.  The second argument is the maximum size of the queue
  // for pending connections.
  prStatus = PR_Listen (listenSocket, 5);
  if (prStatus != PR_SUCCESS)
    {
      server_error (_("Error listening on socket"));
      nssError ();
      goto done;
    }

  // Loop forever. We check our certificate (and regenerate, if necessary) and then start the
  // server. The server will go down when our certificate is no longer valid (e.g. expired). We
  // then generate a new one and start the server again.
  for (;;)
    {
      // Ensure that our certificate is valid. Generate a new one if not.
      if (check_cert (cert_db_path, server_cert_nickname (), use_db_password) != 0)
	{
	  // Message already issued
	  goto done;
	}

      // Ensure that our certificate is trusted by our local client.
      // Construct the client database path relative to the server database path.
      SECStatus secStatus = add_client_cert (server_cert_file (),
					     local_client_cert_db_path ());
      if (secStatus != SECSuccess)
	{
	  server_error (_("Unable to authorize certificate for the local client"));
	  goto done;
	}

      // Launch the server.
      secStatus = server_main (listenSocket);
    } // loop forever

 done:
  if (PR_Close (listenSocket) != PR_SUCCESS)
    {
      server_error (_("Error closing listen socket"));
      nssError ();
    }
}

int
main (int argc, char **argv) {
  initialize (argc, argv);
  listen ();
  cleanup ();
  return 0;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
