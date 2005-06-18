#ifndef _STRING_C_ /* -*- linux-c -*- */
#define _STRING_C_

#include <linux/config.h>
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
		_stp_log ("_stp_string_init internal error: requested string exceeded allocated number or was negative");
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
		int cpu = smp_processor_id();
		char *buf = &_stp_pbuf[cpu][STP_PRINT_BUF_START] + _stp_pbuf_len[cpu];
		int size = STP_PRINT_BUF_LEN -_stp_pbuf_len[cpu] + 1;
		va_start(args, fmt);
		num = vsnprintf(buf, size, fmt, args);
		va_end(args);
		if (unlikely(num >= size)) { 
			/* overflowed the buffer */
			if (_stp_pbuf_len[cpu] == 0) {
				_stp_pbuf_len[cpu] = STP_PRINT_BUF_LEN;
				_stp_print_flush();
			} else {
				_stp_print_flush();
				va_start(args, fmt);
				_stp_vsprintf(_stp_stdout, fmt, args); 
				va_end(args);
			}
		} else {
			_stp_pbuf_len[cpu] += num;
		}

	} else {
		va_start(args, fmt);
		num = vscnprintf(str->buf + str->len, STP_STRING_SIZE - str->len, fmt, args);
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
		int cpu = smp_processor_id();
		char *buf = &_stp_pbuf[cpu][STP_PRINT_BUF_START] + _stp_pbuf_len[cpu];
		int size = STP_PRINT_BUF_LEN -_stp_pbuf_len[cpu] + 1;
		num = vsnprintf(buf, size, fmt, args);
		if (num < size)
			_stp_pbuf_len[cpu] += num;
		else {
			_stp_pbuf_len[cpu] = STP_PRINT_BUF_LEN;
			_stp_print_flush();
		}
	} else {
		num = vscnprintf(str->buf + str->len, STP_STRING_SIZE - str->len, fmt, args);
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
		char *buf;
		int cpu = smp_processor_id();
		int size = STP_PRINT_BUF_LEN -_stp_pbuf_len[cpu];
		if (num >= size) {
			_stp_print_flush();
			if (num > STP_PRINT_BUF_LEN)
				num = STP_PRINT_BUF_LEN;
		}
		buf = &_stp_pbuf[cpu][STP_PRINT_BUF_START] + _stp_pbuf_len[cpu];
		strncpy (buf, str2, num + 1);
		_stp_pbuf_len[cpu] += num;
	} else {
		int size = STP_STRING_SIZE - str1->len - 1; 
		if (num > size)
			num = size;
		strncpy (str1->buf + str1->len, str2, num);
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
		char *buf;
		int cpu = smp_processor_id();
		int size = STP_PRINT_BUF_LEN -_stp_pbuf_len[cpu];
		if (1 >= size)
			_stp_print_flush();
		buf = &_stp_pbuf[cpu][STP_PRINT_BUF_START] + _stp_pbuf_len[cpu];
		buf[0] = c;
		buf[1] = 0;
		_stp_pbuf_len[cpu] ++;
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

/** @} */
#endif /* _STRING_C_ */
