/*
  This program signs the given file using the named certificate and private
  key in the given certificate database and places the signature in the named
  output file.

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
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

extern "C" {
#include <nspr.h>
#include <nss.h>
}
#include <string>

#include "util.h"
#include "nsscommon.h"

using namespace std;

// Called by methods within nsscommon.cxx.
extern "C"
void
nsscommon_error (const char *msg, int logit __attribute ((unused)))
{
  clog << msg << endl << flush;
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  if (argc < 2) {
    nsscommon_error (_("Module name was not specified."));
    return 1;
  }
  string module_name = argv[1];

  string cert_db_path;
  if (argc >= 3)
    cert_db_path = argv[2];
  else
    cert_db_path = server_cert_db_path ();

  const char *nickName = server_cert_nickname ();
  if (check_cert (cert_db_path, nickName) != 0)
    return 1;

  /* Call the NSPR initialization routines. */
  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

  /* Set the cert database password callback. */
  PK11_SetPasswordFunc (nssPasswordCallback);

  /* Initialize NSS. */
  SECStatus secStatus = nssInit (cert_db_path.c_str());
  if (secStatus != SECSuccess)
    {
      // Message already issued.
      return 1;
    }

  sign_file (cert_db_path, nickName, module_name, module_name + ".sgn");

  /* Shutdown NSS and exit NSPR gracefully. */
  nssCleanup (cert_db_path.c_str ());
  PR_Cleanup ();

  return 0;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
