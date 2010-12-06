/* Systemtap test case
 * Copyright (C) 2010, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "dtrace_fork_parent_probes.h"

#if !defined(USE_SEMAPHORES)
#undef PARENT_MAIN_ENABLED
#define PARENT_MAIN_ENABLED() (1)
#undef PARENT_CHILD_ENABLED
#define PARENT_CHILD_ENABLED() (1)
#undef PARENT_CHILD_PID_ENABLED
#define PARENT_CHILD_PID_ENABLED() (1)
#undef PARENT_FINISHED_ENABLED
#define PARENT_FINISHED_ENABLED() (1)
#endif

int
main(int argc, char **argv)
{
    int pid;
    int rc = 0;
    char *child_argv[] = { argv[1], NULL };

    /* Create the child. */
    if (PARENT_MAIN_ENABLED()) {
	PARENT_MAIN(getpid());
    }
    pid = fork();
    if (pid == 0) {			/* child */
	if (PARENT_CHILD_ENABLED()) {
	    PARENT_CHILD(getpid());
	}
	rc = execve(argv[1], child_argv, NULL);
	_exit(rc);
    }

    if (PARENT_CHILD_PID_ENABLED()) {
	PARENT_CHILD_PID(pid);
    }
    if (pid < 0) {
	/* error */
	perror("fork failed:");
	rc = -1;
    }
    else {
	int status;

	waitpid(pid, &status, 0);
	rc = WEXITSTATUS(status);
    }

    if (PARENT_FINISHED_ENABLED()) {
	PARENT_FINISHED();
    }
    return rc;
}
