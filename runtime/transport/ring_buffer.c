#include <linux/types.h>
#include <linux/ring_buffer.h>

static struct ring_buffer *__stp_ring_buffer = NULL;
//DEFINE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);

#if 1
/*
 * Trace iterator - used by printout routines who present trace
 * results to users and which routines might sleep, etc:
 */
struct _stp_ring_buffer_iterator {
#if 0
	struct trace_array	*tr;
	struct tracer		*trace;
	void			*private;
	struct ring_buffer_iter	*buffer_iter[NR_CPUS];

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
	struct trace_entry	*ent;
#endif
	int			cpu;
	u64			ts;

#if 0
	unsigned long		iter_flags;
	loff_t			pos;
	long			idx;

	cpumask_var_t		started;
#endif
};
static struct _stp_ring_buffer_iterator _stp_iter;
#else
static struct ring_buffer_iter *__stp_ring_buffer_iter[NR_CPUS];
#endif

static void __stp_free_ring_buffer(void)
{
	int i;

#if 0
	if (__stp_ring_buffer) {
		ring_buffer_record_disable(__stp_ring_buffer);
		for_each_possible_cpu(i) {
			ring_buffer_record_disable_cpu(__stp_ring_buffer, i);
		}
	}
#endif

#if 0
	for_each_possible_cpu(i) {
		if (__stp_ring_buffer_iter[i])
			ring_buffer_read_finish(__stp_ring_buffer_iter[i]);
	}
#endif

	if (__stp_ring_buffer)
		ring_buffer_free(__stp_ring_buffer);
	__stp_ring_buffer = NULL;
}

static int __stp_alloc_ring_buffer(void)
{
	int i;
	unsigned long buffer_size = _stp_bufsize;

	if (buffer_size == 0)
		buffer_size = STP_BUFFER_SIZE;
	dbug_trans(1, "%lu\n", buffer_size);
	__stp_ring_buffer = ring_buffer_alloc(buffer_size, 0);
	if (!__stp_ring_buffer)
		goto fail;

#if 0
	dbug_trans(1, "enabling recording...\n");
	ring_buffer_record_enable(__stp_ring_buffer);
	for_each_possible_cpu(i) {
		ring_buffer_record_enable_cpu(__stp_ring_buffer, i);
	}
#endif


// DRS: do we need this?
#if 0
	for_each_possible_cpu(i) {
		struct oprofile_cpu_buffer *b = &per_cpu(cpu_buffer, i);

		b->last_task = NULL;
		b->last_is_kernel = -1;
		b->tracing = 0;
		b->buffer_size = buffer_size;
		b->sample_received = 0;
		b->sample_lost_overflow = 0;
		b->backtrace_aborted = 0;
		b->sample_invalid_eip = 0;
		b->cpu = i;
		INIT_DELAYED_WORK(&b->work, wq_sync_buffer);
	}

	/* Allocate the first page for all buffers */
	for_each_possible_cpu(i) {
		data = global_trace.data[i] = &per_cpu(global_trace_cpu, i);
		max_tr.data[i] = &per_cpu(max_data, i);
	}
#endif

#if 0
	for_each_possible_cpu(i) {
		__stp_ring_buffer_iter[i] =
			ring_buffer_read_start(__stp_ring_buffer, i);

		if (!__stp_ring_buffer_iter[i])
			goto fail;
	}
#endif

	return 0;

fail:
	__stp_free_ring_buffer();
	return -ENOMEM;
}


static atomic_t _stp_trace_attached = ATOMIC_INIT(0);

static int _stp_data_open_trace(struct inode *inode, struct file *file)
{
	/* We only allow for one reader */
	dbug_trans(1, "trace attach\n");
	if (atomic_inc_return(&_stp_trace_attached) != 1) {
		atomic_dec(&_stp_trace_attached);
		dbug_trans(1, "returning EBUSY\n");
		return -EBUSY;
	}

//	file->private_data = &_stp_trace_iter;
	return 0;
}

static int _stp_data_release_trace(struct inode *inode, struct file *file)
{
	dbug_trans(1, "trace detach\n");
	atomic_dec(&_stp_trace_attached);
	return 0;
}

