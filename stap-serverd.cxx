/*
  SSL server program listens on a port, accepts client connection, reads
  the data into a temporary file, calls the systemtap translator and
  then transmits the resulting file back to the client.

  Copyright (C) 2011 Red Hat Inc.

  This file is part of systemtap, and is free software.  You can
  redistribute it and/or modify it under the terms of the GNU General Public
  License as published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include "config.h"

#include <string>
#include <cerrno>
#include <cassert>

extern "C" {
#include <getopt.h>
#include <wordexp.h>
#include <glob.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <nspr.h>
#include <ssl.h>
#include <nss.h>
#include <keyhi.h>

#if HAVE_AVAHI
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#endif
}

#include "util.h"
#include "nsscommon.h"

using namespace std;

static void cleanup ();
static PRStatus spawn_and_wait (const vector<string> &argv,
                                const char* fd0, const char* fd1, const char* fd2, const char *pwd);

/* getopt variables */
extern int optind;

/* File scope statics. Set during argument parsing. */
static bool set_ulimits;
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
static pthread_t avahi_thread = 0;

// Message handling. Error messages occur during the handling of a request and
// are logged, printed to stderr and also to the client's stderr.
extern "C"
void
nsscommon_error (const char *msg, int logit)
{
  clog << msg << endl << flush;
  // Log it, but avoid repeated messages to the terminal.
  if (logit && log_ok ())
    log (msg);
}

// Fatal errors are treated the same as errors but also result in termination
// of the server.
static void
fatal (const string &msg)
{
  nsscommon_error (msg, true/*logit*/);
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
  // Examine the command line. We need not do much checking, but we do need to
  // parse all options in order to discover the ones we're interested in.
  while (true)
    {
      int long_opt;
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
      int grc = getopt_long (argc, argv, "a:B:I:Pr:R:", long_options, NULL);
      if (grc < 0)
        break;
      switch (grc)
        {
        case 'a':
	  process_a (optarg);
	  break;
	case 'B':
	  B_options += optarg;
	  stap_options += string (" -") + (char)grc + optarg;
	  break;
	case 'I':
	  I_options += optarg;
	  stap_options += string (" -") + (char)grc + optarg;
	  break;
	case 'P':
	  use_db_password = true;
	  break;
	case 'r':
	  process_r (optarg);
	  break;
	case 'R':
	  R_option = optarg;
	  stap_options += string (" -") + (char)grc + optarg;
	  break;
	case '?':
	  // Invalid/unrecognized option given. Message has already been issued.
	  break;
        default:
          // Reached when one added a getopt option but not a corresponding switch/case:
          if (optarg)
	    nsscommon_error (_F("%s: unhandled option '%c %s'", argv[0], (char)grc, optarg));
          else
	    nsscommon_error (_F("%s: unhandled option '%c'", argv[0], (char)grc));
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
		nsscommon_error (_F("%s: unhandled option '--%s=%s'", argv[0],
				    long_options[long_opt - 1].name, optarg));
	      else
		nsscommon_error (_F("%s: unhandled option '--%s'", argv[0],
				    long_options[long_opt - 1].name));
            }
          break;
        }
    }

  for (int i = optind; i < argc; i++)
    nsscommon_error (_F("%s: unrecognized argument '%s'", argv[0], argv[i]));
}

// Signal handling. When an interrupt is received, kill any spawned processes
// and exit.
extern "C"
void
handle_interrupt (int sig)
{
  log (_F("Received interrupt %d, exiting", sig));
  kill_stap_spawn (sig);
  cleanup ();
  exit (0);
}

static void
setup_signals (sighandler_t handler)
{
  struct sigaction sa;

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
    }
  sa.sa_flags = SA_RESTART;

  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGPIPE, &sa, NULL);
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGTTIN, &sa, NULL);
  sigaction (SIGTTOU, &sa, NULL);
}

