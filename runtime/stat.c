/* -*- linux-c -*-
 * Statistics Aggregation
 * Copyright (C) 2005 Red Hat Inc.
 * Copyright (C) 2006 Intel Corporation
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */
#ifndef _STAT_C_
#define _STAT_C_

/** @file stat.c
 * @brief Statistics Aggregation
 */
/** @addtogroup stat Statistics Aggregation
 * The Statistics aggregations keep per-cpu statistics. You
 * must create all aggregations at probe initialization and it is
 * best to not read them until probe exit. If you must read them
 * while probes are running, the values may be slightly off due
 * to a probe updating the statistics of one cpu while another cpu attempts
 * to read the same data. This will also negatively impact performance.
 *
 * If you have a need to poll Stat data while probes are running, and
 * you want to be sure the data is accurate, you can do
 * @verbatim
#define NEED_STAT_LOCKS 1
@endverbatim
 * This will insert per-cpu spinlocks around all accesses to Stat data, 
 * which will reduce performance some.
 *
 * Stats keep track of count, sum, min and max. Average is computed
 * from the sum and count when required. Histograms are optional.
 * If you want a histogram, you must set "type" to HIST_LOG
 * or HIST_LINEAR when you call _stp_stat_init().
 *
 * @{
 */

#include "stat-common.c"

/* for the paranoid. */
#if NEED_STAT_LOCKS == 1
#define STAT_LOCK(st) spin_lock(&st->lock)
#define STAT_UNLOCK(st) spin_unlock(&st->lock)
#else
#define STAT_LOCK(st) ;
#define STAT_UNLOCK(st) ;
#endif

/** Stat struct for stat.c. Maps do not need this */
struct _Stat {
	struct _Hist hist;
	/* per-cpu data. allocated with alloc_percpu() */
	stat *sd;
	/* aggregated data */   
	stat *agg;  
};

typedef struct _Stat *Stat;


/** Initialize a Stat.
 * Call this during probe initialization to create a Stat.
 *
 * @param type HIST_NONE, HIST_LOG, or HIST_LINEAR
 *
 * For HIST_LOG, the following additional parametrs are required:
 * @param buckets - An integer specifying the number of buckets.
 *
 * For HIST_LINEAR, the following additional parametrs are required:
 * @param start - An integer. The start of the histogram.
 * @param stop - An integer. The stopping value. Should be > start.
 * @param interval - An integer. The interval. 
 */
Stat _stp_stat_init (int type, ...)
{
	int size, buckets=0, start=0, stop=0, interval=0;
	stat *sd, *agg;
	Stat st;

	if (type != HIST_NONE) {
		va_list ap;
		va_start (ap, type);
		
		if (type == HIST_LOG) {
			buckets = va_arg(ap, int);
		} else {
			start = va_arg(ap, int);
			stop = va_arg(ap, int);
			interval = va_arg(ap, int);
			/* FIXME. check interval != 0 and not too large */
			buckets = (stop - start) / interval;
			if ((stop - start) % interval) buckets++;
		}
		va_end (ap);
	}
	st = (Stat) kmalloc (sizeof(struct _Stat), GFP_KERNEL);
	if (st == NULL)
		return NULL;
	
	size = buckets * sizeof(int64_t) + sizeof(stat);	
	sd = (stat *) __alloc_percpu (size, 8);
	if (sd == NULL)
		goto exit1;

#if NEED_STAT_LOCKS == 1
	{
		int i;
		for_each_cpu(i) {
			stat *sdp = per_cpu_ptr (sd, i);
			sdp->lock = SPIN_LOCK_UNLOCKED;
		}
	}
#endif
	
	agg = (stat *)kmalloc (size, GFP_KERNEL);
	if (agg == NULL)
		goto exit2;

	st->hist.type = type;
	st->hist.start = start;
	st->hist.stop = stop;
	st->hist.interval = interval;
	st->hist.buckets = buckets;
	st->sd = sd;
	st->agg = agg;
	return st;

exit2:
	kfree (sd);
exit1:
	kfree (st);
	return NULL;
}

/** Add to a Stat.
 * Add an int64 to a Stat.
 *
 * @param st Stat
 * @param val Value to add
 */
