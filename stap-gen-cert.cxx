/*
  Generate the SSL/signing certificate used by the Systemtap Compile Server.

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
#include <getopt.h>
#include <nspr.h>
}
#include <string>

#include "util.h"
#include "nsscommon.h"

using namespace std;

// Called from methods within nsscommon.cxx.
extern "C"
void
nsscommon_error (const char *msg, int logit __attribute ((unused)))
{
  clog << msg << endl;
}

/* getopt variables */
extern int optind;

/* File scope statics */
static bool use_db_password;
static string cert_db_path;
static string dnsNames;

static void
parse_options (int argc, char **argv)
{
  // Examine the command line.
  while (true)
    {
      int grc = getopt (argc, argv, "P");
      if (grc < 0)
        break;
      switch (grc)
        {
        case 'P':
	  use_db_password = true;
	  break;
	case '?':
	  // Invalid/unrecognized option given. Message has already been issued.
	  break;
        default:
          // Reached when one added a getopt option but not a corresponding switch/case:
          if (optarg)
	    nsscommon_error (_F("%s : unhandled option '%c %s'", argv[0], (char)grc, optarg));
          else
	    nsscommon_error (_F("%s : unhandled option '%c'", argv[0], (char)grc));
	  break;
	}
    }
  
  if (optind < argc)
    {
      // The first non-option is the certificate database path.
      cert_db_path = argv[optind];
      ++optind;

      // All other non options are additional dns names for the certificate.
      for (int i = optind; i < argc; i++)
	{
	  if (! dnsNames.empty ())
	    dnsNames += ",";
	  dnsNames += argv[i];
	}
    }
}

int
main (int argc, char **argv) {
  // Initial values.
  dnsNames.clear ();
  use_db_password = false;

  // Parse the arguments.
  parse_options (argc, argv);

  // Where is the ssl certificate/key database?
  if (cert_db_path.empty ())
    cert_db_path = server_cert_db_path ();

  // Make sure NSPR is initialized. Must be done before NSS is initialized
  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
  /* Set the cert database password callback. */
  PK11_SetPasswordFunc (nssPasswordCallback);

  // Generate the certificate database.
  int rc = gen_cert_db (cert_db_path, dnsNames, use_db_password);
  if (rc != 0)
    {
      // NSS message already issued.
      nsscommon_error (_("Unable to generate certificate"));
    }
  
  /* Exit NSPR gracefully. */
  PR_Cleanup ();

  return rc;
}
