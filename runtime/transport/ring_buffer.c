#include <linux/types.h>
#include <linux/ring_buffer.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/cpumask.h>

#ifndef STP_RELAY_TIMER_INTERVAL
/* Wakeup timer interval in jiffies (default 10 ms) */
#define STP_RELAY_TIMER_INTERVAL		((HZ + 99) / 100)
#endif

struct _stp_data_entry {
	size_t			len;
	unsigned char		buf[];
};

/*
 * Trace iterator - used by printout routines who present trace
 * results to users and which routines might sleep, etc:
 */
struct _stp_iterator {
#if 0
	struct trace_array	*tr;
	struct tracer		*trace;
	void			*private;
#endif
	int			cpu_file;
#if 0
	struct mutex		mutex;
#endif
	struct ring_buffer_iter	*buffer_iter[NR_CPUS];
#if 0
	unsigned long		iter_flags;

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
	struct trace_entry	*ent;
#endif
	int			cpu;
	u64			ts;
#if 0
	loff_t			pos;
	long			idx;

	cpumask_var_t		started;
#endif
};

/* In bulk mode, we need 1 'struct _stp_iterator' for each cpu.  In
 * 'normal' mode, we only need 1 'struct _stp_iterator' (since all
 * output is sent through 1 file). */
#ifdef STP_BULKMODE
#define NR_ITERS NR_CPUS
#else
#define NR_ITERS 1
#endif

struct _stp_relay_data_type {
	enum _stp_transport_state transport_state;
	struct ring_buffer *rb;
	struct _stp_iterator iter[NR_ITERS];
	cpumask_var_t trace_reader_cpumask;
	struct timer_list timer;
	int overwrite_flag;
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

	dbug_trans(0, "size = %lu\n", ring_buffer_size(_stp_relay_data.rb));
	return 0;

fail:
	__stp_free_ring_buffer();
	return -ENOMEM;
}

static int _stp_data_open_trace(struct inode *inode, struct file *file)
{
	struct _stp_iterator *iter = inode->i_private;
#ifdef STP_BULKMODE
	int cpu_file = iter->cpu_file;
#endif

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
	struct _stp_iterator *iter = inode->i_private;

	dbug_trans(1, "trace detach\n");
#ifdef STP_BULKMODE
	cpumask_clear_cpu(iter->cpu_file, _stp_relay_data.trace_reader_cpumask);
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
	if (event == NULL || ubuf == NULL) {
		dbug_trans(1, "returning -EFAULT(1)\n");
		return -EFAULT;
	}

	entry = (struct _stp_data_entry *)ring_buffer_event_data(event);
	if (entry == NULL) {
		dbug_trans(1, "returning -EFAULT(2)\n");
		return -EFAULT;
	}

	/* We don't do partial entries - just fail. */
	if (entry->len > cnt) {
		dbug_trans(1, "returning -EBUSY\n");
		return -EBUSY;
	}

#if defined(DEBUG_TRANS) && (DEBUG_TRANS >= 2)
	{
		char *last = entry->buf + (entry->len - 5);
		dbug_trans2("copying %.5s...%.5s\n", entry->buf, last);
	}
#endif

	if (cnt > entry->len)
		cnt = entry->len;
	ret = copy_to_user(ubuf, entry->buf, cnt);
	if (ret) {
		dbug_trans(1, "returning -EFAULT(3)\n");
		return -EFAULT;
	}

	return cnt;
}

static int _stp_ring_buffer_empty_cpu(struct _stp_iterator *iter)
{
	int cpu;

#ifdef STP_BULKMODE
	cpu = iter->cpu_file;
	if (iter->buffer_iter[cpu]) {
		if (ring_buffer_iter_empty(iter->buffer_iter[cpu]))
			return 1;
	}
	else {
		if (ring_buffer_empty_cpu(_stp_relay_data.rb, cpu))
			return 1;
	}
	return 0;
#else
	for_each_possible_cpu(cpu) {
		if (iter->buffer_iter[cpu]) {
			if (!ring_buffer_iter_empty(iter->buffer_iter[cpu]))
				return 0;
		}
		else {
			if (!ring_buffer_empty_cpu(_stp_relay_data.rb, cpu))
				return 0;
		}
	}
	return 1;
#endif
}

