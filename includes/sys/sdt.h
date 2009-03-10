/* Copyright (C) 2005-2009 Red Hat Inc.
   Copyright (C) 2006 Intel Corporation.

   This file is part of systemtap, and is free software.  You can
   redistribute it and/or modify it under the terms of the GNU General
   Public License (GPL); either version 2, or (at your option) any
   later version.
*/

#ifndef _SYS_SDT_H
#define _SYS_SDT_H    1

#include <string.h>
#include <sys/types.h>

#if _LP64
#define STAP_PROBE_STRUCT_ARG(arg)		\
  __uint64_t arg
#else
#define STAP_PROBE_STRUCT_ARG(arg)		\
  long arg __attribute__ ((aligned(8)))		 
#endif

#define STAP_SENTINEL 0x31425250

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
__extension__ static volatile struct _probe_ ## probe _probe_ ## probe __attribute__ ((section (".probes"))) = {STAP_SENTINEL,(size_t)& probe ## _ ## probe_name[0],argc};

/* The goto _probe_ prevents the label from "drifting" */
#define STAP_LABEL_REF(probe, label)				    \
  if (__builtin_expect(_probe_ ## probe.probe_type < 0, 0)) \
    goto label;

/* These baroque macros are used to create a unique label */
#define STAP_CONCAT(a,b) a ## b
#define STAP_LABEL_PREFIX(p) _stapprobe1_ ## p
/* __COUNTER__ is not present in gcc 4.1 */
#if __GNUC__ == 4 && __GNUC_MINOR__ >= 3
#define STAP_COUNTER  STAP_CONCAT(__,COUNTER__)
#else
#define STAP_COUNTER  STAP_CONCAT(__,LINE__)
#endif
#define STAP_LABEL(a,b) STAP_CONCAT(a,b) 

#define STAP_PROBE_(probe,label)		\
do { \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label: \
  __asm__ volatile ("nop");}	    \
 while (0)

#define STAP_PROBE1_(probe,label,parm1)		\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1;	\
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label: \
  __asm__ volatile ("nop /* %0 */" :: "X"( arg1));}  \
while (0)

#define STAP_PROBE2_(probe,label,parm1,parm2)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1;	\
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2;	\
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label: \
  __asm__ volatile ("nop /* %0 %1 */" :: "X"(arg1), "X"(arg2)); \
} while (0)

#define STAP_PROBE3_(probe,label,parm1,parm2,parm3)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1;     \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label:					   \
  __asm__ volatile ("nop /* %0 %1 %2 */" :: "X"(arg1), "X"(arg2), "X"(arg3)); \
} while (0)

#define STAP_PROBE4_(probe,label,parm1,parm2,parm3,parm4)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label)	\
  STAP_LABEL_REF(probe,label);		    \
label:				\
  __asm__ volatile ("nop /* %0 %1 %2 %3 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4)); \
} while (0)

#define STAP_PROBE5_(probe,label,parm1,parm2,parm3,parm4,parm5)	\
do  { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label:				\
  __asm__ volatile ("nop /* %0 %1 %2 %3 %4 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5)); \
} while (0)

#define STAP_PROBE6_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label:				\
  __asm__ volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6)); \
} while (0)

#define STAP_PROBE7_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7)	\
do  { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile __typeof__((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label:				\
  __asm__ volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7)); \
} while (0)

#define STAP_PROBE8_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile __typeof__((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  volatile __typeof__((parm8)) arg8  __attribute__ ((unused)) = parm8; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);		    \
label:				\
  __asm__ volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8)); \
} while (0)

#define STAP_PROBE9_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile __typeof__((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  volatile __typeof__((parm8)) arg8  __attribute__ ((unused)) = parm8; \
  volatile __typeof__((parm9)) arg9  __attribute__ ((unused)) = parm9; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);   	    \
label:				\
  __asm__ volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8), "X"(arg9)); \
} while (0)

#define STAP_PROBE10_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile __typeof__((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  volatile __typeof__((parm8)) arg8  __attribute__ ((unused)) = parm8; \
  volatile __typeof__((parm9)) arg9  __attribute__ ((unused)) = parm9; \
  volatile __typeof__((parm10)) arg10  __attribute__ ((unused)) = parm10; \
  STAP_PROBE_STRUCT(probe,(size_t)&& label) \
  STAP_LABEL_REF(probe,label);  	    \
label:				\
  __asm__ volatile ("nop" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8), "X"(arg9), "X"(arg10)); \
} while (0)

#define STAP_PROBE(provider,probe)	\
  STAP_PROBE_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER))
#define STAP_PROBE1(provider,probe,...)	\
  STAP_PROBE1_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE2(provider,probe,...)	\
  STAP_PROBE2_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE3(provider,probe,...)	\
  STAP_PROBE3_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE4(provider,probe,...)	\
  STAP_PROBE4_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE5(provider,probe,...)	\
  STAP_PROBE5_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE6(provider,probe,...)	\
  STAP_PROBE6_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE7(provider,probe,...)	\
  STAP_PROBE7_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE8(provider,probe,...)	\
  STAP_PROBE8_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE9(provider,probe,...)	\
  STAP_PROBE9_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)
#define STAP_PROBE10(provider,probe,...)	\
  STAP_PROBE10_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),__VA_ARGS__)

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
#endif /* sys/sdt.h */

