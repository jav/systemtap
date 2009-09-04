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
#include "staprun.h"
#include "modverify.h"

#include <sys/stat.h>
#include <errno.h>

/* Function: int check_cert_db_permissions (const char *cert_db_path);
 * 
 * Check that the given certificate directory and its contents have
 * the correct permissions.
 *
 * Returns 0 if there is an error, 1 otherwise.
 */
static int
check_db_file_permissions (const char *cert_db_file) {
  struct stat info;
  int rc;

  rc = stat (cert_db_file, & info);
  if (rc)
    {
      fprintf (stderr, "Could not obtain information on certificate database file %s.\n",
	      cert_db_file);
      perror ("");
      return 0;
    }

  rc = 1; /* ok */

  /* The owner of the file must be root.  */
  if (info.st_uid != 0)
    {
      fprintf (stderr, "Certificate database file %s must be owned by root.\n",
	       cert_db_file);
      rc = 0;
    }

  /* Check the access permissions of the file.  */
  if ((info.st_mode & S_IRUSR) == 0)
    fprintf (stderr, "Certificate database file %s should be readable by the owner.\n", cert_db_file);
  if ((info.st_mode & S_IWUSR) == 0)
    fprintf (stderr, "Certificate database file %s should be writeable by the owner.\n", cert_db_file);
  if ((info.st_mode & S_IXUSR) != 0)
    {
      fprintf (stderr, "Certificate database file %s must not be executable by the owner.\n", cert_db_file);
      rc = 0;
    }
  if ((info.st_mode & S_IRGRP) == 0)
    {
      fprintf (stderr, "Certificate database file %s should be readable by the group.\n", cert_db_file);
      rc = 0;
    }
  if ((info.st_mode & S_IWGRP) != 0)
    {
      fprintf (stderr, "Certificate database file %s must not be writable by the group.\n", cert_db_file);
      rc = 0;
    }
  if ((info.st_mode & S_IXGRP) != 0)
    {
      fprintf (stderr, "Certificate database file %s must not be executable by the group.\n", cert_db_file);
      rc = 0;
    }
  if ((info.st_mode & S_IROTH) == 0)
    {
      fprintf (stderr, "Certificate database file %s should be readable by others.\n", cert_db_file);
      rc = 0;
    }
  if ((info.st_mode & S_IWOTH) != 0)
    {
      fprintf (stderr, "Certificate database file %s must not be writable by others.\n", cert_db_file);
      rc = 0;
    }
  if ((info.st_mode & S_IXOTH) != 0)
    {
      fprintf (stderr, "Certificate database file %s must not be executable by others.\n", cert_db_file);
      rc = 0;
    }

  return rc;
}

/* Function: int check_cert_db_permissions (const char *cert_db_path);
 * 
 * Check that the given certificate directory and its contents have
 * the correct permissions.
 *
 * Returns 0 if there is an error, 1 otherwise.
 */
static int
check_cert_db_permissions (const char *cert_db_path) {
  struct stat info;
  char *fileName;
  int rc;

  rc = stat (cert_db_path, & info);
  if (rc)
    {
      /* It is ok if the directory does not exist. This simply means that no signing
	 certificates have been authorized yet.  */
      if (errno == ENOENT)
	return 0;
      fprintf (stderr, "Could not obtain information on certificate database directory %s.\n",
	       cert_db_path);
      perror ("");
      return 0;
    }

  if (! S_ISDIR (info.st_mode))
    {
      fprintf (stderr, "Certificate database %s is not a directory.\n", cert_db_path);
      return 0;
    }

  /* The owner of the database must be root.  */
  if (info.st_uid != 0)
    {
      fprintf (stderr, "Certificate database directory %s must be owned by root.\n", cert_db_path);
      return 0;
    }

  rc = 1; /* ok */

  /* Check the database directory access permissions  */
  if ((info.st_mode & S_IRUSR) == 0)
    fprintf (stderr, "Certificate database %s should be readable by the owner.\n", cert_db_path);
  if ((info.st_mode & S_IWUSR) == 0)
    fprintf (stderr, "Certificate database %s should be writeable by the owner.\n", cert_db_path);
  if ((info.st_mode & S_IXUSR) == 0)
    fprintf (stderr, "Certificate database %s should be searchable by the owner.\n", cert_db_path);
  if ((info.st_mode & S_IRGRP) == 0)
    fprintf (stderr, "Certificate database %s should be readable by the group.\n", cert_db_path);
  if ((info.st_mode & S_IWGRP) != 0)
    {
      fprintf (stderr, "Certificate database %s must not be writable by the group.\n", cert_db_path);
      rc = 0;
    }
  if ((info.st_mode & S_IXGRP) == 0)
    fprintf (stderr, "Certificate database %s should be searchable by the group.\n", cert_db_path);
  if ((info.st_mode & S_IROTH) == 0)
    fprintf (stderr, "Certificate database %s should be readable by others.\n", cert_db_path);
  if ((info.st_mode & S_IWOTH) != 0)
    {
      fprintf (stderr, "Certificate database %s must not be writable by others.\n", cert_db_path);
      rc = 0;
    }
  if ((info.st_mode & S_IXOTH) == 0)
    fprintf (stderr, "Certificate database %s should be searchable by others.\n", cert_db_path);

  /* Now check the permissions of the critical files.  */
  fileName = PORT_Alloc (strlen (cert_db_path) + 11);
  if (! fileName)
    {
      fprintf (stderr, "Unable to allocate memory for certificate database file names\n");
      return 0;
    }

  sprintf (fileName, "%s/cert8.db", cert_db_path);
  rc &= check_db_file_permissions (fileName);
  sprintf (fileName, "%s/key3.db", cert_db_path);
  rc &= check_db_file_permissions (fileName);
  sprintf (fileName, "%s/secmod.db", cert_db_path);
  rc &= check_db_file_permissions (fileName);

  PORT_Free (fileName);

  if (rc == 0)
    fprintf (stderr, "Unable to use certificate database %s due to errors.\n", cert_db_path);

  return rc;
}

