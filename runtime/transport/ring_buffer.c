#include <linux/types.h>
#include <linux/ring_buffer.h>
#include <linux/wait.h>
#include <linux/poll.h>

static struct ring_buffer *__stp_ring_buffer = NULL;
//DEFINE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);

/* _stp_poll_wait is a waitqueue for tasks blocked on
 * _stp_data_poll_trace() */
static DECLARE_WAIT_QUEUE_HEAD(_stp_poll_wait);

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
#endif

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

	if (buffer_size == 0) {
		dbug_trans(1, "using default buffer size...\n");
		buffer_size = _stp_nsubbufs * _stp_subbuf_size;
	}
	/* The number passed to ring_buffer_alloc() is per cpu.  Our
	 * 'buffer_size' is a total number of bytes to allocate.  So,
	 * we need to divide buffer_size by the number of cpus. */
	buffer_size /= num_online_cpus();
	dbug_trans(1, "%lu\n", buffer_size);
	__stp_ring_buffer = ring_buffer_alloc(buffer_size, 0);
	if (!__stp_ring_buffer)
		goto fail;

	dbug_trans(1, "size = %lu\n", ring_buffer_size(__stp_ring_buffer));
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

static ssize_t tracing_wait_pipe(struct file *filp)
{
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
static struct _stp_entry *_stp_find_next_entry(void)
{
	return __find_next_entry(&_stp_iter.cpu, &_stp_iter.ts);
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
	while ((entry = _stp_find_next_entry()) != NULL) {
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


static unsigned int
_stp_data_poll_trace(struct file *filp, poll_table *poll_table)
{
	dbug_trans(1, "entry\n");
	if (! ring_buffer_empty(__stp_ring_buffer))
		return POLLIN | POLLRDNORM;
	poll_wait(filp, &_stp_poll_wait, poll_table);
	if (! ring_buffer_empty(__stp_ring_buffer))
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
	int ret;

	if (unlikely(! entry)) {
		dbug_trans(1, "entry = NULL, returning -EINVAL\n");
		return -EINVAL;
	}

	ret = ring_buffer_unlock_commit(__stp_ring_buffer, entry->event, 0);
	dbug_trans(1, "after commit, empty returns %d\n",
		   ring_buffer_empty(__stp_ring_buffer));

	wake_up_interruptible(&_stp_poll_wait);
	return ret;
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