#if HAVE_AVAHI
static AvahiEntryGroup *avahi_group = NULL;
static AvahiSimplePoll *avahi_simple_poll = NULL;
static char *avahi_service_name = NULL;

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
      nsscommon_error (_F("Avahi service name collision, renaming service to '%s'", avahi_service_name));

      // And recreate the services.
      create_services (avahi_entry_group_get_client (g));
      break;
    }

    case AVAHI_ENTRY_GROUP_FAILURE:
      nsscommon_error (_F("Avahi entry group failure: %s",
		  avahi_strerror (avahi_client_errno (avahi_entry_group_get_client (g)))));
      // Some kind of failure happened while we were registering our services.
      avahi_simple_poll_quit (avahi_simple_poll);
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
	nsscommon_error (_F("avahi_entry_group_new () failed: %s",
		    avahi_strerror (avahi_client_errno (c))));
	  goto fail;
      }

  // If the group is empty (either because it was just created, or
  // because it was reset previously, add our entries.
  if (avahi_entry_group_is_empty (avahi_group))
    {
      log (_F("Adding service '%s'", avahi_service_name));

      // Create the txt tags that will be registered with our service.
      string sysinfo = "sysinfo=" + uname_r + " " + arch;
      string certinfo = "certinfo=" + cert_serial_number;
      string optinfo = "optinfo=";
      string separator;
      if (! R_option.empty ())
	{
	  optinfo += R_option;
	  separator = " ";
	}
      if (! B_options.empty ())
	{
	  optinfo += separator + B_options;
	  separator = " ";
	}
      if (! I_options.empty ())
	optinfo += separator + I_options;

      // We will now our service to the entry group. Only services with the
      // same name should be put in the same entry group.
      int ret;
      if ((ret = avahi_entry_group_add_service (avahi_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
						(AvahiPublishFlags)0,
						avahi_service_name, "_stap._tcp", NULL, NULL, port,
						sysinfo.c_str (), optinfo.c_str (),
						certinfo.c_str (), NULL)) < 0)
	{
	  if (ret == AVAHI_ERR_COLLISION)
	    goto collision;

	  nsscommon_error (_F("Failed to add _ipp._tcp service: %s", avahi_strerror (ret)));
	  goto fail;
	}

      // Tell the server to register the service.
      if ((ret = avahi_entry_group_commit (avahi_group)) < 0)
	{
	  nsscommon_error (_F("Failed to commit avahi entry group: %s", avahi_strerror (ret)));
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
  nsscommon_error (_F("Avahi service name collision, renaming service to '%s'", avahi_service_name));
  avahi_entry_group_reset (avahi_group);
  create_services (c);
  return;

 fail:
  avahi_simple_poll_quit (avahi_simple_poll);
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
      nsscommon_error (_F("Avahi client failure: %s", avahi_strerror (avahi_client_errno (c))));
      avahi_simple_poll_quit (avahi_simple_poll);
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
 
// The entry point for the avahi client thread.
static void *
avahi_publish_service (void *arg)
{
  CERTCertificate *cert = (CERTCertificate *)arg;
  cert_serial_number = get_cert_serial_number (cert);

  string buf = "Systemtap Compile Server, pid=" + lex_cast (getpid ());
  avahi_service_name = avahi_strdup (buf.c_str ());

  AvahiClient *client = 0;

  // Allocate main loop object.
  if (! (avahi_simple_poll = avahi_simple_poll_new ()))
    {
      nsscommon_error (_("Failed to create avahi simple poll object."));
      goto done;
    }

  // Allocate a new client.
  int error;
  client = avahi_client_new (avahi_simple_poll_get (avahi_simple_poll), (AvahiClientFlags)0,
			     client_callback, NULL, & error);

  // Check wether creating the client object succeeded.
  if (! client)
    {
      nsscommon_error (_F("Failed to create avahi client: %s", avahi_strerror(error)));
      goto done;
    }

  // Run the main loop.
  avahi_simple_poll_loop (avahi_simple_poll);
  
 done:
  // Cleanup.
  if (client)
    avahi_client_free (client);
  if (avahi_simple_poll)
    avahi_simple_poll_free (avahi_simple_poll);
  avahi_free (avahi_service_name);
  return NULL;
}
#endif // HAVE_AVAHI

static void
advertise_presence (CERTCertificate *cert __attribute ((unused)))
{
#if HAVE_AVAHI
  // The avahi client must run on its own thread, since the poll loop does not
  // exit. The avahi thread will be cancelled automatically when the main thread
  // finishes. Run the thread as joinable to the main thread, so that we can know, when we
  // cancel it, that it actually was cancelled.
  pthread_attr_t attr;
  pthread_attr_init (& attr);
  pthread_attr_setdetachstate (& attr, PTHREAD_CREATE_JOINABLE);
  int rc = pthread_create (& avahi_thread, & attr, avahi_publish_service, (void *)cert);
  if (rc == EAGAIN)
    {
      nsscommon_error (_("Could not create a thread for the avahi client"));
      avahi_thread = 0;
    }
#else
  nsscommon_error (_("Unable to advertise presence on the network. Avahi is not available"));
#endif
}

static void
unadvertise_presence ()
{
#if HAVE_AVAHI
  if (avahi_thread)
    {
      pthread_cancel (avahi_thread);
      pthread_join (avahi_thread, NULL);
      avahi_thread = 0;
      avahi_group = NULL;
      avahi_simple_poll = NULL;
      avahi_service_name = NULL;
    }
#endif
}

static void
initialize (int argc, char **argv) {
  setup_signals (& handle_interrupt);

  // PR11197: security prophylactics.
  // 1) Reject use as root, except via a special environment variable.
  if (! getenv ("STAP_PR11197_OVERRIDE")) {
    if (geteuid () == 0)
      fatal ("For security reasons, invocation of stap-serverd as root is not supported.");
  }
  // 2) resource limits should be set if the user is the 'stap-server' daemon.
  string login = getlogin ();
  if (login == "stap-server")
    set_ulimits = true;
  else
    set_ulimits = false;

  // Seed the random number generator. Used to generate noise used during key generation.
  srand (time (NULL));

  // Initial values.
  use_db_password = false;
  port = 0;
  struct utsname utsname;
  uname (& utsname);
  uname_r = utsname.release;
  arch = normalize_machine (utsname.machine);

  // Parse the arguments.
  parse_options (argc, argv);

  pid_t pid = getpid ();
  log (_F("===== compile server pid %d starting", pid));

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
  PRInt32     totalBytes;
#define READ_BUFFER_SIZE 4096
  char        buffer[READ_BUFFER_SIZE];

  /* Read the number of bytes to be received.  */
  /* XXX: impose a limit to prevent disk space consumption DoS */
  numBytesRead = PR_Read (sslSocket, & numBytesExpected, sizeof (numBytesExpected));
  if (numBytesRead == 0) /* EOF */
    {
      nsscommon_error (_("Error reading size of request file"));
      goto done;
    }
  if (numBytesRead < 0)
    {
      nsscommon_error (_("Error in PR_Read"));
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
      nsscommon_error (_F("Could not open output file %s", requestFileName));
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
	  nsscommon_error (_("Error in PR_Read"));
	  nssError ();
	  goto done;
	}

      /* Write to the request file. */
      numBytesWritten = PR_Write(local_file_fd, buffer, numBytesRead);
      if (numBytesWritten < 0 || (numBytesWritten != numBytesRead))
        {
          nsscommon_error (_F("Could not write to output file %s", requestFileName));
	  nssError ();
	  goto done;
        }
    }

  if (totalBytes != numBytesExpected)
    {
      nsscommon_error (_F("Expected %d bytes, got %d while reading client request from socket",
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
      nsscommon_error (_("Could not import socket into SSL"));
      nssError ();
      return NULL;
    }
   
  /* Set the appropriate flags. */
  secStatus = SSL_OptionSet (sslSocket, SSL_SECURITY, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error setting SSL security for socket"));
      nssError ();
      return NULL;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_SERVER, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error setting handshake as server for socket"));
      nssError ();
      return NULL;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_REQUEST_CERTIFICATE, PR_FALSE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error setting SSL client authentication mode for socket"));
      nssError ();
      return NULL;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_REQUIRE_CERTIFICATE, PR_FALSE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error setting SSL client authentication mode for socket"));
      nssError ();
      return NULL;
    }

  /* Set the appropriate callback routines. */
#if 0 /* use the default */
  secStatus = SSL_AuthCertificateHook (sslSocket, myAuthCertificate, CERT_GetDefaultCertDB());
  if (secStatus != SECSuccess)
    {
      nssError ();
      nsscommon_error (_("Error in SSL_AuthCertificateHook"));
      return NULL;
    }
#endif
#if 0 /* Use the default */
  secStatus = SSL_BadCertHook(sslSocket, (SSLBadCertHandler)myBadCertHandler, &certErr);
  if (secStatus != SECSuccess)
    {
      nssError ();
      nsscommon_error (_("Error in SSL_BadCertHook"));
      return NULL;
    }
#endif
#if 0 /* no handshake callback */
  secStatus = SSL_HandshakeCallback(sslSocket, myHandshakeCallback, NULL);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error in SSL_HandshakeCallback"));
      nssError ();
      return NULL;
    }
#endif

  certKEA = NSS_FindCertKEAType (cert);

  secStatus = SSL_ConfigSecureServer (sslSocket, cert, privKey, certKEA);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error configuring SSL server"));
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
      nsscommon_error (_("Error in SSL_OptionSet:SSL_REQUEST_CERTIFICATE"));
      nssError ();
      return SECFailure;
    }

  /* If desired, require client to authenticate itself.  Note
   * SSL_REQUEST_CERTIFICATE must also be on, as above.  */
  secStatus = SSL_OptionSet(sslSocket, SSL_REQUIRE_CERTIFICATE, requireCert);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error in SSL_OptionSet:SSL_REQUIRE_CERTIFICATE"));
      nssError ();
      return SECFailure;
    }

  /* Having changed socket configuration parameters, redo handshake. */
  secStatus = SSL_ReHandshake(sslSocket, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error in SSL_ReHandshake"));
      nssError ();
      return SECFailure;
    }

  /* Force the handshake to complete before moving on. */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error in SSL_ForceHandshake"));
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
      nsscommon_error (_F("Could not open input file %s", responseFileName));
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
      nsscommon_error (_("Error writing response to socket"));
      nssError ();
      secStatus = SECFailure;
    }

  PR_Close (local_file_fd);
  return secStatus;
}


