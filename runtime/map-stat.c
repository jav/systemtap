/* -*- linux-c -*- 
 * map functions to handle statistics
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

/** @file map-stat.c
 * @brief Map functions to handle statistics.
 */

#include "stat-common.c"


static void _stp_map_print_histogram (MAP map, stat *sd)
{
	_stp_stat_print_histogram (&map->hist, sd);
}

static MAP _stp_map_new_hstat_log (unsigned max_entries, int key_size, int buckets)
{
	/* add size for buckets */
	int size = buckets * sizeof(int64_t) + sizeof(stat);
	MAP m = _stp_map_new (max_entries, STAT, key_size, size);
	if (m) {
		m->hist.type = HIST_LOG;
		m->hist.buckets = buckets;
		if (buckets < 1 || buckets > 64) {
			_stp_warn("histogram: Bad number of buckets.  Must be between 1 and 64\n");
			m->hist.type = HIST_NONE;
			return m;
		}
	}
	return m;
}

static MAP _stp_map_new_hstat_linear (unsigned max_entries, int ksize, int start, int stop, int interval)
{
	MAP m;
	int size;
	int buckets = (stop - start) / interval;
	if ((stop - start) % interval) buckets++;

        /* add size for buckets */
	size = buckets * sizeof(int64_t) + sizeof(stat);

	m = _stp_map_new (max_entries, STAT, ksize, size);
	if (m) {
		m->hist.type = HIST_LINEAR;
		m->hist.start = start;
		m->hist.stop = stop;
		m->hist.interval = interval;
		m->hist.buckets = buckets;
		if (m->hist.buckets <= 0) {
			_stp_warn("histogram: bad stop, start and/or interval\n");
			m->hist.type = HIST_NONE;
			return m;
		}
		
	}
	return m;
}

static PMAP _stp_pmap_new_hstat_linear (unsigned max_entries, int ksize, int start, int stop, int interval)
{
	PMAP pmap;
	int size;
	int buckets = (stop - start) / interval;
	if ((stop - start) % interval) buckets++;

        /* add size for buckets */
	size = buckets * sizeof(int64_t) + sizeof(stat);

	pmap = _stp_pmap_new (max_entries, STAT, ksize, size);
	if (pmap) {
		int i;
		MAP m;
		for_each_cpu(i) {
			m = (MAP)per_cpu_ptr (pmap->map, i);
			m->hist.type = HIST_LINEAR;
			m->hist.start = start;
			m->hist.stop = stop;
			m->hist.interval = interval;
			m->hist.buckets = buckets;
		}
		/* now set agg map  params */
		m = &pmap->agg;
		m->hist.type = HIST_LINEAR;
		m->hist.start = start;
		m->hist.stop = stop;
		m->hist.interval = interval;
		m->hist.buckets = buckets;
	}
	return pmap;
}

static PMAP _stp_pmap_new_hstat_log (unsigned max_entries, int key_size, int buckets)
{
	/* add size for buckets */
	int size = buckets * sizeof(int64_t) + sizeof(stat);
	PMAP pmap = _stp_pmap_new (max_entries, STAT, key_size, size);
	if (pmap) {
		int i;
		MAP m;
		for_each_cpu(i) {
			m = (MAP)per_cpu_ptr (pmap->map, i);
			m->hist.type = HIST_LOG;
			m->hist.buckets = buckets;
		}
		/* now set agg map  params */
		m = &pmap->agg;
		m->hist.type = HIST_LOG;
		m->hist.buckets = buckets;
	}
	return pmap;
}
