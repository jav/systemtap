#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "relayfs.h"
#include "transport_msgs.h"

void _stp_warn (const char *fmt, ...);

#define STP_BUFFER_SIZE 8192

#ifdef STP_RELAYFS
static unsigned n_subbufs = 4;
static unsigned subbuf_size = 65536;
#define _stp_transport_write(data, len)  _stp_relay_write(data, len)
static int _stp_transport_mode = STP_TRANSPORT_RELAYFS;
#else
#define _stp_transport_write(data, len)  _stp_write(STP_REALTIME_DATA, data, len)
static int _stp_transport_mode = STP_TRANSPORT_PROC;
#endif

extern void _stp_transport_cleanup(void);
extern void _stp_transport_close(void);

#endif /* _TRANSPORT_TRANSPORT_H_ */