static int _stp_ring_buffer_empty(void)
{
	struct _stp_iterator *iter;
#ifdef STP_BULKMODE
	int cpu;

	for_each_possible_cpu(cpu) {
		iter = &_stp_relay_data.iter[cpu];
		if (! _stp_ring_buffer_empty_cpu(iter))
			return 0;
	}
	return 1;
#else
	iter = &_stp_relay_data.iter[0];
	return _stp_ring_buffer_empty_cpu(iter);
#endif
}

static void _stp_ring_buffer_iterator_increment(struct _stp_iterator *iter)
{
	if (iter->buffer_iter[iter->cpu])
		ring_buffer_read(iter->buffer_iter[iter->cpu], NULL);
}

static void _stp_ring_buffer_consume(struct _stp_iterator *iter)
{
	_stp_ring_buffer_iterator_increment(iter);
	ring_buffer_consume(_stp_relay_data.rb, iter->cpu, &iter->ts);
}

static ssize_t _stp_tracing_wait_pipe(struct file *filp)
{
	struct _stp_iterator *iter = filp->private_data;

	if (_stp_ring_buffer_empty_cpu(iter)) {
		if ((filp->f_flags & O_NONBLOCK)) {
			dbug_trans(1, "returning -EAGAIN\n");
			return -EAGAIN;
		}

		if (signal_pending(current)) {
			dbug_trans(1, "returning -EINTR\n");
			return -EINTR;
		}
		dbug_trans(1, "returning 0\n");
		return 0;
	}

	dbug_trans(1, "returning 1\n");
	return 1;
}

static struct ring_buffer_event *
_stp_peek_next_event(struct _stp_iterator *iter, int cpu, u64 *ts)
{
	if (iter->buffer_iter[cpu])
		return ring_buffer_iter_peek(iter->buffer_iter[cpu], ts);
	else
		return ring_buffer_peek(_stp_relay_data.rb, cpu, ts);
}

