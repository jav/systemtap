/* -*- linux-c -*- 
 * common stats functions for aggragations and maps
 * Copyright (C) 2005, 2006, 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAT_COMMON_C_
#define _STAT_COMMON_C_
#include "stat.h"

static int _stp_stat_calc_buckets(int stop, int start, int interval)
{
	int buckets;

	if (interval == 0) {
		_stp_warn("histogram: interval cannot be zero.\n");
		return 0;
	}

	/* don't forget buckets for underflow and overflow */
	buckets = (stop - start) / interval + 3;

	if (buckets > STP_MAX_BUCKETS || buckets < 3) {
		_stp_warn("histogram: Number of buckets must be between 1 and %d\n"
			  "Number_of_buckets = (stop - start) / interval.\n"
			  "Please adjust your start, stop, and interval values.\n",
			  STP_MAX_BUCKETS-2);
		return 0;
	}
	return buckets;
}

static int needed_space(int64_t v)
{
	int space = 0;

	if (v == 0)
		return 1;

	if (v < 0) {
		space++;
		v = -v;
	}
	while (v) {
		/* v /= 10; */
		do_div (v, 10);
		space++;
	}
	return space;
}

static void reprint (int num, char *s)
{
	while (num > 0) {
		_stp_print(s);
		num--;
	}
}

/* Given a bucket number for a log histogram, return the value. */
static int64_t _stp_bucket_to_val(int num)
{
	if (num == HIST_LOG_BUCKET0)
		return 0;
	if (num < HIST_LOG_BUCKET0) {
		int64_t val = 0x8000000000000000LL;
		return  val >> num;
	} else
		return 1LL << (num - HIST_LOG_BUCKET0 - 1);
}

/* Implements a log base 2 function. Returns a bucket 
 * number from 0 to HIST_LOG_BUCKETS.
 */
static int _stp_val_to_bucket(int64_t val)
{
	int neg = 0, res = HIST_LOG_BUCKETS;
	
	if (val == 0)
		return HIST_LOG_BUCKET0;

	if (val < 0) {
		val = -val;
		neg = 1;
	}
	
	/* shortcut. most values will be 16-bit */
	if (unlikely(val & 0xffffffffffff0000ull)) {
		if (!(val & 0xffffffff00000000ull)) {
			val <<= 32;
			res -= 32;
		}
		
		if (!(val & 0xffff000000000000ull)) {
			val <<= 16;
			res -= 16;
		}
	} else {
		val <<= 48;
		res -= 48;
	}
	
	if (!(val & 0xff00000000000000ull)) {
		val <<= 8;
		res -= 8;
	}
	
	if (!(val & 0xf000000000000000ull)) {
		val <<= 4;
		res -= 4;
	}
	
	if (!(val & 0xc000000000000000ull)) {
		val <<= 2;
		res -= 2;
	}

	if (!(val & 0x8000000000000000ull)) {
		val <<= 1;
		res -= 1;
	}
	if (neg)
		res = HIST_LOG_BUCKETS - res;

	return res;
}

#ifndef HIST_WIDTH
#define HIST_WIDTH 50
#endif

#ifndef HIST_ELISION
#define HIST_ELISION 2 /* zeroes before and after */
#endif


