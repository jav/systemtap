#ifndef _STAT_COMMON_C_ /* -*- linux-c -*- */
#define _STAT_COMMON_C_

/* common stats functions for aggragations and maps */

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

static void _stp_stat_print_histogram (Stat st, stat *sd)
{
	int scale, i, j, val_space, cnt_space;
	int64_t val, v, max = 0;

	if (st->hist_type != HIST_LOG && st->hist_type != HIST_LINEAR)
		return;
	
	/* get the maximum value, for scaling */
	for (i = 0; i < st->hist_buckets; i++)
		if (sd->histogram[i] > max)
			max = sd->histogram[i];
	
	if (max <= HIST_WIDTH)
		scale = 1;
	else {
		int64_t tmp = max;
		int rem = do_div (tmp, HIST_WIDTH);
		scale = tmp;
		if (rem) scale++;
	}

	cnt_space = needed_space (max);
	if (st->hist_type == HIST_LINEAR)
		val_space = needed_space (st->hist_start +  st->hist_int * (st->hist_buckets - 1));
	else
		val_space = needed_space (1 << (st->hist_buckets - 1));
	dbug ("max=%lld scale=%d val_space=%d\n", max, scale, val_space);

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
	if (st->hist_type == HIST_LINEAR)
		val = st->hist_start;
	else
		val = 0;
	for (i = 0; i < st->hist_buckets; i++) {
		reprint (val_space - needed_space(val), " ");
		_stp_printf("%d", val);
		_stp_print_cstr (" |");

		/* v = s->histogram[i] / scale; */
		v = sd->histogram[i];
		do_div (v, scale);
		
		reprint (v, "@");
		reprint (HIST_WIDTH - v + 1 + cnt_space - needed_space(sd->histogram[i]), " ");
		_stp_printf ("%lld\n", sd->histogram[i]);
		if (st->hist_type == HIST_LINEAR) 
			val += st->hist_int;
		else if (val == 0)
			val = 1;
		else
			val *= 2;
	}
}

static void _stp_stat_print_valtype (char *fmt, Stat st, struct stat_data *sd, int cpu)
{
	dbug ("fmt=%c\n", *fmt);
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
		if (sd->count) {
			avg = sd->sum;
			do_div (avg, (int)sd->count); /* FIXME: check for overflow */
		}
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

static void __stp_stat_add (Stat st, struct stat_data *sd, int64_t val)
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
	switch (st->hist_type) {
	case HIST_LOG:
		n = msb64 (val);
		if (n >= st->hist_buckets)
			n = st->hist_buckets - 1;
		sd->histogram[n]++;
		break;
	case HIST_LINEAR:
		val -= st->hist_start;
		do_div (val, st->hist_int);
		n = val;
		if (n < 0)
			n = 0;
		if (n >= st->hist_buckets)
			n = st->hist_buckets - 1;
		sd->histogram[n]++;
	default:
		break;
	}
}

#endif /* _STAT_COMMON_C_ */
