/** Logs data.
 * This function is compatible with printk.  In fact it currently
 * sends all output to vprintk, after sending "STP: ". This allows
 * us to easily detect SystemTap output in the log file.
 *
 * @param fmt A variable number of args.
 * @bug Lines are limited in length by printk buffer.
 * @todo Needs replaced with something much faster that does not
 * use the system log.
 */
void dlog (const char *fmt, ...)
{
  va_list args;
  printk("STP: ");
  va_start(args, fmt);
  vprintk(fmt, args);
  va_end(args);
}

