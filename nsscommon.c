/*
  Common functions used by the NSS-aware code in systemtap.

  Copyright (C) 2009 Red Hat Inc.

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

#include <nss.h>
#include <nspr.h>
#include <prerror.h>
#include <secerr.h>
#include <sslerr.h>

void
nssError (void)
{
  PRErrorCode errorNumber;
  PRInt32 errorTextLength;
  PRInt32 rc;
  char *errorText;
  
  /* See if PR_GetErrorText can tell us what the error is.  */
  errorNumber = PR_GetError ();
  if (errorNumber >= PR_NSPR_ERROR_BASE && errorNumber <= PR_MAX_ERROR)
    {
      errorTextLength = PR_GetErrorTextLength ();
      if (errorTextLength != 0) {
	errorText = PORT_Alloc (errorTextLength);
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
    case PR_CONNECT_RESET_ERROR:
      fputs ("Connection reset by peer.\n", stderr);
      break;
    case SEC_ERROR_BAD_DATABASE:
      fputs ("The specified certificate database does not exist or is not valid.\n", stderr);
      break;
    case SEC_ERROR_BAD_SIGNATURE:
      fputs ("Certificate does not match the signature.\n", stderr);
      break;
    case SEC_ERROR_CA_CERT_INVALID:
      fputs ("The issuer's certificate is invalid.\n", stderr);
      break;
    case SSL_ERROR_BAD_CERT_DOMAIN:
      fputs ("The requested domain name does not match the server's certificate.\n", stderr);
      break;
    default:
      fprintf (stderr, "Unknown NSS error: %d.\n", errorNumber);
      break;
    }
}

void
nssCleanup (void)
{
  /* Shutdown NSS and exit NSPR gracefully. */
  NSS_Shutdown ();
  PR_Cleanup ();
}
