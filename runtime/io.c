#ifndef _IO_C_ /* -*- linux-c -*- */
#define _IO_C_

#include "transport/transport.c"

/** @file io.c
 * @brief I/O functions
 */
/** @addtogroup io I/O
 * I/O functions
 * @{
 */

/** private buffer for _stp_log() */
#define STP_LOG_BUF_LEN 2047
static char _stp_lbuf[NR_CPUS][STP_LOG_BUF_LEN + 1];

/** Logs Data.
 * This function sends the message immediately to stpd.
 * @param fmt A variable number of args.
 * @note Lines are limited in length by printk buffer. If there is
 * no newline in the format string, then other syslog output could
 * get appended to the SystemTap line.
 * @todo Evaluate if this function is necessary.
 */

void _stp_log (const char *fmt, ...)
{
	int num, ret;
	char *buf = &_stp_lbuf[get_cpu()][0];
	va_list args;
	va_start(args, fmt);
	num = vscnprintf (buf, STP_LOG_BUF_LEN, fmt, args);
	va_end(args);
	buf[num] = '\0';

	ret = _stp_ctrl_send(STP_REALTIME_DATA, buf, num + 1, t->pid);
	if (ret < 0)
		atomic_inc (&_stp_transport_failures);
	put_cpu();
}

/** @} */
#endif /* _IO_C_ */
