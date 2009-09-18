/* Copyright (C) 2005-2009 Red Hat Inc.

   This file is part of systemtap, and is free software in the public domain.
*/

#ifndef _SYS_SDT_H
#define _SYS_SDT_H    1

#include <string.h>
#include <sys/types.h>
#include <errno.h>


#ifdef __LP64__
#define STAP_PROBE_ADDR "\t.quad "
#else
#define STAP_PROBE_ADDR "\t.long "
#endif

/* Allocated section needs to be writable when creating pic shared objects
   because we store relocatable addresses in them. */
#ifdef __PIC__
#define ALLOCSEC "\"aw\""
#else
#define ALLOCSEC "\"a\""
#endif

/* An allocated section .probes that holds the probe names and addrs. */
#define STAP_PROBE_DATA_(probe,guard,arg)	\
  __asm__ volatile (".section .probes," ALLOCSEC "\n" \
		    "\t.align 8\n"		\
		    "1:\n\t.asciz " #probe "\n" \
		    "\t.align 4\n"		\
		    "\t.int " #guard "\n"	\
  		    "\t.align 8\n"		\
		    STAP_PROBE_ADDR "1b\n"	\
  		    "\t.align 8\n"		\
		    STAP_PROBE_ADDR #arg "\n"	\
		    "\t.int 0\n"  		\
		    "\t.previous\n")

#define STAP_PROBE_DATA(probe, guard, arg)	\
  STAP_PROBE_DATA_(#probe,guard,arg)

#if defined STAP_HAS_SEMAPHORES && defined EXPERIMENTAL_UTRACE_SDT
#define STAP_SEMAPHORE(probe)			\
  if ( probe ## _semaphore )
#else
#define STAP_SEMAPHORE(probe)
#endif

#if ! (defined EXPERIMENTAL_UTRACE_SDT || defined EXPERIMENTAL_KPROBE_SDT)

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

#if defined __x86_64__ || defined __i386__  || defined __powerpc__ || defined __arm__
#define STAP_NOP "\tnop "
#else
#define STAP_NOP "\tnop 0 "
#endif

#define STAP_UPROBE_GUARD 0x31425250

#define STAP_PROBE_(probe)			\
do { \
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);	\
  __asm__ volatile ("2:\n"	\
		    STAP_NOP);	\
 } while (0)

#define STAP_PROBE1_(probe,label,parm1)			\
do STAP_SEMAPHORE(probe) {							\
  volatile __typeof__((parm1)) arg1 = parm1;		\
  STAP_UNINLINE;					\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);		\
  __asm__ volatile ("2:\n"				\
		    STAP_NOP "/* %0 */" :: "g"(arg1));	\
 } while (0)

#define STAP_PROBE2_(probe,label,parm1,parm2)				\
do STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 */" :: "g"(arg1), "g"(arg2));	\
} while (0)

#define STAP_PROBE3_(probe,label,parm1,parm2,parm3)			\
do STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 */" :: "g"(arg1), "g"(arg2), "g"(arg3)); \
} while (0)

#define STAP_PROBE4_(probe,label,parm1,parm2,parm3,parm4)		\
do STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  volatile __typeof__((parm4)) arg4 = parm4;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 %3 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4)); \
} while (0)

#define STAP_PROBE5_(probe,label,parm1,parm2,parm3,parm4,parm5)		\
do  STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  volatile __typeof__((parm4)) arg4 = parm4;				\
  volatile __typeof__((parm5)) arg5 = parm5;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 %3 %4 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5)); \
} while (0)

#define STAP_PROBE6_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6)	\
do STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  volatile __typeof__((parm4)) arg4 = parm4;				\
  volatile __typeof__((parm5)) arg5 = parm5;				\
  volatile __typeof__((parm6)) arg6 = parm6;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 %3 %4 %5 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6)); \
} while (0)

#define STAP_PROBE7_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7) \
do  STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  volatile __typeof__((parm4)) arg4 = parm4;				\
  volatile __typeof__((parm5)) arg5 = parm5;				\
  volatile __typeof__((parm6)) arg6 = parm6;				\
  volatile __typeof__((parm7)) arg7 = parm7;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 %3 %4 %5 %6 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7)); \
} while (0)

