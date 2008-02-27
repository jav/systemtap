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

#ifdef DEBUG_TRANSPORT
#undef DEBUG_TRANSPORT
#define DEBUG_TRANSPORT 1
#else
#define DEBUG_TRANSPORT 0
#endif

#ifdef DEBUG_UNWIND
#undef DEBUG_UNWIND
#define DEBUG_UNWIND 2
#else
#define DEBUG_UNWIND 0
#endif

#ifdef DEBUG_SYMBOLS
#undef DEBUG_SYMBOLS
#define DEBUG_SYMBOLS 4
#else
#define DEBUG_SYMBOLS 0
#endif

#define DEBUG_TYPE (DEBUG_TRANSPORT|DEBUG_UNWIND|DEBUG_SYMBOLS)

#if DEBUG_TYPE > 0

#define dbug(type, args...) do {					\
		if ((type) & DEBUG_TYPE)				\
			_stp_dbug(__FUNCTION__, __LINE__, args);	\
	} while (0)

#define kbug(type, args...) do {					\
		if ((type) & DEBUG_TYPE) {				\
			printk("%s:%d ",__FUNCTION__, __LINE__);	\
			printk(args);					\
		}							\
	} while (0)

#else
#define dbug(type, args...) ;
#define kbug(type, args...) ;
#endif /* DEBUG_TYPE > 0 */

#endif /* _STP_DEBUG_H_ */
