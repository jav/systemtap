/*
  SSL client program that sets up a connection to a SSL server, transmits
  the given input file and then writes the reply to the given output file.

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

#include <ssl.h>
#include <nspr.h>
#include <plgetopt.h>
#include <nss.h>
#include <prerror.h>
#include <secerr.h>

#define READ_BUFFER_SIZE (60 * 1024)
static char *hostName = NULL;
static unsigned short port = 0;
static const char *infileName = NULL;
static const char *outfileName = NULL;

static void
Usage(const char *progName)
{
  fprintf(stderr, "Usage: %s -h hostname -p port -d dbdir -i infile -o outfile\n",
	  progName);
  exit(1);
}

static void
errWarn(char *function)
{
  PRErrorCode errorNumber;
  PRInt32 errorTextLength;
  PRInt32 rc;
  char *errorText;
  
  errorNumber = PR_GetError();
  fprintf(stderr, "Error in function %s: %d: ", function, errorNumber);

  /* See if PR_GetErrorText can tell us what the error is.  */
  if (errorNumber >= PR_NSPR_ERROR_BASE && errorNumber <= PR_MAX_ERROR)
    {
      errorTextLength = PR_GetErrorTextLength ();
      if (errorTextLength != 0) {
	errorText = PORT_Alloc(errorTextLength);
	rc = PR_GetErrorText (errorText);
	if (rc != 0)
	  fprintf (stderr, "%s\n", errorText);
	PR_Free (errorText);
	if (rc != 0)
	  return;
      }
    }

  /* Otherwise handle common errors ourselves.  */
  switch (errorNumber)
    {
    case SEC_ERROR_CA_CERT_INVALID:
      fputs ("The issuer's certificate is invalid\n", stderr);
      break;
    case PR_CONNECT_RESET_ERROR:
      fputs ("Connection reset by peer\n", stderr);
      break;
    default:
      fputs ("Unknown error\n", stderr);
      break;
    }
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

static PRFileDesc *
setupSSLSocket(void)
{
  PRFileDesc         *tcpSocket;
  PRFileDesc         *sslSocket;
  PRSocketOptionData	socketOption;
  PRStatus            prStatus;
  SECStatus           secStatus;

  tcpSocket = PR_NewTCPSocket();
  if (tcpSocket == NULL)
    {
      errWarn("PR_NewTCPSocket");
    }

  /* Make the socket blocking. */
  socketOption.option = PR_SockOpt_Nonblocking;
  socketOption.value.non_blocking = PR_FALSE;

  prStatus = PR_SetSocketOption(tcpSocket, &socketOption);
  if (prStatus != PR_SUCCESS)
    {
      errWarn("PR_SetSocketOption");
      goto loser;
    } 

  /* Import the socket into the SSL layer. */
  sslSocket = SSL_ImportFD(NULL, tcpSocket);
  if (!sslSocket)
    {
      errWarn("SSL_ImportFD");
      goto loser;
    }

  /* Set configuration options. */
  secStatus = SSL_OptionSet(sslSocket, SSL_SECURITY, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_OptionSet:SSL_SECURITY");
      goto loser;
    }

  secStatus = SSL_OptionSet(sslSocket, SSL_HANDSHAKE_AS_CLIENT, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_OptionSet:SSL_HANDSHAKE_AS_CLIENT");
      goto loser;
    }

  /* Set SSL callback routines. */
#if 0 /* no client authentication */
  secStatus = SSL_GetClientAuthDataHook(sslSocket,
					(SSLGetClientAuthData)myGetClientAuthData,
					(void *)certNickname);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_GetClientAuthDataHook");
      goto loser;
    }
#endif
#if 0 /* Use the default */
  secStatus = SSL_AuthCertificateHook(sslSocket,
				      (SSLAuthCertificate)myAuthCertificate,
				      (void *)CERT_GetDefaultCertDB());
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_AuthCertificateHook");
      goto loser;
    }
#endif
#if 0 /* Use the default */
  secStatus = SSL_BadCertHook(sslSocket, 
			      (SSLBadCertHandler)myBadCertHandler, NULL);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_BadCertHook");
      goto loser;
    }
