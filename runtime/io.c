/* -*- linux-c -*- 
 * I/O for printing warnings, errors and debug messages
 * Copyright (C) 2005, 2006, 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _IO_C_
#define _IO_C_

/** @file io.c
 * @brief I/O for printing warnings, errors and debug messages.
 */
/** @addtogroup io I/O
 * @{
 */

#define WARN_STRING "WARNING: "
#define ERR_STRING "ERROR: "
enum code { INFO=0, WARN, ERROR, DBUG };

static void _stp_vlog (enum code type, const char *func, int line, const char *fmt, va_list args)
{
	int num;
	char *buf = per_cpu_ptr(Stp_lbuf, smp_processor_id());
	int start = 0;

	if (type == DBUG) {
		start = _stp_snprintf(buf, STP_LOG_BUF_LEN, "\033[36m%s:%d:\033[0m ", func, line);
	} else if (type == WARN) {
		strcpy (buf, WARN_STRING);
		start = sizeof(WARN_STRING) - 1;
	} else if (type == ERROR) {
		strcpy (buf, ERR_STRING);
		start = sizeof(ERR_STRING) - 1;
	}

	num = _stp_vscnprintf (buf + start, STP_LOG_BUF_LEN - start - 1, fmt, args);
	if (num + start) {
		if (buf[num + start - 1] != '\n') {
			buf[num + start] = '\n';
			num++;
			buf[num + start] = '\0';
		}

		if (type != DBUG)
			_stp_write(STP_OOB_DATA, buf, start + num + 1);
		else {
			_stp_print(buf);
			_stp_print_flush();
		}
	}
}

/** Logs Data.
 * This function sends the message immediately to staprun. It
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
 * This function sends a warning message immediately to staprun. It
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
 * This function sends a signal to staprun to tell it to
 * unload the module and exit. The module will not be 
 * unloaded until after the current probe returns.
 * @note Be careful to not treat this like the Linux exit() 
 * call. You should probably call return immediately after
 * calling _stp_exit().
 */
void _stp_exit (void)
{
	_stp_exit_flag = 1;
}

/** Prints error message and exits.
 * This function sends an error message immediately to staprun. It
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


/** Prints error message.
 * This function sends an error message immediately to staprun. It
 * will also be sent over the bulk transport (relayfs) if it is
 * being used. If the last character is not a newline, then one 
 * is added. 
 *
 * @param fmt A variable number of args.
 * @sa _stp_error
 */
void _stp_softerror (const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (ERROR, NULL, 0, fmt, args);
	va_end(args);
}


static void _stp_dbug (const char *func, int line, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	_stp_vlog (DBUG, func, line, fmt, args);
	va_end(args);
}

/** @} */
#endif /* _IO_C_ */
