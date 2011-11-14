// Copyright (C) 2011 Red Hat Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include <iostream>
#include <climits>

extern "C" {
#include <unistd.h>
#include <sys/types.h>
}

#include "util.h"
#include "privilege.h"

using namespace std;

privilege_t pr_next (privilege_t p)
{
  switch (p)
    {
    case pr_stapusr:
      p = pr_stapdev;
      break;
    case pr_stapdev:
    default:
      p = pr_end;
      break;
    }
  return p;
}

const char *pr_name (privilege_t p)
{
  switch (p)
    {
    case pr_stapusr:
      return "stapusr";
    case pr_stapdev:
      return "stapdev";
    default:
      break;
    }
  return "unknown";
}

int pr_contains (privilege_t actual, privilege_t required)
{
  return (actual & required) == required;
}

/* Determine the privilege credentials of the current user. If the user is not root, this
   is determined by the user's group memberships. */
privilege_t get_privilege_credentials (void)
{
  gid_t gid, gidlist[NGROUPS_MAX];
  int i, ngids;
  gid_t stapdev_gid, stapusr_gid;

  static privilege_t stp_privilege = pr_unknown;

  /* Have we already computed this? */
  if (stp_privilege != pr_unknown)
    return stp_privilege;

  /* If the real uid of the user is root, then this user has all privileges. */
  if (getuid() == 0)
    {
      stp_privilege = pr_all;
      return stp_privilege;
    }

  /* These are the gids of the groups we are interested in. */
  stapdev_gid = get_gid("stapdev");
  stapusr_gid = get_gid("stapusr");

  /* If neither group was found, then the group memberships are irrelevant.  */
  if (stapdev_gid == (gid_t)-1 && stapusr_gid == (gid_t)-1)
    {
      stp_privilege = pr_none;
      return stp_privilege;
    }

  /* Obtain a list of the user's groups. */
  ngids = getgroups(NGROUPS_MAX, gidlist);
  if (ngids < 0)
    {
      cerr << _("Unable to retrieve group list") << endl;
      stp_privilege = pr_none;
      return stp_privilege;
    }

  /* The privilege credentials will be represented by a bit mask of the user's group memberships.
     Start with an empty mask. */
  stp_privilege = pr_none;

  /* According to the getgroups() man page, getgroups() may not
   * return the effective gid, so examine the effective gid first first followed by the group
   * gids obtained by getgroups. */
  for (i = -1, gid = getegid(); i < ngids; ++i, gid = gidlist[i])
    {
      if (gid == stapdev_gid)
	stp_privilege = privilege_t (stp_privilege | pr_stapdev);
      else if (gid == stapusr_gid)
	stp_privilege = privilege_t (stp_privilege | pr_stapusr);
      if (stp_privilege == pr_all)
	break;
    }

  return stp_privilege;
}
