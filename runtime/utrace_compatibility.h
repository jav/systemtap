/*
 * utrace compatibility defines and inlines
 * Copyright (C) 2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _UTRACE_COMPATIBILITY_H_
#define _UTRACE_COMPATIBILITY_H_

#include <linux/utrace.h>

#ifdef UTRACE_ACTION_RESUME

/* 
 * If UTRACE_ACTION_RESUME is defined after including utrace.h, we've
 * got the original version of utrace.  So that utrace clients can be
 * written using the new interface (mostly), provide a (very thin)
 * compatibility layer that hides the differences.
 */

#define UTRACE_ORIG_VERSION

#define UTRACE_RESUME	UTRACE_ACTION_RESUME
#define UTRACE_DETACH	UTRACE_ACTION_DETACH
#define UTRACE_STOP	UTRACE_ACTION_QUIESCE

static inline struct utrace_attached_engine *
utrace_attach_task(struct task_struct *target, int flags,
		   const struct utrace_engine_ops *ops, void *data)
{
	return utrace_attach(target, flags, ops, data);
}

static inline int __must_check
utrace_control(struct task_struct *target,
	       struct utrace_attached_engine *engine,
	       unsigned long action)
{
	if (action == UTRACE_DETACH)
		return utrace_detach(target, engine);
	return -EINVAL;
}

static inline int __must_check
utrace_set_events(struct task_struct *target,
		  struct utrace_attached_engine *engine,
		  unsigned long eventmask)
{
	return utrace_set_flags(target, engine, eventmask);
}
#endif

#endif	/* _UTRACE_COMPATIBILITY_H_ */
