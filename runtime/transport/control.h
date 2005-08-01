#ifndef _TRANSPORT_CONTROL_H_ /* -*- linux-c -*- */
#define _TRANSPORT_CONTROL_H_

/** @file control.h
 * @brief Header file for transport control channel
 */

/* command handlers hash table entry struct */
struct cmd_handler
{
	struct hlist_node hlist;
	int pid;
	int (*handler) (int pid, int cmd, void *data);
};

#define HANDLER_SHIFT	5
#define HANDLER_SLOTS	(1 << HANDLER_SHIFT)

extern int _stp_ctrl_register(int pid, int (*cmd_handler) (int pid, int cmd, void *data));
extern void _stp_ctrl_unregister(int pid);
extern int _stp_ctrl_send(int type, void *reply, int len, int pid);

#endif /* _TRANSPORT_CONTROL_H_ */
