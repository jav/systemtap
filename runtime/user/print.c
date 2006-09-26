#ifndef _PRINT_C_ /* -*- linux-c -*- */
#define _PRINT_C_

#include <linux/config.h>
#include <stdio.h>

/** @file print.c
 * @addtogroup print Print Buffer
 * Print Buffer Functions.
 * The print buffer is for collecting output to send to the user daemon.
 * This is a per-cpu static buffer.  The buffer is sent when
 * _stp_print_flush() is called.
 *
 * The reason to do this is to allow multiple small prints to be combined then
 * timestamped and sent together to staprun. It could flush automatically on newlines,
 * but what about stack traces which span many lines?  So try this and see how it works for us.
 * @{
 */

/** Size of buffer, not including terminating NULL */
#ifndef STP_PRINT_BUF_LEN
#define STP_PRINT_BUF_LEN 8000
#endif

#include "string.h"
#include "io.c"

static int _stp_pbuf_len[NR_CPUS];

#define STP_PRINT_BUF_START 0
static char _stp_pbuf[NR_CPUS][STP_PRINT_BUF_LEN + 1];

void _stp_print_flush (void)
{
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][0];
	int len = _stp_pbuf_len[cpu];

	if (len == 0)
		return;

	fwrite (buf, len, 1, stdout);
	_stp_pbuf_len[cpu] = 0;
}

#define _stp_printf(args...) _stp_sprintf(_stp_stdout,args)
#define _stp_vprintf(fmt,args) _stp_vsprintf(_stp_stdout,fmt,args)
#define _stp_print_cstr(str) _stp_string_cat_cstr(_stp_stdout,str)
#define _stp_print_string(str) _stp_string_cat_string(_stp_stdout,str)
/** Write a String or C string into the print buffer.
 * This macro selects the proper function to call.
 * @param str A String or C string (char *)
 * @sa _stp_print_cstr _stp_print_string
 */


/* Print stuff until a format specification is found. */
/* Return pointer to that. */
static char *next_fmt(char *fmt, int *num)
{
	char *f = fmt;
	int in_fmt = 0;
	*num = 0; 
	while (*f) {
		if (in_fmt) {
			if (*f == '%') {
				_stp_string_cat_char(_stp_stdout,'%');
				in_fmt = 0;
			} else if (*f > '0' && *f <= '9') {
				*num = *f - '0';
				f++;
				return f;
			} else
				return f;
		} else if (*f == '%')
			in_fmt = 1;
		else
			_stp_string_cat_char(_stp_stdout,*f);
		f++;
	}
	return f;
}

#define _stp_print(str)							\
	({								\
	  if (__builtin_types_compatible_p (typeof (str), char[])) {	\
		  char *x = (char *)str;				\
		  _stp_string_cat_cstr(_stp_stdout,x);				\
	  } else {							\
		  String x = (String)str;				\
		  _stp_string_cat_string(_stp_stdout,x);				\
	  }								\
  })

/** @} */
#endif /* _PRINT_C_ */
