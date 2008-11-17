// Copyright (C) 2005-2008 Red Hat Inc.
// Copyright (C) 2006 Intel Corporation.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include <stdlib.h>
#include <string.h>
extern int _stap_probe_sentinel;

#define STAP_PROBE_START()		      \
  char *stap_sdt = getenv("SYSTEMTAP_SDT"); \
  if (stap_sdt != NULL)		\
     _stap_probe_start ()

#define STAP_PROBE_STRUCT(probe,argc) \
struct _probe_ ## probe  \
{				 \
  char probe_name [strlen(#probe)+1]; \
  int arg_count;		 \
}; \
static volatile struct _probe_ ## probe _probe_ ## probe __attribute__ ((section (".probes"))) = {#probe,argc};

#define STAP_PROBE(provider,probe) \
STAP_PROBE_STRUCT(probe,0)	   \
  if (__builtin_expect(_stap_probe_sentinel, 0)) \
    _stap_probe_0 (_probe_ ## probe.probe_name);


#define STAP_PROBE1(provider,probe,arg1) \
STAP_PROBE_STRUCT(probe,1)	   \
  if (__builtin_expect(_stap_probe_sentinel, 0)) \
    _stap_probe_1 (_probe_ ## probe.probe_name,(size_t)arg1);


#define STAP_PROBE2(provider,probe,arg1,arg2) \
STAP_PROBE_STRUCT(probe,2)	   \
  if (__builtin_expect(_stap_probe_sentinel, 0)) \
    _stap_probe_2 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2);

#define STAP_PROBE3(provider,probe,arg1,arg2,arg3) \
STAP_PROBE_STRUCT(probe,3)	   \
  if (__builtin_expect(_stap_probe_sentinel, 0)) \
    _stap_probe_3 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3);

#define STAP_PROBE4(provider,probe,arg1,arg2,arg3,arg4)	\
STAP_PROBE_STRUCT(probe,4)	   \
  if (__builtin_expect(_stap_probe_sentinel, 0)) \
    _stap_probe_4 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3,(size_t)arg4);

#define STAP_PROBE5(provider,probe,arg1,arg2,arg3,arg4,arg5)	\
STAP_PROBE_STRUCT(probe,5)	   \
  if (__builtin_expect(_stap_probe_sentinel, 0)) \
    _stap_probe_5 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3,(size_t)arg4,(size_t)arg5);
