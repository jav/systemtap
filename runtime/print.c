#ifndef _PRINT_C_ /* -*- linux-c -*- */
#define _PRINT_C_

#include <linux/config.h>

#include "io.c"

/** @file print.c
 * @addtogroup print Print Buffer
 * Print Buffer Functions.
 * The print buffer is for collecting output to send to the user daemon.
 * This is a per-cpu static buffer.  The buffer is sent when
 * _stp_print_flush() is called.
 *
 * The reason to do this is to allow multiple small prints to be combined then
 * timestamped and sent together to stpd. This is more efficient than sending
 * numerous small packets.
 *
 * This function is called automatically when the print buffer is full.
 * It MUST also be called at the end of every probe that prints something.
 * @{
 */

static int _stp_pbuf_len[NR_CPUS];

#ifdef STP_NETLINK_ONLY
#define STP_PRINT_BUF_START 0

/** Size of buffer, not including terminating NULL */
#ifndef STP_PRINT_BUF_LEN
#define STP_PRINT_BUF_LEN 8191
#endif

static char _stp_pbuf[NR_CPUS][STP_PRINT_BUF_LEN + 1];

void _stp_print_flush (void)
{
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][0];
	int len = _stp_pbuf_len[cpu];
	int ret;

	if (len == 0)
		return;

	ret =_stp_transport_write(t, buf, len + 1);
	if (unlikely(ret < 0))
		atomic_inc (&_stp_transport_failures);

	_stp_pbuf_len[cpu] = 0;
	*buf = 0;
}

#else /* ! STP_NETLINK_ONLY */

/* size of timestamp, in bytes, including space */
#define TIMESTAMP_SIZE 19
#define STP_PRINT_BUF_START (TIMESTAMP_SIZE + 1)

/** Size of buffer, not including terminating NULL */
#ifndef STP_PRINT_BUF_LEN
#define STP_PRINT_BUF_LEN (8192 - TIMESTAMP_SIZE - 2)
#endif

static char _stp_pbuf[NR_CPUS][STP_PRINT_BUF_LEN + STP_PRINT_BUF_START + 1];

/** Send the print buffer now.
 * Output accumulates in the print buffer until this is called.
 * Size is limited by length of print buffer, #STP_PRINT_BUF_LEN.
 */

void _stp_print_flush (void)
{
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][0];
	char *ptr = buf + STP_PRINT_BUF_START;
	struct timeval tv;

	if (_stp_pbuf_len[cpu] == 0)
		return;
	
	do_gettimeofday(&tv);
	scnprintf (buf, TIMESTAMP_SIZE+1, "[%li.%06li] ", tv.tv_sec, tv.tv_usec);
	buf[TIMESTAMP_SIZE] = ' ';
	_stp_transport_write(t, buf, _stp_pbuf_len[cpu] + TIMESTAMP_SIZE + 2);
	_stp_pbuf_len[cpu] = 0;
	*ptr = 0;
}
#endif /* STP_NETLINK_ONLY */

/** Print into the print buffer.
 * Like printf, except output goes to the print buffer.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of print buffer, #STP_PRINT_BUF_LEN.
 *
 * @param fmt A printf-style format string followed by a 
 * variable number of args.
 * @sa _stp_print_flush()
 */
#define _stp_printf(args...) _stp_sprintf(_stp_stdout,args)

/** Print into the print buffer.
 * Use this if your function already has a va_list.
 * You probably want _stp_printf().
 */

#define _stp_vprintf(fmt,args) _stp_vsprintf(stdout,fmt,args)

/** Write a C string into the print buffer.
 * Copies a string into a print buffer.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of print buffer, #STP_PRINT_BUF_LEN.
 * This is more efficient than using _stp_printf() if you don't
 * need fancy formatting.
 *
 * @param str A C string.
 * @sa _stp_print
 */
#define _stp_print_cstr(str) _stp_string_cat_cstr(_stp_stdout,str)


/** Write a String into the print buffer.
 * Copies a String into a print buffer.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of print buffer, #STP_PRINT_BUF_LEN.
 * This is more efficient than using _stp_printf() if you don't
 * need fancy formatting.
 *
 * @param str A String.
 * @sa _stp_print
 */
#define _stp_print_string(str) _stp_string_cat_string(_stp_stdout,str)

/** Write a String or C string into the print buffer.
 * This macro selects the proper function to call.
 * @param str A String or C string (char *)
 * @sa _stp_print_cstr _stp_print_string
 */

#define _stp_print(str)							\
	({								\
	  if (__builtin_types_compatible_p (typeof (str), char[])) {	\
		  char *x = (char *)str;				\
		  _stp_string_cat_cstr(_stp_stdout,x);			\
	  } else {							\
		  String x = (String)str;				\
		  _stp_string_cat_string(_stp_stdout,x);		\
	  }								\
  })

/** @} */
#endif /* _PRINT_C_ */