struct trace_seq {
	unsigned char		buffer[PAGE_SIZE];
	unsigned int		len;
	unsigned int		readpos;
};

ssize_t
_stp_trace_seq_to_user(struct trace_seq *s, char __user *ubuf, size_t cnt)
{
	int len;
	int ret;

	dbug_trans(1, "s: %p\n", s);
	if (s == NULL)
		return -EFAULT;

	dbug_trans(1, "len: %d, readpos: %d, buffer: %p\n", s->len,
		   s->readpos, s->buffer);
	if (s->len <= s->readpos)
		return -EBUSY;

	len = s->len - s->readpos;
	if (cnt > len)
		cnt = len;
	ret = copy_to_user(ubuf, s->buffer + s->readpos, cnt);
	if (ret)
		return -EFAULT;

	s->readpos += len;
	return cnt;
}

size_t
_stp_entry_to_user(struct _stp_entry *entry, char __user *ubuf, size_t cnt)
{
	int ret;

	dbug_trans(1, "entry(%p), ubuf(%p), cnt(%lu)\n", entry, ubuf, cnt);
	if (entry == NULL || ubuf == NULL)
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

static void
trace_seq_reset(struct trace_seq *s)
{
	s->len = 0;
	s->readpos = 0;
}

#if 0
/*
 * Trace iterator - used by printout routines who present trace
 * results to users and which routines might sleep, etc:
 */
struct trace_iterator {
#if 0
	struct trace_array	*tr;
	struct tracer		*trace;
	void			*private;
#endif
	struct ring_buffer_iter	*buffer_iter[NR_CPUS];

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
#if 0
	struct trace_entry	*ent;
	int			cpu;
	u64			ts;

	unsigned long		iter_flags;
#endif
	loff_t			pos;
#if 0
	long			idx;

	cpumask_var_t		started;
#endif
};
#endif

#if 0
static int _stp_data_empty(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
#if 0
		if (__stp_ring_buffer_iter[cpu]) {
			if (!ring_buffer_iter_empty(__stp_ring_buffer_iter[cpu]))
				return 0;
		}
#else
		if (!ring_buffer_empty_cpu(iter->tr->buffer, cpu))
			return 0;
#endif
	}

	return 1;
}
#endif

/* Must be called with trace_types_lock mutex held. */
static ssize_t tracing_wait_pipe(struct file *filp)
{
//	while (_stp_data_empty()) {
	while (ring_buffer_empty(__stp_ring_buffer)) {

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
		//iter->tr->waiter = current;

		/* sleep for 100 msecs, and try again. */
		schedule_timeout(HZ/10);

		//iter->tr->waiter = NULL;

		if (signal_pending(current)) {
			dbug_trans(1, "returning -EINTR\n");
			return -EINTR;
		}

#if 0
		if (iter->trace != current_trace)
			return 0;

		/*
		 * We block until we read something and tracing is disabled.
		 * We still block if tracing is disabled, but we have never
		 * read anything. This allows a user to cat this file, and
		 * then enable tracing. But after we have read something,
		 * we give an EOF when tracing is again disabled.
		 *
		 * iter->pos will be 0 if we haven't read anything.
		 */
		if (!tracer_enabled && iter->pos)
			break;
#endif

		continue;
	}

	dbug_trans(1, "returning 1\n");
	return 1;
}

static struct _stp_entry *
peek_next_entry(int cpu, u64 *ts)
{
	struct ring_buffer_event *event;

	event = ring_buffer_peek(__stp_ring_buffer, cpu, ts);

	return event ? ring_buffer_event_data(event) : NULL;
}

static struct _stp_entry *
__find_next_entry(int *ent_cpu, u64 *ent_ts)
{
	struct _stp_entry *ent, *next = NULL;
	u64 next_ts = 0, ts;
	int next_cpu = -1;
	int cpu;

	for_each_possible_cpu(cpu) {

		if (ring_buffer_empty_cpu(__stp_ring_buffer, cpu))
			continue;

		ent = peek_next_entry(cpu, &ts);

		/*
		 * Pick the entry with the smallest timestamp:
		 */
		if (ent && (!next || ts < next_ts)) {
			next = ent;
			next_cpu = cpu;
			next_ts = ts;
		}
	}

	if (ent_cpu)
		*ent_cpu = next_cpu;

	if (ent_ts)
		*ent_ts = next_ts;

	return next;
}

