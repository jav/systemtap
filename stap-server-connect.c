/*
  SSL server program listens on a port, accepts client connection, reads
  the data into a temporary file, calls the systemtap server script and
  then transmits the resulting fileback to the client.

  Copyright (C) 2008, 2009 Red Hat Inc.

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <ssl.h>
#include <nspr.h>
#include <plgetopt.h>
#include <nss.h>
#include <pk11func.h>

#define READ_BUFFER_SIZE (60 * 1024)

/* Global variables */
static char             *password = NULL;
static CERTCertificate  *cert = NULL;
static SECKEYPrivateKey *privKey = NULL;
static char             *dbdir   = NULL;
static char requestFileName[] = "/tmp/stap.server.client.zip.XXXXXX";
static char responseDirName[] = "/tmp/stap.server.XXXXXX";
static char responseZipName[] = "/tmp/stap.server.XXXXXX.zip.XXXXXX";

static void
Usage(const char *progName)
{
  fprintf(stderr, 
	  "Usage: %s -p port -d dbdir -n rsa_nickname -w passwordFile\n",
	  progName);
  exit(1);
}

static void
errWarn(char *function)
{
  PRErrorCode  errorNumber = PR_GetError();

  printf("Error in function %s: %d\n\n", function, errorNumber);
}

static void
exitErr(char *function)
{
  errWarn(function);
  /* Exit gracefully. */
  /* ignoring return value of NSS_Shutdown as code exits with 1*/
  (void) NSS_Shutdown();
  PR_Cleanup();
  exit(1);
}

/* Function:  readDataFromSocket()
 *
 * Purpose:  Read data from the socket into a temporary file.
 *
 */
static SECStatus
readDataFromSocket(PRFileDesc *sslSocket)
{
  PRFileDesc *local_file_fd;
  PRFileInfo  info;
  PRInt32     numBytesRead;
  PRInt32     numBytesWritten;
  PRInt32     totalBytes;
  char        buffer[READ_BUFFER_SIZE];

  /* Open the output file.  */
  local_file_fd = PR_Open(requestFileName, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
			  PR_IRUSR | PR_IWUSR | PR_IRGRP | PR_IWGRP | PR_IROTH);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "could not open output file %s\n", requestFileName);
      return SECFailure;
    }

  /* Read the number fo bytes to be received.  */
  numBytesRead = PR_Read(sslSocket, & info.size, sizeof (info.size));
  if (numBytesRead == 0) /* EOF */
    {
      fprintf (stderr, "Error reading size of request file\n");
      return SECFailure;
    }
  if (numBytesRead < 0)
    {
      errWarn("PR_Read");
      return SECFailure;
    }

  /* Read until EOF or until the expected number of bytes has been read. */
  for (totalBytes = 0; totalBytes < info.size; totalBytes += numBytesRead)
    {
      numBytesRead = PR_Read(sslSocket, buffer, READ_BUFFER_SIZE);
      if (numBytesRead == 0)
	break;	/* EOF */
      if (numBytesRead < 0)
	{
	  errWarn("PR_Read");
	  break;
	}

      /* Write to stdout */
      numBytesWritten = PR_Write(local_file_fd, buffer, numBytesRead);
      if (numBytesWritten < 0)
	fprintf (stderr, "could not write to output file %s\n", requestFileName);
      if (numBytesWritten != numBytesRead)
	fprintf (stderr, "could not write to output file %s\n", requestFileName);
#if DEBUG
      fprintf(stderr, "***** Connection read %d bytes.\n", numBytesRead);
#if 0
      buffer[numBytesRead] = '\0';
      fprintf(stderr, "************\n%s\n************\n", buffer);
#endif
#endif
    }

  if (totalBytes != info.size)
    {
      fprintf (stderr, "Expected %d bytes, got %d\n", info.size, totalBytes);
      return SECFailure;
    }

  PR_Close(local_file_fd);

  return SECSuccess;
}

/* Function:  setupSSLSocket()
 *
 * Purpose:  Configure a socket for SSL.
 *
 *
 */
