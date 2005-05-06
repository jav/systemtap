/*
 *  stp-control - stp control channel
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
 * Copyright (C) Redhat Inc, 2005
 *
 */

/** @file control.c
 * @brief Systemtap control channel functions
 */

/** @addtogroup transport Transport Functions
 * @{
 */

#include <linux/module.h>
#include <linux/hash.h>
#include "control.h"
#include "netlink.h"

/* the control channel */
struct sock *stp_control;

/* the command handlers hash table */
static struct hlist_head *handlers;

/* protection for the handlers table */
static DEFINE_SPINLOCK(handlers_lock);

/**
 *	_stp_lookup_handler - look up the command handler in the handlers table
 *	@pid: the pid to find the corresponding handler of
 *
 *	Returns the pointer to the cmd_handler struct, NULL if not
 *	found.
 *
 *	NOTE: the handlers_lock must be held when calling this function
 */
static struct cmd_handler *_stp_lookup_handler(int pid)
{
	struct hlist_node *node;
	struct cmd_handler *handler;
	unsigned long key = hash_long((unsigned long)pid, HANDLER_SHIFT);
	struct hlist_head *head = &handlers[key];
	
	hlist_for_each(node, head) {
		handler = hlist_entry(node, struct cmd_handler, hlist);
		if (handler->pid == pid) {
			return handler;
			break;
		}
	}
	
	return NULL;
}

/**
 *	_stp_handler_find - find the command handler for a given pid
 *	@pid: the pid to find the corresponding handler of
 *
 *	Returns the pointer to the command handler callback, NULL if
 *	not found.
 */
static int (*_stp_handler_find(int pid))(int, int, void *)
{
	struct cmd_handler *cmd_handler;

	spin_lock(&handlers_lock);
	cmd_handler = _stp_lookup_handler(pid);
	spin_unlock(&handlers_lock);
	
	if (cmd_handler)
		return cmd_handler->handler;
	
	return NULL;
}

/**
 *	_stp_ctrl_register - register a command handler for a pid
 *	@pid: the pid to unregister
 *	@cmd_handler: the callback function to be called to handle commands
 *
 *	Adds a pid's command handler to the handler table.  The
 *	command handler will be called to handle commands from the
 *	daemon with the given pid.  Should be called at probe module
 *	initialization before any commands are sent by the daemon.
 */
int _stp_ctrl_register(int pid,
		       int (*cmd_handler) (int pid, int cmd, void *data))
{
	unsigned long key = hash_long((unsigned long)pid, HANDLER_SHIFT);
	struct hlist_head *head = &handlers[key];
	struct cmd_handler *handler;

	spin_lock(&handlers_lock);
	handler = _stp_lookup_handler(pid);
	spin_unlock(&handlers_lock);

	if (handler)
		return -EBUSY;

	handler = kmalloc(sizeof(struct cmd_handler), GFP_KERNEL);
	if (!handler)
		return -ENOMEM;
	handler->pid = pid;
	handler->handler = cmd_handler;
	INIT_HLIST_NODE(&handler->hlist);

	spin_lock(&handlers_lock);
	hlist_add_head(&handler->hlist, head);
	spin_unlock(&handlers_lock);

	return 0;
}

/**
 *	_stp_ctrl_unregister - unregister a pid's command handler
 *	@pid: the pid to unregister
 *
 *	Removes the pid's handler from the handler table.  Should be
 *	called when the daemon is no longer sending commands.
 */
void _stp_ctrl_unregister(int pid)
{
	struct cmd_handler *handler;
	
	spin_lock(&handlers_lock);
	handler = _stp_lookup_handler(pid);
	if (handler) {
		hlist_del(&handler->hlist);
		kfree(handler);
	}
	spin_unlock(&handlers_lock);
}

/**
 *	_stp_ctrl_send - send data over the control channel
 *	@type: the type of data being sent
 *	@data: the data
 *	@len: the data length
 *	@pid: the pid to send the data to
 *
 *	Returns the result of the transport's send function.
 */
int _stp_ctrl_send(int type, void *data, int len, int pid)
{
	return _stp_netlink_send(type, data, len, pid);
}

/**
 *	_stp_ctrl_handler - control channel command dispatcher
 *	@pid: the pid the command is from
 *	@cmd: the command
 *	@data: command-specific data
 *
 *	Returns the result from the pid's command handler, 0 if the
 *	command was handled, non-zero otherwise.
 */
static int _stp_ctrl_handler(int pid, int cmd, void *data)
{
	int (*cmd_handler) (int, int, void *);

	cmd_handler = _stp_handler_find(pid);
	if (!cmd_handler)
		return -EINVAL;
	
	return cmd_handler(pid, cmd, data);
}

/**
 *	_stp_ctrl_init - module init function
 */
static int __init _stp_ctrl_init(void)
{
	int i;

	handlers = kmalloc(sizeof(struct hlist_head) * HANDLER_SLOTS, GFP_KERNEL);
	if (!handlers)
		return -ENOMEM;
	
	for (i = 0; i < HANDLER_SLOTS; i++)
		INIT_HLIST_HEAD(&handlers[i]);

	stp_control = _stp_netlink_open(NETLINK_USERSOCK, _stp_ctrl_handler);
	if (!stp_control) {
		printk ("stp_ctrl: couldn't open netlink socket\n");
		kfree(handlers);
		return -ENOMEM;
	}

	return 0;
}

/**
 *	_stp_ctrl_exit - module exit function
 */
static void _stp_ctrl_exit(void)
{
	_stp_netlink_close(stp_control);
	kfree(handlers);
}

module_init(_stp_ctrl_init);
module_exit(_stp_ctrl_exit);

EXPORT_SYMBOL_GPL(_stp_ctrl_register);
EXPORT_SYMBOL_GPL(_stp_ctrl_unregister);
EXPORT_SYMBOL_GPL(_stp_ctrl_send);

MODULE_DESCRIPTION("SystemTap control channel");
MODULE_AUTHOR("Tom Zanussi <zanussi@us.ibm.com>");
MODULE_LICENSE("GPL");

