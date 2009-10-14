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

  fprintf(stderr, "(%d) ", errorNumber);

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

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
