/* -*- linux-c -*- */
/** @file io.c
 * @brief I/O functions
 */

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


/** Lookup symbol.
 * This simply calls the kernel function kallsyms_lookup().
 * That function is not exported, so this workaround is required.
 * See the kernel source, kernel/kallsyms.c for more information.
 */
static const char * (*_stp_kallsyms_lookup)(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf)=(void *)KALLSYMS_LOOKUP;


#define STP_BUF_LEN 8191

/** Static buffer for printing */
static char _stp_pbuf[STP_BUF_LEN+1];
static int _stp_pbuf_len = STP_BUF_LEN;

/** Print into the print buffer.
 * Like printf, except output goes into  _stp_pbuf,
 * which will contain the null-terminated output.
 * Safe because overflowing _stp_pbuf is not allowed.
 * Size is limited by length of print buffer.
 *
 * @param fmt A variable number of args.
 * @note Formatting output should never be done within
 * a probe. Use at module exit time.
 * @sa _stp_print_buf_init
 */

void _stp_print_buf (const char *fmt, ...)
{
  int num;
  va_list args;
  char *buf = _stp_pbuf + STP_BUF_LEN - _stp_pbuf_len;
  va_start(args, fmt);
  num = vscnprintf(buf, _stp_pbuf_len, fmt, args);
  va_end(args);
  if (num > 0)
    _stp_pbuf_len -= num;
}

/** Clear the print buffer.
 * Output from _stp_print_buf() will accumulate in the buffer
 * until this is called.
 */

void _stp_print_buf_init (void)
{
  _stp_pbuf_len = STP_BUF_LEN;
  _stp_pbuf[0] = 0;
}

/** Print addresses symbolically into the print buffer.
 * @param fmt A variable number of args.
 * @param address The address to lookup.
 * @note Formatting output should never be done within
 * a probe. Use at module exit time.
 */

void _stp_print_symbol(const char *fmt, unsigned long address)
{
        char *modname;
        const char *name;
        unsigned long offset, size;
        char namebuf[KSYM_NAME_LEN+1];

        name = _stp_kallsyms_lookup(address, &size, &offset, &modname, namebuf);

        if (!name)
                _stp_print_buf("0x%lx", address);
        else {
	  if (modname)
	    _stp_print_buf("%s+%#lx/%#lx [%s]", name, offset,
			   size, modname);
	  else
	    _stp_print_buf("%s+%#lx/%#lx", name, offset, size);
        }
}

/** Get the current return address.
 * Call from kprobes (not jprobes).
 * @param regs The pt_regs saved by the kprobe.
 * @return The return address saved in esp or rsp.
 * @note i386 and x86_64 only.
 */
 
unsigned long cur_ret_addr (struct pt_regs *regs)
{
#ifdef __x86_64__
  unsigned long *ra = (unsigned long *)regs->rsp;
#else
  unsigned long *ra = (unsigned long *)regs->esp;
#endif
  if (ra)
    return *ra;
  else
    return 0;
}