/* Run the translator on the data in the request directory, and produce output
   in the given output directory. */
static void
handleRequest (const char* requestDirName, const char* responseDirName)
{
  char stapstdout[PATH_MAX];
  char stapstderr[PATH_MAX];
  char staprc[PATH_MAX];
  char stapsymvers[PATH_MAX];
  vector<string> stapargv;
  int rc;
  wordexp_t words;
  unsigned u;
  unsigned i;
  FILE* f;
  int unprivileged = 0;
  struct stat st;

  stapargv.push_back ((char *)(getenv ("SYSTEMTAP_STAP") ?: STAP_PREFIX "/bin/stap"));

  /* Transcribe stap_options.  We use plain wordexp(3), since these
     options are coming from the local trusted user, so malicious
     content is not a concern. */
  // TODO: Use tokenize here.
  rc = wordexp (stap_options.c_str (), & words, WRDE_NOCMD|WRDE_UNDEF);
  if (rc) 
    { 
      nsscommon_error (_("Cannot parse stap options"));
      return;
    }

  for (u=0; u<words.we_wordc; u++)
    stapargv.push_back (words.we_wordv[u]);

  stapargv.push_back ("-k"); /* Need to keep temp files in order to package them up again. */

  /* Process the saved command line arguments.  Avoid quoting/unquoting errors by
     transcribing literally. */
  stapargv.push_back ("--client-options");

  for (i=1 ; ; i++)
    {
      char stapargfile[PATH_MAX];
      FILE* argfile;
      struct stat st;
      char *arg;

      snprintf (stapargfile, PATH_MAX, "%s/argv%d", requestDirName, i);

      rc = stat(stapargfile, & st);
      if (rc) break;

      arg = (char *)malloc (st.st_size+1);
      if (!arg)
        {
          nsscommon_error (_("Out of memory"));
          return;
        }

      argfile = fopen(stapargfile, "r");
      if (! argfile)
        {
          nsscommon_error (_F("Error opening %s: %s", stapargfile, strerror (errno)));
          return;
        }

      rc = fread(arg, 1, st.st_size, argfile);
      if (rc != st.st_size)
        {
          nsscommon_error (_F("Error reading %s: %s", stapargfile, strerror (errno)));
          return;
        }

      arg[st.st_size] = '\0';
      stapargv.push_back (arg);
      free (arg);
      fclose (argfile);
    }

  snprintf (stapstdout, PATH_MAX, "%s/stdout", responseDirName);
  snprintf (stapstderr, PATH_MAX, "%s/stderr", responseDirName);

  /* Check for the unprivileged flag; we need this so that we can decide to sign the module. */
  for (i=0; i < stapargv.size (); i++)
    {
      if (stapargv[i] == "--unprivileged")
	{
	  unprivileged=1;
	  break;
	}
    }
  /* NB: but it's not that easy!  What if an attacker passes
     --unprivileged as some sort of argument-parameter, so that the
     translator does not interpret it as an --unprivileged mode flag,
     but something else?  Then it could generate unrestrained modules,
     but silly we might still sign it, and let the attacker get away
     with murder.  And yet we don't want to fully getopt-parse the
     args here for duplication of effort.

     So let's do a hack: forcefully add --unprivileged to stapargv[]
     near the front in this case, something which a later option
     cannot undo. */
  if (unprivileged)
    {
      stapargv.insert (stapargv.begin () + 1, "--unprivileged"); /* better not be resettable by later option */
    }

  /* All ready, let's run the translator! */
  rc = spawn_and_wait (stapargv, "/dev/null", stapstdout, stapstderr, requestDirName);

  /* Save the RC */
  snprintf (staprc, PATH_MAX, "%s/rc", responseDirName);
  f = fopen(staprc, "w");
  if (f) 
    {
      /* best effort basis */
      fprintf(f, "%d", rc);
      fclose(f);
    }

  /* Parse output to extract the -k-saved temporary directory.
     XXX: bletch. */
  f = fopen(stapstderr, "r");
  if (!f)
    {
      nsscommon_error (_("Error in stap stderr open"));
      return;
    }

  while (1)
    {
      char line[PATH_MAX];
      char *l = fgets(line, PATH_MAX, f); /* NB: normally includes \n at end */
      if (!l) break;
      char key[]="Keeping temporary directory \"";

      /* Look for line from main.cxx: s.keep_tmpdir */
      if (strncmp(l, key, strlen(key)) == 0 &&
          l[strlen(l)-2] == '"')  /* "\n */
        { 
          /* Move this directory under responseDirName.  We don't have to
             preserve the exact stapXXXXXX suffix part, since stap-client
             will accept anything ("stap......" regexp), and rewrite it
             to a client-local string.
             
             We don't just symlink because then we'd have to
             remember to delete it later anyhow. */
          vector<string> mvargv;
          char *orig_staptmpdir = & l[strlen(key)];
          char new_staptmpdir[PATH_MAX];

          orig_staptmpdir[strlen(orig_staptmpdir)-2] = '\0'; /* Kill the closing "\n */
          snprintf(new_staptmpdir, PATH_MAX, "%s/stap000000", responseDirName);
          mvargv.push_back ("mv");
          mvargv.push_back (orig_staptmpdir);
          mvargv.push_back (new_staptmpdir);
          rc = stap_system (0, mvargv);
          if (rc != PR_SUCCESS)
            nsscommon_error (_("Error in stap tmpdir move"));

          /* In unprivileged mode, if we have a module built, we need to
             sign the sucker. */
          if (unprivileged) 
            {
              glob_t globber;
              char pattern[PATH_MAX];
              snprintf (pattern,PATH_MAX,"%s/*.ko", new_staptmpdir);
              rc = glob (pattern, GLOB_ERR, NULL, &globber);
              if (rc)
                nsscommon_error (_F("Unable to find a module in %s", new_staptmpdir));
              else if (globber.gl_pathc != 1)
                nsscommon_error (_F("Too many modules (%zu) in %s", globber.gl_pathc, new_staptmpdir));
              else
                {
		  sign_file (cert_db_path, server_cert_nickname (),
			     globber.gl_pathv[0], string (globber.gl_pathv[0]) + ".sgn");
                }
            }

	  /* If uprobes.ko is required, then we need to return it to the client.
	     uprobes.ko was required if the file "Module.symvers" is not empty in
	     the temp directory.  */
	  snprintf (stapsymvers, PATH_MAX, "%s/Module.symvers", new_staptmpdir);
	  rc = stat (stapsymvers, & st);
	  if (rc == 0 && st.st_size != 0)
	    {
	      /* uprobes.ko is required. Link to it from the response directory.  */
	      vector<string> lnargv;
	      lnargv.push_back ("/bin/ln");
	      lnargv.push_back ("-s");
	      lnargv.push_back (PKGDATADIR "/runtime/uprobes/uprobes.ko");
	      lnargv.push_back (responseDirName);
	      rc = stap_system (0, lnargv);
	      if (rc != PR_SUCCESS)
		nsscommon_error (_F("Could not link to %s from %s", lnargv[2].c_str (), lnargv[3].c_str ()));

	      /* In unprivileged mode, we need to return the signature as well. */
	      if (unprivileged) 
		{
		  lnargv[2] = PKGDATADIR "/runtime/uprobes/uprobes.ko.sgn";
		  rc = stap_system (0, lnargv);
		  if (rc != PR_SUCCESS)
		    nsscommon_error (_F("Could not link to %s from %s", lnargv[2].c_str (), lnargv[3].c_str ()));
		}
	    }
        }
    }

  /* Free up all the arg string copies.  Note that the first few were alloc'd
     by wordexp(), which wordfree() frees; others were hand-set to literal strings. */
  wordfree (& words);

  /* Sorry about the inconvenience.  C string/file processing is such a pleasure. */
}


