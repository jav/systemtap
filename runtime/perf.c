/* -*- linux-c -*- 
 * Perf Functions
 * Copyright (C) 2006 Red Hat Inc.
 * Copyright (C) 2010 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _PERF_C_
#define _PERF_C_

#include <linux/perf_event.h>

#include "perf.h"

/** @file perf.c
 * @brief Implements performance monitoring hardware support
 */

/** Initialize performance sampling
 * Call this during probe initialization to set up performance event sampling
 * for all online cpus.  Returns ERR_PTR on error.
 *
 * @param attr description of event to sample
 * @param callback function to call when perf event overflows
 * @param pp associated probe point
 * @param ph probe handler
 */
static Perf *_stp_perf_init (struct perf_event_attr *attr,
			     perf_overflow_handler_t callback,
			     const char *pp, void (*ph) (struct context *) )
{
	long rc = -EINVAL;
	int cpu;
	Perf *pe;

	pe = (Perf *) _stp_kmalloc (sizeof(Perf));
	if (pe == NULL)
		return ERR_PTR(-ENOMEM);

	/* allocate space for the event descriptor for each cpu */
	pe->pd = (perfcpu *) _stp_alloc_percpu (sizeof(perfcpu));
	if (pe->pd == NULL) {
		rc = -ENOMEM;
		goto exit1;
	}

	/* initialize event on each processor */
	stp_for_each_cpu(cpu) {
		perfcpu *pd = per_cpu_ptr (pe->pd, cpu);
		struct perf_event **event = &(pd->event);
		if (cpu_is_offline(cpu)) {
		 	*event = NULL;
			continue;
		}
		*event = perf_event_create_kernel_counter(attr, cpu, -1,
							  callback);

		if (IS_ERR(*event)) {
			rc = PTR_ERR(*event);
			*event = NULL;
			goto exit2;
		}
		pd->pp = pp;
		pd->ph = ph;
	}
	return pe;

exit2:
	stp_for_each_cpu(cpu) {
		perfcpu *pd = per_cpu_ptr (pe->pd, cpu);
		struct perf_event **event = &(pd->event);
		if (*event) perf_event_release_kernel(*event);
	}
	_stp_free_percpu(pe->pd);
exit1:
	_stp_kfree(pe);
	return ERR_PTR(rc);
}

/** Delete performance event.
 * Call this to shutdown performance event sampling
 *
 * @param pe
 */
static void _stp_perf_del (Perf *pe)
{
	if (pe) {
		int cpu;
		/* shut down performance event sampling */
		stp_for_each_cpu(cpu) {
			perfcpu *pd = per_cpu_ptr (pe->pd, cpu);
			struct perf_event **event = &(pd->event);
			if (*event) {
				perf_event_release_kernel(*event);
			}
		}
		_stp_free_percpu (pe->pd);
		_stp_kfree (pe);
	}
}

#endif /* _PERF_C_ */