/* Find the next real entry, and increment the iterator to the next entry */
static struct _stp_entry *find_next_entry_inc(void)
{
	return __find_next_entry(&_stp_iter.cpu, &_stp_iter.ts);

//	if (iter->ent)
//		trace_iterator_increment(iter);

//	return iter->ent ? iter : NULL;
}


/*
 * Consumer reader.
 */
static ssize_t
_stp_data_read_trace(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	ssize_t sret;
	struct _stp_entry *entry;

	dbug_trans(1, "%lu\n", (unsigned long)cnt);

	sret = tracing_wait_pipe(filp);
	dbug_trans(1, "tracing_wait_pipe returned %ld\n", sret);
	if (sret <= 0)
		goto out;

	/* stop when tracing is finished */
	if (ring_buffer_empty(__stp_ring_buffer)) {
		sret = 0;
		goto out;
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	dbug_trans(1, "sret = %lu\n", (unsigned long)sret);
	sret = 0;
	while ((entry = find_next_entry_inc()) != NULL) {
		ssize_t len;

		len = _stp_entry_to_user(entry, ubuf, cnt);
		if (len <= 0)
			break;

		ring_buffer_consume(__stp_ring_buffer, _stp_iter.cpu,
				    &_stp_iter.ts);
		ubuf += len;
		cnt -= len;
		sret += len;
		if (cnt <= 0)
			break;
	}
out:
	return sret;
}

static struct file_operations __stp_data_fops = {
	.owner = THIS_MODULE,
	.open = _stp_data_open_trace,
	.release = _stp_data_release_trace,
#if 0
	.poll		= tracing_poll_pipe,
#endif
	.read		= _stp_data_read_trace,
#if 0
	.splice_read	= tracing_splice_read_pipe,
#endif
};

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
static struct _stp_entry *
_stp_data_write_reserve(size_t size)
{
	struct ring_buffer_event *event;
	struct _stp_entry *entry;

	event = ring_buffer_lock_reserve(__stp_ring_buffer,
					 (sizeof(struct _stp_entry) + size),
					 0);
	if (unlikely(! event)) {
		dbug_trans(1, "event = NULL (%p)?\n", event);
		return NULL;
	}

	entry = ring_buffer_event_data(event);
	entry->event = event;
	entry->len = size;
	return entry;
}

static int _stp_data_write_commit(struct _stp_entry *entry)
{
    if (unlikely(! entry)) {
	    dbug_trans(1, "entry = NULL, returning -EINVAL\n");
		return -EINVAL;
    }

#if 0
	return ring_buffer_unlock_commit(__stp_ring_buffer, entry->event, 0);
#else
	{
	    int ret;
	    ret = ring_buffer_unlock_commit(__stp_ring_buffer, entry->event, 0);
	    dbug_trans(1, "after commit, empty returns %d\n",
		       ring_buffer_empty(__stp_ring_buffer));
	    return ret;
	}
#endif
}


static struct dentry *__stp_entry;

static int _stp_transport_data_fs_init(void)
{
	int rc;

	// allocate buffer
	dbug_trans(1, "entry...\n");
	rc = __stp_alloc_ring_buffer();
	if (rc != 0)
		return rc;

	// create file(s)
	__stp_entry = debugfs_create_file("trace0", 0600,
					  _stp_get_module_dir(),
					  NULL, &__stp_data_fops);
	if (!__stp_entry)
		pr_warning("Could not create debugfs 'trace' entry\n");
	else {
		__stp_entry->d_inode->i_uid = _stp_uid;
		__stp_entry->d_inode->i_gid = _stp_gid;
	}

	dbug_trans(1, "returning 0...\n");
	return 0;
}

static void _stp_transport_data_fs_close(void)
{
	if (__stp_entry)
		debugfs_remove(__stp_entry);
	__stp_entry = NULL;

	__stp_free_ring_buffer();
}