/* A front end for stap_spawn that handles stdin, stdout, stderr, switches to a working
   directory and returns overall success or failure. */
static PRStatus
spawn_and_wait (const vector<string> &argv,
		const char* fd0, const char* fd1, const char* fd2, const char *pwd)
{ 
  pid_t pid;
  int rc;
  posix_spawn_file_actions_t actions;
  int dotfd = -1;

#define CHECKRC(msg) do { if (rc) { nsscommon_error (_(msg)); return PR_FAILURE; } } while (0)

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
    rc = posix_spawn_file_actions_addopen(& actions, 2, fd2, O_WRONLY|O_CREAT, 0600);
    CHECKRC ("Error in spawn file actions fd2");
  }

  /* change temporarily to a directory if requested */
  if (pwd)
    {
      dotfd = open (".", O_RDONLY);
      if (dotfd < 0)
        { 
          nsscommon_error (_("Error in spawn getcwd"));
          return PR_FAILURE;
        }
      
      rc = chdir (pwd);
      CHECKRC ("Error in spawn chdir");
    } 
 
  pid = stap_spawn (0, argv, & actions);
  /* NB: don't react to pid==-1 right away; need to chdir back first. */

  if (pwd && dotfd >= 0)
    {
      int subrc;
      subrc = fchdir (dotfd);
      subrc |= close (dotfd);
      if (subrc) 
        nsscommon_error (_("Error in spawn unchdir"));
    }

  if (pid == -1)
    {
      nsscommon_error (_("Error in spawn"));
      return PR_FAILURE;
    }

  rc = stap_waitpid (0, pid);
  if (rc == -1)
    {
      nsscommon_error (_("Error in waitpid"));
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
  PRSocketOptionData socketOption;
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

  /* Make sure the socket is blocking. */
  socketOption.option             = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;
  PR_SetSocketOption (tcpSocket, &socketOption);

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
      nsscommon_error (_("Error resetting SSL handshake"));
      nssError ();
      goto cleanup;
    }

