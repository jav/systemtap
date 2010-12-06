/* Systemtap test case
 * Copyright (C) 2010, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include "dtrace_child_probes.h"

#if !defined(USE_SEMAPHORES)
#undef CHILD_MAIN_ENABLED
#define CHILD_MAIN_ENABLED() (1)
#endif

int
main(int argc, char **argv)
{
    if (CHILD_MAIN_ENABLED()) {
	CHILD_MAIN(getpid());
    }
    return 0;
}
