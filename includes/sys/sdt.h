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

#define STAP_PROBE_DATA_(probe,dataop)	\
  __asm__ volatile (".section .probes\n" \
		    "\t.align 8\n"   \
		    "1:\n\t.asciz " #probe "\n" \
		    "\t.align 4\n" \
		    "\t.int 0x31425250\n" \
  		    "\t.align 8\n" \
		    "\t" #dataop " 1b\n" \
  		    "\t.align 8\n" \
		    "\t" #dataop " 2f\n" \
		    "\t.previous\n")

#if _LP64
#define STAP_PROBE_DATA(probe)					\
  STAP_PROBE_DATA_(#probe,.quad)
#else
#define STAP_PROBE_DATA(probe)					\
  STAP_PROBE_DATA_(#probe,.long)
#endif

#define STAP_PROBE_(probe)		\
do { \
  STAP_PROBE_DATA(probe);	\
  __asm__ volatile ("2:\n" \
		    "\tnop");	\
 } while (0)

#define STAP_PROBE1_(probe,parm1)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1;	\
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n" \
		    "\tnop /* %0 */" :: "X"(arg1));	\
 } while (0)

#define STAP_PROBE2_(probe,parm1,parm2)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1;	\
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2;	\
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 */" :: "X"(arg1), "X"(arg2));	\
} while (0)

#define STAP_PROBE3_(probe,parm1,parm2,parm3)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1;     \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 */" :: "X"(arg1), "X"(arg2), "X"(arg3)); \
} while (0)

#define STAP_PROBE4_(probe,parm1,parm2,parm3,parm4)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4)); \
} while (0)

#define STAP_PROBE5_(probe,parm1,parm2,parm3,parm4,parm5)	\
do  { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5)); \
} while (0)

#define STAP_PROBE6_(probe,parm1,parm2,parm3,parm4,parm5,parm6)	\
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6)); \
} while (0)

#define STAP_PROBE7_(probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7) \
do  { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile __typeof__((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7)); \
} while (0)

#define STAP_PROBE8_(probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
do { \
  volatile __typeof__((parm1)) arg1  __attribute__ ((unused)) = parm1; \
  volatile __typeof__((parm2)) arg2  __attribute__ ((unused)) = parm2; \
  volatile __typeof__((parm3)) arg3  __attribute__ ((unused)) = parm3; \
  volatile __typeof__((parm4)) arg4  __attribute__ ((unused)) = parm4; \
  volatile __typeof__((parm5)) arg5  __attribute__ ((unused)) = parm5; \
  volatile __typeof__((parm6)) arg6  __attribute__ ((unused)) = parm6; \
  volatile __typeof__((parm7)) arg7  __attribute__ ((unused)) = parm7; \
  volatile __typeof__((parm8)) arg8  __attribute__ ((unused)) = parm8; \
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 %7 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8)); \
} while (0)

#define STAP_PROBE9_(probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
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
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 %7 %8 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8), "X"(arg9)); \
} while (0)

#define STAP_PROBE10_(probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
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
  STAP_PROBE_DATA(probe);						\
  __asm__ volatile ("2:\n"						\
		    "\tnop /* %0 %1 %2 %3 %4 %5 %6 %7 %8 %9 */" :: "X"(arg1), "X"(arg2), "X"(arg3), "X"(arg4), "X"(arg5), "X"(arg6), "X"(arg7), "X"(arg8), "X"(arg9), "X"(arg10)); \
} while (0)

#define STAP_PROBE(provider,probe)	\
  STAP_PROBE_(probe)
#define STAP_PROBE1(provider,probe,parm1)	\
  STAP_PROBE1_(probe,(parm1))
#define STAP_PROBE2(provider,probe,parm1,parm2)                              \
  STAP_PROBE2_(probe,(parm1),(parm2))
#define STAP_PROBE3(provider,probe,parm1,parm2,parm3)                        \
  STAP_PROBE3_(probe,(parm1),(parm2),(parm3))
#define STAP_PROBE4(provider,probe,parm1,parm2,parm3,parm4)                  \
  STAP_PROBE4_(probe,(parm1),(parm2),(parm3),(parm4))
#define STAP_PROBE5(provider,probe,parm1,parm2,parm3,parm4,parm5)            \
  STAP_PROBE5_(probe,(parm1),(parm2),(parm3),(parm4),(parm5))
#define STAP_PROBE6(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6)      \
  STAP_PROBE6_(probe,(parm1),(parm2),(parm3),(parm4),(parm5),(parm6))
#define STAP_PROBE7(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7) \
  STAP_PROBE7_(probe,(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7))
#define STAP_PROBE8(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
  STAP_PROBE8_(probe,(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7),(parm8))
#define STAP_PROBE9(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
  STAP_PROBE9_(probe,(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7),(parm8),(parm9))
#define STAP_PROBE10(provider,probe,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
  STAP_PROBE10_(probe,(parm1),(parm2),(parm3),(parm4),(parm5),(parm6),(parm7),(parm8),(parm9),(parm10))

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

