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

static unsigned _stp_nsubbufs = 4;
static unsigned _stp_subbuf_size = 65536*2;
extern void _stp_transport_close(void);
extern int _stp_print_init(void);
extern void _stp_print_cleanup(void);
static struct dentry *_stp_get_root_dir(const char *name);
static int _stp_lock_debugfs(void);
static void _stp_unlock_debugfs(void);
#endif /* _TRANSPORT_TRANSPORT_H_ */
