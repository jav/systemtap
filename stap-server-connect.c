/*
  SSL server program listens on a port, accepts client connection, reads
  the data into a temporary file, calls the systemtap translator and
  then transmits the resulting file back to the client.

  Copyright (C) 2008-2010 Red Hat Inc.

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
#include <unistd.h>
#include <errno.h>
#include <spawn.h>
#include <fcntl.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <wordexp.h>
#include <sys/param.h>

#include <ssl.h>
#include <nspr.h>
#include <plgetopt.h>
#include <nss.h>
#include <pk11func.h>

#include "config.h"
#include "nsscommon.h"


/* Global variables */
static char             *password = NULL;
static CERTCertificate  *cert = NULL;
static SECKEYPrivateKey *privKey = NULL;
static char             *dbdir   = NULL;
static const char       *stapOptions = "";


static PRStatus spawn_and_wait (char **argv,
                                const char* fd0, const char* fd1, const char* fd2, const char *pwd);


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
  fprintf(stderr, "Error in function %s: ", function);
  nssError();
}

static void
exitErr(char *function)
{
  errWarn(function);
  /* Exit gracefully. */
  /* ignoring return value of NSS_Shutdown as code exits with 1*/
  (void) NSS_Shutdown();
#if 0 /* PR_Cleanup is known to hang on some systems */
  PR_Cleanup();
#endif
  exit(1);
}




/* Function:  readDataFromSocket()
 *
 * Purpose:  Read data from the socket into a temporary file.
 *
 */
static SECStatus readDataFromSocket(PRFileDesc *sslSocket, const char *requestFileName)
{
  PRFileDesc *local_file_fd;
  PRFileInfo  info;
  PRInt32     numBytesRead;
  PRInt32     numBytesWritten;
  PRInt32     totalBytes;
#define READ_BUFFER_SIZE 4096
  char        buffer[READ_BUFFER_SIZE];

  /* Open the output file.  */
  local_file_fd = PR_Open(requestFileName, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
			  PR_IRUSR | PR_IWUSR);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "could not open output file %s\n", requestFileName);
      return SECFailure;
    }

  /* Read the number of bytes to be received.  */
  /* XXX: impose a limit to prevent disk space consumption DoS */
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
      if (numBytesWritten < 0 || (numBytesWritten != numBytesRead))
        {
          fprintf (stderr, "could not write to output file %s\n", requestFileName);
          break;
        }
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
      errWarn("SSL_SetPKCS11PinArg");
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
writeDataToSocket(PRFileDesc *sslSocket, const char *responseFileName)
{
  int         numBytes;
  PRFileDesc *local_file_fd;

  local_file_fd = PR_Open(responseFileName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "Could not open input file %s\n", responseFileName);
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
	  numBytes, responseFileName);
#endif

  PR_Close(local_file_fd);

  return SECSuccess;
}


/* Run the translator on the data in the request directory, and produce output
   in the given output directory. */
