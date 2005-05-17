#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "control.h"
#include "netlink.h"
#include "relayfs.h"

/* transport data structure */
struct stp_transport
{
	struct rchan *chan;
	struct dentry *dir;
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

/**
 *	_stp_streaming - boolean, are we using 'streaming' output?
 */
#ifdef STP_NETLINK_ONLY
#define _stp_streaming()	(1)
#else
#define _stp_streaming()	(0)
#endif

/**
 *	_stp_transport_flush - flush the transport, if applicable
 */
static inline void _stp_transport_flush(void)
{
#ifndef STP_NETLINK_ONLY
	extern struct stp_transport *t;

	BUG_ON(!t->chan);
	relay_flush(t->chan);
	ssleep(1); /* FIXME: time for data to be flushed */
#endif
}

extern int _stp_transport_open(unsigned n_subbufs, unsigned subbuf_size,
			       int pid);
extern void _stp_transport_close(void);
extern int _stp_transport_send (int pid, void *data, int len);
#endif /* _TRANSPORT_TRANSPORT_H_ */
