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

/* x86_64 has a different flag name from all other arches... */
#include <linux/thread_info.h>
#if defined(CONFIG_COMPAT) && !defined(TIF_32BIT) && defined (__x86_64__)
  #define TIF_32BIT TIF_IA32
#endif

/* is_compat_task - returns true if this is a 32-on-64 bit user task. */
#if !defined(STAPCONF_IS_COMPAT_TASK)
  #ifdef CONFIG_COMPAT
    static inline int is_compat_task(void)
    {
	return test_thread_flag(TIF_32BIT);
    }
  #else
    static inline int is_compat_task(void) { return 0; }
  #endif
#else
  #include <linux/compat.h>
#endif

#endif /* _STP_COMPAT_H_ */
