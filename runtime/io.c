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


static const char * (*_stp_kallsyms_lookup)(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname, char *namebuf)=(void *)KALLSYMS_LOOKUP;


#define STP_BUF_LEN 8191

/* FIXME. These need to be per-cpu */
static char _stp_pbuf[STP_BUF_LEN+1];
static int _stp_pbuf_len = STP_BUF_LEN;

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

void _stp_print_buf_init (void)
{
  _stp_pbuf_len = STP_BUF_LEN;
  _stp_pbuf[0] = 0;
}

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
