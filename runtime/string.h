/* -*- linux-c -*-
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */
#ifndef _STRING_H_
#define _STRING_H_

/** Maximum string size allowed in Strings */
#ifndef STP_STRING_SIZE
#define STP_STRING_SIZE 2048
#endif

/** Maximum number of strings a probe uses. */
#ifndef STP_NUM_STRINGS
#define STP_NUM_STRINGS 0
#endif

struct string {
	int len;
	char buf[STP_STRING_SIZE];
};

static struct string _stp_string[STP_NUM_STRINGS][NR_CPUS];

typedef struct string *String;

/* set up a special stdout string */
static struct string __stp_stdout = {0};
String _stp_stdout = &__stp_stdout;

void _stp_vsprintf (String str, const char *fmt, va_list args);
void _stp_string_cat_char (String str1, const char c);

#endif /* _STRING_H_ */
