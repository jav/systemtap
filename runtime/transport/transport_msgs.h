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
	STP_SYMBOLS,
	STP_MODULE,
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
};

/* control channel messages */

/* command to execute: sent to staprun */
struct _stp_msg_cmd
{
	char cmd[128];
};

/* request for symbol data. sent to staprun */
struct _stp_msg_symbol
{
	int32_t endian;
	int32_t ptr_size;
};

/* Request to start probes. */
/* Sent from staprun. Then returned from module. */
struct _stp_msg_start
{
	pid_t target;
        int32_t res;    // for reply: result of probe_start()
};

struct _stp_symbol32
{
	uint32_t addr;
	uint32_t symbol;
};

struct _stp_symbol64
{
	uint64_t addr;
	uint64_t symbol;
};

struct _stp_msg_symbol_hdr
{
	uint32_t num_syms;
	uint32_t sym_size;
	uint32_t unwind_size;
};

struct _stp_msg_module {
	/* the module name, or "" for kernel */
	char name[STP_MODULE_NAME_LEN];
	
	/* A pointer to the struct module */
	uint64_t module;

	/* the start of the module's text and data sections */
	uint64_t text;
	uint64_t data;
	
	/* how many sections this module has */
	uint32_t num_sections;

	/* length of unwind data */
	uint32_t unwind_len;
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
