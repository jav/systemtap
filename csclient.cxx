/*
 Compile server client functions
 Copyright (C) 2010-2012 Red Hat Inc.

 This file is part of systemtap, and is free software.  You can
 redistribute it and/or modify it under the terms of the GNU General
 Public License (GPL); either version 2, or (at your option) any
 later version.
*/

// Completely disable the client if NSS is not available.
#include "config.h"
#if HAVE_NSS
#include "session.h"
#include "cscommon.h"
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
#include <algorithm>

extern "C" {
#include <unistd.h>
#include <linux/limits.h>
#include <sys/time.h>
#include <glob.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <net/if.h>
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

extern "C" {
#include <ssl.h>
#include <nspr.h>
#include <nss.h>
#include <certdb.h>
#include <pk11pub.h>
#include <prerror.h>
#include <secerr.h>
#include <sslerr.h>
}

#include "nsscommon.h"

using namespace std;

#define STAP_CSC_01 _("WARNING: The domain name, %s, does not match the DNS name(s) on the server certificate:\n")
#define STAP_CSC_02 _("could not find input file %s\n")
#define STAP_CSC_03 _("could not open input file %s\n")
#define STAP_CSC_04 _("Unable to open output file %s\n")
#define STAP_CSC_05 _("could not write to %s\n")

static PRIPv6Addr &copyAddress (PRIPv6Addr &PRin6, const in6_addr &in6);
bool operator!= (const PRNetAddr &x, const PRNetAddr &y);

extern "C"
void
nsscommon_error (const char *msg, int logit __attribute ((unused)))
{
  clog << msg << endl << flush;
}

// Information about compile servers.
struct compile_server_info
{
  compile_server_info ()
  {
    memset (& address, 0, sizeof (address));
  }

  string host_name;
  PRNetAddr address;
  string version;
  string sysinfo;
  string certinfo;

  bool empty () const
  {
    return this->host_name.empty () && ! this->hasAddress ();
  }
  bool hasAddress () const
  {
    return this->address.raw.family != 0;
  }
  unsigned short port () const
  {
    if (this->address.raw.family == PR_AF_INET)
      return ntohs (this->address.inet.port);
    if (this->address.raw.family == PR_AF_INET6)
      return ntohs (this->address.ipv6.port);
    return 0;
  }
  unsigned short setPort (unsigned short port)
  {
    if (this->address.raw.family == PR_AF_INET)
      return this->address.inet.port = htons (port);
    if (this->address.raw.family == PR_AF_INET6)
      return this->address.ipv6.port = htons (port);
    return 0;
  }

  bool operator== (const compile_server_info &that) const
  {
    // If both ip addressed are not set, then the host names must match, otherwise
    // the addresses must match.
    if (! this->hasAddress() || ! that.hasAddress())
      {
	if (this->host_name != that.host_name)
	  return false;
      }
    else if (this->address != that.address)
      return false;

    // Compare the other fields only if they have both been set.
    if (this->port() != 0 && that.port() != 0 &&
	this->port() != that.port())
      return false;
    if (! this->version.empty () && ! that.version.empty () &&
	this->version != that.version)
      return false;
    if (! this->sysinfo.empty () && ! that.sysinfo.empty () &&
	this->sysinfo != that.sysinfo)
      return false;
    if (! this->certinfo.empty () && ! that.certinfo.empty () &&
	this->certinfo != that.certinfo)
      return false;

    return true; // They are equal
  }

  // Used to sort servers by preference for order of contact. The preferred server is
  // "less" than the other one.
  bool operator< (const compile_server_info &that) const
  {
    // Prefer servers with a later (higher) version number.
    cs_protocol_version this_version (this->version.c_str ());
    cs_protocol_version that_version (that.version.c_str ());
    return that_version < this_version;
  }
};

ostream &operator<< (ostream &s, const compile_server_info &i);
ostream &operator<< (ostream &s, const vector<compile_server_info> &v);

static void
preferred_order (vector<compile_server_info> &servers)
{
  // Sort the given list of servers into the preferred order for contacting.
  // Don't bother if there are less than 2 servers in the list.
  if (servers.size () < 2)
    return;

  // Sort the list using compile_server_info::operator<
  sort (servers.begin (), servers.end ());
}

struct compile_server_cache
{
  vector<compile_server_info> default_servers;
  vector<compile_server_info> specified_servers;
  vector<compile_server_info> trusted_servers;
  vector<compile_server_info> signing_servers;
  vector<compile_server_info> online_servers;
};

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
static compile_server_cache* cscache(systemtap_session& s);
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

/* Exit error codes */
#define SUCCESS                   0
#define GENERAL_ERROR             1
#define CA_CERT_INVALID_ERROR     2
#define SERVER_CERT_EXPIRED_ERROR 3

// -----------------------------------------------------
// NSS related code used by the compile server client
// -----------------------------------------------------
static void add_server_trust (systemtap_session &s, const string &cert_db_path, const vector<compile_server_info> &server_list);
static void revoke_server_trust (systemtap_session &s, const string &cert_db_path, const vector<compile_server_info> &server_list);
static void get_server_info_from_db (systemtap_session &s, vector<compile_server_info> &servers, const string &cert_db_path);

static string global_client_cert_db_path () {
  return SYSCONFDIR "/systemtap/ssl/client";
}

static string
private_ssl_cert_db_path ()
{
  return local_client_cert_db_path ();
}

static string
global_ssl_cert_db_path ()
{
  return global_client_cert_db_path ();
}

static string
signing_cert_db_path ()
{
  return SYSCONFDIR "/systemtap/staprun";
}

/* Connection state.  */
typedef struct connectionState_t
{
  const char *hostName;
  PRNetAddr   addr;
  const char *infileName;
  const char *outfileName;
  const char *trustNewServerMode;
} connectionState_t;

#if 0 /* No client authorization */
static char *
myPasswd(PK11SlotInfo *info, PRBool retry, void *arg)
{
  char * passwd = NULL;

  if ( (!retry) && arg )
    passwd = PORT_Strdup((char *)arg);

  return passwd;
}
#endif

/* Add the server's certificate to our database of trusted servers.  */
static SECStatus
trustNewServer (CERTCertificate *serverCert)
{
  SECStatus secStatus;
  CERTCertTrust *trust = NULL;
  PK11SlotInfo *slot = NULL;

  /* Import the certificate.  */
  slot = PK11_GetInternalKeySlot();
  const char *nickname = server_cert_nickname ();
  secStatus = PK11_ImportCert(slot, serverCert, CK_INVALID_HANDLE, nickname, PR_FALSE);
  if (secStatus != SECSuccess)
    goto done;
  
  /* Make it a trusted peer.  */
  trust = (CERTCertTrust *)PORT_ZAlloc(sizeof(CERTCertTrust));
  if (! trust)
    {
      secStatus = SECFailure;
      goto done;
    }

  secStatus = CERT_DecodeTrustString(trust, "P,P,P");
  if (secStatus != SECSuccess)
    goto done;

  secStatus = CERT_ChangeCertTrust(CERT_GetDefaultCertDB(), serverCert, trust);

done:
  if (slot)
    PK11_FreeSlot (slot);
  if (trust)
    PORT_Free(trust);
  return secStatus;
}

/* Called when the server certificate verification fails. This gives us
   the chance to trust the server anyway and add the certificate to the
   local database.  */
static SECStatus
badCertHandler(void *arg, PRFileDesc *sslSocket)
{
  SECStatus secStatus;
  PRErrorCode errorNumber;
  CERTCertificate *serverCert = NULL;
  SECItem subAltName;
  PRArenaPool *tmpArena = NULL;
  CERTGeneralName *nameList, *current;
  char *expected = NULL;
  const connectionState_t *connectionState = (connectionState_t *)arg;

  errorNumber = PR_GetError ();
  switch (errorNumber)
    {
    case SSL_ERROR_BAD_CERT_DOMAIN:
      /* Since we administer our own client-side databases of trustworthy
	 certificates, we don't need the domain name(s) on the certificate to
	 match. If the cert is in our database, then we can trust it.
	 Issue a warning and accept the certificate.  */
      expected = SSL_RevealURL (sslSocket);
      fprintf (stderr, STAP_CSC_01, expected);

      /* List the DNS names from the server cert as part of the warning.
	 First, find the alt-name extension on the certificate.  */
      subAltName.data = NULL;
      serverCert = SSL_PeerCertificate (sslSocket);
      secStatus = CERT_FindCertExtension (serverCert,
					  SEC_OID_X509_SUBJECT_ALT_NAME,
					  & subAltName);
      if (secStatus != SECSuccess || ! subAltName.data)
	{
	  fprintf (stderr, _("Unable to find alt name extension on the server certificate\n"));
	  secStatus = SECSuccess; /* Not a fatal error */
	  break;
	}

      // Now, decode the extension.
      tmpArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
      if (! tmpArena) 
	{
	  fprintf (stderr, _("Out of memory\n"));
	  secStatus = SECSuccess; /* Not a fatal error here */
	  break;
	}
      nameList = CERT_DecodeAltNameExtension (tmpArena, & subAltName);
      SECITEM_FreeItem(& subAltName, PR_FALSE);
      if (! nameList)
	{
	  fprintf (stderr, _("Unable to decode alt name extension on server certificate\n"));
	  secStatus = SECSuccess; /* Not a fatal error */
	  break;
	}

      /* List the DNS names from the server cert as part of the warning.
	 The names are in a circular list.  */
      current = nameList;
      do
	{
	  /* Make sure this is a DNS name.  */
	  if (current->type == certDNSName)
	    {
	      fprintf (stderr, "  %.*s\n",
		       (int)current->name.other.len, current->name.other.data);
	    }
	  current = CERT_GetNextGeneralName (current);
	}
      while (current != nameList);

      /* Accept the certificate */
      secStatus = SECSuccess;
      break;

    case SEC_ERROR_CA_CERT_INVALID:
      /* The server's certificate is not trusted. Should we trust it? */
      secStatus = SECFailure; /* Do not trust by default. */
      if (! connectionState->trustNewServerMode)
	break;

      /* Trust it for this session only?  */
      if (strcmp (connectionState->trustNewServerMode, "session") == 0)
	{
	  secStatus = SECSuccess;
	  break;
	}

      /* Trust it permanently?  */
      if (strcmp (connectionState->trustNewServerMode, "permanent") == 0)
	{
	  /* The user wants to trust this server. Get the server's certificate so
	     and add it to our database.  */
	  serverCert = SSL_PeerCertificate (sslSocket);
	  if (serverCert != NULL)
	    {
	      secStatus = trustNewServer (serverCert);
	    }
	}
      break;
    default:
      secStatus = SECFailure; /* Do not trust this server */
      break;
    }

  if (expected)
    PORT_Free (expected);
  if (tmpArena)
    PORT_FreeArena (tmpArena, PR_FALSE);

  if (serverCert != NULL)
    {
      CERT_DestroyCertificate (serverCert);
    }

  return secStatus;
}

static PRFileDesc *
setupSSLSocket (connectionState_t *connectionState)
{
  PRFileDesc         *tcpSocket;
  PRFileDesc         *sslSocket;
  PRSocketOptionData	socketOption;
  PRStatus            prStatus;
  SECStatus           secStatus;

  tcpSocket = PR_OpenTCPSocket(connectionState->addr.raw.family);
  if (tcpSocket == NULL)
    goto loser;

  /* Make the socket blocking. */
  socketOption.option = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;

  prStatus = PR_SetSocketOption(tcpSocket, &socketOption);
  if (prStatus != PR_SUCCESS)
    goto loser;

  /* Import the socket into the SSL layer. */
  sslSocket = SSL_ImportFD(NULL, tcpSocket);
  if (!sslSocket)
    goto loser;

  /* Set configuration options. */
  secStatus = SSL_OptionSet(sslSocket, SSL_SECURITY, PR_TRUE);
  if (secStatus != SECSuccess)
    goto loser;

  secStatus = SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_CLIENT, PR_TRUE);
  if (secStatus != SECSuccess)
    goto loser;

  /* Set SSL callback routines. */
#if 0 /* no client authentication */
  secStatus = SSL_GetClientAuthDataHook(sslSocket,
					(SSLGetClientAuthData)myGetClientAuthData,
					(void *)certNickname);
  if (secStatus != SECSuccess)
    goto loser;
#endif
#if 0 /* Use the default */
  secStatus = SSL_AuthCertificateHook(sslSocket,
				      (SSLAuthCertificate)myAuthCertificate,
				      (void *)CERT_GetDefaultCertDB());
  if (secStatus != SECSuccess)
    goto loser;
#endif

  secStatus = SSL_BadCertHook(sslSocket, (SSLBadCertHandler)badCertHandler,
			      connectionState);
  if (secStatus != SECSuccess)
    goto loser;

#if 0 /* No handshake callback */
  secStatus = SSL_HandshakeCallback(sslSocket, myHandshakeCallback, NULL);
  if (secStatus != SECSuccess)
    goto loser;
#endif

  return sslSocket;

 loser:
  if (tcpSocket)
    PR_Close(tcpSocket);
  return NULL;
}


