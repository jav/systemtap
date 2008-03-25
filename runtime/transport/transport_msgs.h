/* -*- linux-c -*- 
 * transport_msgs.h - messages exchanged between module and userspace
 *
 * Copyright (C) Red Hat Inc, 2006-2008
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#define STP_MODULE_NAME_LEN 64

struct _stp_trace {
	uint32_t sequence;	/* event number */
	uint32_t pdu_len;	/* length of data after this trace */
};

/* stp control channel command values */
enum
{
	STP_START,
        STP_EXIT,
	STP_OOB_DATA,
	STP_SYSTEM,
	STP_UNWIND,
	STP_TRANSPORT,
	STP_CONNECT,
 	STP_DISCONNECT,
	STP_BULK,
	STP_READY,
#ifdef STP_OLD_TRANSPORT
	/** deprecated **/
	STP_BUF_INFO,
	STP_SUBBUFS_CONSUMED,
	STP_REALTIME_DATA,
#endif
	STP_MAX_CMD
};

#ifdef DEBUG_TRANS
static const char *_stp_command_name[] = {
	"STP_START",
	"STP_EXIT",
	"STP_OOB_DATA",
	"STP_SYSTEM",
	"STP_UNWIND",
	"STP_TRANSPORT",
	"STP_CONNECT",
	"STP_DISCONNECT",
	"STP_BULK",
	"STP_READY",
#ifdef STP_OLD_TRANSPORT
	"STP_BUF_INFO",
	"STP_SUBBUFS_CONSUMED",
	"STP_REALTIME_DATA",
#endif
};
#endif /* DEBUG_TRANS */

/* control channel messages */

/* command to execute: module->stapio */
struct _stp_msg_cmd
{
	char cmd[128];
};

/* Unwind data. stapio->module */
struct _stp_msg_unwind
{
	/* the module name, or "*" for all */
	char name[STP_MODULE_NAME_LEN];
	/* length of unwind data */
	uint64_t unwind_len;
	/* data ...*/
};

/* Request to start probes. */
/* stapio->module->stapio */
struct _stp_msg_start
{
	pid_t target;
        int32_t res;    // for reply: result of probe_start()
};

#ifdef STP_OLD_TRANSPORT
/**** for compatibility with old relayfs ****/
struct _stp_buf_info
{
        int32_t cpu;
        uint32_t produced;
        uint32_t consumed;
        int32_t flushing;
};
struct _stp_consumed_info
{
        int32_t cpu;
        uint32_t consumed;
};
#endif
