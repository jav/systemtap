/* -*- linux-c -*- 
 * Statistics Header
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STAT_H_
#define _STAT_H_

#ifndef NEED_STAT_LOCKS
#define NEED_STAT_LOCKS 0
#endif

/** histogram type */
enum histtype { HIST_NONE, HIST_LOG, HIST_LINEAR };

/** Statistics are stored in this struct.  This is per-cpu or per-node data 
    and is variable length due to the unknown size of the histogram. */
struct stat_data {
	int64_t count;
	int64_t sum;
	int64_t min, max;
#if NEED_STAT_LOCKS == 1
	spinlock_t lock;
#endif
	int64_t histogram[];
};
typedef struct stat_data stat;

/** Information about the histogram data collected. This data 
    is global and not duplicated per-cpu. */

struct _Hist {
	enum histtype type;
	int start;
	int stop;
	int interval;
	int buckets;
};
typedef struct _Hist *Hist;

#endif /* _STAT_H_ */