/* Find the next real event */
static struct ring_buffer_event *
_stp_find_next_event(struct _stp_iterator *iter)
{
	struct ring_buffer_event *event;
	int cpu_file = iter->cpu_file;

#ifdef STP_BULKMODE
	/*
	 * If we are in a per_cpu trace file, don't bother by iterating over
	 * all cpus and peek directly.
	 */
	if (ring_buffer_iter_empty(iter->buffer_iter[cpu_file]))
		return NULL;
	event = _stp_peek_next_event(iter, cpu_file, &iter->ts);
	iter->cpu = cpu_file;

	return event;
#else
	struct ring_buffer_event *next = NULL;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int cpu;

	for_each_possible_cpu(cpu) {
		if (iter->buffer_iter[cpu] == NULL)
			continue;

		if (ring_buffer_iter_empty(iter->buffer_iter[cpu]))
			continue;

		event = _stp_peek_next_event(iter, cpu, &ts);

		/*
		 * Pick the event with the smallest timestamp:
		 */
		if (event && (!next || ts < next_ts)) {
			next = event;
			next_cpu = cpu;
			next_ts = ts;
		}
	}

	iter->cpu = next_cpu;
	iter->ts = next_ts;
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
	struct _stp_iterator *iter = filp->private_data;
#ifdef STP_BULKMODE
	int cpu_file = iter->cpu_file;
#else
	int cpu;
#endif

	dbug_trans(1, "%lu\n", (unsigned long)cnt);

	sret = _stp_tracing_wait_pipe(filp);
	dbug_trans(1, "_stp_tracing_wait_pipe returned %ld\n", sret);
	if (sret <= 0)
		goto out;

#ifdef STP_BULKMODE
	iter->buffer_iter[cpu_file]
	    = ring_buffer_read_start(_stp_relay_data.rb, cpu_file);
	if (iter->buffer_iter[cpu_file] == NULL) {
		dbug_trans(0, "buffer_iter[%d] was NULL\n", cpu_file);
		goto out;
	}
#else
	for_each_possible_cpu(cpu) {
		iter->buffer_iter[cpu]
		    = ring_buffer_read_start(_stp_relay_data.rb, cpu);
	}
#endif
	iter->ts = 0;
	dbug_trans(0, "iterator(s) started\n");

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	dbug_trans(1, "sret = %lu\n", (unsigned long)sret);
	sret = 0;
	while ((event = _stp_find_next_event(iter)) != NULL) {
		ssize_t len;

		len = _stp_event_to_user(event, ubuf, cnt);
		if (len <= 0)
			break;

		_stp_ring_buffer_consume(iter);
		dbug_trans(1, "event consumed\n");
		ubuf += len;
		cnt -= len;
		sret += len;
		if (cnt <= 0)
			break;
	}

out:
#ifdef STP_BULKMODE
	if (iter->buffer_iter[cpu_file]) {
		ring_buffer_read_finish(iter->buffer_iter[cpu_file]);
		iter->buffer_iter[cpu_file] = NULL;
		dbug_trans(0, "iterator finished\n");
	}
#else
	for_each_possible_cpu(cpu) {
		if (iter->buffer_iter[cpu]) {
			ring_buffer_read_finish(iter->buffer_iter[cpu]);
			iter->buffer_iter[cpu] = NULL;
		}
	}
	dbug_trans(0, "iterator(s) finished\n");
#endif
	return sret;
}


