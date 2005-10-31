/* -*- linux-c -*- 
 * I/O for printing warnings, errors and debug messages
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _IO_C_
#define _IO_C_

#include "transport/transport.c"

void _stp_print_flush (void);
void _stp_string_cat_cstr (String str1, const char *str2);

/** @file io.c
 * @brief I/O for printing warnings, errors and debug messages.
 */
/** @addtogroup io I/O
 * @{
 */

/** private buffer for _stp_log() */
#define STP_LOG_BUF_LEN 2047
#define WARN_STRING "WARNING: "
#define ERR_STRING "ERROR: "

static char _stp_lbuf[NR_CPUS][STP_LOG_BUF_LEN + 1];

enum code { INFO=0, WARN, ERROR, DBUG };

static void _stp_vlog (enum code type, char *func, int line, const char *fmt, va_list args)
{
	int num;
	char *buf = &_stp_lbuf[get_cpu()][0];
	int start = 0;

	if (type == DBUG) {
		start = scnprintf(buf, STP_LOG_BUF_LEN, "\033[36m%s:%d:\033[0m ", func, line);
	} else if (type == WARN) {
		strcpy (buf, WARN_STRING);
		start = sizeof(WARN_STRING) - 1;
	} else if (type == ERROR) {
		strcpy (buf, ERR_STRING);
		start = sizeof(ERR_STRING) - 1;
	}

	num = vscnprintf (buf + start, STP_LOG_BUF_LEN - start, fmt, args);
	if (num + start) {
		if (buf[num + start - 1] != '\n') {
			buf[num + start] = '\n';
			num++;
		}
		buf[num + start] = '\0';
		
#ifdef STP_RELAYFS
		if (type != DBUG)
			_stp_write(STP_OOB_DATA, buf, start + num + 1);
		_stp_string_cat_cstr(_stp_stdout,buf);
		_stp_print_flush();
#else
		if (type != DBUG)
			_stp_write(STP_OOB_DATA, buf, start + num + 1);
		else {
			_stp_string_cat_cstr(_stp_stdout,buf);
			_stp_print_flush();
		}
#endif
	}
	put_cpu();
}

/** Logs Data.
 * This function sends the message immediately to stpd. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. This function is not as efficient as _stp_printf()
 * and should only be used for urgent messages. You probably want
 * dbug(), or _stp_warn().
 * @param fmt A variable number of args.
 * @todo Evaluate if this function is necessary.
 */
void _stp_log (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (INFO, NULL, 0, fmt, args);
	va_end(args);
}

/** Prints warning.
 * This function sends a warning message immediately to stpd. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 * @param fmt A variable number of args.
 */
void _stp_warn (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (WARN, NULL, 0, fmt, args);
	va_end(args);
}

/** Exits and unloads the module.
 * This function sends a signal to stpd to tell it to
 * unload the module and exit. The module will not be 
 * unloaded until after the current probe returns.
 * @note Be careful to not treat this like the Linux exit() 
 * call. You should probably call return immediately after
 * calling _stp_exit().
 */
void _stp_exit (void)
{
	schedule_work (&stp_exit);
}

/** Prints error message and exits.
 * This function sends an error message immediately to stpd. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 *
 * After the error message is displayed, the module will be unloaded.
 * @param fmt A variable number of args.
 * @sa _stp_exit().
 */
void _stp_error (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (ERROR, NULL, 0, fmt, args);
	va_end(args);
	_stp_exit();
}

static void _stp_dbug (char *func, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (DBUG, func, line, fmt, args);
	va_end(args);
}

/** @} */
#endif /* _IO_C_ */
