/* -*- linux-c -*- 
 * String Functions
 * Copyright (C) 2005, 2006, 2007, 2009 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */
#ifndef _STRING_C_
#define _STRING_C_

#include "string.h"

/** @file string.c
 * @brief Implements string functions.
 */
/** @addtogroup string String Functions
 *
 * @{
 */

/** Sprintf into a string.
 * Like printf, except output goes into a string.
 *
 * @param str string
 * @param fmt A printf-style format string followed by a 
 * variable number of args.
 */

static int _stp_snprintf(char *buf, size_t size, const char *fmt, ...)
{
        va_list args;
        int i;

        va_start(args, fmt);
        i = _stp_vsnprintf(buf,size,fmt,args);
        va_end(args);
        return i;
}

static int _stp_vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	unsigned i = _stp_vsnprintf(buf,size,fmt,args);
	return (i >= size) ? (size - 1) : i;
}


/** Return a printable text string.
 *
 * Takes a string, and any ASCII characters that are not printable are
 * replaced by the corresponding escape sequence in the returned
 * string.
 *
 * @param outstr Output string pointer
 * @param in Input string pointer
 * @param len Maximum length of string to return not including terminating 0.
 * 0 means MAXSTRINGLEN.
 * @param quoted Put double quotes around the string. If input string is truncated
 * in will have "..." after the second quote.
 * @param user Set this to indicate the input string pointer is a userspace pointer.
 */
static void _stp_text_str(char *outstr, char *in, int len, int quoted, int user)
{
	char c, *out = outstr;

	if (len == 0 || len > MAXSTRINGLEN-1)
		len = MAXSTRINGLEN-1;
	if (quoted) {
		len -= 2;
		*out++ = '"';
	}

	if (user) {
		if (!access_ok(VERIFY_READ, (char __user *)in, 1))
			goto bad;
		if (__stp_get_user(c, in))
			goto bad;
	} else
		c = *in;

	while (c && len > 0) {
		int num = 1;
		if (isprint(c) && isascii(c)
                    && c != '"' && c != '\\') /* quoteworthy characters */
                  *out++ = c;
		else {
			switch (c) {
			case '\a':
			case '\b':
			case '\f':
			case '\n':
			case '\r':
			case '\t':
			case '\v':
			case '"':
			case '\\':
				num = 2;
				break;
			default:
				if (c > 077)
					num = 4;
				else if (c > 07)
					num = 3;
				else
					num = 2;
				break;
			}
			
			if (len < num)
				break;

			*out++ = '\\';
			switch (c) {
			case '\a':
				*out++ = 'a';
				break;
			case '\b':
				*out++ = 'b';
				break;
			case '\f':
				*out++ = 'f';
				break;
			case '\n':
				*out++ = 'n';
				break;
			case '\r':
				*out++ = 'r';
				break;
			case '\t':
				*out++ = 't';
				break;
			case '\v':
				*out++ = 'v';
				break;
			case '"':
				*out++ = '"';
				break;
			case '\\':
				*out++ = '\\';
				break;
			default:                  /* output octal representation */
				if (c > 077)
					*out++ = to_oct_digit(c >> 6);
				if (c > 07)
					*out++ = to_oct_digit((c & 070) >> 3);
				*out++ = to_oct_digit(c & 07);
				break;
			}
		}
		len -= num;
		in++;
		if (user) {
			if (__stp_get_user(c, in))
				goto bad;
		} else
			c = *in;
	}

	if (quoted) {
		if (c) {
			out = out - 3 + len;
			*out++ = '"';
			*out++ = '.';
			*out++ = '.';
			*out++ = '.';
		} else
			*out++ = '"';
	}
	*out = '\0';
	return;
bad:
	strlcpy (outstr, "<unknown>", len);
}

/** @} */
#endif /* _STRING_C_ */
