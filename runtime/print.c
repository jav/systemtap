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
 * @{
 */

/** Size of buffer, not including terminating NULL */
#define STP_PRINT_BUF_LEN 8000

static int _stp_pbuf_len[NR_CPUS];

#ifdef STP_NETLINK_ONLY
#define STP_PRINT_BUF_START 0
static char _stp_pbuf[NR_CPUS][STP_PRINT_BUF_LEN + 1];

void _stp_print_flush (void)
{
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][0];
	int len = _stp_pbuf_len[cpu];

	if (len == 0)
		return;

	if ( app.logging == 0) {
		_stp_pbuf_len[cpu] = 0;
		return;
	}
	
	/* enforce newline at end  */
	if (buf[len - 1] != '\n') {
		buf[len++] = '\n';
		buf[len] = '\0';
	}
	
	send_reply (STP_REALTIME_DATA, buf, len + 1, stpd_pid);
	_stp_pbuf_len[cpu] = 0;
}

#else /* ! STP_NETLINK_ONLY */
/* size of timestamp, in bytes, including space */
#define TIMESTAMP_SIZE 19
#define STP_PRINT_BUF_START (TIMESTAMP_SIZE + 1)
static char _stp_pbuf[NR_CPUS][STP_PRINT_BUF_LEN + STP_PRINT_BUF_START + 1];

void _stp_print_flush (void)
{
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][0];
	char *ptr = buf + STP_PRINT_BUF_START;
	struct timeval tv;

	if (_stp_pbuf_len[cpu] == 0)
		return;
	
	/* enforce newline at end  */
	if (ptr[_stp_pbuf_len[cpu]-1] != '\n') {
		ptr[_stp_pbuf_len[cpu]++] = '\n';
		ptr[_stp_pbuf_len[cpu]] = '\0';
	}
	
	do_gettimeofday(&tv);
	scnprintf (buf, TIMESTAMP_SIZE+1, "[%li.%06li] ", tv.tv_sec, tv.tv_usec);
	buf[TIMESTAMP_SIZE] = ' ';
	relayapp_write(buf, _stp_pbuf_len[cpu] + TIMESTAMP_SIZE + 2);
	_stp_pbuf_len[cpu] = 0;
}
#endif /* STP_NETLINK_ONLY */

/** Sprint into the scratch buffer.
 * Like printf, except output goes into a global scratch buffer
 * which will contain the null-terminated output.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of scratch buffer, STP_BUF_LEN.
 *
 * @param fmt A printf-style format string followed by a 
 * variable number of args.
 * @sa _stp_pbuf_clear
 */

void _stp_printf (const char *fmt, ...)
{
	int num;
	va_list args;
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][STP_PRINT_BUF_START] + _stp_pbuf_len[cpu];
	va_start(args, fmt);
	num = vscnprintf(buf, STP_PRINT_BUF_LEN - _stp_pbuf_len[cpu], fmt, args);
	va_end(args);
	if (num > 0)
		_stp_pbuf_len[cpu] += num;
}

void _stp_vprintf (const char *fmt, va_list args)
{
	int num;
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][STP_PRINT_BUF_START] + _stp_pbuf_len[cpu];
	num = vscnprintf(buf, STP_PRINT_BUF_LEN -_stp_pbuf_len[cpu], fmt, args);
	if (num > 0)
		_stp_pbuf_len[cpu] += num;
}

/** Write a string into the scratch buffer.
 * Copies a string into a global scratch buffer.
 * Safe because overflowing the buffer is not allowed.
 * Size is limited by length of scratch buffer, STP_BUF_LEN.
 * This is more efficient than using _stp_sprint().
 *
 * @param str A string.
 */

void _stp_print_cstr (const char *str)
{
	int cpu = smp_processor_id();
	char *buf = &_stp_pbuf[cpu][STP_PRINT_BUF_START] + _stp_pbuf_len[cpu];
	int num = strlen (str);
	if (num > STP_PRINT_BUF_LEN - _stp_pbuf_len[cpu])
		num = STP_PRINT_BUF_LEN - _stp_pbuf_len[cpu];
	strncpy (buf, str, num+1);
	_stp_pbuf_len[cpu] += num;
}

/** Clear the scratch buffer.
 * This function should be called before anything is written to 
 * the scratch buffer.  Output will accumulate in the buffer
 * until this function is called again.  
 * @returns A pointer to the buffer.
 */

char *_stp_print_clear (void)
{
	int cpu = smp_processor_id();
	_stp_pbuf_len[cpu] = 0;
	return &_stp_pbuf[cpu][STP_PRINT_BUF_START];
}

#include "string.c"

void _stp_print_string (String str)
{
	if (str->len)
		_stp_print_cstr (str->buf);
}

#define _stp_print(str)							\
	({								\
	  if (__builtin_types_compatible_p (typeof (str), char[])) {	\
		  char *x = (char *)str;				\
		  _stp_print_cstr(x);					\
	  } else {							\
		  String x = (String)str;				\
		  _stp_print_string(x);					\
	  }								\
  })

/** @} */
#endif /* _PRINT_C_ */