static PRFileDesc * 
setupSSLSocket(PRFileDesc *tcpSocket)
{
  PRFileDesc *sslSocket;
  SSLKEAType  certKEA;
#if 0
  int         certErr = 0;
#endif
  SECStatus   secStatus;

  /* Inport the socket into SSL.  */
  sslSocket = SSL_ImportFD(NULL, tcpSocket);
  if (sslSocket == NULL)
    {
      errWarn("SSL_ImportFD");
      goto loser;
    }
   
  /* Set the appropriate flags. */
  secStatus = SSL_OptionSet(sslSocket, SSL_SECURITY, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_OptionSet SSL_SECURITY");
      goto loser;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_SERVER, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_OptionSet:SSL_HANDSHAKE_AS_SERVER");
      goto loser;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_REQUEST_CERTIFICATE, PR_FALSE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_OptionSet:SSL_REQUEST_CERTIFICATE");
      goto loser;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_REQUIRE_CERTIFICATE, PR_FALSE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_OptionSet:SSL_REQUIRE_CERTIFICATE");
      goto loser;
    }

  /* Set the appropriate callback routines. */
#if 0 /* use the default */
  secStatus = SSL_AuthCertificateHook(sslSocket, myAuthCertificate, 
				      CERT_GetDefaultCertDB());
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_AuthCertificateHook");
      goto loser;
    }
#endif
#if 0 /* Use the default */
  secStatus = SSL_BadCertHook(sslSocket, 
			      (SSLBadCertHandler)myBadCertHandler, &certErr);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_BadCertHook");
      goto loser;
    }
#endif
#if 0 /* no handshake callback */
  secStatus = SSL_HandshakeCallback(sslSocket,
				    myHandshakeCallback,
				    NULL);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_HandshakeCallback");
      goto loser;
    }
#endif
  secStatus = SSL_SetPKCS11PinArg(sslSocket, password);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_HandshakeCallback");
      goto loser;
    }

  certKEA = NSS_FindCertKEAType(cert);

  secStatus = SSL_ConfigSecureServer(sslSocket, cert, privKey, certKEA);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_ConfigSecureServer");
      goto loser;
    }

  return sslSocket;

loser:
  PR_Close(tcpSocket);
  return NULL;
}

#if 0 /* No client authentication and not authenticating after each transaction.  */
/* Function:  authenticateSocket()
 *
 * Purpose:  Perform client authentication on the socket.
 *
 */
static SECStatus
authenticateSocket(PRFileDesc *sslSocket, PRBool requireCert)
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
      errWarn("SSL_OptionSet:SSL_REQUEST_CERTIFICATE");
      return SECFailure;
    }

  /* If desired, require client to authenticate itself.  Note
   * SSL_REQUEST_CERTIFICATE must also be on, as above.  */
  secStatus = SSL_OptionSet(sslSocket, SSL_REQUIRE_CERTIFICATE, requireCert);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_OptionSet:SSL_REQUIRE_CERTIFICATE");
      return SECFailure;
    }

  /* Having changed socket configuration parameters, redo handshake. */
  secStatus = SSL_ReHandshake(sslSocket, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_ReHandshake");
      return SECFailure;
    }

  /* Force the handshake to complete before moving on. */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_ForceHandshake");
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
writeDataToSocket(PRFileDesc *sslSocket)
{
  int         numBytes;
  PRFileDesc *local_file_fd;
  PRFileInfo  info;
  PRStatus    prStatus;

  /* Try to open the local file named.	
   * If successful, then write it to the client.
   */
  prStatus = PR_GetFileInfo(responseZipName, &info);
  if (prStatus != PR_SUCCESS || info.type != PR_FILE_FILE || info.size < 0)
    {
      fprintf (stderr, "Input file %s not found\n", responseZipName);
      return SECFailure;
    }

  local_file_fd = PR_Open(responseZipName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "Could not open input file %s\n", responseZipName);
      return SECFailure;
    }

  /* Transmit the local file across the socket.
   */
  numBytes = PR_TransmitFile(sslSocket, local_file_fd, 
			     NULL, 0,
			     PR_TRANSMITFILE_KEEP_OPEN,
			     PR_INTERVAL_NO_TIMEOUT);

  /* Error in transmission. */
  if (numBytes < 0)
    {
      errWarn("PR_TransmitFile");
      return SECFailure;
    }
