/*
 *  relay-app.h - kernel 'library' functions for typical relayfs applications
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2005
 *
 * 2005-Feb	Created by Tom Zanussi <zanussi@us.ibm.com>
 *
 * This header file encapsulates the details of channel setup and
 * teardown and communication between the kernel and user parts of a
 * typical and common type of relayfs application, which is that
 * kernel logging is kicked off when a userspace data collection
 * application starts and stopped when the collection app exits, and
 * data is automatically logged to disk in-between.  Channels are
 * created when the collection app is started and destroyed when it
 * exits, not when the kernel module is inserted, so different channel
 * buffer sizes can be specified for each separate run via
 * command-line options for instance.
 *
 * Writing to the channel is done using 2 macros, relayapp_write() and
 * _relayapp_write(), which are just wrappers around relay_write() and
 * _relay_write() but without the channel param.  You can safely call
 * these at any time - if there's no channel yet, they'll just be
 * ignored.
 *
 * To create a relay-app application, do the following:
 *
 * In your kernel module:
 *
 * - #include "relay-app.h"
 *
 * - Call init_relay_app() in your module_init function, with the
 *   names of the directory to create relayfs files in and the base name
 *   of the per-cpu relayfs files e.g. to have /mnt/relay/myapp/cpuXXX
 *   created call init_relay_app("myapp", "cpu", callbacks).
 *
 *   NOTE: The callbacks are entirely optional - pass NULL if you
 *   don't want to define any.  If you want to define some but not
 *   others, just set the ones you want, and ignore or NULL out the
 *   others.
 *
 *   NOTE: This won't actually create the relayfs files - that will
 *   happen when the userspace application starts (i.e. you can supply
 *   the buffer sizes on the application command-line for each new run
 *   of your program).
 *
 *   NOTE: If you pass in NULL for the directory name, the relay files
 *   will be created in the root directory of the relayfs filesystem.
 *
 * - Call close_relay_app() in your module_exit function - this cleans
 *   up the control channel and the relay files from the previous run,
 *   if any.
 *
 * - relay-apps use a control channel to communicate initialization
 *   and status information between the kernel module and user space
 *   program.  This is hidden beneath the API so you normally don't need
 *   to know anything about it, but if you want you can also use it to
 *   send user-defined commands from your user space application.  To do
 *   this, you need to define a definition for the user_command()
 *   callback and in the callback sort out and handle handle the
 *   commands you send from user space (via send_request()).  The
 *   callback must return 1 if the command was handled, or 0 if not
 *   (which will result in a send_error in the user space program,
 *   alerting you to the fact that you're sending something bogus).
 *
 *   NOTE: Currently commands can only be sent before the user space
 *   application enters relay_app_main_loop() i.e. for initialization
 *   purposes only.
 *
 * - the app_started() and app_stopped() callbacks provide an
 *   opportunity for your kernel module to perform app-specific
 *   initialization and cleanup, if desired.  They are purely
 *   informational.  app_started() is called when the user space
 *   application has started and app_stopped() is called when the user
 *   space application has stopped.
 *
 * In your user space application do the following:
 *
 * - Call init_relay_app() with the names of the relayfs file base
 *   name and the base filename of the output files that will be
 *   created, as well as the sub-buffer size and count for the current
 *   run (which can be passed in on the command-line if you want).  This
 *   will create the channel and set up the ouptut files and buffer
 *   mappings.  e.g. to set up reading from the relayfs files specified in the
 *   above example and write them to a set of per-cpu output files named
 *   myoutputXXX:
 * 
 *   init_relay_app("/mnt/relay/myapp/cpu", "myoutput",
 *                  subbuf_size_opt, n_subbufs_opt, 1);
 *
 *   (the last parameter just specifies whether or not to print out a
 *   summary of the number of buffers processed, and the maximum backlog
 *   of sub-buffers encountered e.g. if you have 4 sub-buffers, a
 *   maximum backlog of 3 would mean that you came close to having a
 *   full buffer, so you might want to use more or bigger sub-buffers
 *   next time.  Of course, if the buffers actually filled up, the
 *   maximum backlog would be 4 and you'd have lost data).
 *
 * - Call relay_app_main_loop().  This will set up an infinite loop
 *   (press Control-C to break out and finalize the data) which
 *   automatically reads the data from the relayfs buffers as it becomes
 *   available and and writes it out to per-cpu output files.
 *
 *   NOTE: The control channel is implemented as a netlink socket.
 *   relay-app defaults to using NETLINK_USERSOCK for all
 *   applications, which means that you can't have more than 1
 *   relay-app in use at a time, unless you use different netlink
 *   'units' for each one.  If you want to have more than one
 *   relay-app in use at a time, you can specify a different netlink
 *   'unit' by using the _init_relay_app() versions of the
 *   init_relay_app() functions, on both the kernel and user sides,
 *   which are the same as the init_relay_app() functions but add a
 *   netlink unit param.  See netlink.h for the currently unused
 *   numbers.
 */

