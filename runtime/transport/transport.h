#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "transport_msgs.h"

/* The size of print buffers. This limits the maximum */
/* amount of data a print can send. */
#define STP_BUFFER_SIZE 8192

/* STP_CTL_BUFFER_SIZE is the maximum size of a message */
/* exchanged on the control channel. */
#ifdef STP_OLD_TRANSPORT
/* Old transport sends print output on control channel */
#define STP_CTL_BUFFER_SIZE STP_BUFFER_SIZE
#else
#define STP_CTL_BUFFER_SIZE 256
#endif

/* how often the work queue wakes up and checks buffers */
#define STP_WORK_TIMER (HZ/100)

static unsigned _stp_nsubbufs = 8;
static unsigned _stp_subbuf_size = 65536*4;

static void _stp_warn (const char *fmt, ...);
static void _stp_transport_close(void);
static int _stp_print_init(void);
static void _stp_print_cleanup(void);
static struct dentry *_stp_get_root_dir(const char *name);
static int _stp_lock_debugfs(void);
static void _stp_unlock_debugfs(void);
static void _stp_attach(void);
static void _stp_detach(void);
static void _stp_handle_start(struct _stp_msg_start *st);

static int _stp_pid = 0;
static uid_t _stp_uid = 0;
static gid_t _stp_gid = 0;
static pid_t _stp_init_pid = 0;
static int _stp_attached = 0;
#endif /* _TRANSPORT_TRANSPORT_H_ */