#if 0 // The client authenticates the server, so the client initiates the handshake
  /* Force the handshake to complete before moving on. */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Error forcing SSL handshake"));
      nssError ();
      goto cleanup;
    }
#endif

  secStatus = SECFailure;
  snprintf(tmpdir, PATH_MAX, "%s/stap-server.XXXXXX", getenv("TMPDIR") ?: "/tmp");
  rc1 = mkdtemp(tmpdir);
  if (! rc1)
    {
      nsscommon_error (_F("Could not create temporary directory %s: %s", tmpdir, strerror(errno)));
      tmpdir[0]=0; /* prevent /bin/rm */
      goto cleanup;
    }

  /* Create a temporary files names and directories.  */
  snprintf (requestFileName, PATH_MAX, "%s/request.zip", tmpdir);

  snprintf (requestDirName, PATH_MAX, "%s/request", tmpdir);
  rc = mkdir(requestDirName, 0700);
  if (rc)
    {
      nsscommon_error (_F("Could not create temporary directory %s: %s", requestDirName, strerror (errno)));
      goto cleanup;
    }

  snprintf (responseDirName, PATH_MAX, "%s/response", tmpdir);
  rc = mkdir(responseDirName, 0700);
  if (rc)
    {
      nsscommon_error (_F("Could not create temporary directory %s: %s", responseDirName, strerror (errno)));
      goto cleanup;
    }

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
  if (rc != PR_SUCCESS)
    {
      nsscommon_error (_("Unable to extract client request"));
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
      nsscommon_error (_("Unable to compress server response"));
      goto cleanup;
    }
  
  secStatus = writeDataToSocket (sslSocket, responseFileName);

