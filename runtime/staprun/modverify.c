/*
  This program verifies the given file using the given signature, the named
  certificate and public key in the given certificate database.

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

#include <nspr.h>
#include <nss.h>
#include <pk11pub.h>
#include <cryptohi.h>
#include <cert.h>
#include <certt.h>

#include "nsscommon.h"

static int
verify_it (const char *inputName, const char *signatureName, SECKEYPublicKey *pubKey)
{
  unsigned char buffer[4096];
  PRFileInfo info;
  PRStatus prStatus;
  PRInt32  numBytes;
  PRFileDesc *local_file_fd;
  VFYContext *vfy;
  SECItem signature;
  SECStatus secStatus;

  /* Get the size of the signature file.  */
  prStatus = PR_GetFileInfo (signatureName, &info);
  if (prStatus != PR_SUCCESS || info.type != PR_FILE_FILE || info.size < 0)
    {
      fprintf (stderr, "Unable to obtain information on the signature file %s.\n", signatureName);
      nssError ();
      return -1;
    }

  /* Open the signature file.  */
  local_file_fd = PR_Open (signatureName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "Could not open the signature file %s\n.", signatureName);
      nssError ();
      return -1;
    }

  /* Allocate space to read the signature file.  */
  signature.data = PORT_Alloc (info.size);
  if (! signature.data)
    {
      fprintf (stderr, "Unable to allocate memory for the signature in %s.\n", signatureName);
      nssError ();
      return -1;
    }

  /* Read the signature.  */
  numBytes = PR_Read (local_file_fd, signature.data, info.size);
  if (numBytes == 0) /* EOF */
    {
      fprintf (stderr, "EOF reading signature file %s.\n", signatureName);
      return -1;
    }
  if (numBytes < 0)
    {
      fprintf (stderr, "Error reading signature file %s.\n", signatureName);
      nssError ();
      return -1;
    }
  if (numBytes != info.size)
    {
      fprintf (stderr, "Incomplete data while reading signature file %s.\n", signatureName);
      return -1;
    }
  signature.len = info.size;

  /* Done with the signature file.  */
  PR_Close (local_file_fd);

  /* Create a verification context.  */
  vfy = VFY_CreateContextDirect (pubKey, & signature, SEC_OID_PKCS1_RSA_ENCRYPTION,
				 SEC_OID_UNKNOWN, NULL, NULL);
  if (! vfy)
    {
      fprintf (stderr, "Unable to create verification context while verifying %s using the signature in %s.\n",
	       inputName, signatureName);
      nssError ();
      return -1;
    }

  /* Begin the verification process.  */
  secStatus = VFY_Begin(vfy);
  if (secStatus != SECSuccess)
    {
      fprintf (stderr, "Unable to initialize verification context while verifying %s using the signature in %s.\n",
	       inputName, signatureName);
      nssError ();
      return -1;
    }

  /* Now read the data and add it to the signature.  */
  local_file_fd = PR_Open (inputName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "Could not open module file %s.\n", inputName);
      nssError ();
      return -1;
    }

  for (;;)
    {
      numBytes = PR_Read (local_file_fd, buffer, sizeof (buffer));
      if (numBytes == 0)
	break;	/* EOF */

      if (numBytes < 0)
	{
	  fprintf (stderr, "Error reading module file %s.\n", inputName);
	  nssError ();
	  return -1;
	}

      /* Add the data to the signature.  */
      secStatus = VFY_Update (vfy, buffer, numBytes);
      if (secStatus != SECSuccess)
	{
	  fprintf (stderr, "Error while verifying module file %s.\n", inputName);
	  nssError ();
	  return -1;
	}
    }

  PR_Close(local_file_fd);

  /* Complete the verification.  */
  secStatus = VFY_End (vfy);
  if (secStatus != SECSuccess)
    return 0;

  return 1;
}

int verify_module (const char *module_name, const char *signature_name)
{
  const char *dbdir  = SYSCONFDIR "/systemtap/staprun";
  SECKEYPublicKey *pubKey;
  SECStatus secStatus;
  CERTCertList *certList;
  CERTCertListNode *certListNode;
  CERTCertificate *cert;
  int rc = 0;

  /* Call the NSPR initialization routines. */
  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

  /* Initialize NSS. */
  secStatus = NSS_Init (dbdir);
  if (secStatus != SECSuccess)
    {
      fprintf (stderr, "Unable to initialize nss library using the database in %s.\n",
	       dbdir);
      nssError ();
      return -1;
    }

  certList = PK11_ListCerts (PK11CertListAll, NULL);
  if (certList == NULL)
    {
      fprintf (stderr, "Unable to find certificates in the certificate database in %s.\n",
	       dbdir);
      nssError ();
      return -1;
    }

  /* We need to look at each certificate in the database. */
  for (certListNode = CERT_LIST_HEAD (certList);
       ! CERT_LIST_END (certListNode, certList);
       certListNode = CERT_LIST_NEXT (certListNode))
    {
      cert = certListNode->cert;

      pubKey = CERT_ExtractPublicKey (cert);
      if (pubKey == NULL)
	{
	  fprintf (stderr, "Unable to extract public key from the certificate with nickname %s from the certificate database in %s.\n",
		   cert->nickname, dbdir);
	  nssError ();
	  return -1;
	}

      /* Verify the file. */
      rc = verify_it (module_name, signature_name, pubKey);
      if (rc == 1)
	break; /* Verified! */
    }

  /* Shutdown NSS and exit NSPR gracefully. */
  nssCleanup ();

  return rc;
}
