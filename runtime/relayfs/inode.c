/*
 * VFS-related code for RelayFS, a high-speed data relay filesystem.
 *
 * Copyright (C) 2003-2005 - Tom Zanussi <zanussi@us.ibm.com>, IBM Corp
 * Copyright (C) 2003-2005 - Karim Yaghmour <karim@opersys.com>
 *
 * Based on ramfs, Copyright (C) 2002 - Linus Torvalds
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/relayfs_fs.h>
#include "relay.h"
#include "buffers.h"

#define RELAYFS_MAGIC			0xF0B4A981

static struct vfsmount *		relayfs_mount;
static int				relayfs_mount_count;
static kmem_cache_t *			relayfs_inode_cachep;

static struct backing_dev_info		relayfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.memory_backed  = 1,    /* Does not contribute to dirty memory */
};

static struct inode *relayfs_get_inode(struct super_block *sb, int mode,
				       struct rchan *chan)
{
	struct rchan_buf *buf = NULL;
	struct inode *inode;

	if (S_ISREG(mode)) {
		BUG_ON(!chan);
		buf = relay_create_buf(chan);
		if (!buf)
			return NULL;
	}

	inode = new_inode(sb);
	if (!inode) {
		relay_destroy_buf(buf);
		return NULL;
	}

	inode->i_mode = mode;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks = 0;
	inode->i_mapping->backing_dev_info = &relayfs_backing_dev_info;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	switch (mode & S_IFMT) {
	case S_IFREG:
		inode->i_fop = &relayfs_file_operations;
		RELAYFS_I(inode)->buf = buf;
		break;
	case S_IFDIR:
		inode->i_op = &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;

		/* directory inodes start off with i_nlink == 2 (for "." entry) */
		inode->i_nlink++;
		break;
	default:
		break;
	}

	return inode;
}

/**
 *	relayfs_create_entry - create a relayfs directory or file
 *	@name: the name of the file to create
 *	@parent: parent directory
 *	@mode: mode
 *	@chan: relay channel associated with the file
 *
 *	Returns the new dentry, NULL on failure
 *
 *	Creates a file or directory with the specifed permissions.
 */
static struct dentry *relayfs_create_entry(const char *name,
					   struct dentry *parent,
					   int mode,
					   struct rchan *chan)
{
	struct qstr qname;
	struct dentry *d;
	struct inode *inode;
	int error = 0;

	BUG_ON(!name || !(S_ISREG(mode) || S_ISDIR(mode)));

	error = simple_pin_fs("relayfs", &relayfs_mount, &relayfs_mount_count);
	if (error) {
		printk(KERN_ERR "Couldn't mount relayfs: errcode %d\n", error);
		return NULL;
	}

	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(name, qname.len);

	if (!parent && relayfs_mount && relayfs_mount->mnt_sb)
		parent = relayfs_mount->mnt_sb->s_root;

	if (!parent) {
		simple_release_fs(&relayfs_mount, &relayfs_mount_count);
		return NULL;
	}

	parent = dget(parent);
	down(&parent->d_inode->i_sem);
	d = lookup_hash(&qname, parent);
	if (IS_ERR(d)) {
		d = NULL;
		goto release_mount;
	}

	if (d->d_inode) {
		d = NULL;
		goto release_mount;
	}

	inode = relayfs_get_inode(parent->d_inode->i_sb, mode, chan);
	if (!inode) {
		d = NULL;
		goto release_mount;
	}

	d_instantiate(d, inode);
	dget(d);	/* Extra count - pin the dentry in core */

	if (S_ISDIR(mode))
		parent->d_inode->i_nlink++;

	goto exit;

release_mount:
	simple_release_fs(&relayfs_mount, &relayfs_mount_count);

exit:
	up(&parent->d_inode->i_sem);
	dput(parent);
	return d;
}

/**
 *	relayfs_create_file - create a file in the relay filesystem
 *	@name: the name of the file to create
 *	@parent: parent directory
 *	@mode: mode, if not specied the default perms are used
 *	@chan: channel associated with the file
 *
 *	Returns file dentry if successful, NULL otherwise.
 *
 *	The file will be created user r on behalf of current user.
 */
struct dentry *relayfs_create_file(const char *name, struct dentry *parent,
				   int mode, struct rchan *chan)
{
	if (!mode)
		mode = S_IRUSR;
	mode = (mode & S_IALLUGO) | S_IFREG;

	return relayfs_create_entry(name, parent, mode, chan);
}

/**
 *	relayfs_create_dir - create a directory in the relay filesystem
 *	@name: the name of the directory to create
 *	@parent: parent directory, NULL if parent should be fs root
 *
 *	Returns directory dentry if successful, NULL otherwise.
 *
 *	The directory will be created world rwx on behalf of current user.
 */
struct dentry *relayfs_create_dir(const char *name, struct dentry *parent)
{
	int mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;
	return relayfs_create_entry(name, parent, mode, NULL);
}

/**
 *	relayfs_remove - remove a file or directory in the relay filesystem
 *	@dentry: file or directory dentry
 */
int relayfs_remove(struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;
	if (!parent)
		return -EINVAL;

	parent = dget(parent);
	down(&parent->d_inode->i_sem);
	if (dentry->d_inode) {
		simple_unlink(parent->d_inode, dentry);
		d_delete(dentry);
	}
	dput(dentry);
	up(&parent->d_inode->i_sem);
	dput(parent);

	simple_release_fs(&relayfs_mount, &relayfs_mount_count);

	return 0;
}

