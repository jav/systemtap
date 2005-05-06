#ifndef _TRANSPORT_RELAYFS_H_ /* -*- linux-c -*- */
#define _TRANSPORT_RELAYFS_H_

/** @file relayfs.h
 * @brief Header file for relayfs transport
 */

#include <linux/relayfs_fs.h>

struct rchan *_stp_relayfs_open(unsigned n_subbufs,
				unsigned subbuf_size,
				int pid,
				struct dentry **outdir);
void _stp_relayfs_close(struct rchan *chan, struct dentry *dir);

#endif /* _TRANSPORT_RELAYFS_H_ */
