#include <linux/types.h>
#include <linux/ring_buffer.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/cpumask.h>

struct _stp_data_entry {
	size_t			len;
	unsigned char		buf[];
};

/*
 * Trace iterator - used by printout routines who present trace
 * results to users and which routines might sleep, etc:
 */
struct _stp_ring_buffer_data {
	int			cpu;
	u64			ts;
};

struct _stp_relay_data_type {
	struct ring_buffer *rb;
	struct _stp_ring_buffer_data rb_data;
	cpumask_var_t trace_reader_cpumask;
};
static struct _stp_relay_data_type _stp_relay_data;

/* _stp_poll_wait is a waitqueue for tasks blocked on
 * _stp_data_poll_trace() */
static DECLARE_WAIT_QUEUE_HEAD(_stp_poll_wait);

static void __stp_free_ring_buffer(void)
{
	free_cpumask_var(_stp_relay_data.trace_reader_cpumask);
	if (_stp_relay_data.rb)
		ring_buffer_free(_stp_relay_data.rb);
	_stp_relay_data.rb = NULL;
}

static int __stp_alloc_ring_buffer(void)
{
	int i;
	unsigned long buffer_size = _stp_bufsize;

	if (!alloc_cpumask_var(&_stp_relay_data.trace_reader_cpumask,
			       GFP_KERNEL))
		goto fail;
	cpumask_clear(_stp_relay_data.trace_reader_cpumask);

	if (buffer_size == 0) {
		dbug_trans(1, "using default buffer size...\n");
		buffer_size = _stp_nsubbufs * _stp_subbuf_size;
	}
	/* The number passed to ring_buffer_alloc() is per cpu.  Our
	 * 'buffer_size' is a total number of bytes to allocate.  So,
	 * we need to divide buffer_size by the number of cpus. */
	buffer_size /= num_online_cpus();
	dbug_trans(1, "%lu\n", buffer_size);
	_stp_relay_data.rb = ring_buffer_alloc(buffer_size, 0);
	if (!_stp_relay_data.rb)
		goto fail;

	dbug_trans(1, "size = %lu\n", ring_buffer_size(_stp_relay_data.rb));
	return 0;

fail:
	__stp_free_ring_buffer();
	return -ENOMEM;
}

static int _stp_data_open_trace(struct inode *inode, struct file *file)
{
	long cpu_file = (long) inode->i_private;

	/* We only allow for one reader per cpu */
	dbug_trans(1, "trace attach\n");
#ifdef STP_BULKMODE
	if (!cpumask_test_cpu(cpu_file, _stp_relay_data.trace_reader_cpumask))
		cpumask_set_cpu(cpu_file, _stp_relay_data.trace_reader_cpumask);
	else {
		dbug_trans(1, "returning EBUSY\n");
		return -EBUSY;
	}
#else
	if (!cpumask_empty(_stp_relay_data.trace_reader_cpumask)) {
		dbug_trans(1, "returning EBUSY\n");
		return -EBUSY;
	}
	cpumask_setall(_stp_relay_data.trace_reader_cpumask);
#endif
	file->private_data = inode->i_private;
	return 0;
}

static int _stp_data_release_trace(struct inode *inode, struct file *file)
{
	long cpu_file = (long) inode->i_private;
	dbug_trans(1, "trace detach\n");
#ifdef STP_BULKMODE
	cpumask_clear_cpu(cpu_file, _stp_relay_data.trace_reader_cpumask);
#else
	cpumask_clear(_stp_relay_data.trace_reader_cpumask);
#endif

	return 0;
}

size_t
_stp_event_to_user(struct ring_buffer_event *event, char __user *ubuf,
		   size_t cnt)
{
	int ret;
	struct _stp_data_entry *entry;

	dbug_trans(1, "event(%p), ubuf(%p), cnt(%lu)\n", event, ubuf, cnt);
	if (event == NULL || ubuf == NULL)
		return -EFAULT;

	entry = (struct _stp_data_entry *)ring_buffer_event_data(event);
	if (entry == NULL)
		return -EFAULT;

	/* We don't do partial entries - just fail. */
	if (entry->len > cnt)
		return -EBUSY;

	if (cnt > entry->len)
		cnt = entry->len;
	ret = copy_to_user(ubuf, entry->buf, cnt);
	if (ret)
		return -EFAULT;

	return cnt;
}

