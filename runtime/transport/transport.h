#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "relayfs.h"
#include "transport_msgs.h"

void _stp_warn (const char *fmt, ...);

#define STP_BUFFER_SIZE 8192

/* how often the work queue wakes up and checks buffers */
#define STP_WORK_TIMER (HZ/100)

#ifdef STP_RELAYFS
static unsigned n_subbufs = 16;
static unsigned subbuf_size = 65536;
#define _stp_transport_write(data, len)  _stp_relay_write(data, len)
static int _stp_transport_mode = STP_TRANSPORT_RELAYFS;
#else
static int _stp_transport_mode = STP_TRANSPORT_PROC;
#endif

extern void _stp_transport_close(void);
extern int _stp_print_init(void);
extern void _stp_print_cleanup(void);
#endif /* _TRANSPORT_TRANSPORT_H_ */
