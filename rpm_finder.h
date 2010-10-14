// systemtap debuginfo rpm finder
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

extern void missing_rpm_list_print (systemtap_session &, const char *);
extern int find_debug_rpms (systemtap_session &, const char *);
extern int find_devel_rpms (systemtap_session &, const char *);