static void handleRequest (const char* requestDirName, const char* responseDirName)
{
  char stapstdout[PATH_MAX];
  char stapstderr[PATH_MAX];
  char staprc[PATH_MAX];
#define MAXSTAPARGC 1000 /* sorry, too lazy to dynamically allocate */
  char* stapargv[MAXSTAPARGC];
  int stapargc=0;
  int rc;
  wordexp_t words;
  int i;
  FILE* f;
  int unprivileged = 0;
  int stapargv_freestart = 0;

  stapargv[stapargc++]= STAP_PREFIX "/bin/stap";

  /* Transcribe stapOptions.  We use plain wordexp(3), since these
     options are coming from the local trusted user, so malicious
     content is not a concern. */

  rc = wordexp (stapOptions, & words, WRDE_NOCMD|WRDE_UNDEF);
  if (rc) 
    { 
      errWarn("cannot parse -s stap options");
      return;
    }
  if (words.we_wordc+10 >= MAXSTAPARGC)  /* 10: padding for literal entries */
    {
      errWarn("too many -s options; MAXSTAPARGC");
      return;
    }

  for (i=0; i<words.we_wordc; i++)
    stapargv[stapargc++] = words.we_wordv[i];

  stapargv[stapargc++] = "-k"; /* Need to keep temp files in order to package them up again. */

  /* Process the saved command line arguments.  Avoid quoting/unquoting errors by
     transcribing literally. */
  stapargv[stapargc++] = "--client-options";
  stapargv_freestart = stapargc;

  for (i=1 ; ; i++)
    {
      char stapargfile[PATH_MAX];
      FILE* argfile;
      struct stat st;
      char *arg;

      if (stapargc >= MAXSTAPARGC) 
        {
          errWarn("too many stap options; MAXSTAPARGC");
          return;
        }

      snprintf (stapargfile, PATH_MAX, "%s/argv%d", requestDirName, i);

      rc = stat(stapargfile, & st);
      if (rc) break;

      arg = malloc (st.st_size+1);
      if (!arg)
        {
          errWarn("stap arg malloc");
          return;
        }

      argfile = fopen(stapargfile, "r");
      if (! argfile)
        {
          errWarn("stap arg fopen");
          return;
        }

      rc = fread(arg, 1, st.st_size, argfile);
      if (rc != st.st_size)
        {
          errWarn("stap arg fread");
          return;
        }

      arg[st.st_size] = '\0';
      stapargv[stapargc++] = arg; /* freed later */
      fclose (argfile);
    }

  snprintf (stapstdout, PATH_MAX, "%s/stdout", responseDirName);
  snprintf (stapstderr, PATH_MAX, "%s/stderr", responseDirName);

  stapargv[stapargc] = NULL; /* spawn_and_wait expects NULL termination */

  /* Check for the unprivileged flag; we need this so that we can decide to sign the module. */
  for (i=0; i<stapargc; i++)
    if (strcmp (stapargv[i], "--unprivileged") == 0)
      unprivileged=1;
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
      if (stapargc+1 >= MAXSTAPARGC) 
        {
          errWarn("too many stap options; MAXSTAPARGC");
          return;
        }

      /* Shift all stapargv[] entries up one, including the NULL. */
      for (i=stapargc; i>=1; i--)
        stapargv[i+1]=stapargv[i];
      stapargv_freestart ++; /* adjust for shift */

      stapargv[1]="--unprivileged"; /* better not be resettable by later option */
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

  /* Parse output to extract the -k-saved temprary directory.
     XXX: bletch. */
  f = fopen(stapstderr, "r");
  if (!f)
    {
      errWarn("stap stderr open");
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
          char *mvargv[10];
          char *orig_staptmpdir = & l[strlen(key)];
          char new_staptmpdir[PATH_MAX];

          orig_staptmpdir[strlen(orig_staptmpdir)-2] = '\0'; /* Kill the closing "\n */
          snprintf(new_staptmpdir, PATH_MAX, "%s/stap000000", responseDirName);
          mvargv[0]="mv";
          mvargv[1]=orig_staptmpdir;
          mvargv[2]=new_staptmpdir;
          mvargv[3]=NULL;
          rc = spawn_and_wait (mvargv, NULL, NULL, NULL, NULL);
          if (rc != PR_SUCCESS)
            errWarn("stap tmpdir move");

          /* In unprivileged mode, if we have a module built, we need to
             sign the sucker. */
          if (unprivileged) 
            {
              glob_t globber;
              char pattern[PATH_MAX];
              snprintf (pattern,PATH_MAX,"%s/*.ko", new_staptmpdir);
              rc = glob (pattern, GLOB_ERR, NULL, &globber);
              if (rc)
                errWarn("stap tmpdir .ko glob");
              else if (globber.gl_pathc != 1)
                errWarn("stap tmpdir too many .ko globs");
              else
                {
                  char *signargv [10];
                  signargv[0] = STAP_PREFIX "/libexec/systemtap/stap-sign-module";
                  signargv[1] = globber.gl_pathv[0];
                  signargv[2] = dbdir;
                  signargv[3] = NULL;
                  rc = spawn_and_wait (signargv, NULL, NULL, NULL, NULL);
                  if (rc != PR_SUCCESS)
                    errWarn("stap-sign-module");
                }
            }
        }

      /* XXX: What about uprobes.ko? */
    }

  /* Free up all the arg string copies.  Note that the first few were alloc'd
     by wordexp(), which wordfree() frees; others were hand-set to literal strings. */
  for (i= stapargv_freestart; i<stapargc; i++)
    free (stapargv[i]);
  wordfree (& words);

  /* Sorry about the inconvenience.  C string/file processing is such a pleasure. */
}


/* A frontend for posix_spawnp that waits for the child process and
   returns overall success or failure. */
