#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "control.h"
#include "netlink.h"
#include "relayfs.h"

/* SystemTap transport values */
enum
{
	STP_TRANSPORT_NETLINK = 1,
	STP_TRANSPORT_RELAYFS
};

/* transport data structure */
struct stp_transport
{
	struct rchan *chan;
	struct dentry *dir;
	int transport_mode;
	int pid;
};

/* control channel command structs */
struct buf_info
{
	int cpu;
	unsigned produced;
	unsigned consumed;
};

struct consumed_info
{
	int cpu;
	unsigned consumed;
};

struct transport_info
{
	int transport_mode;
	unsigned subbuf_size;
	unsigned n_subbufs;
};

/**
 *	_stp_transport_write - write data to the transport
 *	@t: the transport struct
 *	@data: the data to send
 *	@len: length of the data to send
 */
#ifdef STP_NETLINK_ONLY
#define _stp_transport_write(t, data, len)  _stp_transport_send (t->pid, data, len)
#else
#define _stp_transport_write(t, data, len)  relay_write(t->chan, data, len)
#endif

extern int _stp_transport_open(int transport_mode,
			       unsigned n_subbufs,
			       unsigned subbuf_size,
			       int pid);
extern void _stp_transport_close(void);
extern int _stp_transport_send (int pid, void *data, int len);
#endif /* _TRANSPORT_TRANSPORT_H_ */