#if DEBUG
  /* Transmitted bytes successfully. */
  fprintf(stderr, "PR_TransmitFile wrote %d bytes from %s\n",
	  numBytes, responseZipName);
#endif

  PR_Close(local_file_fd);

  return SECSuccess;
}

/* Function:  int handle_connection()
 *
 * Purpose:  Handle a connection to a socket.
 *
 */
static SECStatus
handle_connection(PRFileDesc *tcpSocket)
{
  PRFileDesc *       sslSocket = NULL;
  SECStatus          secStatus = SECFailure;
  PRStatus           prStatus;
  PRSocketOptionData socketOption;
  PRFileInfo         info;
  char              *cmdline;
  int                rc;
  char              *rc1;

  /* Make sure the socket is blocking. */
  socketOption.option             = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;
  PR_SetSocketOption(tcpSocket, &socketOption);

  sslSocket = setupSSLSocket(tcpSocket);
  if (sslSocket == NULL)
    {
      errWarn("setupSSLSocket");
      goto cleanup;
    }

  secStatus = SSL_ResetHandshake(sslSocket, /* asServer */ PR_TRUE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_ResetHandshake");
      goto cleanup;
    }

  /* Force the handshake to complete before moving on. */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_ForceHandshake");
      goto cleanup;
    }

  /* Create a temporary files and directories.  */
  memcpy (requestFileName + sizeof (requestFileName) - 1 - 6, "XXXXXX", 6);
  rc = mkstemp(requestFileName);
  if (rc == -1)
    {
      fprintf (stderr, "Could not create temporary file %s\n", requestFileName);
      perror ("");
      secStatus = SECFailure;
      goto cleanup;
    }

  memcpy (responseDirName + sizeof (responseDirName) - 1 - 6, "XXXXXX", 6);
  rc1 = mkdtemp(responseDirName);
  if (! rc1)
    {
      fprintf (stderr, "Could not create temporary directory %s\n", responseDirName);
      perror ("");
      secStatus = SECFailure;
      goto cleanup;
    }

  memcpy (responseZipName, responseDirName, sizeof (responseDirName) - 1);
  memcpy (responseZipName + sizeof (responseZipName) - 1 - 6, "XXXXXX", 6);
  rc = mkstemp(responseZipName);
  if (rc == -1)
    {
      fprintf (stderr, "Could not create temporary file %s\n", responseZipName);
      perror ("");
      secStatus = SECFailure;

      /* Remove this so that the other temp files will get removed in cleanup.  */
      prStatus = PR_RmDir (responseDirName);
      if (prStatus != PR_SUCCESS)
	errWarn ("PR_RmDir");
      goto cleanup;
    }

  /* Read data from the socket.
   * If the user is requesting/requiring authentication, authenticate
   * the socket.  */
#if DEBUG
  fprintf(stdout, "\nReading data from socket...\n\n");
#endif
  secStatus = readDataFromSocket(sslSocket);
  if (secStatus != SECSuccess)
    goto cleanup;

#if 0 /* Don't authenticate after each transaction */
  if (REQUEST_CERT_ALL)
    {
      fprintf(stdout, "\nAuthentication requested.\n\n");
      secStatus = authenticateSocket(sslSocket);
      if (secStatus != SECSuccess)
	goto cleanup;
    }
#endif

  /* Call the stap-server script.  */
  cmdline = PORT_Alloc(sizeof ("stap-server") +
		       sizeof (requestFileName) +
		       sizeof (responseDirName) +
		       sizeof (responseZipName) +
		       strlen (dbdir) + 1);
  if (! cmdline) {
    errWarn ("PORT_Alloc");
    secStatus = SECFailure;
    goto cleanup;
  }

  sprintf (cmdline, "stap-server %s %s %s %s",
	   requestFileName, responseDirName, responseZipName, dbdir);
  rc = system (cmdline);

  PR_Free (cmdline);

#if DEBUG
  fprintf(stdout, "\nWriting data to socket...\n\n");
#endif
  secStatus = writeDataToSocket(sslSocket);