static void _stp_stat_print_histogram (Hist st, stat *sd)
{
	int scale, i, j, val_space, cnt_space;
	int low_bucket = -1, high_bucket = 0, over = 0, under = 0;
	int64_t val, v, max = 0;
        int eliding = 0;

	if (st->type != HIST_LOG && st->type != HIST_LINEAR)
		return;

	/* Get the maximum value, for scaling. Also calculate the low
	   and high values to bound the reporting range. */
	for (i = 0; i < st->buckets; i++) {
		if (sd->histogram[i] > 0 && low_bucket == -1)
			low_bucket = i;
		if (sd->histogram[i] > 0)
			high_bucket = i;
		if (sd->histogram[i] > max)
			max = sd->histogram[i];
	}

	/* Touch up the bucket margin to show up to two zero-slots on
	   either side of the data range, seems aesthetically pleasant. */
	for (i = 0; i < 2; i++) {
		if (st->type == HIST_LOG) {
			/* For log histograms, don't go negative */
			/* unless there are negative values. */
			if (low_bucket != HIST_LOG_BUCKET0 && low_bucket > 0)
				low_bucket--;
		} else {
			if (low_bucket > 0)
				low_bucket--;
		}
		if (high_bucket < (st->buckets-1))
			high_bucket++;
	}
	if (st->type == HIST_LINEAR) {
		/* Don't include under or overflow if they are 0. */
		if (low_bucket == 0 && sd->histogram[0] == 0)
			low_bucket++;
		if (high_bucket == st->buckets-1 && sd->histogram[high_bucket] == 0)
			high_bucket--;
		if (low_bucket == 0)
			under = 1;
		if (high_bucket == st->buckets-1)
			over = 1;
	}
	
	if (max <= HIST_WIDTH)
		scale = 1;
	else {
		int64_t tmp = max;
		int rem = do_div (tmp, HIST_WIDTH);
		scale = tmp;
		if (rem) scale++;
	}

	/* count space */
	cnt_space = needed_space (max);

	/* Compute value space */
	if (st->type == HIST_LINEAR) {
		i = needed_space (st->start) + under;
		val_space = needed_space (st->start +  st->interval * high_bucket) + over;
	} else {
		i = needed_space(_stp_bucket_to_val(high_bucket));
		val_space = needed_space(_stp_bucket_to_val(low_bucket));
	}
	if (i > val_space)
		val_space = i;


	/* print header */
	j = 0;
	if (val_space > 5)		/* 5 = sizeof("value") */
		j = val_space - 5;
	else
		val_space = 5;
	for ( i = 0; i < j; i++)
		_stp_print(" ");
	_stp_print("value |");
	reprint (HIST_WIDTH, "-");
	_stp_print(" count\n");

        eliding=0;
	for (i = low_bucket;  i <= high_bucket; i++) {
		int over_under = 0;

                /* Elide consecutive zero buckets.  Specifically, skip
                   this row if it is zero and some of its nearest
                   neighbours are also zero.  */
                int k;
                int elide=1;
                for (k=i-HIST_ELISION; k<=i+HIST_ELISION; k++)
                  {
                    if (k >= 0 && k < st->buckets && sd->histogram[k] != 0)
                      elide = 0;
                  }
                if (elide)
                  { 
                    eliding = 1;
                    continue;
                  }

                /* State change: we have elided some rows, but now are about
                   to print a new one.  So let's print a mark on the vertical
                   axis to represent the missing rows.  */
                if (eliding)
                  {
                    reprint (val_space, " ");
                    _stp_print(" ~\n");
                    eliding = 0;
                  }


		if (st->type == HIST_LINEAR) {
			if (i == 0) {
				/* underflow */
				val = st->start;
				over_under = 1;
			} else if (i == st->buckets-1) {
				/* overflow */
				val = st->start + (i - 2) * st->interval;
				over_under = 1;
			} else				
				val = st->start + (i - 1) * st->interval;
		} else
			val = _stp_bucket_to_val(i);
		
		
		reprint (val_space - needed_space(val) - over_under, " ");

		if (over_under) {
			if (i == 0)
				_stp_printf("<%lld", val);
			else if (i == st->buckets-1)
				_stp_printf(">%lld", val);
			else
				_stp_printf("%lld", val);
		} else
			_stp_printf("%lld", val);
		_stp_print(" |");
		
		/* v = s->histogram[i] / scale; */
		v = sd->histogram[i];
		do_div (v, scale);
		
		reprint (v, "@");
		reprint (HIST_WIDTH - v + 1 + cnt_space - needed_space(sd->histogram[i]), " ");
		_stp_printf ("%lld\n", sd->histogram[i]);
	}
	_stp_print_char('\n');
	_stp_print_flush();
}

static void _stp_stat_print_valtype (char *fmt, Hist st, stat *sd, int cpu)
{
	switch (*fmt) {
	case 'C':
		_stp_printf("%lld", sd->count);
		break;
	case 'm':
		_stp_printf("%lld", sd->min);
		break;
	case 'M':
		_stp_printf("%lld", sd->max);
		break;
	case 'S':
		_stp_printf("%lld", sd->sum);
		break;
	case 'A':
	{
		int64_t avg = 0;
		if (sd->count)
			avg = _stp_div64 (NULL, sd->sum, sd->count);
		_stp_printf("%lld", avg);
		break;
	}
	case 'H':
		_stp_stat_print_histogram (st, sd);
		_stp_print_flush();
		break;
	case 'c':
		_stp_printf("%d", cpu);
		break;
	}
}

static void __stp_stat_add (Hist st, stat *sd, int64_t val)
{
	int n;
	if (sd->count == 0) {
		sd->count = 1;
		sd->sum = sd->min = sd->max = val;
	} else {
		sd->count++;
		sd->sum += val;
		if (val > sd->max)
			sd->max = val;
		if (val < sd->min)
			sd->min = val;
	}
	switch (st->type) {
	case HIST_LOG:
		n = _stp_val_to_bucket (val);
		if (n >= st->buckets)
			n = st->buckets - 1;
		sd->histogram[n]++;
		break;
	case HIST_LINEAR:
		val -= st->start;

		/* underflow */
		if (val < 0)
			val = 0;
		else {
			do_div (val, st->interval);
			val++;
		}

		/* overflow */
		if (val >= st->buckets - 1)
			val = st->buckets - 1;

		sd->histogram[val]++;
	default:
		break;
	}
}

#endif /* _STAT_COMMON_C_ */
