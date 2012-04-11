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
 * for all online cpus.  Returns non-zero on error.
 *
 * @param stp Handle for the event to be registered.
 */
static long _stp_perf_init (struct stap_perf_probe *stp)
{
	int cpu;

	/* allocate space for the event descriptor for each cpu */
	stp->events = _stp_alloc_percpu (sizeof(struct perf_event*));
	if (stp->events == NULL) {
		return -ENOMEM;
	}

	/* initialize event on each processor */
	for_each_possible_cpu(cpu) {
		struct perf_event **event = per_cpu_ptr (stp->events, cpu);
		if (cpu_is_offline(cpu)) {
			*event = NULL;
			continue;
		}
		*event = perf_event_create_kernel_counter(&stp->attr,
							  cpu,
#if defined(STAPCONF_PERF_STRUCTPID) || defined (STAPCONF_PERF_COUNTER_CONTEXT)
                                                          NULL,
#else
                                                          -1,
#endif
							  stp->callback
#ifdef STAPCONF_PERF_COUNTER_CONTEXT
							  , NULL
#endif
							  );

		if (IS_ERR(*event)) {
			long rc = PTR_ERR(*event);
			*event = NULL;
			_stp_perf_del(stp);
			return rc;
		}
	}
	return 0;
}

/** Delete performance event.
 * Call this to shutdown performance event sampling
 *
 * @param stp Handle for the event to be unregistered.
 */
static void _stp_perf_del (struct stap_perf_probe *stp)
{
	if (stp && stp->events) {
		int cpu;
		/* shut down performance event sampling */
		for_each_possible_cpu(cpu) {
			struct perf_event **event = per_cpu_ptr (stp->events, cpu);
			if (*event) {
				perf_event_release_kernel(*event);
			}
		}
		_stp_free_percpu (stp->events);
		stp->events = NULL;
	}
}

#endif /* _PERF_C_ */
