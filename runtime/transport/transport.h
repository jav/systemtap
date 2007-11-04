#ifndef _TRANSPORT_TRANSPORT_H_ /* -*- linux-c -*- */
#define _TRANSPORT_TRANSPORT_H_

/** @file transport.h
 * @brief Header file for stp transport
 */

#include "transport_msgs.h"

void _stp_warn (const char *fmt, ...);

#define STP_BUFFER_SIZE 8192

/* how often the work queue wakes up and checks buffers */
#define STP_WORK_TIMER (HZ/100)

static unsigned _stp_nsubbufs = 8;
static unsigned _stp_subbuf_size = 65536*4;
extern void _stp_transport_close(void);
extern int _stp_print_init(void);
extern void _stp_print_cleanup(void);
static struct dentry *_stp_get_root_dir(const char *name);
static int _stp_lock_debugfs(void);
static void _stp_unlock_debugfs(void);
int _stp_pid = 0;
uid_t _stp_uid = 0;
gid_t _stp_gid = 0;
pid_t _stp_init_pid = 0;
int _stp_attached = 0;
#endif /* _TRANSPORT_TRANSPORT_H_ */
