/* -*- linux-c -*- 
 * String Functions
 * Copyright (C) 2005, 2006 Red Hat Inc.
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
 * @brief Implements String type.
 */
/** @addtogroup string String Functions
 *
 * @{
 */

/** Initialize a String for our use.
 * This grabs one of the global Strings for our temporary use.
 *
 * @param num Number of the preallocated String to use. 
 * #STP_NUM_STRINGS are statically allocated for our use. The
 * translator (or author) should be sure to grab a free one.
 * @returns An empty String.
 */

String _stp_string_init (int num)
{
	String str;

	if (num >= STP_NUM_STRINGS || num < 0) {
		_stp_error ("_stp_string_init internal error: requested string exceeded allocated number or was negative");
		return NULL;
	}
	str = &_stp_string[num][smp_processor_id()];
	str->len = 0;
	str->buf[0] = 0;
	return str;
}


/** Sprintf into a String.
 * Like printf, except output goes into a String.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of String, #STP_STRING_SIZE.
 *
 * @param str String
 * @param fmt A printf-style format string followed by a 
 * variable number of args.
 */
void _stp_sprintf (String str, const char *fmt, ...)
{
	int num;
	va_list args;
	if (str == _stp_stdout) {
		_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
		char *buf = pb->buf + pb->len;
		int size = STP_BUFFER_SIZE - pb->len;
		va_start(args, fmt);
		num = _stp_vsnprintf(buf, size, fmt, args);
		va_end(args);
		if (unlikely(num >= size)) { 
			/* overflowed the buffer */
			if (pb->len == 0) {
				/* A single print request exceeded the buffer size. */
				/* Should not be possible with Systemtap-generated code. */
				pb->len = STP_BUFFER_SIZE;
				_stp_print_flush();
				num = 0;
			} else {
				/* Need more space. Flush the previous contents */
				_stp_print_flush();

				/* try again */
				va_start(args, fmt);
				num = _stp_vsnprintf(pb->buf, STP_BUFFER_SIZE, fmt, args);
				va_end(args);
			}
		}
		pb->len += num;
	} else {
		va_start(args, fmt);
		num = _stp_vscnprintf(str->buf + str->len, STP_STRING_SIZE - str->len, fmt, args);
		va_end(args);
		if (likely(num > 0))
			str->len += num;
	}
}

/** Vsprintf into a String
 * Use this if your function already has a va_list.
 * You probably want _stp_sprintf().
 */
void _stp_vsprintf (String str, const char *fmt, va_list args)
{
	int num;
	if (str == _stp_stdout) {
		_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
		char *buf = pb->buf + pb->len;
		int size = STP_BUFFER_SIZE - pb->len;
		num = _stp_vsnprintf(buf, size, fmt, args);
		if (unlikely(num > size)) { 
			/* overflowed the buffer */
			if (pb->len == 0) {
				/* A single print request exceeded the buffer size. */
				/* Should not be possible with Systemtap-generated code. */
				pb->len = STP_BUFFER_SIZE;
				_stp_print_flush();
				num = 0;
			} else {
				/* Need more space. Flush the previous contents */
				_stp_print_flush();

				/* try again */
				num = _stp_vsnprintf(pb->buf, STP_BUFFER_SIZE, fmt, args);
			}
		}
		pb->len += num;
	} else {
		num = _stp_vscnprintf(str->buf + str->len, STP_STRING_SIZE - str->len, fmt, args);
		if (num > 0)
			str->len += num;
	}
}

/** ConCATenate (append) a C string to a String.
 * Like strcat().
 * @param str1 String
 * @param str2 C string (char *)
 * @sa _stp_string_cat
 */
void _stp_string_cat_cstr (String str1, const char *str2)
{
	int num = strlen (str2);
	if (str1 == _stp_stdout) {
		_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
		int size = STP_BUFFER_SIZE - pb->len;
		if (num > size) {
			_stp_print_flush();
			if (num > STP_BUFFER_SIZE)
				num = STP_BUFFER_SIZE;
		}
		memcpy (pb->buf + pb->len, str2, num);
		pb->len += num;
	} else {
		int size = STP_STRING_SIZE - str1->len - 1; 
		if (num > size)
			num = size;
		memcpy (str1->buf + str1->len, str2, num);
		str1->len += num;
		str1->buf[str1->len] = 0;
	}
}

/** ConCATenate (append) a String to a String.
 * Like strcat().
 * @param str1 String
 * @param str2 String
 * @sa _stp_string_cat
 */
void _stp_string_cat_string (String str1, String str2)
{
	if (str2->len)
		_stp_string_cat_cstr (str1, str2->buf);
}


void _stp_string_cat_char (String str1, const char c)
{
	if (str1 == _stp_stdout) {
		_stp_pbuf *pb = per_cpu_ptr(Stp_pbuf, smp_processor_id());
		int size = STP_BUFFER_SIZE - pb->len;
		char *buf;

		if (1 >= size)
			_stp_print_flush();
			
		buf = pb->buf + pb->len;
		buf[0] = c;
		pb->len ++;
	} else {
		int size = STP_STRING_SIZE - str1->len - 1; 
		if (size > 0) {
			char *buf = str1->buf + str1->len;
			buf[0] = c;
			buf[1] = 0;
			str1->len ++;
		}
	}
}

/** Get a pointer to String's buffer
 * For rare cases when a C string is needed and you have a String.
 * One example is when you want to print a String with _stp_printf().
 * @param str String
 * @returns A C string (char *)
 * @note Readonly. Don't write to this pointer or it will mess up
 * the internal String state and probably mess up your output or crash something.
 */
char * _stp_string_ptr (String str)
{
	return str->buf;
}

/** ConCATenate (append) a String or C string to a String.
 * This macro selects the proper function to call.
 * @param str1 A String
 * @param str2 A String or C string (char *)
 * @sa _stp_string_cat_cstr _stp_string_cat_string
 */
#define _stp_string_cat(str1, str2)					\
  ({                                                            \
	  if (__builtin_types_compatible_p (typeof (str2), char[])) {	\
		  char *x = (char *)str2;				\
		  _stp_string_cat_cstr(str1,x);				\
	  } else {							\
		  String x = (String)str2;				\
		  _stp_string_cat_string(str1,x);			\
	  }								\
  })


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
void _stp_text_str(char *outstr, char *in, int len, int quoted, int user)
{
	const int length = len;
	char c, *out = outstr;

	if (len == 0 || len > STP_STRING_SIZE-1)
		len = STP_STRING_SIZE-1;
	if (quoted) {
		len -= 2;
		*out++ = '\"';
	}

	if (user) {
		if (!access_ok(VERIFY_READ, (char __user *)in, 1))
			goto bad;
		if (__get_user(c, in))
			goto bad;
	} else
		c = *in;

	while (c && len > 0) {
		int num = 1;
		if (isprint(c) && isascii(c))
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
			if (__get_user(c, in))
				goto bad;
		} else
			c = *in;
	}

	if (quoted) {
		if (c) {
			out = out - 3 + len;
			*out++ = '\"';
			*out++ = '.';
			*out++ = '.';
			*out++ = '.';
		} else
			*out++ = '\"';
	}
	*out = '\0';
	return;
bad:
	strlcpy (outstr, "<unknown>", len);
}

/** @} */
#endif /* _STRING_C_ */
