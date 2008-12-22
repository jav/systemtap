// Copyright (C) 2005-2008 Red Hat Inc.
// Copyright (C) 2006 Intel Corporation.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include <string.h>

#if _LP64
#define STAP_PROBE_STRUCT_ARG(arg)		\
  __uint64_t arg;
#else
#define STAP_PROBE_STRUCT_ARG(arg)		\
  long arg __attribute__ ((aligned(8)));		 
#endif

#define STAP_PROBE_STRUCT(probe,type,argc)	\
struct _probe_ ## probe				\
{						\
  char probe_name [strlen(#probe)+1];		\
  int probe_type;		\
  STAP_PROBE_STRUCT_ARG	(probe_arg);		\
};						\
 static volatile struct _probe_ ## probe _probe_ ## probe __attribute__ ((section (".probes"))) = {#probe,type,argc};

// The goto _probe_ prevents the label from "drifting"
#ifdef USE_STAP_PROBE
#define STAP_PROBE(provider,probe)		\
  STAP_PROBE_STRUCT(probe,0,0)			\
  _stap_probe_0 (_probe_ ## probe.probe_name);
#else
#define STAP_PROBE(provider,probe)		\
_probe_ ## probe:				\
  asm volatile ("nop");				\
  STAP_PROBE_STRUCT(probe,1,(size_t)&& _probe_ ## probe) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto _probe_ ## probe;
#endif


#ifdef USE_STAP_PROBE
#define STAP_PROBE1(provider,probe,arg1)	\
  STAP_PROBE_STRUCT(probe,0,1)			\
  _stap_probe_1 (_probe_ ## probe.probe_name,(size_t)arg1);
#else
#define STAP_PROBE1(provider,probe,parm1)	\
 {volatile typeof(parm1) arg1 = parm1; \
_probe_ ## probe:				\
  asm volatile ("nop" :: "r"(arg1)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& _probe_ ## probe) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto _probe_ ## probe;}
#endif


#ifdef USE_STAP_PROBE
#define STAP_PROBE2(provider,probe,arg1,arg2)	\
  STAP_PROBE_STRUCT(probe,0,2)			\
  _stap_probe_2 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2);
#else
#define STAP_PROBE2(provider,probe,parm1,parm2)	\
 {volatile typeof(parm1) arg1 = parm1;	\
  volatile typeof(parm2) arg2 = parm2; \
_probe_ ## probe:				\
  asm volatile ("nop" :: "r"(arg1), "r"(arg2)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& _probe_ ## probe)\
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto _probe_ ## probe;}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE3(provider,probe,arg1,arg2,arg3) \
  STAP_PROBE_STRUCT(probe,0,3)			\
  _stap_probe_3 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3);
#else
#define STAP_PROBE3(provider,probe,parm1,parm2,parm3) \
 {volatile typeof(parm1) arg1 = parm1;     \
  volatile typeof(parm2) arg2 = parm2; \
  volatile typeof(parm3) arg3 = parm3; \
_probe_ ## probe:				\
  asm volatile ("nop" :: "r"(arg1), "r"(arg2), "r"(arg3)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& _probe_ ## probe) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto _probe_ ## probe;}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE4(provider,probe,arg1,arg2,arg3,arg4)	\
  STAP_PROBE_STRUCT(probe,0,4)			\
  _stap_probe_4 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3,(size_t)arg4);
#else
#define STAP_PROBE4(provider,probe,parm1,parm2,parm3,parm4) \
 {volatile typeof(parm1) arg1 = parm1;	    \
  volatile typeof(parm2) arg2 = parm2; \
  volatile typeof(parm3) arg3 = parm3; \
  volatile typeof(parm4) arg4 = parm4; \
_probe_ ## probe:				\
  asm volatile ("nop" "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4));		\
  STAP_PROBE_STRUCT(probe,1,(size_t)&& _probe_ ## probe) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto _probe_ ## probe;}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE5(provider,probe,arg1,arg2,arg3,arg4,arg5)	\
  STAP_PROBE_STRUCT(probe,0,5)			\
  _stap_probe_5 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3,(size_t)arg4,(size_t)arg5);
#else
#define STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5) \
 {volatile typeof(parm1) arg1 = parm1;		  \
  volatile typeof(parm2) arg2 = parm2; \
  volatile typeof(parm3) arg3 = parm3; \
  volatile typeof(parm4) arg4 = parm4; \
  volatile typeof(parm5) arg5 = parm5; \
_probe_ ## probe:				\
  asm volatile ("nop" "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4), "r"(arg5)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& _probe_ ## probe) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
    goto _probe_ ## probe;}
#endif

#define DTRACE_PROBE(provider,probe) \
STAP_PROBE(provider,probe)
#define DTRACE_PROBE1(provider,probe,parm1) \
STAP_PROBE1(provider,probe,parm1)
#define DTRACE_PROBE2(provider,probe,parm1,parm2) \
STAP_PROBE2(provider,probe,parm1,parm2)
#define DTRACE_PROBE3(provider,probe,parm1,parm2,parm3) \
STAP_PROBE3(provider,probe,parm1,parm2,parm3) 
#define DTRACE_PROBE4(provider,probe,parm1,parm2,parm3,parm4) \
STAP_PROBE4(provider,probe,parm1,parm2,parm3,parm4) 
