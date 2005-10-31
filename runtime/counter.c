/* -*- linux-c -*- 
 * Counter aggregation Functions
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _COUNTER_C_
#define _COUNTER_C_

/** @file counter.c
 * @brief Counter Aggregation
 */

/** @addtogroup counter Counter Aggregation
 * This is a 64-bit per-cpu Counter.  It is much more efficient than an atomic counter
 * because there is no contention between processors and caches in an SMP system. Use
 * it when you want to count things and do not read the counter often.  Ideally you
 * should wait until probe exit time to read the Counter.
 * @{
 */

/* This define will probably go away with the next checkin. */
/* locks are only here for testing */
#ifndef NEED_COUNTER_LOCKS
#define NEED_COUNTER_LOCKS 0
#endif

#if NEED_COUNTER_LOCKS == 1
#define COUNTER_LOCK(c) spin_lock(&c->lock)
#define COUNTER_UNLOCK(c) spin_unlock(&c->lock)
#else
#define COUNTER_LOCK(c) ;
#define COUNTER_UNLOCK(c) ;
#endif

struct _counter {
	int64_t count;
#if NEED_COUNTER_LOCKS == 1
	spinlock_t lock;
#endif
};

typedef struct _counter *Counter;


/** Initialize a Counter.
 * Call this during probe initialization to create a Counter
 * 
 * @return a Counter. Will be NULL on error.
 */
Counter _stp_counter_init (void)
{
	Counter cnt = alloc_percpu (struct _counter);
#if NEED_COUNTER_LOCKS == 1
	{
		int i;
		for_each_cpu(i) {
			Counter c = per_cpu_ptr (cnt, i);
			c->lock = SPIN_LOCK_UNLOCKED;
		}
	}
#endif
	return cnt;
}

/** Add to a Counter.
 * Adds an int64 to a Counter
 * 
 * @param cnt Counter
 * @param val int64 value
 */
void _stp_counter_add (Counter cnt,  int64_t val)
{
	Counter c = per_cpu_ptr (cnt, get_cpu());
	COUNTER_LOCK(c);
	c->count += val;
	COUNTER_UNLOCK(c);
	put_cpu();
}

/** Get a Counter's per-cpu value.
 * Get the value of a Counter for a specific CPU.
 * 
 * @param cnt Counter
 * @param cpu CPU number
 * @param clear Set this to have the value cleared after reading.
 * @return An int64 value.
 */
int64_t _stp_counter_get_cpu (Counter cnt, int cpu, int clear)
{
	int64_t val;
	Counter c = per_cpu_ptr (cnt, cpu);
	COUNTER_LOCK(c);
	val = c->count;
	if (clear)
		c->count = 0;
	COUNTER_UNLOCK(c);
	return val;
}

/** Get a Counter's value.
 * Get the value of a Counter. This is the sum of the counters for
 * all CPUs. Because computing this sum requires reading all of the
 * per-cpu values, doing it often will result in poor performance in
 * multiprocessor systems.
 * 
 * The clear parameter is intended for use in a polling situation when the
 * values should be immediately cleared after reading.  
 * @param cnt Counter
 * @param clear Set this to have the value cleared after reading.
 * @return An int64 value.
 */
int64_t _stp_counter_get (Counter cnt, int clear)
{
	int i;
	int64_t sum = 0;

	for_each_cpu(i) {
		Counter c = per_cpu_ptr (cnt, i);
		COUNTER_LOCK(c);
		sum += c->count;
		if (clear)
			c->count = 0;
		COUNTER_UNLOCK(c);
	}
	return sum;
}

/** Free a Counter.
 * @param cnt Counter
 */
void _stp_counter_free (Counter cnt)
{
	free_percpu (cnt);
}

/** @} */

#undef COUNTER_LOCK
#undef COUNTER_UNLOCK
#endif /* _COUNTER_C_ */

