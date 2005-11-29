/* -*- linux-c -*- 
 * common stats functions for aggragations and maps
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAT_COMMON_C_
#define _STAT_COMMON_C_
#include "stat.h"

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
		_stp_print_cstr (s);
		num--;
	}
}

/* implements a log base 2 function, or Most Significant Bit */
/* with bits from 1 (lsb) to 64 (msb) */
/* msb64(0) = 0 */
/* msb64(1) = 1 */
/* msb64(8) = 4 */
/* msb64(512) = 10 */

static int msb64(int64_t val)
{
  int res = 64;

  if (val == 0)
    return 0;

  /* shortcut. most values will be 16-bit */
  if (val & 0xffffffffffff0000ull) {
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

  return res;
}

#ifndef HIST_WIDTH
#define HIST_WIDTH 50
#endif

static void _stp_stat_print_histogram (Hist st, stat *sd)
{
	int scale, i, j, val_space, cnt_space, 
		low_bucket = -1, high_bucket = 0;
	int64_t val, v, max = 0;

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
		if (low_bucket > 0)
			low_bucket--;
		
		if (high_bucket < (st->buckets-1))
			high_bucket++;
	}

	if (max <= HIST_WIDTH)
		scale = 1;
	else {
		int64_t tmp = max;
		int rem = do_div (tmp, HIST_WIDTH);
		scale = tmp;
		if (rem) scale++;
	}

	cnt_space = needed_space (max);
	if (st->type == HIST_LINEAR)
		val_space = needed_space (st->start +  st->interval * high_bucket);
	else
		val_space = needed_space (((int64_t)1) << high_bucket);
	//dbug ("max=%lld scale=%d val_space=%d\n", max, scale, val_space);

	/* print header */
	j = 0;
	if (val_space > 5)		/* 5 = sizeof("value") */
		j = val_space - 5;
	else
		val_space = 5;
	for ( i = 0; i < j; i++)
		_stp_print_cstr (" ");
	_stp_print_cstr("value |");
	reprint (HIST_WIDTH, "-");
	_stp_print_cstr (" count\n");
	_stp_print_flush();
	if (st->type == HIST_LINEAR)
		val = st->start;
	else
		val = 0;
	for (i = 0; i < st->buckets; i++) {
		if (i >= low_bucket && i <= high_bucket) {
			reprint (val_space - needed_space(val), " ");
			_stp_printf("%lld", val);
			_stp_print_cstr (" |");
			
			/* v = s->histogram[i] / scale; */
			v = sd->histogram[i];
			do_div (v, scale);
		
			reprint (v, "@");
			reprint (HIST_WIDTH - v + 1 + cnt_space - needed_space(sd->histogram[i]), " ");
			_stp_printf ("%lld\n", sd->histogram[i]);
		}
		if (st->type == HIST_LINEAR) 
			val += st->interval;
		else if (val == 0)
			val = 1;
		else
			val *= 2;
	}
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
		n = msb64 (val);
		if (n >= st->buckets)
			n = st->buckets - 1;
		sd->histogram[n]++;
		break;
	case HIST_LINEAR:
		if (val < st->start)
			val = st->start;
		else
			val -= st->start;
		do_div (val, st->interval);
		if (val >= st->buckets)
			val = st->buckets - 1;
		sd->histogram[val]++;
	default:
		break;
	}
}

#endif /* _STAT_COMMON_C_ */