cleanup:
  /* Close down the socket. */
  prStatus = PR_Close(tcpSocket);
  if (prStatus != PR_SUCCESS)
    errWarn("PR_Close");

  /* Attempt to remove temporary files, unless the temporary directory was
     not deleted by the server script.  */
  prStatus = PR_GetFileInfo(responseDirName, &info);
  if (prStatus != PR_SUCCESS)
    {
      prStatus = PR_Delete (requestFileName);
      if (prStatus != PR_SUCCESS)
	errWarn ("PR_Delete");
      prStatus = PR_Delete (responseZipName);
      if (prStatus != PR_SUCCESS)
	errWarn ("PR_Delete");
    }

  return secStatus;
}

/* Function:  int accept_connection()
 *
 * Purpose:  Accept a connection to the socket.
 *
 */
static SECStatus
accept_connection(PRFileDesc *listenSocket)
{
  PRNetAddr   addr;
  PRStatus    prStatus;
  PRFileDesc *tcpSocket;
#if 0
  SECStatus   result;
#endif

  while (PR_TRUE)
    {
#if DEBUG
      fprintf(stderr, "\n\n\nAbout to call accept.\n");
#endif

      /* Accept a connection to the socket. */
      tcpSocket = PR_Accept(listenSocket, &addr, PR_INTERVAL_NO_TIMEOUT);
      if (tcpSocket == NULL)
	{
	  errWarn("PR_Accept");
	  break;
	}

      /* Accepted the connection, now handle it. */
      /*result =*/ handle_connection (tcpSocket);
#if 0 /* Not necessary */
      if (result != SECSuccess)
	{
	  prStatus = PR_Close(tcpSocket);
	  if (prStatus != PR_SUCCESS)
	    exitErr("PR_Close");
	  break;
	}
#endif
    }

#if DEBUG
  fprintf(stderr, "Closing listen socket.\n");
#endif
  prStatus = PR_Close(listenSocket);
  if (prStatus != PR_SUCCESS)
    exitErr("PR_Close");

  return SECSuccess;
}

/* Function:  void server_main()
 *
 * Purpose:  This is the server's main function.  It configures a socket
 *			 and listens to it.
 *
 */
static void
server_main(unsigned short port, SECKEYPrivateKey *privKey, CERTCertificate *cert)
{
  SECStatus           secStatus;
  PRStatus            prStatus;
  PRFileDesc *        listenSocket;
  PRNetAddr           addr;
  PRSocketOptionData  socketOption;

  /* Create a new socket. */
  listenSocket = PR_NewTCPSocket();
  if (listenSocket == NULL)
    exitErr("PR_NewTCPSocket");

  /* Set socket to be blocking -
   * on some platforms the default is nonblocking.
   */
  socketOption.option = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;

  prStatus = PR_SetSocketOption(listenSocket, &socketOption);
  if (prStatus != PR_SUCCESS)
    exitErr("PR_SetSocketOption");

#if 0
  /* This cipher is not on by default. The Acceptance test
   * would like it to be. Turn this cipher on.
   */
  secStatus = SSL_CipherPrefSetDefault(SSL_RSA_WITH_NULL_MD5, PR_TRUE);
  if (secStatus != SECSuccess)
    exitErr("SSL_CipherPrefSetDefault:SSL_RSA_WITH_NULL_MD5");
#endif

  /* Configure the network connection. */
  addr.inet.family = PR_AF_INET;
  addr.inet.ip	 = PR_INADDR_ANY;
  addr.inet.port	 = PR_htons(port);

  /* Bind the address to the listener socket. */
  prStatus = PR_Bind(listenSocket, &addr);
  if (prStatus != PR_SUCCESS)
    exitErr("PR_Bind");

  /* Listen for connection on the socket.  The second argument is
   * the maximum size of the queue for pending connections.
   */
  prStatus = PR_Listen(listenSocket, 5);
  if (prStatus != PR_SUCCESS)
    exitErr("PR_Listen");

  /* Handle connections to the socket. */
  secStatus = accept_connection (listenSocket);
  if (secStatus != SECSuccess)
    PR_Close(listenSocket);
}