static SECStatus
handle_connection (PRFileDesc *sslSocket, connectionState_t *connectionState)
{
  PRInt32     numBytes;
  char       *readBuffer;
  PRFileInfo  info;
  PRFileDesc *local_file_fd;
  PRStatus    prStatus;
  SECStatus   secStatus = SECSuccess;

#define READ_BUFFER_SIZE (60 * 1024)

  /* If we don't have both the input and output file names, then we're
     contacting this server only in order to establish trust. In this case send
     0 as the file size and exit. */
  if (! connectionState->infileName || ! connectionState->outfileName)
    {
      numBytes = htonl ((PRInt32)0);
      numBytes = PR_Write (sslSocket, & numBytes, sizeof (numBytes));
      if (numBytes < 0)
	return SECFailure;
      return SECSuccess;
    }

  /* read and send the data. */
  /* Try to open the local file named.	
   * If successful, then write it to the server
   */
  prStatus = PR_GetFileInfo(connectionState->infileName, &info);
  if (prStatus != PR_SUCCESS ||
      info.type != PR_FILE_FILE ||
      info.size < 0)
    {
      fprintf (stderr, STAP_CSC_02,
	       connectionState->infileName);
      return SECFailure;
    }

  local_file_fd = PR_Open(connectionState->infileName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, STAP_CSC_03, connectionState->infileName);
      return SECFailure;
    }

  /* Send the file size first, so the server knows when it has the entire file. */
  numBytes = htonl ((PRInt32)info.size);
  numBytes = PR_Write(sslSocket, & numBytes, sizeof (numBytes));
  if (numBytes < 0)
    {
      PR_Close(local_file_fd);
      return SECFailure;
    }

  /* Transmit the local file across the socket.  */
  numBytes = PR_TransmitFile(sslSocket, local_file_fd, 
			     NULL, 0,
			     PR_TRANSMITFILE_KEEP_OPEN,
			     PR_INTERVAL_NO_TIMEOUT);
  if (numBytes < 0)
    {
      PR_Close(local_file_fd);
      return SECFailure;
    }

  PR_Close(local_file_fd);

  /* read until EOF */
  readBuffer = (char *)PORT_Alloc(READ_BUFFER_SIZE);
  if (! readBuffer) {
    fprintf (stderr, _("Out of memory\n"));
    return SECFailure;
  }

  local_file_fd = PR_Open(connectionState->outfileName, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
			  PR_IRUSR | PR_IWUSR | PR_IRGRP | PR_IWGRP | PR_IROTH);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, STAP_CSC_04, connectionState->outfileName);
      return SECFailure;
    }
  while (PR_TRUE)
    {
      numBytes = PR_Read(sslSocket, readBuffer, READ_BUFFER_SIZE);
      if (numBytes == 0)
	break;	/* EOF */

      if (numBytes < 0)
	{
	  secStatus = SECFailure;
	  break;
	}

      /* Write to output file */
      numBytes = PR_Write(local_file_fd, readBuffer, numBytes);
      if (numBytes < 0)
	{
	  fprintf (stderr, STAP_CSC_05, connectionState->outfileName);
	  secStatus = SECFailure;
	  break;
	}
    }

  PR_Free(readBuffer);
  PR_Close(local_file_fd);

  /* Caller closes the socket. */
  return secStatus;
}