static int
verify_it (const char *signatureName, const SECItem *signature,
	   const void *module_data, off_t module_size,
	   const SECKEYPublicKey *pubKey)
{
  VFYContext *vfy;
  SECStatus secStatus;

  /* Create a verification context.  */
  vfy = VFY_CreateContextDirect (pubKey, signature, SEC_OID_PKCS1_RSA_ENCRYPTION,
				 SEC_OID_UNKNOWN, NULL, NULL);
  if (! vfy)
    {
      /* The key does not match the signature. This is not an error. It just
	 means we are currently trying the wrong certificate/key. i.e. the
	 module remains untrusted for now.  */
      return MODULE_UNTRUSTED;
    }

  /* Begin the verification process.  */
  secStatus = VFY_Begin(vfy);
  if (secStatus != SECSuccess)
    {
      fprintf (stderr, "Unable to initialize verification context while verifying %s using the signature in %s.\n",
	       modpath, signatureName);
      nssError ();
      return MODULE_CHECK_ERROR;
    }

  /* Add the data to be verified.  */
  secStatus = VFY_Update (vfy, module_data, module_size);
  if (secStatus != SECSuccess)
    {
      fprintf (stderr, "Error while verifying %s using the signature in %s.\n",
	       modpath, signatureName);
      nssError ();
      return MODULE_CHECK_ERROR;
    }

  /* Complete the verification.  */
  secStatus = VFY_End (vfy);
  if (secStatus != SECSuccess) {
    fprintf (stderr, "Unable to verify the signed module %s. It may have been altered since it was created.\n",
	     modpath);
    nssError ();
    return MODULE_ALTERED;
  }

  return MODULE_OK;
}

int verify_module (const char *signatureName, const void *module_data,
		   off_t module_size)
{
  const char *dbdir  = SYSCONFDIR "/systemtap/staprun";
  SECKEYPublicKey *pubKey;
  SECStatus secStatus;
  CERTCertList *certList;
  CERTCertListNode *certListNode;
  CERTCertificate *cert;
  PRStatus prStatus;
  PRFileInfo info;
  PRInt32  numBytes;
  PRFileDesc *local_file_fd;
  SECItem signature;
  int rc = 0;

  /* Verify the permissions of the certificate database and its files.  */
  if (! check_cert_db_permissions (dbdir))
    return MODULE_UNTRUSTED;

  /* Get the size of the signature file.  */
  prStatus = PR_GetFileInfo (signatureName, &info);
  if (prStatus != PR_SUCCESS || info.type != PR_FILE_FILE || info.size < 0)
    return MODULE_UNTRUSTED; /* Not signed */

  /* Open the signature file.  */
  local_file_fd = PR_Open (signatureName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "Could not open the signature file %s\n.", signatureName);
      nssError ();
      return MODULE_CHECK_ERROR;
    }

  /* Allocate space to read the signature file.  */
  signature.data = PORT_Alloc (info.size);
  if (! signature.data)
    {
      fprintf (stderr, "Unable to allocate memory for the signature in %s.\n", signatureName);
      nssError ();
      return MODULE_CHECK_ERROR;
    }

  /* Read the signature.  */
  numBytes = PR_Read (local_file_fd, signature.data, info.size);
  if (numBytes == 0) /* EOF */
    {
      fprintf (stderr, "EOF reading signature file %s.\n", signatureName);
      return MODULE_CHECK_ERROR;
    }
  if (numBytes < 0)
    {
      fprintf (stderr, "Error reading signature file %s.\n", signatureName);
      nssError ();
      return MODULE_CHECK_ERROR;
    }
  if (numBytes != info.size)
    {
      fprintf (stderr, "Incomplete data while reading signature file %s.\n", signatureName);
      return MODULE_CHECK_ERROR;
    }
  signature.len = info.size;

  /* Done with the signature file.  */
  PR_Close (local_file_fd);

  /* Call the NSPR initialization routines. */
  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

  /* Initialize NSS. */
  secStatus = NSS_Init (dbdir);
  if (secStatus != SECSuccess)
    {
      fprintf (stderr, "Unable to initialize nss library using the database in %s.\n",
	       dbdir);
      nssError ();
      return MODULE_CHECK_ERROR;
    }

  certList = PK11_ListCerts (PK11CertListAll, NULL);
  if (certList == NULL)
    {
      fprintf (stderr, "Unable to find certificates in the certificate database in %s.\n",
	       dbdir);
      nssError ();
      return MODULE_UNTRUSTED;
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
	  return MODULE_CHECK_ERROR;
	}

      /* Verify the file. */
      rc = verify_it (signatureName, & signature, module_data, module_size, pubKey);
      if (rc == MODULE_OK || rc == MODULE_ALTERED || rc == MODULE_CHECK_ERROR)
	break; /* resolved or error */
    }

  /* Shutdown NSS and exit NSPR gracefully. */
  nssCleanup ();

  return rc;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