static ssize_t tracing_wait_pipe(struct file *filp)
{
	while (ring_buffer_empty(_stp_relay_data.rb)) {

		if ((filp->f_flags & O_NONBLOCK)) {
			dbug_trans(1, "returning -EAGAIN\n");
			return -EAGAIN;
		}

		/*
		 * This is a make-shift waitqueue. The reason we don't use
		 * an actual wait queue is because:
		 *  1) we only ever have one waiter
		 *  2) the tracing, traces all functions, we don't want
		 *     the overhead of calling wake_up and friends
		 *     (and tracing them too)
		 *     Anyway, this is really very primitive wakeup.
		 */
		set_current_state(TASK_INTERRUPTIBLE);

		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ/10);

		if (signal_pending(current)) {
			dbug_trans(1, "returning -EINTR\n");
			return -EINTR;
		}
	}

	dbug_trans(1, "returning 1\n");
	return 1;
}

static struct ring_buffer_event *
peek_next_event(int cpu, u64 *ts)
{
	return ring_buffer_peek(_stp_relay_data.rb, cpu, ts);
}

/* Find the next real event */
static struct ring_buffer_event *
_stp_find_next_event(long cpu_file)
{
	struct ring_buffer_event *event;

#ifdef STP_BULKMODE
	/*
	 * If we are in a per_cpu trace file, don't bother by iterating over
	 * all cpus and peek directly.
	 */
	if (ring_buffer_empty_cpu(_stp_relay_data.rb, (int)cpu_file))
		return NULL;
	event = peek_next_event(cpu_file, &_stp_relay_data.rb_data.ts);
	_stp_relay_data.rb_data.cpu = cpu_file;

	return event;
#else
	struct ring_buffer_event *next = NULL;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int cpu;

	for_each_possible_cpu(cpu) {

		if (ring_buffer_empty_cpu(_stp_relay_data.rb, cpu))
			continue;

		event = peek_next_event(cpu, &ts);

		/*
		 * Pick the event with the smallest timestamp:
		 */
		if (event && (!next || ts < next_ts)) {
			next = event;
			next_cpu = cpu;
			next_ts = ts;
		}
	}

	_stp_relay_data.rb_data.cpu = next_cpu;
	_stp_relay_data.rb_data.ts = next_ts;

	return next;
#endif
}


/*
 * Consumer reader.
 */
static ssize_t
_stp_data_read_trace(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	ssize_t sret;
	struct ring_buffer_event *event;
	long cpu_file = (long) filp->private_data;

	dbug_trans(1, "%lu\n", (unsigned long)cnt);

	sret = tracing_wait_pipe(filp);
	dbug_trans(1, "tracing_wait_pipe returned %ld\n", sret);
	if (sret <= 0)
		goto out;

	/* stop when tracing is finished */
	if (ring_buffer_empty(_stp_relay_data.rb)) {
		sret = 0;
		goto out;
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	dbug_trans(1, "sret = %lu\n", (unsigned long)sret);
	sret = 0;
	while ((event = _stp_find_next_event(cpu_file)) != NULL) {
		ssize_t len;

		len = _stp_event_to_user(event, ubuf, cnt);
		if (len <= 0)
			break;

		ring_buffer_consume(_stp_relay_data.rb,
				    _stp_relay_data.rb_data.cpu,
				    &_stp_relay_data.rb_data.ts);
		ubuf += len;
		cnt -= len;
		sret += len;
		if (cnt <= 0)
			break;
	}
out:
	return sret;
}


static unsigned int
_stp_data_poll_trace(struct file *filp, poll_table *poll_table)
{
	dbug_trans(1, "entry\n");
	if (! ring_buffer_empty(_stp_relay_data.rb))
		return POLLIN | POLLRDNORM;
	poll_wait(filp, &_stp_poll_wait, poll_table);
	if (! ring_buffer_empty(_stp_relay_data.rb))
		return POLLIN | POLLRDNORM;

	dbug_trans(1, "exit\n");
	return 0;
}

static struct file_operations __stp_data_fops = {
	.owner		= THIS_MODULE,
	.open		= _stp_data_open_trace,
	.release	= _stp_data_release_trace,
	.poll		= _stp_data_poll_trace,
	.read		= _stp_data_read_trace,
#if 0
	.splice_read	= tracing_splice_read_pipe,
#endif
};

/*
 * Here's how __STP_MAX_RESERVE_SIZE is figured.  The value of
 * BUF_PAGE_SIZE was gotten from the kernel's ring_buffer code.  It
 * is divided by 4, so we waste a maximum of 1/4 of the buffer (in
 * the case of a small reservation).
 */
#define __STP_MAX_RESERVE_SIZE ((/*BUF_PAGE_SIZE*/ 4080 / 4)	\
				- sizeof(struct _stp_data_entry) \
				- sizeof(struct ring_buffer_event))