/* make the connection.
*/
static SECStatus
do_connect (connectionState_t *connectionState)
{
  PRFileDesc *sslSocket;
  PRStatus    prStatus;
  SECStatus   secStatus;

  secStatus = SECSuccess;

  /* Set up SSL secure socket. */
  sslSocket = setupSSLSocket (connectionState);
  if (sslSocket == NULL)
    return SECFailure;

#if 0 /* no client authentication */
  secStatus = SSL_SetPKCS11PinArg(sslSocket, password);
  if (secStatus != SECSuccess)
    goto done;
#endif

  secStatus = SSL_SetURL(sslSocket, connectionState->hostName);
  if (secStatus != SECSuccess)
    goto done;

  prStatus = PR_Connect(sslSocket, & connectionState->addr, PR_INTERVAL_NO_TIMEOUT);
  if (prStatus != PR_SUCCESS)
    {
      secStatus = SECFailure;
      goto done;
    }

  /* Established SSL connection, ready to send data. */
  secStatus = SSL_ResetHandshake(sslSocket, /* asServer */ PR_FALSE);
  if (secStatus != SECSuccess)
    goto done;

  /* This is normally done automatically on the first I/O operation,
     but doing it here catches any authentication problems early.  */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    goto done;

  // Connect to the server and make the request.
  secStatus = handle_connection(sslSocket, connectionState);

 done:
  prStatus = PR_Close(sslSocket);
  return secStatus;
}

int
client_connect (const compile_server_info &server,
		const char* infileName, const char* outfileName,
		const char* trustNewServer)
{
  SECStatus   secStatus;
  PRErrorCode errorNumber;
  int         attempt;
  int         errCode = GENERAL_ERROR;
  struct connectionState_t connectionState;

  // Set up a connection state for use by NSS error callbacks.
  memset (& connectionState, 0, sizeof (connectionState));
  connectionState.hostName = server.host_name.c_str ();
  connectionState.addr = server.address;
  connectionState.infileName = infileName;
  connectionState.outfileName = outfileName;
  connectionState.trustNewServerMode = trustNewServer;

  /* Some errors (see below) represent a situation in which trying again
     should succeed. However, don't try forever.  */
  for (attempt = 0; attempt < 5; ++attempt)
    {
      secStatus = do_connect (& connectionState);
      if (secStatus == SECSuccess)
	return SUCCESS;

      errorNumber = PR_GetError ();
      switch (errorNumber)
	{
	case PR_CONNECT_RESET_ERROR:
	  /* Server was not ready. */
	  sleep (1);
	  break; /* Try again */
	case SEC_ERROR_EXPIRED_CERTIFICATE:
	  /* The server's certificate has expired. It should
	     generate a new certificate. Return now and we'll try again. */
	  errCode = SERVER_CERT_EXPIRED_ERROR;
	  return errCode;
	case SEC_ERROR_CA_CERT_INVALID:
	  /* The server's certificate is not trusted. The exit code must
	     reflect this.  */
	  errCode = CA_CERT_INVALID_ERROR;
	  return errCode;
	default:
	  /* This error is fatal.  */
	  return errCode;
	}
    }

  return errCode;
}

int
compile_server_client::passes_0_4 ()
{
  PROBE1(stap, client__start, &s);

  // arguments parsed; get down to business
  if (s.verbose)
    clog << _("Using a compile server.") << endl;

  struct tms tms_before;
  times (& tms_before);
  struct timeval tv_before;
  gettimeofday (&tv_before, NULL);

  // Create the request package.
  int rc = initialize ();
  assert_no_interrupts();
  if (rc != 0) goto done;
  rc = create_request ();
  assert_no_interrupts();
  if (rc != 0) goto done;
  rc = package_request ();
  assert_no_interrupts();
  if (rc != 0) goto done;

  // Submit it to the server.
  rc = find_and_connect_to_server ();
  assert_no_interrupts();
  if (rc != 0) goto done;

  // Unpack and process the response.
  rc = unpack_response ();
  assert_no_interrupts();
  if (rc != 0) goto done;
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
      string ws = s.winning_server;
      if (ws == "") ws = "?";
      clog << _("Passes: via server ") << ws << " "
           << getmemusage()
           << TIMESPRINT
           << endl;
    }
  if (rc)
    {
      clog << _("Passes: via server failed.  Try again with another '-v' option.") << endl;
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
	  copy_file (module_src_path, module_dest_path, s.verbose >= 3);
	  // Also copy the module signature, it it exists.
	  module_src_path += ".sgn";
	  if (file_exists (module_src_path))
	    {
	      module_dest_path += ".sgn";
	      copy_file(module_src_path, module_dest_path, s.verbose >= 3);
	    }
	}
    }

  PROBE1(stap, client__end, &s);

  return rc;
}

// Initialize a client/server session.
int
compile_server_client::initialize ()
{
  int rc = 0;

  // Initialize session state
  argc = 0;

  // Private location for server certificates.
  private_ssl_dbs.push_back (private_ssl_cert_db_path ());

  // Additional public location.
  public_ssl_dbs.push_back (global_ssl_cert_db_path ());

  // Create a temporary directory to package things in.
  client_tmpdir = s.tmpdir + "/client";
  rc = create_dir (client_tmpdir.c_str ());
  if (rc != 0)
    {
      const char* e = strerror (errno);
      clog << _("ERROR: cannot create temporary directory (\"")
	   << client_tmpdir << "\"): " << e
	   << endl;
    }

  return rc;
}

// Create the request package.
int
compile_server_client::create_request ()
{
  // Add the current protocol version.
  int rc = write_to_file (client_tmpdir + "/version", CURRENT_CS_PROTOCOL_VERSION);
  if (rc != 0)
    return rc;

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
	      clog << _("ERROR: cannot create temporary directory ")
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
  if (rc != 0)
    return rc;

  // Add localization data
  rc = add_localization_variables();

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
  const string &subdir, const string &path
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

      // Including / would require special handling in the code below and
      // is a bad idea anyway. Let's not allow it.
      if (rpath == "/")
	{
	  if (rpath != path)
	    clog << _F("%s resolves to %s\n", path.c_str (), rpath.c_str ());
	  clog << _F("Unable to send %s to the server\n", path.c_str ());
	  return 1;
	}

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

  // Name this file or directory in the packaged arguments.
  rc = add_package_arg (subdir + "/" + rpath.substr (1));

 done:
  if (rc != 0)
    {
      const char* e = strerror (errno);
      clog << "ERROR: unable to add "
	   << rpath
	   << " to temp directory as "
	   << name << ": " << e
	   << endl;
    }
  return rc;
}

// Add the localization variables to the server request
// package.
int
compile_server_client::add_localization_variables()
{
  int rc;
  string envVar;
  string fname;

  const set<string> &locVars = localization_variables();
  set<string>::iterator it;

  /* Note: We don't have to check for the contents of the environment
   * variables here, since they will be checked extensively on the
   * server.
   */
  for (it = locVars.begin(); it != locVars.end(); it++)
    {
      char* var = getenv((*it).c_str());
      if (var)
        envVar += *it + "=" + (string)var + "\n";
    }
  fname = client_tmpdir + "/locale";
  rc = write_to_file(fname, envVar);
  return rc;
}

