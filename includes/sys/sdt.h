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

#define STAP_PROBE_DATA_(probe)	\
  __asm__ volatile (".section .probes\n" \
		    "\t.align 8\n"   \
		    "1:\n\t.asciz " #probe "\n" \
		    "\t.align 4\n" \
		    "\t.int 0x31425250\n" \
  		    "\t.align 8\n" \
		    "\t.long 1b\n" \
  		    "\t.align 8\n" \
		    "\t.long 2f\n" \
		    "\t.previous\n")

#define STAP_PROBE_DATA(probe)					\
  STAP_PROBE_DATA_(#probe)

/* These baroque macros are used to create a unique label. */
#define STAP_CONCAT(a,b) a ## b
#define STAP_LABEL_PREFIX(p) _stapprobe1_ ## p
/* __COUNTER__ is not present in gcc 4.1 */
#if __GNUC__ == 4 && __GNUC_MINOR__ >= 3
#define STAP_COUNTER  STAP_CONCAT(__,COUNTER__)
#else
#define STAP_COUNTER  STAP_CONCAT(__,LINE__)
#endif
#define STAP_LABEL(a,b) STAP_CONCAT(a,b) 

/* Taking the address of a local label and/or referencing alloca prevents the
   containing function from being inlined, which keeps the parameters visible. */

#if __GNUC__ == 4 && __GNUC_MINOR__ <= 1
#include <alloca.h>
#define STAP_UNINLINE alloca((size_t)0)
#else
#define STAP_UNINLINE
#endif

#define STAP_UNINLINE_LABEL(label) \
  __extension__ static volatile long labelval  __attribute__ ((unused)) = (long) &&label

#define STAP_PROBE_(probe)			\
do { \
  STAP_PROBE_DATA(probe);	\
  __asm__ volatile ("2:\n"	\
		    "\tnop");	\
 } while (0)

#define STAP_PROBE1_(probe,label,parm1)		\
do { \
  STAP_UNINLINE_LABEL(label);			\
  volatile __typeof__((parm1)) arg1 = parm1;	\
  STAP_UNINLINE;				\
  STAP_PROBE_DATA(probe);						\
  label:						\
  __asm__ volatile ("2:\n" \
		    "\tnop /* %0 */" :: "g"(arg1));	\
 } while (0)

#define STAP_PROBE2_(probe,label,parm1,parm2)	\
do { \
  STAP_UNINLINE_LABEL(label);			\
  volatile __typeof__((parm1)) arg1 = parm1;	\
  volatile __typeof__((parm2)) arg2 = parm2;	\
  STAP_UNINLINE;				\
  STAP_PROBE_DATA(probe);						\
  label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 */" :: "g"(arg1), "g"(arg2));	\
} while (0)

#define STAP_PROBE3_(probe,label,parm1,parm2,parm3)	\
do { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  STAP_UNINLINE;			     \
  STAP_PROBE_DATA(probe);						\
   label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 */" :: "g"(arg1), "g"(arg2), "g"(arg3)); \
} while (0)

#define STAP_PROBE4_(probe,label,parm1,parm2,parm3,parm4)	\
do { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  volatile __typeof__((parm4)) arg4 = parm4; \
  STAP_UNINLINE;			     \
  STAP_PROBE_DATA(probe);						\
  label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4)); \
} while (0)

#define STAP_PROBE5_(probe,label,parm1,parm2,parm3,parm4,parm5)	\
do  { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  volatile __typeof__((parm4)) arg4 = parm4; \
  volatile __typeof__((parm5)) arg5 = parm5; \
  STAP_UNINLINE;			     \
  STAP_PROBE_DATA(probe);						\
  label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5)); \
} while (0)

#define STAP_PROBE6_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6)	\
do { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  volatile __typeof__((parm4)) arg4 = parm4; \
  volatile __typeof__((parm5)) arg5 = parm5; \
  volatile __typeof__((parm6)) arg6 = parm6; \
  STAP_UNINLINE;				     \
  STAP_PROBE_DATA(probe);						\
  label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6)); \
} while (0)