#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/netlink.h>
#include <linux/relayfs_fs.h>

/* relay-app pseudo-API */

/*
 * relay-app callbacks
 */
struct relay_app_callbacks
{
	/*
	 *	user_command - app-specific command callback
	 *	@command: user-defined command id
	 *	@data: user-defined data associated with the command
	 *
	 *	Return value: 1 if this callback handled it, 0 if not
	 *
	 *	define this callback to handle user-defined commands sent
	 *	from the user space application via send_request()
	 *
	 *	NOTE: user commands must be >= RELAY_APP_USERCMD_START
	 */
	int (*user_command) (int command, void *data);

	/*
	 *	app_started - the user-space application has started
	 *
	 *	Do app-specific initializations now, if desired
	 */
	void (*app_started) (void);

	/*
	 *	app_stopped - the user-space application has stopped
	 *
	 *	Do app-specific cleanup now, if desired
	 */
	void (*app_stopped) (void);
};

/*
 * relay-app API functions
 */
static int init_relay_app(const char *dirname,
			  const char *file_basename,
			  struct relay_app_callbacks *callbacks);
static void close_relay_app(void);

/*
 * relay-app write wrapper macros - use these instead of directly
 * using relay_write() and _relay_write() relayfs functions.
 */
#define relayapp_write(data, len) \
	if (app.logging) relay_write(app.chan, data, len)

#define _relayapp_write(data, len) \
	if (app.logging) _relay_write(app.chan, data, len)

/* relay-app control channel command values */
enum
{
	RELAY_APP_BUF_INFO = 1,
	RELAY_APP_SUBBUFS_CONSUMED,
	RELAY_APP_START,
	RELAY_APP_STOP,
	RELAY_APP_CHAN_CREATE,
	RELAY_APP_CHAN_DESTROY,
	RELAY_APP_USERCMD_START = 32
};

/* SystemTap extensions */
enum
{
        STP_REALTIME_DATA = RELAY_APP_USERCMD_START,
        STP_EXIT,
	STP_DONE
};

/* internal stuff below here */

/* netlink control channel */
static struct sock *control;
static int seq;
static int stpd_pid = 0;

/* info for this application */
static struct relay_app
{
	char dirname[1024];
	char file_basename[1024];
	struct relay_app_callbacks *cb;
	struct rchan *chan;
	struct dentry *dir;
	int logging;
	int mappings;
} app;

/*
 * subbuf_start() relayfs callback.
 */
static int relay_app_subbuf_start(struct rchan_buf *buf,
				  void *subbuf,
				  unsigned prev_subbuf_idx,
				  void *prev_subbuf)
{
	unsigned padding = buf->padding[prev_subbuf_idx];
	if (prev_subbuf)
		*((unsigned *)prev_subbuf) = padding;

	return sizeof(padding); /* reserve space for padding */
}

/*
 * buf_full() relayfs callback.
 */
static void relay_app_buf_full(struct rchan_buf *buf,
			       unsigned subbuf_idx,
			       void *subbuf)
{
	unsigned padding = buf->padding[subbuf_idx];
	*((unsigned *)subbuf) = padding;
}

static void relay_app_buf_mapped(struct rchan_buf *buf, struct file *filp)
{
	if (app.cb && app.cb->app_started && !app.mappings++)
		app.cb->app_started();
}

static void relay_app_buf_unmapped(struct rchan_buf *buf, struct file *filp)
{
	if (app.cb && app.cb->app_started && !--app.mappings)
		app.cb->app_stopped();
}

static struct rchan_callbacks app_rchan_callbacks =
{
	.subbuf_start = relay_app_subbuf_start,
	.buf_full = relay_app_buf_full,
	.buf_mapped = relay_app_buf_mapped,
	.buf_unmapped = relay_app_buf_unmapped
};

/**
 *	create_app_chan - creates channel /mnt/relay/dirname/filebaseXXX
 *
 *	Returns channel on success, NULL otherwise.
 */
static struct rchan *create_app_chan(unsigned subbuf_size,
				     unsigned n_subbufs)
{
	struct rchan *chan;

	if (strlen(app.dirname)) {
		app.dir = relayfs_create_dir(app.dirname, NULL);
		if (!app.dir) {
			printk("Couldn't create relayfs app directory %s.\n", app.dirname);
			return NULL;
		}
	}

	chan = relay_open(app.file_basename, app.dir, subbuf_size,
			      n_subbufs, 0, &app_rchan_callbacks);
	
	if (!chan) {
		printk("relay app channel creation failed\n");
		if (app.dir)
			relayfs_remove_dir(app.dir);
		return NULL;
	}

	return chan;
}

/**
 *	destroy_app_chan - destroys channel /mnt/relay/dirname/filebaseXXX
 */