static unsigned int
_stp_data_poll_trace(struct file *filp, poll_table *poll_table)
{
	struct _stp_iterator *iter = filp->private_data;

	dbug_trans(1, "entry\n");
	if (! _stp_ring_buffer_empty_cpu(iter))
		return POLLIN | POLLRDNORM;
	poll_wait(filp, &_stp_poll_wait, poll_table);
	if (! _stp_ring_buffer_empty_cpu(iter))
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

#ifdef STAPCONF_RING_BUFFER_FLAGS
	event = ring_buffer_lock_reserve(_stp_relay_data.rb,
					 (sizeof(struct _stp_data_entry)
					  + size_request), 0);
#else
	event = ring_buffer_lock_reserve(_stp_relay_data.rb,
					 (sizeof(struct _stp_data_entry)
					  + size_request));
#endif
	if (unlikely(! event)) {
		int cpu;
		struct _stp_iterator *iter;

		dbug_trans(0, "event = NULL (%p)?\n", event);
		if (! _stp_relay_data.overwrite_flag) {
			entry = NULL;
			return 0;
		}

		/* If we're in overwrite mode and all the buffers are
		 * full, take a event out of the buffer and consume it
		 * (throw it away).  This should make room for the new
		 * data. */
#ifdef STP_BULKMODE
		cpu = raw_smp_processor_id();
		iter = &_stp_relay_data.iter[cpu];
#else
		iter = &_stp_relay_data.iter[0];
#endif
		event = _stp_find_next_event(iter);
		if (event) {
			ssize_t len;

			sde = (struct _stp_data_entry *)ring_buffer_event_data(event);
			if (sde->len < size_request)
				size_request = sde->len;
			_stp_ring_buffer_consume(iter);

			/* Try to reserve again. */
#ifdef STAPCONF_RING_BUFFER_FLAGS
			event = ring_buffer_lock_reserve(_stp_relay_data.rb,
							 sizeof(struct _stp_data_entry) + size_request,
							 0);
#else
			event = ring_buffer_lock_reserve(_stp_relay_data.rb,
							 sizeof(struct _stp_data_entry) + size_request);
#endif
			dbug_trans(0, "overwritten event = 0x%p\n", event);
		}

		if (unlikely(! event)) {
			entry = NULL;
			return 0;
		}
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
	struct ring_buffer_event *event = (struct ring_buffer_event *)entry;

	if (unlikely(! entry)) {
		dbug_trans(1, "entry = NULL, returning -EINVAL\n");
		return -EINVAL;
	}

#if defined(DEBUG_TRANS) && (DEBUG_TRANS >= 2)
	{
		struct _stp_data_entry *sde = (struct _stp_data_entry *)ring_buffer_event_data(event);
		char *last = sde->buf + (sde->len - 5);
		dbug_trans2("commiting %.5s...%.5s\n", sde->buf, last);
	}
#endif

#ifdef STAPCONF_RING_BUFFER_FLAGS
	return ring_buffer_unlock_commit(_stp_relay_data.rb, event, 0);
#else
	return ring_buffer_unlock_commit(_stp_relay_data.rb, event);
#endif
}

static void __stp_relay_wakeup_timer(unsigned long val)
{
	if (waitqueue_active(&_stp_poll_wait)
	    && ! _stp_ring_buffer_empty())
		wake_up_interruptible(&_stp_poll_wait);
 	mod_timer(&_stp_relay_data.timer, jiffies + STP_RELAY_TIMER_INTERVAL);
}

static void __stp_relay_timer_start(void)
{
	init_timer(&_stp_relay_data.timer);
	_stp_relay_data.timer.expires = jiffies + STP_RELAY_TIMER_INTERVAL;
	_stp_relay_data.timer.function = __stp_relay_wakeup_timer;
	_stp_relay_data.timer.data = 0;
	add_timer(&_stp_relay_data.timer);
	smp_mb();
}

static void __stp_relay_timer_stop(void)
{
	del_timer_sync(&_stp_relay_data.timer);
}

static struct dentry *__stp_entry[NR_CPUS] = { NULL };

static int _stp_transport_data_fs_init(void)
{
	int rc;
	int cpu;

	_stp_relay_data.transport_state = STP_TRANSPORT_STOPPED;
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
		sprintf(cpu_file, "trace%d", cpu);
		__stp_entry[cpu] = debugfs_create_file(cpu_file, 0600,
						       _stp_get_module_dir(),
						       (void *)(long)cpu,
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
		__stp_entry[cpu]->d_inode->i_private = &_stp_relay_data.iter[cpu];

#ifndef STP_BULKMODE
		break;
#endif
	}

#ifdef STP_BULKMODE
	for_each_possible_cpu(cpu) {
		_stp_relay_data.iter[cpu].cpu_file = cpu;
		_stp_relay_data.iter[cpu].cpu = cpu;
	}
#endif

	dbug_trans(1, "returning 0...\n");
	_stp_relay_data.transport_state = STP_TRANSPORT_INITIALIZED;
	return 0;
}

static void _stp_transport_data_fs_start(void)
{
	if (_stp_relay_data.transport_state == STP_TRANSPORT_INITIALIZED) {
		__stp_relay_timer_start();
		_stp_relay_data.transport_state = STP_TRANSPORT_RUNNING;
	}
}

static void _stp_transport_data_fs_stop(void)
{
	if (_stp_relay_data.transport_state == STP_TRANSPORT_RUNNING) {
		__stp_relay_timer_stop();
		_stp_relay_data.transport_state = STP_TRANSPORT_STOPPED;
	}
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

static enum _stp_transport_state _stp_transport_get_state(void)
{
	return _stp_relay_data.transport_state;
}

static void _stp_transport_data_fs_overwrite(int overwrite)
{
	dbug_trans(0, "setting ovewrite to %d\n", overwrite);
	_stp_relay_data.overwrite_flag = overwrite;
}