#define STAP_PROBE7_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7)	\
do  { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  volatile __typeof__((parm4)) arg4 = parm4; \
  volatile __typeof__((parm5)) arg5 = parm5; \
  volatile __typeof__((parm6)) arg6 = parm6; \
  volatile __typeof__((parm7)) arg7 = parm7; \
  STAP_UNINLINE;				     \
  STAP_PROBE_DATA(probe);						\
   label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7)); \
} while (0)

#define STAP_PROBE8_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
do { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  volatile __typeof__((parm4)) arg4 = parm4; \
  volatile __typeof__((parm5)) arg5 = parm5; \
  volatile __typeof__((parm6)) arg6 = parm6; \
  volatile __typeof__((parm7)) arg7 = parm7; \
  volatile __typeof__((parm8)) arg8 = parm8; \
  STAP_UNINLINE;				     \
  STAP_PROBE_DATA(probe);						\
   label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 %7 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7), "g"(arg8)); \
} while (0)

#define STAP_PROBE9_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
do { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  volatile __typeof__((parm4)) arg4 = parm4; \
  volatile __typeof__((parm5)) arg5 = parm5; \
  volatile __typeof__((parm6)) arg6 = parm6; \
  volatile __typeof__((parm7)) arg7 = parm7; \
  volatile __typeof__((parm8)) arg8 = parm8; \
  volatile __typeof__((parm9)) arg9 = parm9; \
  STAP_UNINLINE;				     \
  STAP_PROBE_DATA(probe);						\
  label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 %7 %8 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7), "g"(arg8), "g"(arg9)); \
} while (0)

#define STAP_PROBE10_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
do { \
  STAP_UNINLINE_LABEL(label);		     \
  volatile __typeof__((parm1)) arg1 = parm1; \
  volatile __typeof__((parm2)) arg2 = parm2; \
  volatile __typeof__((parm3)) arg3 = parm3; \
  volatile __typeof__((parm4)) arg4 = parm4; \
  volatile __typeof__((parm5)) arg5 = parm5; \
  volatile __typeof__((parm6)) arg6 = parm6; \
  volatile __typeof__((parm7)) arg7 = parm7; \
  volatile __typeof__((parm8)) arg8 = parm8; \
  volatile __typeof__((parm9)) arg9 = parm9; \
  volatile __typeof__((parm10)) arg10 = parm10; \
  STAP_UNINLINE;				     \
  STAP_PROBE_DATA(probe);						\
  label:						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 %7 %8 %9 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7), "g"(arg8), "g"(arg9), "g"(arg10)); \
} while (0)

#define STAP_PROBE(provider,probe)	\
  STAP_PROBE_(probe)
#define STAP_PROBE1(provider,probe,parm1)	\
  STAP_PROBE1_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1))
#define STAP_PROBE2(provider,probe,parm1,parm2)                              \
  STAP_PROBE2_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2))
#define STAP_PROBE3(provider,probe,parm1,parm2,parm3)                        \
  STAP_PROBE3_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3))
#define STAP_PROBE4(provider,probe,parm1,parm2,parm3,parm4)                  \
  STAP_PROBE4_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3),(parm4))
#define STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5)            \
  STAP_PROBE5_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3),(parm4),(parm5))
#define STAP_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6)      \
  STAP_PROBE6_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3),(parm4),(parm5),(parm6))
#define STAP_PROBE7(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7) \
  STAP_PROBE7_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7))
#define STAP_PROBE8(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
  STAP_PROBE8_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7),(parm8))
#define STAP_PROBE9(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
  STAP_PROBE9_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7),(parm8),(parm9))
#define STAP_PROBE10(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
  STAP_PROBE10_(probe,STAP_LABEL(STAP_LABEL_PREFIX(probe),STAP_COUNTER),(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7),(parm8),(parm9),(parm10))

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