// Package the client's temp directory into a form suitable for sending to the
// server.
int
compile_server_client::package_request ()
{
  // Package up the temporary directory into a zip file.
  client_zipfile = client_tmpdir + ".zip";
  string cmd = "cd " + cmdstr_quoted(client_tmpdir) + " && zip -qr "
      + cmdstr_quoted(client_zipfile) + " *";
  vector<string> sh_cmd;
  sh_cmd.push_back("sh");
  sh_cmd.push_back("-c");
  sh_cmd.push_back(cmd);
  int rc = stap_system (s.verbose, sh_cmd);
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
  vector<compile_server_info> server_list;
  for (vector<compile_server_info>::const_iterator i = specified_servers.begin ();
       i != specified_servers.end ();
       ++i)
    {
      // If we have an ip address and port number, then just use the one we've
      // been given. Otherwise, check for matching online servers and try their
      // ip addresses and ports.
      if (i->hasAddress() && i->port() != 0)
	add_server_info (*i, server_list);
      else
	{
	  // Obtain a list of online servers.
	  vector<compile_server_info> online_servers;
	  get_or_keep_online_server_info (s, online_servers, false/*keep*/);

	  // If no specific server (port) has been specified,
	  // then we'll need the servers to be
	  // compatible and possible trusted as signers as well.
	  if (i->port() == 0)
	    {
	      get_or_keep_compatible_server_info (s, online_servers, true/*keep*/);
	      if (! pr_contains (s.privilege, pr_stapdev))
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
      clog << _("Unable to find a suitable compile server.") << endl;

      // Try to explain why.
      vector<compile_server_info> online_servers;
      get_or_keep_online_server_info (s, online_servers, false/*keep*/);
      if (online_servers.empty ())
	clog << _("No servers online to select from.") << endl;
      else
	{
	  clog << _("The following servers are online:") << endl;
	  clog << online_servers;
	  if (! specified_servers.empty ())
	    {
	      clog << _("The following servers were requested:") << endl;
	      clog << specified_servers;
	    }
	  else
	    {
	      string criteria = "online,trusted,compatible";
	      if (! pr_contains (s.privilege, pr_stapdev))
		criteria += ",signer";
	      clog << _F("No servers matched the selection criteria of %s.", criteria.c_str())
		   << endl;
	    }
	}
      return 1;
    }

  // Sort the list of servers into a preferred order.
  preferred_order (server_list);

  // Now try each of the identified servers in turn.
  int rc = compile_using_server (server_list);
  if (rc == SUCCESS)
    return 0; // success!

  // If the error was that a server's cert was expired, try again. This is because the server
  // should generate a new cert which may be automatically trusted by us if it is our server.
  // Give the server a chance to do this before retrying.
  if (rc == SERVER_CERT_EXPIRED_ERROR)
    {
      if (s.verbose >= 2)
	clog << _("The server's certificate was expired. Trying again") << endl << flush;
      sleep (2);
      rc = compile_using_server (server_list);
      if (rc == SUCCESS)
	return 0; // success!
    }

  // We were unable to use any available server
  clog << _("Unable to connect to a server.") << endl;
  clog << _("The following servers were tried:") << endl;
  clog << server_list;
  return 1; // Failure
}

int 
compile_server_client::compile_using_server (
  const vector<compile_server_info> &servers
)
{
  // Make sure NSPR is initialized. Must be done before NSS is initialized
  s.NSPR_init ();

  // Attempt connection using each of the available client certificate
  // databases. Assume the server certificate is invalid until proven otherwise.
  PR_SetError (SEC_ERROR_CA_CERT_INVALID, 0);
  vector<string> dbs = private_ssl_dbs;
  vector<string>::iterator i = dbs.end();
  dbs.insert (i, public_ssl_dbs.begin (), public_ssl_dbs.end ());
  int rc = GENERAL_ERROR; // assume failure
  bool serverCertExpired = false;
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
      SECStatus secStatus = nssInit (cert_dir);
      if (secStatus != SECSuccess)
	{
	  // Message already issued.
	  continue; // try next database
	}

      // Enable cipher suites which are allowed by U.S. export regulations.
      // SSL_ClearSessionCache is required for the new settings to take effect.
      secStatus = NSS_SetExportPolicy ();
      SSL_ClearSessionCache ();
      if (secStatus != SECSuccess)
	{
	  clog << _("Unable to set NSS export policy");
	  nssError ();
	  nssCleanup (cert_dir);
	  continue; // try next database
	}
  
      server_zipfile = s.tmpdir + "/server.zip";

      // Try each server in turn.
      for (vector<compile_server_info>::const_iterator j = servers.begin ();
	   j != servers.end ();
	   ++j)
	{
	  // At a minimum we need an ip_address along with a port
	  // number in order to contact the server.
	  if (! j->hasAddress() || j->port() == 0)
	    continue;

	  if (s.verbose >= 2)
           clog << _F("Attempting SSL connection with %s\n"
                "  using certificates from the database in %s\n",
                lex_cast(*j).c_str(), cert_dir);

	  rc = client_connect (*j, client_zipfile.c_str(), server_zipfile.c_str (),
			       NULL/*trustNewServer_p*/);
	  if (rc == SUCCESS)
	    {
	      s.winning_server = lex_cast(*j);
	      break; // Success!
	    }

	  // Server cert has expired. Try other servers and/or databases, but take note because
	  // server should generate a new certificate. If no other servers succeed, we'll try again
	  // in case the new cert works.
	  if (rc == SERVER_CERT_EXPIRED_ERROR)
	    {
	      serverCertExpired = true;
	      continue;
	    }

	  if (s.verbose >= 2)
	    {
	      clog << _("  Unable to connect: ");
	      nssError ();
	    }
	}

      // SSL_ClearSessionCache is required before shutdown for client applications.
      SSL_ClearSessionCache ();
      nssCleanup (cert_dir);

      if (rc == SECSuccess)
	break; // Success!
    }

  // Indicate whether a server cert was expired, so we can try again, if desired.
  if (rc != SUCCESS)
    {
      if (serverCertExpired)
	rc = SERVER_CERT_EXPIRED_ERROR;
    }

  return rc;
}

int
compile_server_client::unpack_response ()
{
  // Unzip the response package.
  server_tmpdir = s.tmpdir + "/server";
  vector<string> cmd;
  cmd.push_back("unzip");
  cmd.push_back("-qd");
  cmd.push_back(server_tmpdir);
  cmd.push_back(server_zipfile);
  int rc = stap_system (s.verbose, cmd);
  if (rc != 0)
    {
      clog << _F("Unable to unzip the server response '%s'\n", server_zipfile.c_str());
      return rc;
    }

  // Determine the server protocol version.
  string filename = server_tmpdir + "/version";
  if (file_exists (filename))
    ::read_from_file (filename, server_version);

  // Warn about the shortcomings of this server, if it is down level.
  show_server_compatibility ();

  // If the server's response contains a systemtap temp directory, move
  // its contents to our temp directory.
  glob_t globbuf;
  string filespec = server_tmpdir + "/stap??????";
  if (s.verbose >= 3)
    clog << _F("Searching \"%s\"\n", filespec.c_str());
  int r = glob(filespec.c_str (), 0, NULL, & globbuf);
  if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
    {
      if (globbuf.gl_pathc > 1)
	{
	  clog << _("Incorrect number of files in server response") << endl;
	  rc = 1;
	  goto done;
	}

      assert (globbuf.gl_pathc == 1);
      string dirname = globbuf.gl_pathv[0];
      if (s.verbose >= 3)
	clog << _("  found ") << dirname << endl;

      filespec = dirname + "/*";
      if (s.verbose >= 3)
       clog << _F("Searching \"%s\"\n", filespec.c_str());
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
	      if (s.verbose >= 3)
               clog << _F("  found %s -- linking from %s", oldname.c_str(), newname.c_str());
	      rc = symlink (oldname.c_str (), newname.c_str ());
	      if (rc != 0)
		{
                 clog << _F("Unable to link '%s' to '%s':%s\n",
			    oldname.c_str(), newname.c_str(), strerror(errno));
		  goto done;
		}
	    }
	}
    }

  // If the server version is less that 1.6, remove the output line due to the synthetic
  // server-side -k. Look for a message containing the name of the temporary directory.
  // We can look for the English message since server versions before 1.6 do not support
  // localization.
  if (server_version < "1.6")
    {
      cmd.clear();
      cmd.push_back("sed");
      cmd.push_back("-i");
      cmd.push_back("/^Keeping temporary directory.*/ d");
      cmd.push_back(server_tmpdir + "/stderr");
      stap_system (s.verbose, cmd);
    }

  // Remove the output line due to the synthetic server-side -p4
  cmd.clear();
  cmd.push_back("sed");
  cmd.push_back("-i");
  cmd.push_back("/^.*\\.ko$/ d");
  cmd.push_back(server_tmpdir + "/stdout");
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
      if (s.verbose >= 3)
       clog << _F("Searching \"%s\"\n", filespec.c_str());

      glob_t globbuf;
      int r = glob(filespec.c_str (), 0, NULL, & globbuf);
      if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
	{
	  if (globbuf.gl_pathc > 1)
	    clog << _("Incorrect number of modules in server response") << endl;
	  else
	    {
	      assert (globbuf.gl_pathc == 1);
	      string modname = globbuf.gl_pathv[0];
	      if (s.verbose >= 3)
		clog << _("  found ") << modname << endl;

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
	      string uprobes_ko;
	      if (server_version < "1.6")
		uprobes_ko = s.tmpdir + "/server/uprobes.ko";
	      else
		uprobes_ko = s.tmpdir + "/uprobes/uprobes.ko";

	      if (file_exists (uprobes_ko))
		{
		  s.need_uprobes = true;
		  s.uprobes_path = uprobes_ko;
		}
	    }
	}
      else if (s.have_script)
	{
	  if (rc == 0)
	    {
	      clog << _("No module was returned by the server.") << endl;
	      rc = 1;
	    }
	}
      globfree (& globbuf);
    }

  // Output stdout and stderr.
  filename = server_tmpdir + "/stderr";
  flush_to_stream (filename, clog);

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
      clog << _F("Unable to open file '%s' for reading: ", fname.c_str());
      goto error;
    }

  // Read the data;
  errno = 0;
  f >> data;
  if (f.fail ())
    {
      clog << _F("Unable to read from file '%s': ", fname.c_str());
      goto error;
    }

  // NB: not necessary to f.close ();
  return 0; // Success

 error:
  if (errno)
    clog << strerror (errno) << endl;
  else
    clog << _("unknown error") << endl;
  return 1; // Failure
}

