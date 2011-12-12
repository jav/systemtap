/* -*- linux-c -*-
 * Common functions for using inode-based uprobes
 * Copyright (C) 2011 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _UPROBES_INODE_C_
#define _UPROBES_INODE_C_

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/uprobes.h>

// PR13489, inodes-uprobes export kludge
#if !defined(CONFIG_UPROBES)
#error "not to be built without CONFIG_UPROBES"
#endif
#if !defined(STAPCONF_REGISTER_UPROBE_EXPORTED)
typedef int (*register_uprobe_fn)(struct inode *inode, loff_t offset, 
                                  struct uprobe_consumer *consumer);
#define register_uprobe (* (register_uprobe_fn)kallsyms_register_uprobe)
#endif
#if !defined(STAPCONF_UNREGISTER_UPROBE_EXPORTED)
typedef void (*unregister_uprobe_fn)(struct inode *inode, loff_t offset,
                                     struct uprobe_consumer *consumer);
#define unregister_uprobe (* (unregister_uprobe_fn)kallsyms_unregister_uprobe)
#endif


struct stp_inode_uprobe_target {
	const char * const filename;
	struct inode *inode;
};

struct stp_inode_uprobe_consumer {
	struct uprobe_consumer consumer;
	struct stp_inode_uprobe_target * const target;
	loff_t offset;
	/* XXX sdt_sem_offset support? */

	struct stap_probe * const probe;
};


static void
stp_inode_uprobes_put(struct stp_inode_uprobe_target *targets,
		      size_t ntargets)
{
	size_t i;
	for (i = 0; i < ntargets; ++i) {
		struct stp_inode_uprobe_target *ut = &targets[i];
		iput(ut->inode);
		ut->inode = NULL;
	}
}

static int
stp_inode_uprobes_get(struct stp_inode_uprobe_target *targets,
		      size_t ntargets)
{
	int ret = 0;
	size_t i;
	for (i = 0; i < ntargets; ++i) {
		struct path path;
		struct stp_inode_uprobe_target *ut = &targets[i];
		ret = kern_path(ut->filename, LOOKUP_FOLLOW, &path);
		if (!ret) {
			ut->inode = igrab(path.dentry->d_inode);
			if (!ut->inode)
				ret = -EINVAL;
		}
		if (ret)
			break;
	}
	if (ret)
		stp_inode_uprobes_put(targets, i);
	return ret;
}

static void
stp_inode_uprobes_unreg(struct stp_inode_uprobe_consumer *consumers,
			size_t nconsumers)
{
	size_t i;
	for (i = 0; i < nconsumers; ++i) {
		struct stp_inode_uprobe_consumer *uc = &consumers[i];
		unregister_uprobe(uc->target->inode, uc->offset,
				  &uc->consumer);
	}
}

static int
stp_inode_uprobes_reg(struct stp_inode_uprobe_consumer *consumers,
		      size_t nconsumers)
{
	int ret = 0;
	size_t i;
	for (i = 0; i < nconsumers; ++i) {
		struct stp_inode_uprobe_consumer *uc = &consumers[i];
		ret = register_uprobe(uc->target->inode, uc->offset,
				      &uc->consumer);
		if (ret)
			break;
	}
	if (ret)
		stp_inode_uprobes_unreg(consumers, i);
	return ret;
}

static int
stp_inode_uprobes_init(struct stp_inode_uprobe_target *targets, size_t ntargets,
		       struct stp_inode_uprobe_consumer *consumers, size_t nconsumers)
{
	int ret = stp_inode_uprobes_get(targets, ntargets);
	if (!ret) {
		ret = stp_inode_uprobes_reg(consumers, nconsumers);
		if (ret)
			stp_inode_uprobes_put(targets, ntargets);
	}
	return ret;
}

static void
stp_inode_uprobes_exit(struct stp_inode_uprobe_target *targets, size_t ntargets,
		       struct stp_inode_uprobe_consumer *consumers, size_t nconsumers)
{
	stp_inode_uprobes_unreg(consumers, nconsumers);
	stp_inode_uprobes_put(targets, ntargets);
}

#endif /* _UPROBES_INODE_C_ */
