#ifndef _STRING_C_ /* -*- linux-c -*- */
#define _STRING_C_

#include <linux/config.h>

/** @file string.c
 * @addtogroup scbuf Scratch Buffer
 * Scratch Buffer Functions.
 * The scratch buffer is for collecting output before storing in a map,
 * printing, etc. This is a per-cpu static buffer.  It is necessary because 
 * of the limited stack space available in the kernel.
 * @todo Need careful review of these to insure safety.
 * @{
 */

#ifndef STP_STRING_SIZE
#define STP_STRING_SIZE 2048
#endif

struct string {
	short len;
	short global;
	char buf[STP_STRING_SIZE];
};

static struct string _stp_string[STP_NUM_STRINGS][NR_CPUS];

typedef struct string *String;

String _stp_string_init (int num)
{
	int global = 0;
	String str;

	if (num  < 0) {
		num = -num;
		global = 1;
	}
	
	if (num >= STP_NUM_STRINGS) {
		_stp_log ("_stp_string_init internal error: requested string exceeded allocated number");
		return NULL;
	}

	if (global)
		str = &_stp_string[num][0];
	else
		str = &_stp_string[num][smp_processor_id()];

	str->global = global;
	str->len = 0;
	return str;
}


/** Sprint into the scratch buffer.
 * Like printf, except output goes into a global scratch buffer
 * which will contain the null-terminated output.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of scratch buffer, STP_BUF_LEN.
 *
 * @param fmt A printf-style format string followed by a 
 * variable number of args.
 * @sa _stp_scbuf_clear
 */

void _stp_sprintf (String str, const char *fmt, ...)
{
	int num;
	va_list args;
	va_start(args, fmt);
	num = vscnprintf(str->buf + str->len, STP_STRING_SIZE - str->len - 1, fmt, args);
	va_end(args);
	if (num > 0)
		str->len += num;
}

void _stp_vsprintf (String str, const char *fmt, va_list args)
{
	int num;
	num = vscnprintf(str->buf + str->len, STP_STRING_SIZE - str->len - 1, fmt, args);
	if (num > 0)
		str->len += num;
}

/** Write a string into the scratch buffer.
 * Copies a string into a global scratch buffer.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of scratch buffer, STP_BUF_LEN.
 * This is more efficient than using _stp_sprint().
 *
 * @param str A string.
 */

void _stp_string_cat_cstr (String str, const char *newstr)
{
	int num = strlen (newstr);
	if (num > STP_STRING_SIZE - str->len - 1)
		num = STP_STRING_SIZE - str->len - 1;
	strncpy (str->buf + str->len, newstr, num+1);
	str->len += num;
}

void _stp_string_cat_string (String str1, String str2)
{
	int num = str2->len;
	if (num > STP_STRING_SIZE - str1->len - 1)
		num = STP_STRING_SIZE - str1->len - 1;
	strncpy (str1->buf + str1->len, str2->buf, num);
	str1->len += num;
}

char * _stp_string_ptr (String str)
{
	return str->buf;
}

#define _stp_string_cat(str1, str2)					\
  ({                                                            \
	  if (__builtin_types_compatible_p (typeof (str2), char[])) {	\
		  char *x = (char *)str2;				\
		  _str_string_cat_cstr(str1,x);				\
	  } else {							\
		  String x = (String)str2;				\
		  _str_string_cat_string(str1,x);			\
	  }								\
  })

/** @} */
#endif /* _STRING_C_ */
