#include <linux/types.h>
#include <linux/ring_buffer.h>

static struct ring_buffer *__stp_ring_buffer = NULL;
//DEFINE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);

static void __stp_free_ring_buffer(void)
{
	if (__stp_ring_buffer)
		ring_buffer_free(__stp_ring_buffer);
	__stp_ring_buffer = NULL;
}

static int __stp_alloc_ring_buffer(void)
{
	int i;
	unsigned long buffer_size = _stp_bufsize;

	dbug_trans(1, "%lu\n", buffer_size);
	__stp_ring_buffer = ring_buffer_alloc(buffer_size, 0);
	if (!__stp_ring_buffer)
		goto fail;

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
#endif
	return 0;

fail:
	__stp_free_ring_buffer();
	return -ENOMEM;
}


static atomic_t _stp_trace_attached = ATOMIC_INIT(0);
static struct trace_iterator _stp_trace_iter;

static int _stp_data_open_trace(struct inode *inode, struct file *file)
{
	/* We only allow for one reader */
	dbug_trans(1, "trace attach\n");
	if (atomic_inc_return(&_stp_trace_attached) != 1) {
		atomic_dec(&_stp_trace_attached);
		dbug_trans(1, "returning EBUSY\n");
		return -EBUSY;
	}

	file->private_data = &_stp_trace_iter;
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

static void
trace_seq_reset(struct trace_seq *s)
{
	s->len = 0;
	s->readpos = 0;
}

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

static int trace_empty(struct trace_iterator *iter)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (iter->buffer_iter[cpu]) {
			if (!ring_buffer_iter_empty(iter->buffer_iter[cpu]))
				return 0;
#if 0
		} else {
			if (!ring_buffer_empty_cpu(iter->tr->buffer, cpu))
				return 0;
#endif
		}
	}

	return 1;
}

/* Must be called with trace_types_lock mutex held. */
static int tracing_wait_pipe(struct file *filp)
{
	struct trace_iterator *iter = filp->private_data;

	while (trace_empty(iter)) {

		if ((filp->f_flags & O_NONBLOCK)) {
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
#else
		if (iter->pos)
			break;
#endif

		continue;
	}

	return 1;
}

/*
 * Consumer reader.
 */
static ssize_t
_stp_data_read_trace(struct file *filp, char __user *ubuf,
		     size_t cnt, loff_t *ppos)
{
	struct trace_iterator *iter = filp->private_data;
	ssize_t sret;

	/* return any leftover data */
	dbug_trans(1, "%lu\n", (unsigned long)cnt);
	sret = _stp_trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (sret != -EBUSY)
		return sret;

	trace_seq_reset(&iter->seq);

waitagain:
	sret = tracing_wait_pipe(filp);
	if (sret <= 0)
		goto out;

	/* stop when tracing is finished */
	if (trace_empty(iter)) {
		sret = 0;
		goto out;
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	/* reset all but tr, trace, and overruns */
	memset(&iter->seq, 0,
	       sizeof(struct trace_iterator) -
	       offsetof(struct trace_iterator, seq));
	iter->pos = -1;

#if 0
	while (find_next_entry_inc(iter) != NULL) {
		enum print_line_t ret;
		int len = iter->seq.len;

		ret = print_trace_line(iter);
		if (ret == TRACE_TYPE_PARTIAL_LINE) {
			/* don't print partial lines */
			iter->seq.len = len;
			break;
		}
		if (ret != TRACE_TYPE_NO_CONSUME)
			trace_consume(iter);

		if (iter->seq.len >= cnt)
			break;
	}
#endif

	/* Now copy what we have to the user */
	sret = _stp_trace_seq_to_user(&iter->seq, ubuf, cnt);
	if (iter->seq.readpos >= iter->seq.len)
		trace_seq_reset(&iter->seq);

	/*
	 * If there was nothing to send to user, inspite of consuming trace
	 * entries, go back to wait for more entries.
	 */
	if (sret == -EBUSY)
		goto waitagain;

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
	__stp_entry = debugfs_create_file("trace", 0600, _stp_get_module_dir(),
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