template <class T>
int
compile_server_client::write_to_file (const string &fname, const T &data)
{
  // C++ streams may not set errno in the even of a failure. However if we
  // set it to 0 before each operation and it gets set during the operation,
  // then we can use its value in order to determine what happened.
  errno = 0;
  ofstream f (fname.c_str ());
  if (! f.good ())
    {
      clog << _F("Unable to open file '%s' for writing: ", fname.c_str());
      goto error;
    }

  // Write the data;
  f << data;
  errno = 0;
  if (f.fail ())
    {
      clog << _F("Unable to write to file '%s': ", fname.c_str());
      goto error;
    }

  // NB: not necessary to f.close ();
  return 0; // Success

 error:
  if (errno)
    clog << strerror (errno) << endl;
  else
    clog << _("unknown error") << endl;
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
      clog << _F("Unable to open file '%s' for reading: ", fname.c_str());
      goto error;
    }

  // Stream the data

  // NB: o << f.rdbuf() misbehaves for some reason, appearing to close o,
  // which is unfortunate if o == clog or cout.
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
    clog << strerror (errno) << endl;
  else
    clog << _("unknown error") << endl;
  return 1; // Failure
}

void
compile_server_client::show_server_compatibility () const
{
  // Locale sensitivity was added in version 1.6
  if (server_version < "1.6")
    {
      clog << _F("Server protocol version is %s\n", server_version.v);
      clog << _("The server does not use localization information passed by the client\n");
    }
}

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
    purpose = _("as a module signer for all users");
  else
    {
      purpose = _("as an SSL peer");
      if (cert_db_path == global_ssl_cert_db_path ())
	purpose += _(" for all users");
      else
	purpose += _(" for the current user");
    }

  // Issue a message for each server in the list with the same certificate.
  unsigned limit = server_list.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      if (server.certinfo != server_list[i].certinfo)
	continue;
      clog << server_list[i] << _(" is already ");
      if (revoking)
	clog << _("untrusted ") << purpose << endl;
      else
       clog << _("trusted ") << purpose << endl;
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
      clog << _F("Unable to find or create the client certificate database directory %s: ", cert_db_path.c_str());
      perror ("");
      return;
    }

  // Must predeclare this because of jumps to cleanup: below.
  vector<string> processed_certs;

  // Make sure NSPR is initialized. Must be done before NSS is initialized
  s.NSPR_init ();

  // Initialize the NSS libraries -- read/write
  SECStatus secStatus = nssInit (cert_db_path.c_str (), 1/*readwrite*/);
  if (secStatus != SECSuccess)
    {
      // Message already issued.
      goto cleanup;
    }

  // Enable cipher suites which are allowed by U.S. export regulations.
  // SSL_ClearSessionCache is required for the new settings to take effect.
  secStatus = NSS_SetExportPolicy ();
  SSL_ClearSessionCache ();
  if (secStatus != SECSuccess)
    {
      clog << _("Unable to set NSS export policy");
      nssError ();
      goto cleanup;
    }
  
  // Iterate over the servers to become trusted. Contact each one and
  // add it to the list of trusted servers if it is not already trusted.
  // client_connect will issue any error messages.
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
	  if (s.verbose >= 2)
	    trust_already_in_place (*server, server_list, cert_db_path, false/*revoking*/);
	  continue;
	}
      // At a minimum we need an ip_address along with a port
      // number in order to contact the server.
      if (! server->hasAddress() || server->port() == 0)
	continue;
      int rc = client_connect (*server, NULL, NULL, "permanent");
      if (rc != SUCCESS)
	{
	  clog << _F("Unable to connect to %s", lex_cast(*server).c_str()) << endl;
	  nssError ();
	}
    }

 cleanup:
  // Shutdown NSS.
  // SSL_ClearSessionCache is required before shutdown for client applications.
  SSL_ClearSessionCache ();
  nssCleanup (cert_db_path.c_str ());

  // Make sure the database files are readable.
  glob_t globbuf;
  string filespec = cert_db_path + "/*.db";
  if (s.verbose >= 3)
    clog << _F("Searching \"%s\"\n", filespec.c_str());
  int r = glob (filespec.c_str (), 0, NULL, & globbuf);
  if (r != GLOB_NOSPACE && r != GLOB_ABORTED && r != GLOB_NOMATCH)
    {
      for (unsigned i = 0; i < globbuf.gl_pathc; ++i)
	{
	  string filename = globbuf.gl_pathv[i];
	  if (s.verbose >= 3)
	    clog << _("  found ") << filename << endl;

	  if (chmod (filename.c_str (), 0644) != 0)
	    {
             s.print_warning("Unable to change permissions on " + filename + ": ");
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
      if (s.verbose >= 2)
	{
	  clog << _F("Certificate database '%s' does not exist",
		     cert_db_path.c_str()) << endl;
	  for (vector<compile_server_info>::const_iterator server = server_list.begin();
	       server != server_list.end ();
	       ++server)
	    trust_already_in_place (*server, server_list, cert_db_path, true/*revoking*/);
	}
      return;
    }

  // Must predeclare these because of jumps to cleanup: below.
  CERTCertDBHandle *handle;
  PRArenaPool *tmpArena = NULL;
  CERTCertList *certs = NULL;
  CERTCertificate *db_cert;
  vector<string> processed_certs;
  const char *nickname;

  // Make sure NSPR is initialized. Must be done before NSS is initialized
  s.NSPR_init ();

  // Initialize the NSS libraries -- read/write
  SECStatus secStatus = nssInit (cert_db_path.c_str (), 1/*readwrite*/);
  if (secStatus != SECSuccess)
    {
      // Message already issued
      goto cleanup;
    }
  handle = CERT_GetDefaultCertDB();

  // A memory pool to work in
  tmpArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  if (! tmpArena) 
    {
      clog << _("Out of memory:");
      nssError ();
      goto cleanup;
    }

  // Iterate over the servers to become untrusted.
  nickname = server_cert_nickname ();
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
      db_cert = PK11_FindCertFromNickname (nickname, NULL);
      if (! db_cert)
	{
	  // No trusted servers. Not an error, but issue a status message.
	  if (s.verbose >= 2)
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
         clog << _F("Unable to query certificate database %s: ",
                    cert_db_path.c_str()) << endl;
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
	  string serialNumber = get_cert_serial_number (db_cert);

	  // Does the serial number match that of the current server?
	  if (serialNumber != server->certinfo)
	    continue; // goto next certificate

	  // All is ok! Remove the certificate from the database.
	  break;
	} // Loop over certificates in the database

      // Was a certificate matching the server found?  */
      if (CERT_LIST_END (node, certs))
	{
	  // Not found. Server is already untrusted.
	  if (s.verbose >= 2)
	    trust_already_in_place (*server, server_list, cert_db_path, true/*revoking*/);
	}
      else
	{
	  secStatus = SEC_DeletePermCertificate (db_cert);
	  if (secStatus != SECSuccess)
	    {
             clog << _F("Unable to remove certificate from %s: ",
                        cert_db_path.c_str()) << endl;
	      nssError ();
	    }
	}
      CERT_DestroyCertList (certs);
      certs = NULL;
    } // Loop over servers

 cleanup:
  if (certs)
    CERT_DestroyCertList (certs);
  if (tmpArena)
    PORT_FreeArena (tmpArena, PR_FALSE);

  nssCleanup (cert_db_path.c_str ());
}

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
      if (s.verbose >= 2)
       clog << _F("Certificate database '%s' does not exist.",
                  cert_db_path.c_str()) << endl;
      return;
    }

  // Make sure NSPR is initialized. Must be done before NSS is initialized
  s.NSPR_init ();

  // Initialize the NSS libraries -- readonly
  SECStatus secStatus = nssInit (cert_db_path.c_str ());
  if (secStatus != SECSuccess)
    {
      // Message already issued.
      return;
    }

  // Must predeclare this because of jumps to cleanup: below.
  PRArenaPool *tmpArena = NULL;
  CERTCertList *certs = get_cert_list_from_db (server_cert_nickname ());
  if (! certs)
    {
      if (s.verbose >= 2)
	clog << _F("No certificate found in database %s", cert_db_path.c_str ()) << endl;
      goto cleanup;
    }

  // A memory pool to work in
  tmpArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  if (! tmpArena) 
    {
      clog << _("Out of memory:");
      nssError ();
      goto cleanup;
    }
  for (CERTCertListNode *node = CERT_LIST_HEAD (certs);
       ! CERT_LIST_END (node, certs);
       node = CERT_LIST_NEXT (node))
    {
      compile_server_info server_info;

      // The certificate we're working with.
      CERTCertificate *db_cert = node->cert;

      // Get the host name. It is in the alt-name extension of the
      // certificate.
      SECItem subAltName;
      subAltName.data = NULL;
      secStatus = CERT_FindCertExtension (db_cert,
					  SEC_OID_X509_SUBJECT_ALT_NAME,
					  & subAltName);
      if (secStatus != SECSuccess || ! subAltName.data)
	{
	  clog << _("Unable to find alt name extension on server certificate: ") << endl;
	  nssError ();
	  continue;
	}

      // Decode the extension.
      CERTGeneralName *nameList = CERT_DecodeAltNameExtension (tmpArena, & subAltName);
      SECITEM_FreeItem(& subAltName, PR_FALSE);
      if (! nameList)
	{
	  clog << _("Unable to decode alt name extension on server certificate: ") << endl;
	  nssError ();
	  continue;
	}

      // We're interested in the first alternate name.
      assert (nameList->type == certDNSName);
      server_info.host_name = string ((const char *)nameList->name.other.data,
				      nameList->name.other.len);
      // Don't free nameList. It's part of the tmpArena.

      // Get the serial number.
      server_info.certinfo = get_cert_serial_number (db_cert);

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
  if (tmpArena)
    PORT_FreeArena (tmpArena, PR_FALSE);

  nssCleanup (cert_db_path.c_str ());
}