static void destroy_app_chan(struct rchan *chan)
{
	if (chan)
		relay_close(chan);
	if (app.dir)
		relayfs_remove_dir(app.dir);

	app.chan = NULL;
	app.dir = NULL;
}

/* netlink control channel communication with userspace */

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

struct channel_create_info
{
	unsigned subbuf_size;
	unsigned n_subbufs;
};

/*
 * send_reply - send reply to userspace over netlink control channel
 */
static int send_reply(int type, void *reply, int len, int pid)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	void *data;
	int size;
	int err;
		
	size = NLMSG_SPACE(len);
	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb)
		return -1;
	nlh = NLMSG_PUT(skb, pid, seq++, type, size - sizeof(*nlh));
	nlh->nlmsg_flags = 0;
	data = NLMSG_DATA(nlh);
	memcpy(data, reply, len);
	err = netlink_unicast(control, skb, pid, MSG_DONTWAIT);

	return 0;

nlmsg_failure:
	if (skb)
		kfree_skb(skb);
	
	return -1;
}

static void handle_buf_info(struct buf_info *in, int pid)
{
	struct buf_info out;

	if (!app.chan)
		return;

	out.cpu = in->cpu;
	out.produced = atomic_read(&app.chan->buf[in->cpu]->subbufs_produced);
	out.consumed = atomic_read(&app.chan->buf[in->cpu]->subbufs_consumed);

	send_reply(RELAY_APP_BUF_INFO, &out, sizeof(out), pid);
}

static inline void handle_subbufs_consumed(struct consumed_info *info)
{
	if (!app.chan)
		return;

	relay_subbufs_consumed(app.chan, info->cpu, info->consumed);
}

static inline void handle_create(struct channel_create_info *info)
{
	destroy_app_chan(app.chan);
	app.chan = create_app_chan(info->subbuf_size, info->n_subbufs);
	if(!app.chan)
		return;
	app.mappings = 0;
}

/*
 * msg_rcv_skb - dispatch userspace requests from netlink control channel
 */
static void msg_rcv_skb(struct sk_buff *skb)
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

	stpd_pid = pid = nlh->nlmsg_pid;
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

	switch (nlh->nlmsg_type) {
	case RELAY_APP_CHAN_CREATE:
		handle_create(data);
		break;
	case RELAY_APP_CHAN_DESTROY:
		destroy_app_chan(app.chan);
		break;
	case RELAY_APP_START:
		app.logging = 1;
		break;
	case RELAY_APP_STOP:
		app.logging = 0;
		relay_flush(app.chan);
		break;
	case RELAY_APP_BUF_INFO:
		handle_buf_info(data, pid);
		break;
	case RELAY_APP_SUBBUFS_CONSUMED:
		handle_subbufs_consumed(data);
		break;
	default:
		if (!app.cb || !app.cb->user_command ||
		    !app.cb->user_command(nlh->nlmsg_type, data))
			netlink_ack(skb, nlh, -EINVAL);
		return;
	}
	
	if (flags & NLM_F_ACK)
		netlink_ack(skb, nlh, 0);
}

/*
 * msg_rcv - handle netlink control channel requests
 */
static void msg_rcv(struct sock *sk, int len)
{
	struct sk_buff *skb;
	
	while ((skb = skb_dequeue(&sk->sk_receive_queue))) {
		msg_rcv_skb(skb);
		kfree_skb(skb);
	}
}

/*
 * _init_relay_app - adds netlink 'unit' if other than NETLINK_USERSOCK wanted
 */
static int _init_relay_app(const char *dirname,
			   const char *file_basename,
			   struct relay_app_callbacks *callbacks,
			   int unit)
{
	if (!file_basename)
		return -1;

	if (dirname)
		strncpy(app.dirname, dirname, 1024);
	strncpy(app.file_basename, file_basename, 1024);
	app.cb = callbacks;

	control = netlink_kernel_create(unit, msg_rcv);
	if (!control) {
		printk("Couldn't create control channel\n");
		return -1;
	}

	return 0;
}

/**
 *	init_relay_app - initialize /mnt/relay/dirname/file_basenameXXX
 *	@dirname: the directory to contain relayfs files for this app
 *	@file_basename: the base filename of the relayfs files for this app
 *	@callbacks: the relay_app_callbacks implemented for this app
 *
 *	Returns 0 on success, -1 otherwise.
 *
 *	NOTE: this doesn't create the relayfs files.  That happens via the
 *	control channel protocol.
 */
static int init_relay_app(const char *dirname,
			  const char *file_basename,
			  struct relay_app_callbacks *callbacks)
{
	return _init_relay_app(dirname, file_basename, callbacks, NETLINK_USERSOCK);
}

/**
 *	close_relay_app - close netlink socket and destroy channel if it exists
 *
 *	Returns 0 on success, -1 otherwise.
 */
static void close_relay_app(void)
{
	if (control)
		sock_release(control->sk_socket);
	destroy_app_chan(app.chan);
}
