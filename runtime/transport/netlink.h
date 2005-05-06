#ifndef _TRANSPORT_NETLINK_H_ /* -*- linux-c -*- */
#define _TRANSPORT_NETLINK_H_

/** @file netlink.h
 * @brief Header file for netlink transport
 */

#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <net/sock.h>

extern struct sock *_stp_netlink_open(int unit, int (*handler) (int pid, int cmd, void *data));
extern void _stp_netlink_close(struct sock *nl);
extern int _stp_netlink_send(int type, void *reply, int len, int pid);

#endif /* _TRANSPORT_NETLINK_H_ */