static PRStatus spawn_and_wait (char ** argv,
                                const char* fd0, const char* fd1, const char* fd2, const char *pwd)
{ 
  pid_t pid;
  int rc;
  int status;
  extern char** environ;
  posix_spawn_file_actions_t actions;
  int dotfd = -1;

#define CHECKRC(msg) do { if (rc) { errWarn(msg); return PR_FAILURE; } } while (0)

  rc = posix_spawn_file_actions_init (& actions);
  CHECKRC ("spawn file actions ctor");
  if (fd0) {
    rc = posix_spawn_file_actions_addopen(& actions, 0, fd0, O_RDONLY, 0600);
    CHECKRC ("spawn file actions fd0");
  }
  if (fd1) {
    rc = posix_spawn_file_actions_addopen(& actions, 1, fd1, O_WRONLY|O_CREAT, 0600);
    CHECKRC ("spawn file actions fd1");
  }
  if (fd2) { 
    rc = posix_spawn_file_actions_addopen(& actions, 2, fd2, O_WRONLY|O_CREAT, 0600);
    CHECKRC ("spawn file actions fd2");
  }

  /* change temporarily to a directory if requested */
  if (pwd)
    {
      dotfd = open (".", O_RDONLY);
      if (dotfd < 0)
        { 
          errWarn ("spawn getcwd");
          return PR_FAILURE;
        }
      
      rc = chdir (pwd);
      CHECKRC ("spawn chdir");
    } 
 
  rc = posix_spawnp (& pid, argv[0], & actions, NULL, argv, environ);
  /* NB: don't react to rc!=0 right away; need to chdir back first. */

  if (pwd && dotfd >= 0)
    {
      int subrc;
      subrc = fchdir (dotfd);
      subrc |= close (dotfd);
      if (subrc) 
        errWarn("spawn unchdir");
    }

  CHECKRC ("spawn");

  rc = waitpid (pid, &status, 0);
  if ((rc!=pid) || !WIFEXITED(status))
    {
      errWarn ("waitpid");
      return PR_FAILURE;
    }

  rc = posix_spawn_file_actions_destroy (&actions);
  CHECKRC ("spawn file actions dtor");

  return WEXITSTATUS(status) ? PR_FAILURE : PR_SUCCESS;
#undef CHECKRC
}



/* Function:  int handle_connection()
 *
 * Purpose: Handle a connection to a socket.  Copy in request zip
 * file, process it, copy out response.  Temporary directories are
 * created & destroyed here.
 */
