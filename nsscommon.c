/*
  Common functions used by the NSS-aware code in systemtap.

  Copyright (C) 2009-2011 Red Hat Inc.

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
#include <termios.h>
#include <unistd.h>

#include <nss.h>
#include <nspr.h>
#include <prerror.h>
#include <secerr.h>
#include <sslerr.h>

#include "nsscommon.h"

void
nssError (void)
{
  PRErrorCode errorNumber;
  char *errorText;
  
  /* See if PR_GetError can tell us what the error is.  */
  errorNumber = PR_GetError ();
  fprintf(stderr, "(%d) ", errorNumber);

  /* PR_ErrorToString always returns a valid string for errors in this range.  */
  if (errorNumber >= PR_NSPR_ERROR_BASE && errorNumber <= PR_MAX_ERROR)
    {
      fprintf (stderr, "%s\n", PR_ErrorToString(errorNumber, PR_LANGUAGE_EN));
      return;
    }

  /* PR_ErrorToString does not handle errors outside the range above, so
     we handle them ourselves.  */
  switch (errorNumber) {
  default: errorText = "(unknown)"; break;
#define NSSYERROR(code,msg) case code: errorText = msg; break
#include "stapsslerr.h"
#undef NSSYERROR
    }

  fprintf (stderr, "%s\n", errorText);
}

void
nssCleanup (void)
{
  /* Shutdown NSS and exit NSPR gracefully. */
  NSS_Shutdown ();
  PR_Cleanup ();
}

/* Disable character echoing, if the fd is a tty.  */
static void
echoOff(int fd)
{
  if (isatty(fd)) {
    struct termios tio;
    tcgetattr(fd, &tio);
    tio.c_lflag &= ~ECHO;
    tcsetattr(fd, TCSAFLUSH, &tio);
  }
}

/* Enable character echoing, if the fd is a tty.  */
static void
echoOn(int fd)
{
  if (isatty(fd)) {
    struct termios tio;
    tcgetattr(fd, &tio);
    tio.c_lflag |= ECHO;
    tcsetattr(fd, TCSAFLUSH, &tio);
  }
}

/*
 * This function is our custom password handler that is called by
 * NSS when retrieving private certs and keys from the database. Returns a
 * pointer to a string with a password for the database. Password pointer
 * must be allocated by one of the PORT_* memory allocation fuinctions, or by PORT_Strdup,
 * and will be freed by the caller.
 */
char *
nssPasswordCallback (PK11SlotInfo *info __attribute ((unused)), PRBool retry, void *arg)
{
  static int retries = 0;
  #define PW_MAX 200
  char* password = NULL;
  const char *dbname ;
  int infd;
  int isTTY;

  if (! retry)
    {
      /* Not a retry. */
      retries = 0;
    }
  else
    {
      /* Maximum of 2 retries for bad password.  */
      if (++retries > 2)
	return NULL; /* No more retries */
    }

  /* Can only prompt for a password if stdin is a tty.  */
  infd = fileno (stdin);
  isTTY = isatty (infd);
  if (! isTTY)
    return NULL;

  /* Prompt for password */
  password = PORT_Alloc (PW_MAX);
  if (! password)
    {
      nssError ();
      return NULL;
    }

  dbname = (const char *)arg;
  fprintf (stderr, "Password for certificate database in %s: ", dbname);
  fflush (stderr);
  echoOff (infd);
  fgets (password, PW_MAX, stdin);
  fprintf( stderr, "\n");
  echoOn(infd);

  /* stomp on the newline */
  password [strlen (password) - 1] = '\0';

  return password;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
