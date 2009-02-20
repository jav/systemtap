/* -*- linux-c -*- 
 * Perf Header File
 * Copyright (C) 2006 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _PERF_H_
#define _PERF_H_

/** @file perf.h
 * @brief Header file for performance monitoring hardware support
 */

static int _stp_perfmon_setup(void **desc,
		       struct pfarg_ctx *context,
		       struct pfarg_pmc pmc[], int pmc_count,
		       struct pfarg_pmd pmd[], int pmd_count);

static int _stp_perfmon_shutdown(void *desc);

static int64_t _stp_perfmon_read(void *desc, int counter);

#endif /* _PERF_H_ */
