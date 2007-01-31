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
void _stp_vsprintf (char *str, const char *fmt, va_list args);
void _stp_text_str(char *out, char *in, int len, int quoted, int user);

#endif /* _STRING_H_ */