/*
 * This function prepares the cpu buffer to write a sample.
 *
 * Struct op_entry is used during operations on the ring buffer while
 * struct op_sample contains the data that is stored in the ring
 * buffer. Struct entry can be uninitialized. The function reserves a
 * data array that is specified by size. Use
 * op_cpu_buffer_write_commit() after preparing the sample. In case of
 * errors a null pointer is returned, otherwise the pointer to the
 * sample.
 *
 */
static size_t
_stp_data_write_reserve(size_t size_request, void **entry)
{
	struct ring_buffer_event *event;
	struct _stp_data_entry *sde;

	if (entry == NULL)
		return -EINVAL;

	if (size_request > __STP_MAX_RESERVE_SIZE) {
		size_request = __STP_MAX_RESERVE_SIZE;
	}

	event = ring_buffer_lock_reserve(_stp_relay_data.rb,
					 sizeof(struct _stp_data_entry) + size_request,
					 0);
	if (unlikely(! event)) {
		dbug_trans(1, "event = NULL (%p)?\n", event);
		entry = NULL;
		return 0;
	}

	sde = (struct _stp_data_entry *)ring_buffer_event_data(event);
	sde->len = size_request;
	
	*entry = event;
	return size_request;
}

static unsigned char *_stp_data_entry_data(void *entry)
{
	struct ring_buffer_event *event = entry;
	struct _stp_data_entry *sde;

	if (event == NULL)
		return NULL;

	sde = (struct _stp_data_entry *)ring_buffer_event_data(event);
	return sde->buf;
}

static int _stp_data_write_commit(void *entry)
{
	int ret;
	struct ring_buffer_event *event = (struct ring_buffer_event *)entry;

	if (unlikely(! entry)) {
		dbug_trans(1, "entry = NULL, returning -EINVAL\n");
		return -EINVAL;
	}

	ret = ring_buffer_unlock_commit(_stp_relay_data.rb, event, 0);
	dbug_trans(1, "after commit, empty returns %d\n",
		   ring_buffer_empty(_stp_relay_data.rb));

	wake_up_interruptible(&_stp_poll_wait);
	return ret;
}


static struct dentry *__stp_entry[NR_CPUS] = { NULL };

static int _stp_transport_data_fs_init(void)
{
	int rc;
	long cpu;

	_stp_relay_data.rb = NULL;

	// allocate buffer
	dbug_trans(1, "entry...\n");
	rc = __stp_alloc_ring_buffer();
	if (rc != 0)
		return rc;

	// create file(s)
	for_each_online_cpu(cpu) {
		char cpu_file[9];	/* 5(trace) + 3(XXX) + 1(\0) = 9 */

		if (cpu > 999 || cpu < 0) {
			_stp_transport_data_fs_close();
			return -EINVAL;
		}
		sprintf(cpu_file, "trace%ld", cpu);
		__stp_entry[cpu] = debugfs_create_file(cpu_file, 0600,
						       _stp_get_module_dir(),
						       (void *)cpu,
						       &__stp_data_fops);

		if (!__stp_entry[cpu]) {
			pr_warning("Could not create debugfs 'trace' entry\n");
			__stp_free_ring_buffer();
			return -ENOENT;
		}
		else if (IS_ERR(__stp_entry[cpu])) {
			rc = PTR_ERR(__stp_entry[cpu]);
			pr_warning("Could not create debugfs 'trace' entry\n");
			__stp_free_ring_buffer();
			return rc;
		}

		__stp_entry[cpu]->d_inode->i_uid = _stp_uid;
		__stp_entry[cpu]->d_inode->i_gid = _stp_gid;

#ifndef STP_BULKMODE
		if (cpu != 0)
			break;
#endif
	}

	dbug_trans(1, "returning 0...\n");
	return 0;
}

static void _stp_transport_data_fs_start(void)
{
	/* Do nothing. */
}

static void _stp_transport_data_fs_stop(void)
{
	/* Do nothing. */
}

static void _stp_transport_data_fs_close(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (__stp_entry[cpu])
			debugfs_remove(__stp_entry[cpu]);
		__stp_entry[cpu] = NULL;
	}

	__stp_free_ring_buffer();
}

static void _stp_transport_data_fs_overwrite(int overwrite)
{
	/* FIXME: Just a place holder for now. */
}