void _stp_stat_add (Stat st, int64_t val)
{
	stat *sd = per_cpu_ptr (st->sd, get_cpu());
	STAT_LOCK(sd);
	__stp_stat_add (&st->hist, sd, val);
	STAT_UNLOCK(sd);
	put_cpu();
}

/** Get per-cpu Stats.
 * Gets the Stats for a specific CPU.
 *
 * If NEED_STAT_LOCKS is set, you MUST call STAT_UNLOCK()
 * when you are finished with the returned pointer.
 *
 * @param st Stat
 * @param cpu CPU number
 * @returns A pointer to a stat.
 */
stat *_stp_stat_get_cpu (Stat st, int cpu)
{
	stat *sd = per_cpu_ptr (st->sd, cpu);
	STAT_LOCK(sd);
	return sd;
}

static void _stp_stat_clear_data (Stat st, stat *sd)
{
        int j;
        sd->count = sd->sum = sd->min = sd->max = 0;
        if (st->hist.type != HIST_NONE) {
                for (j = 0; j < st->hist.buckets; j++)
                        sd->histogram[j] = 0;
        }
}

/** Get Stats.
 * Gets the aggregated Stats for all CPUs.
 *
 * If NEED_STAT_LOCKS is set, you MUST call STAT_UNLOCK()
 * when you are finished with the returned pointer.
 *
 * @param st Stat
 * @param clear Set if you want the data cleared after the read. Useful
 * for polling.
 * @returns A pointer to a stat.
 */
stat *_stp_stat_get (Stat st, int clear)
{
	int i, j;
	stat *agg = st->agg;
	STAT_LOCK(agg);
	_stp_stat_clear_data (st, agg);

	for_each_cpu(i) {
		stat *sd = per_cpu_ptr (st->sd, i);
		STAT_LOCK(sd);
		if (sd->count) {
			if (agg->count == 0) {
				agg->min = sd->min;
				agg->max = sd->max;
			}
			agg->count += sd->count;
			agg->sum += sd->sum;
			if (sd->max > agg->max)
				agg->max = sd->max;
			if (sd->min < agg->min)
				agg->min = sd->min;
			if (st->hist.type != HIST_NONE) {
				for (j = 0; j < st->hist.buckets; j++)
					agg->histogram[j] += sd->histogram[j];
			}
			if (clear)
				_stp_stat_clear_data (st, sd);
		}
		STAT_UNLOCK(sd);
	}
	return agg;
}


static void __stp_stat_print (char *fmt, Stat st, stat *sd, int cpu)
{
	int num;
	char *f = (char *)fmt;
	while (*f) {
		f = next_fmt (f, &num);
		_stp_stat_print_valtype (f, &st->hist, sd, cpu);
		if (*f)
			f++;
	}
	_stp_print_cstr ("\n");
	_stp_print_flush();	
}

/** Print per-cpu Stats.
 * Prints the Stats for each CPU.
 *
 * @param st Stat
 * @param fmt @ref format_string
 * @param clear Set if you want the data cleared after the read. Useful
 * for polling.
 */
void _stp_stat_print_cpu (Stat st, char *fmt, int clear)
{
	int i;
	for_each_cpu(i) {
		stat *sd = per_cpu_ptr (st->sd, i);
		STAT_LOCK(sd);
		__stp_stat_print (fmt, st, sd, i);
		if (clear)
			_stp_stat_clear_data (st, sd);
		STAT_UNLOCK(sd);
	}
}

/** Print Stats.
 * Prints the Stats.
 *
 * @param st Stat
 * @param fmt @ref format_string
 * @param clear Set if you want the data cleared after the read. Useful
 * for polling.
 */
void _stp_stat_print (Stat st, char *fmt, int clear)
{
	stat *agg = _stp_stat_get(st, clear);
	__stp_stat_print (fmt, st, agg, 0);
	STAT_UNLOCK(agg);
}

/** Clear Stats.
 * Clears the Stats.
 *
 * @param st Stat
 */
void _stp_stat_clear (Stat st)
{
	int i;
	for_each_cpu(i) {
		stat *sd = per_cpu_ptr (st->sd, i);
		STAT_LOCK(sd);
		_stp_stat_clear_data (st, sd);
		STAT_UNLOCK(sd);
	}
}
/** @} */
#endif /* _STAT_C_ */

