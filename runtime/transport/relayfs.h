#ifndef _TRANSPORT_RELAYFS_H_ /* -*- linux-c -*- */
#define _TRANSPORT_RELAYFS_H_

/** @file relayfs.h
 * @brief Header file for relayfs transport
 */

#ifdef RELAYFS_VERSION_GE_4
#include <linux/relayfs_fs.h>
#else
#include "../relayfs/linux/relayfs_fs.h"
#endif /* RELAYFS_VERSION_GE_4 */

struct rchan *_stp_relayfs_open(unsigned n_subbufs,
				unsigned subbuf_size,
				int pid,
				struct dentry **outdir);
void _stp_relayfs_close(struct rchan *chan, struct dentry *dir);

#endif /* _TRANSPORT_RELAYFS_H_ */
