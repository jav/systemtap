#ifndef _STAT_H_ /* -*- linux-c -*- */
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

struct _Stat {
	enum histtype hist_type;
	int hist_start;
	int hist_stop;
	int hist_int;
	int hist_buckets;
	struct stat_data *sd;
	struct stat_data *agg;
};

typedef struct _Stat *Stat;

#endif /* _STAT_H_ */
