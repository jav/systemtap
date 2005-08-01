#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "control.h"
#include "netlink.h"
#include "relayfs.h"
#include "transport_msgs.h"

void _stp_warn (const char *fmt, ...);

static unsigned n_subbufs = 4;
static unsigned subbuf_size = 65536;


#ifdef STP_NETLINK_ONLY
static int _stp_transport_mode = STP_TRANSPORT_NETLINK;
#else
static int _stp_transport_mode = STP_TRANSPORT_RELAYFS;
#endif

#ifdef STP_NETLINK_ONLY
#define _stp_transport_write(data, len)  _stp_netlink_write(data, len)
#else
#define _stp_transport_write(data, len)  _stp_relay_write(data, len)
#endif

extern void _stp_transport_cleanup(void);
extern void _stp_transport_close(void);

#endif /* _TRANSPORT_TRANSPORT_H_ */
