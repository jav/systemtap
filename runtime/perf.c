/* -*- linux-c -*- 
 * Perf Functions
 * Copyright (C) 2006 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _PERF_C_
#define _PERF_C_

#include <linux/perfmon.h>

#include "perf.h"

/** @file perf.c
 * @brief Implements performance monitoring hardware support
 */

/* TODO fix so this works on SMP machines
 * Need to do context load, register setup, and start on each processor
 *
 * Similarly need to stop and unload on each processor
 */

/* TODO make this work with sampling. There needs to be a help thread
 * handling the sampling. */


static int _stp_pfm_register_setup(void *desc,
		       struct pfarg_pmc pmc[], int pmc_count,
		       struct pfarg_pmd pmd[], int pmd_count)
{
	int err = 0;

	if (desc == 0) return -EINVAL;
	err = pfmk_write_pmcs(desc, pmc, pmc_count);
	if (err) return err;
	
	err = pfmk_write_pmds(desc, pmd, pmd_count);
	return err;
}

static struct completion c;
static struct pfarg_load load_args;
static struct pfarg_start start_args;

/** Sets up the performance monitoring hardware.
 * The locations desc and context point to are modified as
 * side-effects of the setup. desc is a unique pointer used
 * by the various routines.
 * @param desc pointer to void *, handle to describe perfmon config
 * @param context pointer to context information
 * @param pmc, pointer to array describing control register setup
 * @param pmc_count, number of entries in pmc
 * @param pmd, pointer to array describing data register setup
 * @param pmd_count, number of entries in pmd
 * @returns an int, 0 if no errors encountered during setup
 */
int _stp_perfmon_setup(void **desc,
		       struct pfarg_ctx *context,
		       struct pfarg_pmc pmc[], int pmc_count,
		       struct pfarg_pmd pmd[], int pmd_count)
{
	int err = 0;

	/* create a context */
	err = pfmk_create_context(context, NULL, 0, &c, desc, NULL);
	if (err) goto cleanup;

	/* set up the counters */
	err = _stp_pfm_register_setup(*desc, pmc, pmc_count, pmd, pmd_count);
	if (err) goto cleanup2;

	/* start measuring */
	err = pfmk_load_context(*desc, &load_args);
	if (err) {
		printk("pfmk_load_context error\n");
		goto cleanup2;
	}
	err = pfmk_start(*desc, &start_args);
	if (err) {
		printk("pfmk_start error\n");
		goto cleanup3;
	}

	return err;

cleanup3: pfmk_unload_context(*desc);
cleanup2: pfmk_close(*desc);
cleanup: *desc=NULL; 
	return err;
}

/** Shuts down the performance monitoring hardware.
 * @param desc unique pointer to describe configuration
 * @returns an int, 0 if no errors encountered during shutdown
 */
int _stp_perfmon_shutdown(void *desc)
{
	int err=0;

	if (desc == 0) return -EINVAL;
	/* stop the counters */
	err=pfmk_stop(desc);
	if (err) return err;
	err=pfmk_unload_context(desc);
	if (err) return err;
	err=pfmk_close(desc);
	return err;
}

/** Reads the performance counter
 * @param desc unique pointer to describe configuration
 * @returns an int64, raw value of counter
 */
int64_t _stp_perfmon_read(void *desc, int counter)
{
	struct pfarg_pmd storage;
	
	storage.reg_set = 0;
	storage.reg_num = counter;

	if ( desc != NULL) {
		if (pfmk_read_pmds(desc, &storage, 1))
			printk( "pfm_read_pmds error\n");
	}

	return storage.reg_value;
}

#endif /* _PERF_C_ */