// Utility Functions.
//-----------------------------------------------------------------------
ostream &operator<< (ostream &s, const compile_server_info &i)
{
  s << " host=";
  if (! i.host_name.empty ())
    s << i.host_name;
  else
    s << "unknown";
  s << " address=";
  if (i.hasAddress())
    {
      PRStatus prStatus;
      switch (i.address.raw.family)
	{
	case PR_AF_INET:
	case PR_AF_INET6:
	  {
#define MAX_NETADDR_SIZE 46 // from the NSPR API reference.
	    char buf[MAX_NETADDR_SIZE];
	    prStatus = PR_NetAddrToString(& i.address, buf, sizeof (buf));
	    if (prStatus == PR_SUCCESS) {
	      s << buf;
	      break;
	    }
	  }
	  // Fall through
	default:
	  s << "offline";
	  break;
	}
    }
  else
    s << "offline";
  s << " port=";
  if (i.port() != 0)
    s << i.port();
  else
    s << "unknown";
  s << " sysinfo=\"";
  if (! i.sysinfo.empty ())
    s << i.sysinfo << '"';
  else
    s << "unknown\"";
  s << " version=";
  if (! i.version.empty ())
    s << i.version;
  else
    s << "unknown";
  s << " certinfo=\"";
  if (! i.certinfo.empty ())
    s << i.certinfo << '"';
  else
    s << "unknown\"";
  return s;
    }

ostream &operator<< (ostream &s, const vector<compile_server_info> &v)
{
  for (unsigned i = 0; i < v.size(); ++i)
    s << v[i] << endl;
  return s;
}

bool
operator!= (const PRNetAddr &x, const PRNetAddr &y)
{
  // Same address family?
  if (x.raw.family != y.raw.family)
    return true;

  // Compare all fields except the port and the flow info. Do this by comparing the other fields
  // directly as the other (unused) fields can sometimes contain garbage.
  switch (x.raw.family)
    {
    case PR_AF_INET6:
      return x.ipv6.scope_id != y.ipv6.scope_id ||
	memcmp (& x.ipv6.ip, & y.ipv6.ip, sizeof(x.ipv6.ip)) != 0;
      break;
    case PR_AF_INET:
      return x.inet.ip != y.inet.ip;
      break;
    default:
      break;
    }
  return true;
}

static PRIPv6Addr &
copyAddress (PRIPv6Addr &PRin6, const in6_addr &in6)
{
  // The NSPR type is a typedef of struct in6_addr, but C++ won't let us copy it
  assert (sizeof (PRin6) == sizeof (in6));
  memcpy (& PRin6, & in6, sizeof (PRin6));
  return PRin6;
}

// Return the default server specification, used when none is given on the
// command line.
static string
default_server_spec (const systemtap_session &s)
{
  // If --privilege=X has been used, where X is not stapdev,
  //   the default is online,trusted,compatible,signer
  // otherwise
  //   the default is online,trusted,compatible
  //
  // Having said that,
  //   'online' and 'compatible' will only succeed if we have avahi
  //   'trusted' and 'signer' will only succeed if we have NSS
  //
  string working_string = "online,trusted,compatible";
  if (! pr_contains (s.privilege, pr_stapdev))
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
          // XXX PR13274 needs-session to use print_warning()
	  clog << _F("WARNING: unsupported compile server property: %s", property.c_str())
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

  // Sort the list of servers into a preferred order.
  preferred_order (servers);

  // Print the server information. Skip the empty entry at the head of the list.
  clog << _F("Systemtap Compile Server Status for '%s'", working_string.c_str()) << endl;
  bool found = false;
  unsigned limit = servers.size ();
  for (unsigned i = 0; i < limit; ++i)
    {
      assert (! servers[i].empty ());
      // Don't list servers with no cert information. They may not actually
      // exist.
      // TODO: Could try contacting the server and obtaining its cert
      if (servers[i].certinfo.empty ())
	continue;
      clog << servers[i] << endl;
      found = true;
    }
  if (! found)
    clog << _("No servers found") << endl;
}

// Add or remove trust of the servers specified on the command line.
void
manage_server_trust (systemtap_session &s)
{
  // This function should do nothing if we don't have NSS.
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
	      clog << _("Only root can specify 'signer' on --trust-servers") << endl;
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
	      clog << _("Only root can specify 'all-users' on --trust-servers") << endl;
	      error = true;
	    }
	  else
	    all_users = true;
	}
      else if (*i == "no-prompt")
	no_prompt = true;
      else
	s.print_warning("Unrecognized server trust specification: " + *i);
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
      clog << _("No servers identified for trust") << endl;
      return;
    }

  // Create a string representing the request in English.
  // If neither 'ssl' or 'signer' was specified, the default is 'ssl'.
  if (! ssl && ! signer)
    ssl = true;
  ostringstream trustString;
  if (ssl)
    {
      trustString << _("as an SSL peer");
      if (all_users)
	trustString << _(" for all users");
      else
	trustString << _(" for the current user");
    }
  if (signer)
    {
      if (ssl)
	trustString << _(" and ");
      trustString << _("as a module signer for all users");
    }

  // Prompt the user to confirm what's about to happen.
  if (no_prompt)
    {
      if (revoke)
	clog << _("Revoking trust ");
      else
	clog << _("Adding trust ");
    }
  else
    {
      if (revoke)
	clog << _("Revoke trust ");
      else
	clog << _("Add trust ");
    }
  clog << _F("in the following servers %s", trustString.str().c_str());
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
	  clog << _("Server trust unchanged") << endl;
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
	cert_db_path = private_ssl_cert_db_path ();
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
}

static compile_server_cache*
cscache(systemtap_session& s)
{
  if (!s.server_cache)
    s.server_cache = new compile_server_cache();
  return s.server_cache;
}

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
  // Now filter or accumulate the list depending on whether a query has
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
  // We only need to obtain this once per session. This is a good thing(tm)
  // since obtaining this information is expensive.
  vector<compile_server_info>& default_servers = cscache(s)->default_servers;
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

static bool
isPort (const char *pstr, compile_server_info &server_info)
{
  errno = 0;
  char *estr;
  unsigned long p = strtoul (pstr, & estr, 10);
  if (errno != 0 || *estr != '\0' || p > USHRT_MAX)
    {
      clog << _F("Invalid port number specified: %s", pstr) << endl;
      return false;
    }
  server_info.setPort (p);
  return true;
}

