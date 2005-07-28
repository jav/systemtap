/* I/O for printing warnings, errors and debug messages
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _IO_C_
#define _IO_C_

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
	vprintf (fmt, args);
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
	vprintf (fmt, args);
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
  exit (-1);
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
	vprintf (fmt, args);
	va_end(args);
	_stp_exit();
}

/** @} */
#endif /* _IO_C_ */