#endif
#if 0 /* No handshake callback */
  secStatus = SSL_HandshakeCallback(sslSocket, myHandshakeCallback, NULL);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_HandshakeCallback");
      goto loser;
    }
#endif

  return sslSocket;

 loser:
  PR_Close(tcpSocket);
  return NULL;
}


static SECStatus
handle_connection(PRFileDesc *sslSocket)
{
#if DEBUG
  int	      countRead = 0;
#endif
  PRInt32     numBytes;
  char       *readBuffer;
  PRFileInfo  info;
  PRFileDesc *local_file_fd;
  PRStatus    prStatus;

  /* read and send the data. */
  /* Try to open the local file named.	
   * If successful, then write it to the server
   */
  prStatus = PR_GetFileInfo(infileName, &info);
  if (prStatus != PR_SUCCESS ||
      info.type != PR_FILE_FILE ||
      info.size < 0)
    {
      fprintf (stderr, "could not find input file %s\n", infileName);
      return SECFailure;
    }

  local_file_fd = PR_Open(infileName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "could not open input file %s\n", infileName);
      return SECFailure;
    }

  /* Send the file size first, so the server knows when it has the entire file. */
  numBytes = PR_Write(sslSocket, & info.size, sizeof (info.size));
  if (numBytes < 0)
    {
      errWarn("PR_Write");
      return SECFailure;
    }

  /* Transmit the local file across the socket.  */
  numBytes = PR_TransmitFile(sslSocket, local_file_fd, 
			     NULL, 0,
			     PR_TRANSMITFILE_KEEP_OPEN,
			     PR_INTERVAL_NO_TIMEOUT);
  if (numBytes < 0)
    {
      errWarn("PR_TransmitFile");
      return SECFailure;
    }

#if DEBUG
  /* Transmitted bytes successfully. */
  fprintf(stderr, "PR_TransmitFile wrote %d bytes from %s\n",
	  numBytes, infileName);
#endif

  PR_Close(local_file_fd);

  /* read until EOF */
  readBuffer = PORT_Alloc(READ_BUFFER_SIZE);
  if (! readBuffer)
    exitErr("PORT_Alloc");

  local_file_fd = PR_Open(outfileName, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
			  PR_IRUSR | PR_IWUSR | PR_IRGRP | PR_IWGRP | PR_IROTH);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "could not open output file %s\n", outfileName);
      return SECFailure;
    }
  while (PR_TRUE)
    {
      numBytes = PR_Read(sslSocket, readBuffer, READ_BUFFER_SIZE);
      if (numBytes == 0)
	break;	/* EOF */

      if (numBytes < 0)
	{
	  errWarn("PR_Read");
	  break;
	}
#if DEBUG
      countRead += numBytes;
#endif
      /* Write to output file */
      numBytes = PR_Write(local_file_fd, readBuffer, numBytes);
      if (numBytes < 0)
	{
	  fprintf (stderr, "could not write to %s\n", outfileName);
	  break;
	}
#if DEBUG
      fprintf(stderr, "***** Connection read %d bytes (%d total).\n", 
	      numBytes, countRead );
      readBuffer[numBytes] = '\0';
      fprintf(stderr, "************\n%s\n************\n", readBuffer);
#endif
    }

  PR_Free(readBuffer);
  PR_Close(local_file_fd);

  /* Caller closes the socket. */
#if DEBUG
  fprintf(stderr, "***** Connection read %d bytes total.\n", countRead);
#endif

  return SECSuccess;
}

/* make the connection.
*/
static SECStatus
do_connect(PRNetAddr *addr)
{
  PRFileDesc *sslSocket;
  PRHostEnt   hostEntry;
  char        buffer[PR_NETDB_BUF_SIZE];
  PRStatus    prStatus;
  PRIntn      hostenum;
  SECStatus   secStatus;

  secStatus = SECSuccess;

  /* Set up SSL secure socket. */
  sslSocket = setupSSLSocket();
  if (sslSocket == NULL)
    {
      errWarn("setupSSLSocket");
      return SECFailure;
    }

#if 0 /* no client authentication */
  secStatus = SSL_SetPKCS11PinArg(sslSocket, password);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_SetPKCS11PinArg");
      goto done;
    }