/* Function: char * myPasswd()
 * 
 * Purpose: This function is our custom password handler that is called by
 * SSL when retreiving private certs and keys from the database. Returns a
 * pointer to a string that with a password for the database. Password pointer
 * should point to dynamically allocated memory that will be freed later.
 */
static char *
myPasswd(PK11SlotInfo *info, PRBool retry, void *arg)
{
  char * passwd = NULL;

  if (! retry && arg)
    passwd = PORT_Strdup((char *)arg);

  return passwd;
}

/* Obtain the certificate and key database password from the given file.  */
static char *
getPassword(char *fileName)
{
  PRFileDesc *local_file_fd;
  PRFileInfo  fileInfo;
  PRInt32     numBytesRead;
  PRStatus    prStatus;
  char       *password;
  PRInt32     i;

  prStatus = PR_GetFileInfo(fileName, &fileInfo);
  if (prStatus != PR_SUCCESS || fileInfo.type != PR_FILE_FILE || fileInfo.size < 0)
    {
      fprintf (stderr, "Password file %s not found\n", fileName);
      return NULL;
    }

  local_file_fd = PR_Open(fileName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "Could not open password file %s\n", fileName);
      return NULL;
    }
      
  password = PORT_Alloc(fileInfo.size + 1);
  if (! password) {
    errWarn ("PORT_Alloc");
    return NULL;
  }

  numBytesRead = PR_Read(local_file_fd, password, fileInfo.size);
  if (numBytesRead <= 0)
    {
      fprintf (stderr, "Error reading password file\n");
      exitErr ("PR_Read");
    }

  PR_Close(local_file_fd);

  /* Keep only the first line of data.  */
  for (i = 0; i < numBytesRead; ++i)
    {
      if (password[i] == '\n' || password[i] == '\r' ||
	  password[i] == '\0')
	break;
    }
  password[i] = '\0';

  return password;
}

/* Function: int main()
 *
 * Purpose:  Parses command arguments and configures SSL server.
 *
 */
int
main(int argc, char **argv)
{
  char *              progName      = NULL;
  char *              nickName      = NULL;
  char *              passwordFile  = NULL;
  unsigned short      port          = 0;
  SECStatus           secStatus;
  PLOptState *        optstate;
  PLOptStatus         status;

  progName = PL_strdup(argv[0]);

  optstate = PL_CreateOptState(argc, argv, "d:p:n:w:");
  while ((status = PL_GetNextOpt(optstate)) == PL_OPT_OK)
    {
      switch(optstate->option)
	{
	case 'd': dbdir = PL_strdup(optstate->value);           break;
	case 'n': nickName = PL_strdup(optstate->value);      break;
	case 'p': port = PORT_Atoi(optstate->value);          break;
	case 'w': passwordFile = PL_strdup(optstate->value);  break;
	default:
	case '?': Usage(progName);
	}
    }

  if (nickName == NULL || port == 0 || dbdir == NULL || passwordFile == NULL)
    Usage(progName);

  password = getPassword (passwordFile);

  /* Call the NSPR initialization routines. */
  PR_Init( PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

  /* Set the cert database password callback. */
  PK11_SetPasswordFunc(myPasswd);

	/* Initialize NSS. */
  secStatus = NSS_Init(dbdir);
  if (secStatus != SECSuccess)
    exitErr("NSS_Init");

  /* Set the policy for this server (REQUIRED - no default). */
  secStatus = NSS_SetDomesticPolicy();
  if (secStatus != SECSuccess)
    exitErr("NSS_SetDomesticPolicy");

  /* Get own certificate and private key. */
  cert = PK11_FindCertFromNickname(nickName, password);
  if (cert == NULL)
    exitErr("PK11_FindCertFromNickname");

  privKey = PK11_FindKeyByAnyCert(cert, password);
  if (privKey == NULL)
    exitErr("PK11_FindKeyByAnyCert");

  /* Configure the server's cache for a multi-process application
   * using default timeout values (24 hrs) and directory location (/tmp). 
   */
  SSL_ConfigMPServerSIDCache(256, 0, 0, NULL);

  /* Launch server. */
  server_main(port, privKey, cert);

  /* Shutdown NSS and exit NSPR gracefully. */
  NSS_Shutdown();
  PR_Cleanup();

  return 0;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