static SECStatus
handle_connection(PRFileDesc *tcpSocket)
{
  PRFileDesc *       sslSocket = NULL;
  SECStatus          secStatus = SECFailure;
  PRStatus           prStatus;
  PRSocketOptionData socketOption;
  int                rc;
  char              *rc1;
  char               tmpdir[PATH_MAX]; 
  char               requestFileName[PATH_MAX];
  char               requestDirName[PATH_MAX];
  char               responseDirName[PATH_MAX];
  char               responseFileName[PATH_MAX];
  char              *argv[10]; /* we use fewer than these in all the posix_spawn's below. */

  tmpdir[0]='\0'; /* prevent cleanup-time /bin/rm of uninitialized directory */

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

  snprintf(tmpdir, PATH_MAX, "%s/stap-server.XXXXXX", getenv("TMPDIR") ?: "/tmp");
  rc1 = mkdtemp(tmpdir);
  if (! rc1)
    {
      fprintf (stderr, "Could not create temporary directory %s\n", tmpdir);
      perror ("");
      secStatus = SECFailure;
      tmpdir[0]=0; /* prevent /bin/rm */
      goto cleanup;
    }

  /* Create a temporary files names and directories.  */
  snprintf (requestFileName, PATH_MAX, "%s/request.zip", tmpdir);

  snprintf (requestDirName, PATH_MAX, "%s/request", tmpdir);
  rc = mkdir(requestDirName, 0700);
  if (rc)
    {
      fprintf (stderr, "Could not create temporary directory %s\n", requestDirName);
      perror ("");
      secStatus = SECFailure;
      goto cleanup;
    }

  snprintf (responseDirName, PATH_MAX, "%s/response", tmpdir);
  rc = mkdir(responseDirName, 0700);
  if (rc)
    {
      fprintf (stderr, "Could not create temporary directory %s\n", responseDirName);
      perror ("");
      secStatus = SECFailure;
      goto cleanup;
    }

  snprintf (responseFileName, PATH_MAX, "%s/response.zip", tmpdir);

  /* Read data from the socket.
   * If the user is requesting/requiring authentication, authenticate
   * the socket.  */
#if DEBUG
  fprintf(stdout, "\nReading data from socket...\n\n");
#endif
  secStatus = readDataFromSocket(sslSocket, requestFileName);
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

  /* Unzip the request. */
  argv[0]="unzip";
  argv[1]="-d";
  argv[2]=requestDirName;
  argv[3]=requestFileName;
  rc = spawn_and_wait(argv, NULL, NULL, NULL, NULL);
  if (rc != PR_SUCCESS)
    {
      errWarn ("request unzip");
      secStatus = SECFailure;
      goto cleanup;
    }

  /* Handle the request zip file.  An error therein should still result
     in a response zip file (containing stderr etc.) so we don't have to
     have a result code here.  */
  handleRequest(requestDirName, responseDirName);

  /* Zip the response. */
  argv[0]="zip";
  argv[1]="-r";
  argv[2]=responseFileName;
  argv[3]=".";
  argv[4]=NULL;
  rc = spawn_and_wait(argv, NULL, NULL, NULL, responseDirName);
  if (rc != PR_SUCCESS)
    {
      errWarn ("response zip");
      secStatus = SECFailure;
      goto cleanup;
    }
  
#if DEBUG
  fprintf(stdout, "\nWriting data to socket...\n\n");
#endif
  secStatus = writeDataToSocket(sslSocket, responseFileName);

cleanup:
  /* Close down the socket. */
  prStatus = PR_Close(tcpSocket);
  if (prStatus != PR_SUCCESS)
    errWarn("PR_Close");

  if (tmpdir[0]) 
    {
      /* Remove the whole tmpdir and all that lies beneath. */
      argv[0]="rm";
      argv[1]="-r";
      argv[2]=tmpdir;
      argv[3]=NULL;
      rc = spawn_and_wait(argv, NULL, NULL, NULL, NULL);
      if (rc != PR_SUCCESS)
        errWarn ("tmpdir cleanup");
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
  SECStatus   secStatus;
  CERTCertDBHandle *dbHandle;

  dbHandle = CERT_GetDefaultCertDB();

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

      /* Log the accepted connection.  */
      printf ("Accepted connection from %d.%d.%d.%d:%d\n",
	      (addr.inet.ip      ) & 0xff,
	      (addr.inet.ip >>  8) & 0xff,
	      (addr.inet.ip >> 16) & 0xff,
	      (addr.inet.ip >> 24) & 0xff,
	      addr.inet.port);
      fflush (stdout);

      /* XXX: alarm() or somesuch to set a timeout. */
      /* XXX: fork() or somesuch to handle concurrent requests. */

      /* Accepted the connection, now handle it. */
      handle_connection (tcpSocket);

      printf ("Request from %d.%d.%d.%d:%d complete\n",
	      (addr.inet.ip      ) & 0xff,
	      (addr.inet.ip >>  8) & 0xff,
	      (addr.inet.ip >> 16) & 0xff,
	      (addr.inet.ip >> 24) & 0xff,
	      addr.inet.port);
      fflush (stdout);

      /* If our certificate is no longer valid (e.g. has expired),
	 then exit. The daemon, (stap-serverd) will generate a new
	 certificate and restart the connection.  */
      secStatus = CERT_VerifyCertNow(dbHandle, cert, PR_TRUE/*checkSig*/,
				     certUsageSSLServer, NULL/*wincx*/);
      if (secStatus != SECSuccess)
	{
	  errWarn ("CERT_VerifyCertNow");
	  break;
	}
    }

#if DEBUG
  fprintf(stderr, "Closing listen socket.\n");
  fflush (stderr);
#endif
  prStatus = PR_Close(listenSocket);
  if (prStatus != PR_SUCCESS)
    exitErr("PR_Close");

#if DEBUG
  fprintf(stderr, "Closed listen socket.\n");
  fflush (stderr);
#endif
  return SECSuccess;
}

/* Function:  void server_main()
 *
 * Purpose:  This is the server's main function.  It configures a socket
 *			 and listens to it.
 *
 */
static void
server_main(unsigned short port, SECKEYPrivateKey *privKey)
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
  addr.inet.ip	   = PR_INADDR_ANY;
  addr.inet.port   = PR_htons(port);

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
  const char *    progName      = NULL;
  const char *    nickName      = NULL;
  char       *    passwordFile  = NULL;
  unsigned short  port          = 0;
  SECStatus       secStatus;
  PLOptState *    optstate;
  PLOptStatus     status;

  progName = PL_strdup(argv[0]);

  optstate = PL_CreateOptState(argc, argv, "d:p:n:w:s:");
  while ((status = PL_GetNextOpt(optstate)) == PL_OPT_OK)
    {
      switch(optstate->option)
	{
	case 'd': dbdir        = PL_strdup(optstate->value); break;
	case 'n': nickName     = PL_strdup(optstate->value); break;
	case 'p': port         = PORT_Atoi(optstate->value); break;
	case 'w': passwordFile = PL_strdup(optstate->value); break;
	case 's': stapOptions  = PL_strdup(optstate->value); break;
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
  server_main(port, privKey);

  /* Shutdown NSS and exit NSPR gracefully. */
  NSS_Shutdown();
#if 0 /* PR_Cleanup is known to hang on some systems */
  PR_Cleanup();
#endif

  return 0;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