#endif

  secStatus = SSL_SetURL(sslSocket, hostName);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_SetURL");
      goto done;
    }

  /* Prepare and setup network connection. */
  prStatus = PR_GetHostByName(hostName, buffer, sizeof(buffer), &hostEntry);
  if (prStatus != PR_SUCCESS)
    {
      errWarn("PR_GetHostByName");
      secStatus = SECFailure;
      goto done;
    }

  hostenum = PR_EnumerateHostEnt(0, &hostEntry, port, addr);
  if (hostenum == -1)
    {
      errWarn("PR_EnumerateHostEnt");
      secStatus = SECFailure;
      goto done;
    }

  prStatus = PR_Connect(sslSocket, addr, PR_INTERVAL_NO_TIMEOUT);
  if (prStatus != PR_SUCCESS)
    {
      errWarn("PR_Connect");
      secStatus = SECFailure;
      goto done;
    }

  /* Established SSL connection, ready to send data. */
  secStatus = SSL_ResetHandshake(sslSocket, /* asServer */ PR_FALSE);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_ResetHandshake");
      goto done;
    }

  /* This is normally done automatically on the first I/O operation,
     but doing it here catches any authentication problems early.  */
  secStatus = SSL_ForceHandshake(sslSocket);
  if (secStatus != SECSuccess)
    {
      errWarn("SSL_ForceHandshake");
      goto done;
    }

  secStatus = handle_connection(sslSocket);
  if (secStatus != SECSuccess)
    {
      errWarn("handle_connection");
      goto done;
    }

 done:
  prStatus = PR_Close(sslSocket);
  if (prStatus != PR_SUCCESS)
    errWarn("PR_Close");

  return secStatus;
}

static void
client_main(unsigned short port, const char *hostName)
{
  SECStatus   secStatus;
  PRStatus    prStatus;
  PRInt32     rv;
  PRNetAddr   addr;
  PRHostEnt   hostEntry;
  char        buffer[PR_NETDB_BUF_SIZE];

  /* Setup network connection. */
  prStatus = PR_GetHostByName(hostName, buffer, sizeof (buffer), &hostEntry);
  if (prStatus != PR_SUCCESS)
    exitErr("PR_GetHostByName");

  rv = PR_EnumerateHostEnt(0, &hostEntry, port, &addr);
  if (rv < 0)
    exitErr("PR_EnumerateHostEnt");

  secStatus = do_connect (&addr);
  if (secStatus != SECSuccess)
    exitErr("do_connect");
}

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

int
main(int argc, char **argv)
{
  char *       certDir = NULL;
  char *       progName = NULL;
  SECStatus    secStatus;
  PLOptState  *optstate;
  PLOptStatus  status;

  /* Call the NSPR initialization routines */
  PR_Init( PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

  progName = PL_strdup(argv[0]);

  hostName = NULL;
  optstate = PL_CreateOptState(argc, argv, "d:h:i:o:p:");
  while ((status = PL_GetNextOpt(optstate)) == PL_OPT_OK)
    {
      switch(optstate->option)
	{
	case 'd' : certDir = PL_strdup(optstate->value);      break;
	case 'h' : hostName = PL_strdup(optstate->value);     break;
	case 'i' : infileName = PL_strdup(optstate->value);   break;
	case 'o' : outfileName = PL_strdup(optstate->value);  break;
	case 'p' : port = PORT_Atoi(optstate->value);         break;
	case '?' :
	default  : Usage(progName);
	}
    }

  if (port == 0 || hostName == NULL || infileName == NULL || outfileName == NULL || certDir == NULL)
    Usage(progName);

#if 0 /* no client authentication */
  /* Set our password function callback. */
  PK11_SetPasswordFunc(myPasswd);
#endif

  /* Initialize the NSS libraries. */
  secStatus = NSS_Init(certDir);
  if (secStatus != SECSuccess)
    exitErr("NSS_Init");

  /* All cipher suites except RSA_NULL_MD5 are enabled by Domestic Policy. */
  NSS_SetDomesticPolicy();

  client_main(port, hostName);

  NSS_Shutdown();
  PR_Cleanup();

  return 0;
}