#define STAP_PROBE8_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
do STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  volatile __typeof__((parm4)) arg4 = parm4;				\
  volatile __typeof__((parm5)) arg5 = parm5;				\
  volatile __typeof__((parm6)) arg6 = parm6;				\
  volatile __typeof__((parm7)) arg7 = parm7;				\
  volatile __typeof__((parm8)) arg8 = parm8;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 %3 %4 %5 %6 %7 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7), "g"(arg8)); \
} while (0)

#define STAP_PROBE9_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
do STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  volatile __typeof__((parm4)) arg4 = parm4;				\
  volatile __typeof__((parm5)) arg5 = parm5;				\
  volatile __typeof__((parm6)) arg6 = parm6;				\
  volatile __typeof__((parm7)) arg7 = parm7;				\
  volatile __typeof__((parm8)) arg8 = parm8;				\
  volatile __typeof__((parm9)) arg9 = parm9;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 %3 %4 %5 %6 %7 %8 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7), "g"(arg8), "g"(arg9)); \
} while (0)

#define STAP_PROBE10_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
do STAP_SEMAPHORE(probe) {									\
  volatile __typeof__((parm1)) arg1 = parm1;				\
  volatile __typeof__((parm2)) arg2 = parm2;				\
  volatile __typeof__((parm3)) arg3 = parm3;				\
  volatile __typeof__((parm4)) arg4 = parm4;				\
  volatile __typeof__((parm5)) arg5 = parm5;				\
  volatile __typeof__((parm6)) arg6 = parm6;				\
  volatile __typeof__((parm7)) arg7 = parm7;				\
  volatile __typeof__((parm8)) arg8 = parm8;				\
  volatile __typeof__((parm9)) arg9 = parm9;				\
  volatile __typeof__((parm10)) arg10 = parm10;				\
  STAP_UNINLINE;							\
  STAP_PROBE_DATA(probe,STAP_UPROBE_GUARD,2f);				\
  __asm__ volatile ("2:\n"						\
		    STAP_NOP "/* %0 %1 %2 %3 %4 %5 %6 %7 %8 %9 */" :: "g"(arg1), "g"(arg2), "g"(arg3), "g"(arg4), "g"(arg5), "g"(arg6), "g"(arg7), "g"(arg8), "g"(arg9), "g"(arg10)); \
} while (0)

#else /* ! (defined EXPERIMENTAL_UTRACE_SDT || defined EXPERIMENTAL_KPROBE_SDT) */
#include <unistd.h>
#include <sys/syscall.h>
# if defined (__USE_ANSI)
extern long int syscall (long int __sysno, ...) __THROW;
# endif
# if defined EXPERIMENTAL_KPROBE_SDT
# define STAP_SYSCALL __NR_getegid
# define STAP_GUARD 0x32425250
# elif defined EXPERIMENTAL_UTRACE_SDT
# define STAP_SYSCALL 0xbead
# define STAP_GUARD 0x33425250
# endif

#include <sys/syscall.h>

#define STAP_PROBE_(probe)			\
do STAP_SEMAPHORE(probe) {						\
  STAP_PROBE_DATA(probe,STAP_GUARD,0);	\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD);	\
 } while (0)

#define STAP_PROBE1_(probe,label,parm1)				\
do STAP_SEMAPHORE(probe) {								\
  STAP_PROBE_DATA(probe,STAP_GUARD,1);				\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, (size_t)parm1);	\
 } while (0)

#define STAP_PROBE2_(probe,label,parm1,parm2)				\
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));	\
	  size_t arg2 __attribute__((aligned(8)));}			\
  stap_probe2_args = {(size_t)parm1, (size_t)parm2};			\
  STAP_PROBE_DATA(probe,STAP_GUARD,2);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe2_args);		\
 } while (0)

#define STAP_PROBE3_(probe,label,parm1,parm2,parm3)			\
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));	\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));}			\
  stap_probe3_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3};	\
  STAP_PROBE_DATA(probe,STAP_GUARD,3);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe3_args);	\
 } while (0)

#define STAP_PROBE4_(probe,label,parm1,parm2,parm3,parm4)		\
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));	\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));			\
	  size_t arg4 __attribute__((aligned(8)));}			\
  stap_probe4_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3, (size_t)parm4}; \
  STAP_PROBE_DATA(probe,STAP_GUARD,4);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD,&stap_probe4_args);	\
 } while (0)