cleanup:
  if (sslSocket)
    if (PR_Close (sslSocket) != PR_SUCCESS)
      {
	nsscommon_error (_("Error closing ssl socket"));
	nssError ();
      }
  if (tmpdir[0]) 
    {
      /* Remove the whole tmpdir and all that lies beneath. */
      argv.clear ();
      argv.push_back ("rm");
      argv.push_back ("-r");
      argv.push_back (tmpdir);
      rc = stap_system (0, argv);
      if (rc != PR_SUCCESS)
        nsscommon_error (_("Error in tmpdir cleanup"));
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
  time_t      now;

  dbHandle = CERT_GetDefaultCertDB ();

  // cert_db_path gets passed to nssPasswordCallback.
  SECKEYPrivateKey *privKey = PK11_FindKeyByAnyCert (cert, (void*)cert_db_path.c_str ());
  if (privKey == NULL)
    {
      nsscommon_error (_("Unable to obtain certificate private key"));
      nssError ();
      return SECFailure;
    }

  while (PR_TRUE)
    {
      /* Accept a connection to the socket. */
      tcpSocket = PR_Accept (listenSocket, &addr, PR_INTERVAL_NO_TIMEOUT);
      if (tcpSocket == NULL)
	{
	  nsscommon_error (_("Error accepting client connection"));
	  break;
	}

      /* Log the accepted connection.  */
      time (& now);
      log (_F("%sAccepted connection from %d.%d.%d.%d:%d",
              ctime (& now),
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
	nsscommon_error (_("Error processing client request"));

      // Log the end of the request.
      time (& now);
      log (_F("%sRequest from %d.%d.%d.%d:%d complete",
              ctime (& now),
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
server_main ()
{
  SECStatus           secStatus;
  PRStatus            prStatus;
  PRFileDesc *        listenSocket;
  PRNetAddr           addr;
  PRSocketOptionData  socketOption;

  /* Create a new socket. */
  listenSocket = PR_NewTCPSocket();
  if (listenSocket == NULL)
    {
      nsscommon_error (_("Error creating socket"));
      nssError ();
      return SECFailure;
    }

  /* Set socket to be blocking -
   * on some platforms the default is nonblocking.
   */
  socketOption.option = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;

  // Predeclare to keep C++ happy about jumps to 'done'.
  CERTCertificate *cert = NULL;

  secStatus = SECFailure;
  prStatus = PR_SetSocketOption (listenSocket, &socketOption);
  if (prStatus != PR_SUCCESS)
    {
      nsscommon_error (_("Error setting socket properties"));
      nssError ();
      goto done;
    }

  /* Configure the network connection. */
  addr.inet.family = PR_AF_INET;
  addr.inet.ip	   = PR_INADDR_ANY;

  // Bind the socket to an address. Retry if the selected port is busy.
  for (;;)
    {
      //      if (port == 0)
      //	port = IPPORT_USERRESERVED + (rand () % (64000 - IPPORT_USERRESERVED));
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
	  nsscommon_error (_F("Network port %d is unavailable. Trying another port", port));
	  port = 0; // Will automatically select an available port
	  continue;
	case PR_ADDRESS_IN_USE_ERROR:
	  nsscommon_error (_F("Network port %d is busy. Trying another port", port));
	  port = 0; // Will automatically select an available port
	  continue;
	default:
	  nsscommon_error (_("Error setting socket address"));
	  nssError ();
	  goto done;
	}
    }

  // Query the socket for the port that was assigned.
  prStatus = PR_GetSockName (listenSocket, &addr);
  if (prStatus != PR_SUCCESS)
    {
      nsscommon_error (_("Unable to obtain socket address"));
      nssError ();
      goto done;
    }
  port = PR_ntohs (addr.inet.port);
  log (_F("Using network port %d", port));

  /* Listen for connection on the socket.  The second argument is
   * the maximum size of the queue for pending connections.
   */
  prStatus = PR_Listen (listenSocket, 5);
  if (prStatus != PR_SUCCESS)
    {
      nsscommon_error (_("Error listening on socket"));
      nssError ();
      goto done;
    }

  /* Get own certificate. */
  cert = PK11_FindCertFromNickname (server_cert_nickname (), NULL);
  if (cert == NULL)
    {
      nsscommon_error (_F("Unable to find our certificate in the database at %s", 
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
  if (PR_Close (listenSocket) != PR_SUCCESS)
    {
      nsscommon_error (_("Error closing listen socket"));
      nssError ();
    }
  if (cert)
    CERT_DestroyCertificate (cert);

  return secStatus;
}

static void
listen ()
{
  // Listen forever, unless a fatal error occurs.
  for (;;)
    {
      // Ensure that our certificate is valid. Generate a new one if not.
      if (check_cert (cert_db_path, server_cert_nickname (), use_db_password) != 0)
	{
	  // Message already issued
	  return;
	}

      // Ensure that our certificate is trusted by our local client.
      // Construct the client database path relative to the server database path.
      SECStatus secStatus = add_client_cert (server_cert_file (),
					     local_client_cert_db_path ());
      if (secStatus != SECSuccess)
	{
	  nsscommon_error (_("Unable to authorize certificate for the local client"));
	  return;
	}

      /* Initialize NSS. */
      secStatus = nssInit (cert_db_path.c_str ());
      if (secStatus != SECSuccess)
	{
	  // Message already issued.
	  return;
	}

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
	  nsscommon_error (_("Unable to set NSS export policy"));
	  nssError ();
	  nssCleanup (cert_db_path.c_str ());
	  return;
	}

      // Configure the SSL session cache for a single process server with the default settings.
      secStatus = SSL_ConfigServerSessionIDCache (0, 0, 0, NULL);
      if (secStatus != SECSuccess)
	{
	  nsscommon_error (_("Unable to configure SSL server session ID cache"));
	  nssError ();
	  nssCleanup (cert_db_path.c_str ());
	  return;
	}

      /* Launch server. */
      secStatus = server_main ();

      // Shutdown NSS
      if (SSL_ShutdownServerSessionIDCache () != SECSuccess)
	{
	  nsscommon_error (_("Unable to shut down server session ID cache"));
	  nssError ();
	}
      nssCleanup (cert_db_path.c_str ());
      if (secStatus != SECSuccess)
	{
	  // Message already issued.
	  return;
	}
    } // loop forever
}

int
main (int argc, char **argv) {
  initialize (argc, argv);
  listen ();
  cleanup ();
  return 0;
}
