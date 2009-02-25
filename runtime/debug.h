/* Systemtap Debug Macros
 * Copyright (C) 2008 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_DEBUG_H_
#define _STP_DEBUG_H_

/* These are always on.
 * _dbug() writes to systemtap stderr.
 * errk() writes to the system log.
 */

#define _dbug(args...) _stp_dbug(__FUNCTION__, __LINE__, args)

#define errk(args...) do {						\
		printk("Systemtap Error at %s:%d ",__FUNCTION__, __LINE__); \
		printk(args);						\
	} while (0)

/*
 * To use these, enable them from the command line when compiling. 
 * For example, "stap -DDEBUG_UNWIND=3"
 * will activate dbug_unwind() and print messages with level <= 3.
 */

/* Note: DEBUG_MEM is implemented in alloc.c */

#ifdef DEBUG_TRANS /* transport */
/* Note: transport is debugged using printk() */
#define dbug_trans(level, args...) do {					\
		if ((level) <= DEBUG_TRANS) {				\
			printk("%s:%d ",__FUNCTION__, __LINE__);	\
			printk(args);					\
		}							\
	} while (0)
#else
#define dbug_trans(level, args...) ;
#endif

#ifdef DEBUG_UNWIND /* stack unwinder */
#define dbug_unwind(level, args...) do {					\
		if ((level) <= DEBUG_UNWIND)				\
			_stp_dbug(__FUNCTION__, __LINE__, args);	\
	} while (0)
#else
#define dbug_unwind(level, args...) ;
#endif

#ifdef DEBUG_SYMBOLS
#define dbug_sym(level, args...) do {					\
		if ((level) <= DEBUG_SYMBOLS)				\
			_stp_dbug(__FUNCTION__, __LINE__, args);	\
	} while (0)
#else
#define dbug_sym(level, args...) ;
#endif

#endif /* _STP_DEBUG_H_ */
