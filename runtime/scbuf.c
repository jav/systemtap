#ifndef _SCBUF_C_
#define _SCBUF_C_

/* -*- linux-c -*- */
/** @file scbuf.c
 * @addtogroup scbuf Scratch Buffer
 * Scratch Buffer Functions.
 * The scratch buffer is for collecting output before storing in a map,
 * printing, etc. This is a per-cpu static buffer.  It is necessary because 
 * of the limited stack space available in the kernel.
 * @{
 */

/** Maximum size of buffer, not including terminating NULL */
#define STP_BUF_LEN 8191

/** Scratch buffer for printing, building strings, etc */
char _stp_scbuf[STP_BUF_LEN+1];
static int _stp_scbuf_len = STP_BUF_LEN;

/** Sprint into the scratch buffer.
 * Like printf, except output goes into  #_stp_scbuf,
 * which will contain the null-terminated output.
 * Safe because overflowing #_stp_scbuf is not allowed.
 * Size is limited by length of scratch buffer, STP_BUF_LEN.
 *
 * @param fmt A printf-style format string followed by a 
 * variable number of args.
 * @sa _stp_scbuf_clear
 */

void _stp_sprint (const char *fmt, ...)
{
  int num;
  va_list args;
  char *buf = _stp_scbuf + STP_BUF_LEN - _stp_scbuf_len;
  va_start(args, fmt);
  num = vscnprintf(buf, _stp_scbuf_len, fmt, args);
  va_end(args);
  if (num > 0)
    _stp_scbuf_len -= num;
}

void _stp_sprint_str (const char *str)
{
  char *buf = _stp_scbuf + STP_BUF_LEN - _stp_scbuf_len;
  int num = strlen (str);
  if (num > _stp_scbuf_len)
    num = _stp_scbuf_len;
  strncpy (buf, str, num);
  _stp_scbuf_len -= num;
}

/** Clear the scratch buffer.
 * Output from _stp_sprint() will accumulate in the buffer
 * until this is called.
 */

void _stp_scbuf_clear (void)
{
  _stp_scbuf_len = STP_BUF_LEN;
  _stp_scbuf[0] = 0;
}

static char *_stp_scbuf_cur (void)
{
  return _stp_scbuf + STP_BUF_LEN - _stp_scbuf_len;
}

/** @} */
#endif /* _SCBUF_C_ */