#define STAP_PROBE5_(probe,label,parm1,parm2,parm3,parm4,parm5)		\
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));			\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));			\
	  size_t arg4 __attribute__((aligned(8)));			\
	  size_t arg5 __attribute__((aligned(8)));}			\
  stap_probe5_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3, (size_t)parm4, \
	(size_t)parm5};							\
  STAP_PROBE_DATA(probe,STAP_GUARD,5);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe5_args);		\
 } while (0)

#define STAP_PROBE6_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6)	\
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));			\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));			\
	  size_t arg4 __attribute__((aligned(8)));			\
	  size_t arg5 __attribute__((aligned(8)));			\
	  size_t arg6 __attribute__((aligned(8)));}			\
  stap_probe6_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3, (size_t)parm4, \
	(size_t)parm5, (size_t)parm6};					\
  STAP_PROBE_DATA(probe,STAP_GUARD,6);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe6_args);		\
 } while (0)

#define STAP_PROBE7_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7) \
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));			\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));			\
	  size_t arg4 __attribute__((aligned(8)));			\
	  size_t arg5 __attribute__((aligned(8)));			\
	  size_t arg6 __attribute__((aligned(8)));			\
	  size_t arg7 __attribute__((aligned(8)));}			\
  stap_probe7_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3, (size_t)parm4, \
	(size_t)parm5, (size_t)parm6, (size_t)parm7};			\
  STAP_PROBE_DATA(probe,STAP_GUARD,7);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe7_args);		\
 } while (0)

#define STAP_PROBE8_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8) \
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));			\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));			\
	  size_t arg4 __attribute__((aligned(8)));			\
	  size_t arg5 __attribute__((aligned(8)));			\
	  size_t arg6 __attribute__((aligned(8)));			\
	  size_t arg7 __attribute__((aligned(8)));			\
	  size_t arg8 __attribute__((aligned(8)));}			\
  stap_probe8_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3, (size_t)parm4, \
	(size_t)parm5, (size_t)parm6, (size_t)parm7, (size_t)parm8};	\
  STAP_PROBE_DATA(probe,STAP_GUARD,8);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe8_args);		\
 } while (0)

#define STAP_PROBE9_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9) \
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));			\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));			\
	  size_t arg4 __attribute__((aligned(8)));			\
	  size_t arg5 __attribute__((aligned(8)));			\
	  size_t arg6 __attribute__((aligned(8)));			\
	  size_t arg7 __attribute__((aligned(8)));			\
	  size_t arg8 __attribute__((aligned(8)));			\
	  size_t arg9 __attribute__((aligned(8)));}			\
  stap_probe9_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3, (size_t)parm4, \
	(size_t)parm5, (size_t)parm6, (size_t)parm7, (size_t)parm8, (size_t)parm9}; \
  STAP_PROBE_DATA(probe,STAP_GUARD,9);					\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe9_args);		\
 } while (0)

#define STAP_PROBE10_(probe,label,parm1,parm2,parm3,parm4,parm5,parm6,parm7,parm8,parm9,parm10) \
do STAP_SEMAPHORE(probe) {									\
  __extension__ struct {size_t arg1 __attribute__((aligned(8)));			\
	  size_t arg2 __attribute__((aligned(8)));			\
	  size_t arg3 __attribute__((aligned(8)));			\
	  size_t arg4 __attribute__((aligned(8)));			\
	  size_t arg5 __attribute__((aligned(8)));			\
	  size_t arg6 __attribute__((aligned(8)));			\
	  size_t arg7 __attribute__((aligned(8)));			\
	  size_t arg8 __attribute__((aligned(8)));			\
	  size_t arg9 __attribute__((aligned(8)));			\
	  size_t arg10 __attribute__((aligned(8)));}			\
  stap_probe10_args = {(size_t)parm1, (size_t)parm2, (size_t)parm3, (size_t)parm4, \
	(size_t)parm5, (size_t)parm6, (size_t)parm7, (size_t)parm8, (size_t)parm9, (size_t)parm10}; \
  STAP_PROBE_DATA(probe,STAP_GUARD,10);				\
  syscall (STAP_SYSCALL, #probe, STAP_GUARD, &stap_probe10_args);		\
 } while (0)

#endif

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