static bool
isIPv6 (const string &server, compile_server_info &server_info)
{
  // An IPv6 address is 8 hex components separated by colons.
  // One contiguous block of zero segments in the address may be elided using ::.
  // An interface may be specified by appending %IF_NAME to the address (e.g. %eth0).
  // For now, assume eth0 if none is specified.
  // A port may be specified by enclosing the ip address in [] and adding :<port>.
  // Allow a bracketed address without a port.
  assert (! server.empty());
  string ip;
  string::size_type portIx;
  if (server[0] == '[')
    {
      string::size_type endBracket = server.find (']');
      if (endBracket == string::npos)
	return false; // Not a valid IPv6 address
      // Extract the address.
      ip = server.substr (1, endBracket - 1);
      portIx = endBracket + 1;
    }
  else
    {
      ip = server;
      portIx = string::npos;
    }

  // Find out how many components there are. The maximum is 8
  unsigned empty = 0;
  vector<string> components;
  tokenize_full (ip, components, ":");
  if (components.size() > 8)
    return false; // Not a valid IPv6 address

  // The components must be either hex values between 0 and 0xffff, or must be empty.
  // There can be only one empty component.
  string interface;
  for (unsigned i = 0; i < components.size(); ++i)
    {
      if (components[i].empty())
	{
	  if (++empty > 1)
	    return false; // Not a valid IPv6 address
	}
      // If it's the final component, see if it specifies the interface. If so, strip it from the
      // component in order to simplify parsing. It still remains as part of the original ip address
      // string.
      if (i == components.size() - 1)
	{
	  size_t ix = components[i].find ('%');
	  if (ix != string::npos)
	    {
	      interface = components[i].substr(ix);
	      components[i] = components[i].substr(0, ix);
	    }
	}
      // Skip leading zeroes.
      unsigned j;
      for (j = 0; j < components[i].size(); ++j)
	{
	  if (components[i][j] != '0')
	    break;
	}
      // Max of 4 hex digits
      if (components[i].size() - j > 4)
	return false; // Not a valid IPv6 address
      for (/**/; j < components[i].size(); ++j)
	{
	  if (! isxdigit (components[i][j]))
	    return false; // Not a valid IPv6 address
	}
    }
  // If there is no empty component, then there must be exactly 8 components.
  if (! empty && components.size() != 8)
    return false; // Not a valid IPv6 address

  // Calls to setPort is isPort need to know that this is an IPv6 address.
  server_info.address.raw.family = PR_AF_INET6;

  // Examine the optional port
  if (portIx != string::npos)
    {
      string port = server.substr (portIx);
      if (port.size() != 0)
	{
	  if (port.size() < 2 || port[0] != ':')
	    return false; // Not a valid Port

	  port = port.substr (1);
	  if (! isPort (port.c_str(), server_info))
	    return false; // not a valid port
	}
    }
  else
    server_info.setPort (0);

  // If no interface as specified, the use eth0.
  if (interface.empty() && ip != "::1")
    ip += "%eth0";

  // Treat the ip address string like a host name.
  server_info.host_name = ip;
  return true; // valid IPv6 address.
}

static bool
isIPv4 (const string &server, compile_server_info &server_info)
{
  // An IPv4 address is 4 decimal components separated by periods with an
  // additional optional decimal port separated from the address by a colon.
  assert (! server.empty());

  // Find out how many components there are. The maximum is 8
  vector<string> components;
  tokenize (server, components, ":");
  if (components.size() > 2)
    return false; // Not a valid IPv4 address

  // Separate the host from the port (if any).
  string host;
  string port;
  if (components.size() <= 1)
    host = server;
  else {
    host = components[0];
    port = components[1];
  }

  // Separate the host components.
  // There must be exactly 4 components.
  components.clear ();
  tokenize (server, components, ".");
  if (components.size() != 4)
    return false; // Not a valid IPv4 address
  
  // The components must be decimal values between 0 and 255.
  for (unsigned i = 0; i < components.size(); ++i)
    {
      if (components[i].empty())
	return false; // Not a valid IPv4 address
      errno = 0;
      char *estr;
      long p = strtol (components[i].c_str(), & estr, 10);
      if (errno != 0 || *estr != '\0' || p < 0 || p > 255)
	return false; // Not a valid IPv4 address
    }

  // Calls to setPort is isPort need to know that this is an IPv4 address.
  server_info.address.raw.family = PR_AF_INET;

  // Examine the optional port
  if (! port.empty ()) {
    if (! isPort (port.c_str(), server_info))
      return false; // not a valid port
  }
  else
    server_info.setPort (0);

  // Treat the ip address string like a host name.
  server_info.host_name = host;
  return true; // valid IPv4 address.
}

static bool
isCertSerialNumber (const string &server, compile_server_info &server_info)
{
  // This function assumes that we have already ruled out the server spec being an IPv6 address.
  // Certificate serial numbers are 5 fields separated by colons plus an optional 6th decimal
  // field specifying a port.
  // Assume IPv4 (for now) when storing the port.
  server_info.address.raw.family = PR_AF_INET;
  assert (! server.empty());
  string host = server;
  vector<string> components;
  tokenize (host, components, ":");
  switch (components.size ())
    {
    case 6:
      if (! isPort (components.back().c_str(), server_info))
	return false; // not a valid port
      host = host.substr (0, host.find_last_of (':'));
      // fall through
    case 5:
      server_info.certinfo = host;
      break;
    default:
      return false; // not a cert serial number
    }

  return true; // valid cert serial number and optional port
}

static bool
isDomain (const string &server, compile_server_info &server_info)
{
  // Accept one or two components separated by a colon. The first will be the domain name and
  // the second must a port number.
  // Assume IPv4 (for now) when storing the port.
  server_info.address.raw.family = PR_AF_INET;
  assert (! server.empty());
  string host = server;
  vector<string> components;
  tokenize (host, components, ":");
  switch (components.size ())
    {
    case 2:
      if (! isPort (components.back().c_str(), server_info))
	return false; // not a valid port
      host = host.substr (0, host.find_last_of (':'));
      // fall through
    case 1:
      server_info.host_name = host;
      break;
    default:
      return false; // not a valid domain name
    }

  return true;
}

