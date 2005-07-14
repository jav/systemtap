/*
 * netlink.c - stp relayfs-related transport functions
 *
 * Copyright (C) IBM Corporation, 2005
 * Copyright (C) Redhat Inc, 2005
 *
 * This file is released under the GPL.
 */

/** @file netlink.c
 * @brief Systemtap netlink-related transport functions
 */

/** @addtogroup transport Transport Functions
 * @{
 */

#include "netlink.h"

/* the control socket */
extern struct sock *stp_control;

/* queued packets logged from irq context */
static struct sk_buff_head delayed_pkts;

/* for netlink sequence numbers */
static int seq;

/**
 *	_stp_msg_rcv_skb - dispatch netlink control channel requests
 */
static void _stp_msg_rcv_skb(struct sk_buff *skb,
			     int (*cmd_handler) (int pid, int cmd, void *data))
{
	struct nlmsghdr *nlh = NULL;
	int pid, flags;
	int nlmsglen, skblen;
	void *data;
	
	skblen = skb->len;
	
	if (skblen < sizeof (*nlh))
		return;	

	nlh = (struct nlmsghdr *)skb->data;
	nlmsglen = nlh->nlmsg_len;
	
	if (nlmsglen < sizeof(*nlh) || skblen < nlmsglen)
		return;

	pid = nlh->nlmsg_pid;
	flags = nlh->nlmsg_flags;

	if (pid <= 0 || !(flags & NLM_F_REQUEST)) {
		netlink_ack(skb, nlh, -EINVAL);
		return;
	}

	if (flags & MSG_TRUNC) {
		netlink_ack(skb, nlh, -ECOMM);
		return;
	}
	
	data = NLMSG_DATA(nlh);

	if (cmd_handler(pid, nlh->nlmsg_type, data))
		netlink_ack(skb, nlh, -EINVAL);		
	
	if (flags & NLM_F_ACK)
		netlink_ack(skb, nlh, 0);
}

/**
 *	_stp_msg_rcv - handle netlink control channel requests
 */
static void _stp_msg_rcv(struct sock *sk, int len)
{
	struct sk_buff *skb;
	
	while ((skb = skb_dequeue(&sk->sk_receive_queue))) {
		_stp_msg_rcv_skb(skb, sk->sk_user_data);
		kfree_skb(skb);
	}
}

/**
 *	_stp_send_delayed_packets - send delayed netlink packets
 */
static void _stp_send_delayed_pkts(unsigned long ignored)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(&delayed_pkts)) != NULL) {
		struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;
		int pid = nlh->nlmsg_pid;
		netlink_unicast(stp_control, skb, pid, MSG_DONTWAIT);
	}
}
static DECLARE_TASKLET(delayed_pkts_tasklet, _stp_send_delayed_pkts, 0);

/**
 *	_stp_netlink_send - send data over netlink channel
 *	@type: message type
 *	@data: data to send
 *	@len: length of data
 *	@pid: pid to send data to
 */
int _stp_netlink_send(int type, void *data, int len, int pid)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	void *d;
	int size;
	int err = 0;

	size = NLMSG_SPACE(len);
	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	nlh = NLMSG_PUT(skb, pid, seq++, type, size - sizeof(*nlh));
	nlh->nlmsg_flags = 0;
	d = NLMSG_DATA(nlh);
	memcpy(d, data, len);

	if (in_irq()) {
		skb_queue_tail(&delayed_pkts, skb);
		tasklet_schedule(&delayed_pkts_tasklet);
	} else
		err = netlink_unicast(stp_control, skb, pid, MSG_DONTWAIT);

	return err;

nlmsg_failure:
	if (skb)
		kfree_skb(skb);
	
	return -1;
}

/**
 *	_stp_netlink_open - create netlink socket
 *	@unit: the netlink 'unit' to create
 *	@handler: handler function for stp 'commands'
 */
struct sock *_stp_netlink_open(int unit,
			       int (*handler) (int pid, int cmd, void *data))
{
	struct sock *nl = netlink_kernel_create(unit, _stp_msg_rcv);
	if (!nl) {
		printk("stp-control: couldn't create netlink transport\n");
		return NULL;
	}
	nl->sk_user_data = handler;

        skb_queue_head_init(&delayed_pkts);

	return nl;
}

/**
 *	_stp_netlink_close - close netlink socket
 */
void _stp_netlink_close (struct sock *nl)
{
	BUG_ON(!nl);
	sock_release(nl->sk_socket);
}
