/* -*- linux-c -*- 
 * Memory allocation functions
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _ALLOC_C_
#define _ALLOC_C_

/* does this file really need to exist? */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
#define kmalloc_node(size,flags,node) kmalloc(size,flags)
#endif /* LINUX_VERSION_CODE */


#endif /* _ALLOC_C_ */
