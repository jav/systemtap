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

// g++ 4.3.2 doesn't emit DW_TAG_label
#ifdef __cplusplus
#define STAP_PROBE_STRUCT(probe,argc)	\
struct _probe_ ## probe				\
{						\
  int probe_type;				\
  STAP_PROBE_STRUCT_ARG	(probe_name);		\
  STAP_PROBE_STRUCT_ARG	(probe_arg);		\
};						\
static char probe ## _ ## probe_name [strlen(#probe)+1] 	\
       __attribute__ ((section (".probes"))) 	\
       = #probe; 				\
static volatile struct _probe_ ## probe _probe_ ## probe __attribute__ ((section (".probes"))) = {STAP_SENTINEL,(size_t)& probe ## _ ## probe_name[0],argc};
#else
#define STAP_PROBE_STRUCT(probe,argc)	
#endif

#ifdef __cplusplus
#define STAP_LABEL_REF(probe) \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
    goto STAP_LABEL(probe,__LINE__);
#else
#define STAP_LABEL_REF(probe)		\
  volatile static int sentinel_ ## probe = 0;	\
  if (__builtin_expect(sentinel_ ## probe < 0, 0)) \
    goto STAP_LABEL(probe,__LINE__);
#endif

#define STAP_CONCAT(a,b) a ## b
#define STAP_LABEL(p,n) \
  STAP_CONCAT(_stapprobe1_ ## p ## _, n)

// The goto _probe_ prevents the label from "drifting"
#define STAP_PROBE(provider,probe)		\
 {						\
STAP_LABEL(probe,__LINE__):			\
  asm volatile ("nop");				\
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe)					 \
}

#define STAP_PROBE1(provider,probe,parm1)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;		\
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop /* %0 */" :: "g"( arg1)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE2(provider,probe,parm1,parm2)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;	\
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2;		\
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop /* %0 %1 */" :: "g"(arg1), "g"(arg2)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE3(provider,probe,parm1,parm2,parm3) \
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;     \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
STAP_LABEL(probe,__LINE__):					   \
  asm volatile ("nop /* %0 %1 %2 */" :: "g"(arg1), "g"(arg2), "g"(arg3)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE4(provider,probe,parm1,parm2,parm3,parm4) \
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;		    \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop /* %0 %1 %2 %3 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5) \
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1;		  \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile typeof((parm5)) arg5  __attribute__ ((unused)) = parm5; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop /* %0 %1 %2 %3 %4 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile typeof((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile typeof((parm6)) arg6  __attribute__ ((unused)) = parm6; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE7(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile typeof((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile typeof((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile typeof((parm7)) arg7  __attribute__ ((unused)) = parm7; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE8(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile typeof((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile typeof((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile typeof((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  volatile typeof((parm8)) arg8  __attribute__ ((unused)) = parm8; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

#define STAP_PROBE9(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9)	\
 {volatile typeof((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile typeof((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile typeof((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile typeof((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile typeof((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile typeof((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile typeof((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  volatile typeof((parm8)) arg8  __attribute__ ((unused)) = parm8; \
  volatile typeof((parm9)) arg9  __attribute__ ((unused)) = parm9; \
STAP_LABEL(probe,__LINE__):				\
  asm volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8), "X"(arg9)); \
  STAP_PROBE_STRUCT(probe,(size_t)&& STAP_LABEL(probe,__LINE__)) \
  STAP_LABEL_REF(probe);}

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
#define DTRACE_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5) \
STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5) 
#define DTRACE_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6) \
STAP_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6) 
#define DTRACE_PROBE7(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7) \
STAP_PROBE7(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7) 
#define DTRACE_PROBE8(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
STAP_PROBE8(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) 
#define DTRACE_PROBE9(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
STAP_PROBE9(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) 