/**
 *	relayfs_remove_dir - remove a directory in the relay filesystem
 *	@dentry: directory dentry
 *
 *	Returns 0 if successful, negative otherwise.
 */
int relayfs_remove_dir(struct dentry *dentry)
{
	if (!dentry)
		return -EINVAL;

	return relayfs_remove(dentry);
}

/**
 *	relayfs_open - open file op for relayfs files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Increments the channel buffer refcount.
 */
int relayfs_open(struct inode *inode, struct file *filp)
{
	struct rchan_buf *buf = RELAYFS_I(inode)->buf;
	kref_get(&buf->kref);

	return 0;
}

/**
 *	relayfs_mmap - mmap file op for relayfs files
 *	@filp: the file
 *	@vma: the vma describing what to map
 *
 *	Calls upon relay_mmap_buf to map the file into user space.
 */
int relayfs_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct inode *inode = filp->f_dentry->d_inode;
	return relay_mmap_buf(RELAYFS_I(inode)->buf, vma);
}

/**
 *	relayfs_poll - poll file op for relayfs files
 *	@filp: the file
 *	@wait: poll table
 *
 *	Poll implemention.
 */
unsigned int relayfs_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct inode *inode = filp->f_dentry->d_inode;
	struct rchan_buf *buf = RELAYFS_I(inode)->buf;

	if (buf->finalized)
		return POLLERR;

	if (filp->f_mode & FMODE_READ) {
		poll_wait(filp, &buf->read_wait, wait);
		if (!relay_buf_empty(buf))
			mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}

/**
 *	relayfs_release - release file op for relayfs files
 *	@inode: the inode
 *	@filp: the file
 *
 *	Decrements the channel refcount, as the filesystem is
 *	no longer using it.
 */
int relayfs_release(struct inode *inode, struct file *filp)
{
	struct rchan_buf *buf = RELAYFS_I(inode)->buf;
	kref_put(&buf->kref, relay_remove_buf);

	return 0;
}

/**
 *	relayfs alloc_inode() implementation
 */
static struct inode *relayfs_alloc_inode(struct super_block *sb)
{
	struct relayfs_inode_info *p = kmem_cache_alloc(relayfs_inode_cachep, SLAB_KERNEL);
	if (!p)
		return NULL;
	p->buf = NULL;

	return &p->vfs_inode;
}

/**
 *	relayfs destroy_inode() implementation
 */
static void relayfs_destroy_inode(struct inode *inode)
{
	if (RELAYFS_I(inode)->buf)
		relay_destroy_buf(RELAYFS_I(inode)->buf);

	kmem_cache_free(relayfs_inode_cachep, RELAYFS_I(inode));
}

static void init_once(void *p, kmem_cache_t *cachep, unsigned long flags)
{
	struct relayfs_inode_info *i = p;
	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) == SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&i->vfs_inode);
}

struct file_operations relayfs_file_operations = {
	.open		= relayfs_open,
	.poll		= relayfs_poll,
	.mmap		= relayfs_mmap,
	.release	= relayfs_release,
};

static struct super_operations relayfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.alloc_inode	= relayfs_alloc_inode,
	.destroy_inode	= relayfs_destroy_inode,
};

static int relayfs_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode *inode;
	struct dentry *root;
	int mode = S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = RELAYFS_MAGIC;
	sb->s_op = &relayfs_ops;
	inode = relayfs_get_inode(sb, mode, NULL);

	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;

	return 0;
}

static struct super_block * relayfs_get_sb(struct file_system_type *fs_type,
					   int flags, const char *dev_name,
					   void *data)
{
	return get_sb_single(fs_type, flags, data, relayfs_fill_super);
}

static struct file_system_type relayfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "relayfs",
	.get_sb		= relayfs_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init init_relayfs_fs(void)
{
	int err;

	relayfs_inode_cachep = kmem_cache_create("relayfs_inode_cache",
				sizeof(struct relayfs_inode_info), 0,
				0, init_once, NULL);
	if (!relayfs_inode_cachep)
		return -ENOMEM;

	err = register_filesystem(&relayfs_fs_type);
	if (err)
		kmem_cache_destroy(relayfs_inode_cachep);

	return err;
}

static void __exit exit_relayfs_fs(void)
{
	unregister_filesystem(&relayfs_fs_type);
	kmem_cache_destroy(relayfs_inode_cachep);
}

module_init(init_relayfs_fs)
module_exit(exit_relayfs_fs)

EXPORT_SYMBOL_GPL(relayfs_open);
EXPORT_SYMBOL_GPL(relayfs_poll);
EXPORT_SYMBOL_GPL(relayfs_mmap);
EXPORT_SYMBOL_GPL(relayfs_release);
EXPORT_SYMBOL_GPL(relayfs_file_operations);
EXPORT_SYMBOL_GPL(relayfs_create_dir);
EXPORT_SYMBOL_GPL(relayfs_remove_dir);

MODULE_AUTHOR("Tom Zanussi <zanussi@us.ibm.com> and Karim Yaghmour <karim@opersys.com>");
MODULE_DESCRIPTION("Relay Filesystem");
MODULE_LICENSE("GPL");

