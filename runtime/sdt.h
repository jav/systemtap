// Copyright (C) 2005-2009 Red Hat Inc.
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

#define STAP_SENTINEL 0x31425250

#define STAP_PROBE_STRUCT(probe,type,argc)	\
struct _probe_ ## probe				\
{						\
  int probe_type;				\
  STAP_PROBE_STRUCT_ARG	(probe_name);		\
  STAP_PROBE_STRUCT_ARG	(probe_arg);		\
};						\
static char probe ## _ ## probe_name [strlen(#probe)+1] 	\
       __attribute__ ((section (".probes"))) 	\
       = #probe; 				\
 static volatile struct _probe_ ## probe _probe_ ## probe __attribute__ ((section (".probes"))) = {STAP_SENTINEL,(long)& probe ## _ ## probe_name[0],argc};

#define STAP_CONCAT(a,b) a ## b
#define STAP_LABEL(p,n) \
  STAP_CONCAT(_stapprobe1_ ## p ## _, n)

// The goto _probe_ prevents the label from "drifting"
#ifdef USE_STAP_PROBE
#define STAP_PROBE(provider,probe)		\
  STAP_PROBE_STRUCT(probe,0,0)			\
  _stap_probe_0 (_probe_ ## probe.probe_name);
#else
#define STAP_PROBE(provider,probe)		\
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop");				\
  STAP_PROBE_STRUCT(probe,1,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto STAP_LABEL(probe,__LINE__);
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE1(provider,probe,arg1)	\
  STAP_PROBE_STRUCT(probe,0,1)			\
  _stap_probe_1 (_probe_ ## probe.probe_name,(size_t)arg1);
#else
#define STAP_PROBE1(provider,probe,parm1)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;		\
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "g"(arg1)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto STAP_LABEL(probe,__LINE__);}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE2(provider,probe,arg1,arg2)	\
  STAP_PROBE_STRUCT(probe,0,2)			\
  _stap_probe_2 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2);
#else
#define STAP_PROBE2(provider,probe,parm1,parm2)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;	\
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2;		\
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "g"(arg1), "g"(arg2)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto STAP_LABEL(probe,__LINE__);}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE3(provider,probe,arg1,arg2,arg3) \
  STAP_PROBE_STRUCT(probe,0,3)			\
  _stap_probe_3 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3);
#else
#define STAP_PROBE3(provider,probe,parm1,parm2,parm3) \
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;     \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
STAP_LABEL(probe,__LINE__):					   \
  asm volatile ("nop" :: "g"(arg1), "g"(arg2), "g"(arg3)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto STAP_LABEL(probe,__LINE__);}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE4(provider,probe,arg1,arg2,arg3,arg4)	\
  STAP_PROBE_STRUCT(probe,0,4)			\
  _stap_probe_4 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3,(size_t)arg4);
#else
#define STAP_PROBE4(provider,probe,parm1,parm2,parm3,parm4) \
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;		    \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4));	\
  STAP_PROBE_STRUCT(probe,1,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
     goto STAP_LABEL(probe,__LINE__);}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE5(provider,probe,arg1,arg2,arg3,arg4,arg5)	\
  STAP_PROBE_STRUCT(probe,0,5)			\
  _stap_probe_5 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3,(size_t)arg4,(size_t)arg5);
#else
#define STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5) \
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;		  \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile typeof((parm5)) arg5  __attribute__ ((unused)) = parm5; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
    goto STAP_LABEL(probe,__LINE__);}
#endif

#ifdef USE_STAP_PROBE
#define STAP_PROBE6(provider,probe,arg1,arg2,arg3,arg4,arg5,arg6)	\
  STAP_PROBE_STRUCT(probe,0,6)			\
  _stap_probe_6 (_probe_ ## probe.probe_name,(size_t)arg1,(size_t)arg2,(size_t)arg3,(size_t)arg4,(size_t)arg5,(size_t)arg6);
#else
#define STAP_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;		  \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile typeof((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile typeof((parm6)) arg6  __attribute__ ((unused)) = parm6; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6)); \
  STAP_PROBE_STRUCT(probe,1,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
    goto STAP_LABEL(probe,__LINE__);}
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
#define DTRACE_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5)	\
STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5) 
#define DTRACE_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6)	\
STAP_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6) 
