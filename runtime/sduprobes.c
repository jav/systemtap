// Copyright (C) 2005-2008 Red Hat Inc.
// Copyright (C) 2006 Intel Corporation.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include <stddef.h>
#define unused __attribute__ ((unused))

int _stap_probe_sentinel = 0;

void
_stap_probe_start()
{
   _stap_probe_sentinel = 1;
}

int
_stap_probe_0 (char* probe unused)
{
   return 1;
}

int
_stap_probe_1 (char* probe unused,
	       size_t arg1 unused)
{
   return 1;
}

int
_stap_probe_2 (char* probe unused ,
	       size_t arg1 unused,
	       size_t arg2 unused)
{
   return 1;
}

int
_stap_probe_3 (char* probe unused,
	       size_t arg1 unused,
	       size_t arg2 unused,
	       size_t arg3 unused)
{
   return 1;
}

int
_stap_probe_4 (char* probe unused,
	       size_t arg1 unused,
	       size_t arg2 unused,
	       size_t arg3 unused,
	       size_t arg4 unused)
{
   return 1;
}

int
_stap_probe_5 (char* probe unused,
	       size_t arg1 unused,
	       size_t arg2 unused,
	       size_t arg3 unused,
	       size_t arg4 unused,
	       size_t arg5 unused)
{
   return 1;
}