static void
get_specified_server_info (
  systemtap_session &s,
  vector<compile_server_info> &servers,
  bool no_default
)
{
  // We only need to obtain this once per session. This is a good thing(tm)
  // since obtaining this information is expensive.
  vector<compile_server_info>& specified_servers = cscache(s)->specified_servers;
  if (specified_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      specified_servers.push_back (compile_server_info ());

      // If --use-server was not specified at all, then return info for the
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

	      // If no specific server(s) specified, then use the default servers.
	      if (server.empty ())
		{
		  if (! no_default)
		    get_default_server_info (s, specified_servers);
		  continue;
		}

	      // Determine what has been specified. Servers may be specified by:
	      // - domain{:port}
	      // - certificate-serial-number{:port}
              // - IPv4-address{:port}
              // - IPv6-address{:port}
	      // where items within {} are optional.
	      // Check for IPv6 addresses first. It reduces the amount of checking necessary for
	      // certificate serial numbers.
	      compile_server_info server_info;
	      vector<compile_server_info> known_servers;
	      if (isIPv6 (server, server_info) || isIPv4 (server, server_info) ||
		  isDomain (server, server_info))
		{
		  // Find known servers matching the specified information.
		  get_all_server_info (s, known_servers);
		  // Resolve this host and add any information that is discovered.
		  resolve_host (s, server_info, specified_servers);
		  // Keep the common server info.
		  keep_common_server_info (known_servers, specified_servers);
		}
	      else if (isCertSerialNumber (server, server_info))
		{
		  // The host could not be resolved. Try resolving it as a certificate serial
		  // number. Look for all known servers with this serial number and (optional)
		  // port number.
		  get_all_server_info (s, known_servers);
		  keep_server_info_with_cert_and_port (s, server_info, known_servers);
		  // Did we find one?
		  if (known_servers.empty ())
		    {
		      if (s.verbose >= 2)
			clog << _F("No server matching %s found", server.c_str()) << endl;
		    }
		  else
		    add_server_info (known_servers, specified_servers);
		}
	      else
		{
		  clog << _F("Invalid server specification for --use-server: %s", server.c_str())
		       << endl;
		}
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

  // We only need to obtain this once per session. This is a good thing(tm)
  // since obtaining this information is expensive.
  vector<compile_server_info>& trusted_servers = cscache(s)->trusted_servers;
  if (trusted_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      trusted_servers.push_back (compile_server_info ());

      // Check the private database first.
      string cert_db_path = private_ssl_cert_db_path ();
      get_server_info_from_db (s, trusted_servers, cert_db_path);

      // Now check the global database.
      cert_db_path = global_ssl_cert_db_path ();
      get_server_info_from_db (s, trusted_servers, cert_db_path);
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

  // We only need to obtain this once per session. This is a good thing(tm)
  // since obtaining this information is expensive.
  vector<compile_server_info>& signing_servers = cscache(s)->signing_servers;
  if (signing_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      signing_servers.push_back (compile_server_info ());

      // For all users, check the global database.
      string cert_db_path = signing_cert_db_path ();
      get_server_info_from_db (s, signing_servers, cert_db_path);
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
  if (s.verbose >= 2)
    clog << _("Unable to detect server compatibility without avahi") << endl;
  if (keep)
    servers.clear ();
#endif
}

static void
keep_server_info_with_cert_and_port (
  systemtap_session &,
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
	  (servers[i].port() == 0 || server.port() == 0 ||
	   servers[i].port() == server.port()))
	{
	  // If the server is not online, then use the specified
	  // port, if any.
	  if (servers[i].port() == 0)
	    servers[i].setPort (server.port());
	  ++i;
	  continue;
	}
      // The item does not match. Delete it.
      servers.erase (servers.begin () + i);
    }
}

// Obtain missing host name or ip address, if any. Return 0 on success.
static void
resolve_host (
  systemtap_session&,
  compile_server_info &server,
  vector<compile_server_info> &resolved_servers
)
{
  // The server's host_name member is a string containing either a host name or an ip address.
  // Either is acceptable for lookup.
  const char *lookup_name = server.host_name.c_str();
  struct addrinfo hints;
  memset(& hints, 0, sizeof (hints));
  hints.ai_family = AF_UNSPEC; // AF_INET or AF_INET6 to force version
  struct addrinfo *addr_info;
  int rc = getaddrinfo (lookup_name, NULL, & hints, & addr_info);

  // Failure to resolve will result in an appropriate message later, if other methods fail.
  if (rc != 0)
    {
      // At a minimum, return the information we were given.
      add_server_info (server, resolved_servers);
      return;
    }

  // Loop over the results collecting information.
  assert (addr_info);
  for (const struct addrinfo *ai = addr_info; ai != NULL; ai = ai->ai_next)
    {
      // Start with the info we were given.
      compile_server_info new_server = server;

      // We support IPv4 and IPv6, Ignore other protocols,
      if (ai->ai_family == AF_INET)
	{
	  // IPv4 Address
	  struct sockaddr_in *ip = (struct sockaddr_in *)ai->ai_addr;
	  new_server.address.inet.family = PR_AF_INET;
	  if (ip->sin_port != 0)
	    new_server.address.inet.port = ip->sin_port;
	  new_server.address.inet.ip = ip->sin_addr.s_addr;
	}
      else if (ai->ai_family == AF_INET6)
	{
	  // IPv6 Address
	  struct sockaddr_in6 *ip = (struct sockaddr_in6 *)ai->ai_addr;
	  new_server.address.ipv6.family = PR_AF_INET6;
	  if (ip->sin6_port != 0)
	    new_server.address.ipv6.port = ip->sin6_port;
	  new_server.address.ipv6.scope_id = ip->sin6_scope_id;
	  copyAddress (new_server.address.ipv6.ip, ip->sin6_addr);
	}
      else
	continue;

      // Try to obtain a host name. Otherwise, leave it empty.
      char hbuf[NI_MAXHOST];
      int status = getnameinfo (ai->ai_addr, ai->ai_addrlen, hbuf, sizeof (hbuf), NULL, 0,
				NI_NAMEREQD | NI_IDN);
      if (status == 0)
	new_server.host_name = hbuf;

      // Add the new resolved server to the list.
      add_server_info (new_server, resolved_servers);
    }

  assert (addr_info);
  freeaddrinfo (addr_info); // free the linked list
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
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags /*flags*/,
    AVAHI_GCC_UNUSED void* userdata)
 {
   PRStatus prStatus;

    assert(r);
    const browsing_context *context = (browsing_context *)userdata;
    vector<compile_server_info> *servers = context->servers;

    // Called whenever a service has been resolved successfully or timed out.

    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
         clog << _F("Failed to resolve service '%s' of type '%s' in domain '%s': %s",
                 name, type, domain,
                 avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r)))) << endl;
            break;

        case AVAHI_RESOLVER_FOUND: {
	    compile_server_info info;

	    // Decode the address.
            char a[AVAHI_ADDRESS_STR_MAX];
            avahi_address_snprint(a, sizeof(a), address);
	    prStatus = PR_StringToNetAddr (a, & info.address);
	    if (prStatus != PR_SUCCESS) {
	      clog << _F("Invalid address '%s' from avahi", a) << endl;
	      break;
	    }
  
	    // We support both IPv4 and IPv6. Ignore other protocols.
	    if (protocol == AVAHI_PROTO_INET6) {
	      info.address.ipv6.family = PR_AF_INET6;
	      info.address.ipv6.port = htons (port);
	      info.address.ipv6.scope_id = interface;
	    }
	    else if (protocol == AVAHI_PROTO_INET) {
	      info.address.inet.family = PR_AF_INET;
	      info.address.inet.port = htons (port);
	    }
	    else
	      break;

	    // Save the host name.
	    info.host_name = host_name;

	    // Save the text tags.
	    char *t = avahi_string_list_to_string(txt);
	    info.sysinfo = extract_field_from_avahi_txt ("sysinfo=", t);
	    info.certinfo = extract_field_from_avahi_txt ("certinfo=", t);
	    info.version = extract_field_from_avahi_txt ("version=", t);
	    if (info.version.empty ())
	      info.version = "1.0"; // default version is 1.0
	    avahi_free(t);

	    // Add this server to the list of discovered servers.
	    add_server_info (info, *servers);
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
	    clog << _F("Avahi browse failed: %s",
	          avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))))
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
             clog << _F("Failed to resolve service '%s': %s",
                     name, avahi_strerror(avahi_client_errno(c))) << endl;
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
        clog << _F("Avahi Server connection failure: %s", avahi_strerror(avahi_client_errno(c))) << endl;
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

  // We only need to obtain this once per session. This is a good thing(tm)
  // since obtaining this information is expensive.
  vector<compile_server_info>& online_servers = cscache(s)->online_servers;
  if (online_servers.empty ())
    {
      // Maintain an empty entry to indicate that this search has been
      // performed, in case the search comes up empty.
      online_servers.push_back (compile_server_info ());
#if HAVE_AVAHI
      // Must predeclare these due to jumping on error to fail:
      unsigned limit;
      vector<compile_server_info> avahi_servers;

      // Initialize.
      AvahiClient *client = NULL;
      AvahiServiceBrowser *sb = NULL;
 
      // Allocate main loop object.
      AvahiSimplePoll *simple_poll;
      if (!(simple_poll = avahi_simple_poll_new()))
	{
	  clog << _("Failed to create Avahi simple poll object") << endl;
	  goto fail;
	}
      browsing_context context;
      context.simple_poll = simple_poll;
      context.servers = & avahi_servers;

      // Allocate a new Avahi client
      int error;
      client = avahi_client_new (avahi_simple_poll_get (simple_poll),
				 (AvahiClientFlags)0,
				 client_callback, & context, & error);

      // Check whether creating the client object succeeded.
      if (! client)
	{
         clog << _F("Failed to create Avahi client: %s",
                    avahi_strerror(error)) << endl;
	  goto fail;
	}
      context.client = client;
    
      // Create the service browser.
      if (!(sb = avahi_service_browser_new (client, AVAHI_IF_UNSPEC,
					    AVAHI_PROTO_UNSPEC, "_stap._tcp",
					    NULL, (AvahiLookupFlags)0,
					    browse_callback, & context)))
	{
         clog << _F("Failed to create Avahi service browser: %s",
                     avahi_strerror(avahi_client_errno(client))) << endl;
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

      // Resolve each server discovered, in case there are alternate ways to reach them
      // (e.g. localhost).
      limit = avahi_servers.size ();
      for (unsigned i = 0; i < limit; ++i)
	{
	  compile_server_info &avahi_server = avahi_servers[i];

	  // Delete the domain, if it is '.local'
	  string &host_name = avahi_server.host_name;
	  string::size_type dot_index = host_name.find ('.');
	  assert (dot_index != 0);
	  string domain = host_name.substr (dot_index + 1);
	  if (domain == "local")
	    host_name = host_name.substr (0, dot_index);

	  // Add it to the list of servers, unless it is duplicate.
	  resolve_host (s, avahi_server, online_servers);
	}

      // Merge with the list of servers, as obtained by avahi.
      add_server_info (avahi_servers, online_servers);

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
      if (s.verbose >= 2)
	clog << _("Unable to detect online servers without avahi") << endl;
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
  if (! target.hasAddress ())
    target.address = source.address;
  if (target.port() == 0)
    target.setPort (source.port());
  if (target.sysinfo.empty ())
    target.sysinfo = source.sysinfo;
  if (target.version.empty ())
    target.version = source.version;
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
#endif // HAVE_NSS

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
