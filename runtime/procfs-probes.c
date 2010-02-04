#ifndef _STP_PROCFS_PROBES_C_
#define _STP_PROCFS_PROBES_C_

#include <linux/mutex.h>
#include <linux/fs.h>

#if 0
// Currently we have to output _stp_procfs_data early in the
// translation process.  It really should go here.
struct _stp_procfs_data {
	char *buffer;
	unsigned long count;
};
#endif

struct stap_procfs_probe {
	const char *path;
	const char *read_pp;
	void (*read_ph) (struct context*);
	const char *write_pp;
	void (*write_ph) (struct context*);

	// FIXME: Eventually, this could get bigger than MAXSTRINGLEN
	// when we support 'probe procfs("file").read.maxbuf(8192)'
	// (bug 10690).
	string_t buffer;
	size_t count;

	int needs_fill;
	struct mutex lock;
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16)
	atomic_t lockcount;
#endif
};

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,16)
/*
 * Kernels 2.6.16 or less don't really have mutexes.  The 'mutex_*'
 * functions are defined as their similar semaphore equivalents.
 * However, there is no semaphore equivalent of 'mutex_is_locked'.
 * So, we'll fake it with an atomic counter.
 */
static inline void _spp_lock_init(struct stap_procfs_probe *spp)
{
	atomic_set(&spp->lockcount, 0);
	mutex_init(&spp->lock);
}
static inline int _spp_trylock(struct stap_procfs_probe *spp)
{
	int ret = mutex_trylock(&spp->lock);
	if (ret) {
		atomic_inc(&spp->lockcount);
	}
	return(ret);
}
static inline void _spp_lock(struct stap_procfs_probe *spp)
{
	mutex_lock(&spp->lock);
	atomic_inc(&spp->lockcount);
}
static inline void _spp_unlock(struct stap_procfs_probe *spp)
{
	atomic_dec(&spp->lockcount);
	mutex_unlock(&spp->lock);
}
static inline void _spp_lock_shutdown(struct stap_procfs_probe *spp)
{
	if (atomic_read(&spp->lockcount) != 0) {
		_spp_unlock(spp);
	}
	mutex_destroy(&spp->lock);
}
#else /* LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16) */
#define _spp_lock_init(spp)	mutex_init(&(spp)->lock)
#define _spp_trylock(spp)	mutex_trylock(&(spp)->lock)
#define _spp_lock(spp)		mutex_lock(&(spp)->lock)
#define _spp_unlock(spp)	mutex_unlock(&(spp)->lock)
static inline void _spp_lock_shutdown(struct stap_procfs_probe *spp)
{
	if (mutex_is_locked(&spp->lock)) {
		mutex_unlock(&spp->lock);
	}
	mutex_destroy(&spp->lock);
}
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16) */

static int _stp_proc_fill_read_buffer(struct stap_procfs_probe *spp);

static int _stp_process_write_buffer(struct stap_procfs_probe *spp,
				     const char __user *buf, size_t count);

static int
_stp_proc_open_file(struct inode *inode, struct file *filp)
{
	struct stap_procfs_probe *spp;
	int err;

	spp = (struct stap_procfs_probe *)PDE(inode)->data;
	if (spp == NULL) {
		return -EINVAL;
	}

	err = generic_file_open(inode, filp);
	if (err)
		return err;

	/* To avoid concurrency problems, we only allow 1 open at a
	 * time.  (Grabbing a mutex here doesn't really work.  The
	 * debug kernel can OOPS with "BUG: lock held when returning
	 * to user space!".)
	 *
	 * If open() was called with
	 * O_NONBLOCK, don't block, just return EAGAIN. */
	if (filp->f_flags & O_NONBLOCK) {
		if (_spp_trylock(spp) == 0) {
			return -EAGAIN;
		}
	}
	else {
		_spp_lock(spp);
	}

	filp->private_data = spp;
	if ((filp->f_flags & O_ACCMODE) == O_RDONLY) {
		spp->buffer[0] = '\0';
		spp->count = 0;
		spp->needs_fill = 1;
	}
	return 0;
}

static int
_stp_proc_release_file(struct inode *inode, struct file *filp)
{
	struct stap_procfs_probe *spp;

	spp = (struct stap_procfs_probe *)filp->private_data;
	if (spp != NULL) {
		_spp_unlock(spp);
	}
	return 0;
}

static ssize_t
_stp_proc_read_file(struct file *file, char __user *buf, size_t count,
		    loff_t *ppos)
{
	struct stap_procfs_probe *spp = file->private_data;
	ssize_t retval = 0;

	/* If we don't have a probe read function, just return 0 to
	 * indicate there isn't any data here. */
	if (spp == NULL || spp->read_ph == NULL) {
		goto out;
	}

	/* If needed, fill up the buffer.*/
	if (spp->needs_fill) {
		if ((retval = _stp_proc_fill_read_buffer(spp))) {
			goto out;
		}
	}

	/* Return bytes from the buffer. */
	retval = simple_read_from_buffer(buf, count, ppos, spp->buffer,
					 spp->count);
out:
	return retval;
}

static ssize_t
_stp_proc_write_file(struct file *file, const char __user *buf, size_t count,
		     loff_t *ppos)
{
	struct stap_procfs_probe *spp = file->private_data;
	struct _stp_procfs_data pdata;
	ssize_t len;

	/* If we don't have a write probe, return EIO. */
	if (spp->write_ph == NULL) {
		len = -EIO;
		goto out;
	}

	/* Handle the input buffer. */
	len = _stp_process_write_buffer(spp, buf, count);
	if (len > 0) {
		*ppos += len;
	}

out:
	return len;
}

static struct file_operations _stp_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= _stp_proc_open_file,
	.read		= _stp_proc_read_file,
	.write		= _stp_proc_write_file,
	.llseek		= generic_file_llseek,
	.release	= _stp_proc_release_file
};

#endif /* _STP_PROCFS_PROBES_C_ */
