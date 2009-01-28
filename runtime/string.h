/* -*- linux-c -*-
 * Copyright (C) 2005, 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */
#ifndef _STRING_H_
#define _STRING_H_

/* set up a special stdout string */
static char _stp_stdout[] = "_stdout_";

#define to_oct_digit(c) ((c) + '0')
static void _stp_vsprintf (char *str, const char *fmt, va_list args);
static void _stp_text_str(char *out, char *in, int len, int quoted, int user);

/*
 * Powerpc uses a paranoid user address check in __get_user() which
 * spews warnings "BUG: Sleeping function...." when DEBUG_SPINLOCK_SLEEP
 * is enabled. With 2.6.21 and above, a newer variant __get_user_inatomic
 * is provided without the paranoid check. Use it if available, fall back
 * to __get_user() if not. Other archs can use __get_user() as is
 */
#ifdef __powerpc64__
#ifdef __get_user_inatomic
#define __stp_get_user(x, ptr) __get_user_inatomic(x, ptr)
#else /* __get_user_inatomic */
#define __stp_get_user(x, ptr) __get_user(x, ptr)
#endif /* __get_user_inatomic */
#else /* __powerpc64__ */
#define __stp_get_user(x, ptr) __get_user(x, ptr)
#endif /* __powerpc64__ */

#endif /* _STRING_H_ */
