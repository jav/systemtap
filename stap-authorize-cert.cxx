/*
  Add the certificate contained in the given file to the given certificate database.

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
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

extern "C" {
#include <nspr.h>
}
#include <string>

#include "util.h"
#include "nsscommon.h"

using namespace std;

// Called by methods within nsscommon.
extern "C"
void
nsscommon_error (const char *msg, int logit __attribute ((unused)))
{
  clog << msg << endl << flush;
}

static void
fatal (const char *msg)
{
  nsscommon_error (msg);
  exit (1);
}

int
main (int argc, char **argv) {
  // Obtain the filename of the certificate.
  if (argc < 2)
    {
      fatal (_("Certificate file must be specified"));
      return 1;
    }
  const char *certFileName = argv[1];

  // Obtain the certificate database directory name.
  if (argc < 3)
    {
      fatal (_("Certificate database directory must be specified"));
      return 1;
    }
  const char *certDBName = argv[2];

  // Make sure NSPR is initialized. Must be done before NSS is initialized
  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
  /* Set the cert database password callback. */
  PK11_SetPasswordFunc (nssPasswordCallback);

  // Add the certificate to the database.
  SECStatus secStatus = add_client_cert (certFileName, certDBName);
  if (secStatus != SECSuccess)
    {
      // NSS message already issued.
      nsscommon_error (_("Unable to authorize certificate"));
    }

  // Clean up.
  PR_Cleanup ();

  return secStatus == SECSuccess;
}
