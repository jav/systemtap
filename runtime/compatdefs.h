/* Compatibility definitions for older kernels.
 * Copyright (C) 2010 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_COMPAT_H_ /* -*- linux-c -*- */
#define _STP_COMPAT_H

#ifdef CONFIG_COMPAT

/* x86_64 has a different flag name from all other arches and s390... */
#include <linux/thread_info.h>
#if defined (__x86_64__)
  #define TIF_32BIT TIF_IA32
#endif
#if defined(__s390__) || defined(__s390x__)
  #define TIF_32BIT TIF_31BIT
#endif
#if !defined(TIF_32BIT)
#error architecture not supported, no TIF_32BIT flag
#endif

/* _stp_is_compat_task - returns true if this is a 32-on-64 bit user task.
   Note that some kernels/architectures define a function called
   is_compat_task(), but that just tests for being inside a 32bit compat
   syscall. We want to test whether the current task is a 32 bit compat
   task itself.*/
static inline int _stp_is_compat_task(void)
{
  return test_thread_flag(TIF_32BIT);
}

#endif /* CONFIG_COMPAT */

#endif /* _STP_COMPAT_H_ */
