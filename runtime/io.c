#ifndef _IO_C_
#define _IO_C_

/* -*- linux-c -*- */
/** @file io.c
 * @brief I/O functions
 */
/** @addtogroup io I/O
 * I/O functions
 * @{
 */

/** Logs Data.
 * This function is compatible with printk.  In fact it currently
 * sends all output to vprintk, after sending "STP: ". This allows
 * us to easily detect SystemTap output in the log file. 
 *
 * @param fmt A variable number of args.
 * @bug Lines are limited in length by printk buffer. If there is
 * no newline in the format string, then other syslog output could
 * get appended to the SystemTap line.
 * @todo Either deprecate or redefine this as a way to log debug or 
 * status messages, separate from the normal program output.
 */
void dlog (const char *fmt, ...)
{
  va_list args;
  printk("STP: ");
  va_start(args, fmt);
  vprintk(fmt, args);
  va_end(args);
}

/** Prints to the trace buffer.
 * This function uses the same formatting as printk.  It currently
 * writes to the system log. 
 *
 * @param fmt A variable number of args.
 * @todo Needs replaced with something much faster that does not
 * use the system log.
 */

void _stp_print (const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vprintk(fmt, args);
  va_end(args);
}

/** Prints to the trace buffer.
 * This function will write a string to the trace buffer.  It currently
 * writes to the system log. 
 *
 * @param str String.
 * @todo Needs replaced with something much faster that does not
 * use the system log.
 */

void _stp_print_str (char *str)
{
  printk ("%s", str);
}

/** @} */
#endif /* _IO_C_ */
