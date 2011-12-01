/* Systemtap test case
 * Copyright (C) 2010, Red Hat Inc.
 *                                                          
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sched.h>
#include <errno.h>
#include "dtrace_clone_probes.h"

#if !defined(CLONE_NEWPID)
#define CLONE_NEWPID 0
#endif

#define CLONE_FLAGS (CLONE_NEWPID|CLONE_FILES|CLONE_FS|CLONE_VM|SIGCHLD)

#if !defined(USE_SEMAPHORES)
#undef TEST_CHILD2_ENABLED
#define TEST_CHILD2_ENABLED() (1)
#undef TEST_CHILD1_ENABLED
#define TEST_CHILD1_ENABLED() (1)
#undef TEST_CHILD2_PID_ENABLED
#define TEST_CHILD2_PID_ENABLED() (1)
#undef TEST_MAIN_ENABLED
#define TEST_MAIN_ENABLED() (1)
#undef TEST_CHILD1_PID_ENABLED
#define TEST_CHILD1_PID_ENABLED() (1)
#undef TEST_MAIN2_ENABLED
#define TEST_MAIN2_ENABLED() (1)
#endif

void *stack_base2 = NULL;
void *stack_top2 = NULL;

static int uprobes_ns_child2(void *argv __attribute__((__unused__)))
{
    if (TEST_CHILD2_ENABLED()) {
	TEST_CHILD2(getpid());
    }
    _exit(0);
}

static int uprobes_ns_child1(void *argv __attribute__((__unused__)))
{
    int pid;
    int rc = 0;

    /* Create the second clone. */
    if (TEST_CHILD1_ENABLED()) {
	TEST_CHILD1(getpid());
    }

    pid = clone(uprobes_ns_child2, stack_top2, CLONE_FLAGS, NULL);
    if (TEST_CHILD2_PID_ENABLED()) {
	TEST_CHILD2_PID(pid);
    }
    if (pid < 0) {
	/* Error. */
	char *msg2 = "clone 2 failed\n";
	write(2, msg2, strlen(msg2));
	rc = errno;
    }
    else {
	int status;

	waitpid(pid, &status, 0);
	rc = WEXITSTATUS(status);
    }
    _exit(rc);
}

int
main(int argc, char **argv)
{
    long pagesize = sysconf(_SC_PAGESIZE);
    void *stack_base1, *stack_top1;
    int pid;
    int rc = 0;

    /* Allocate both stacks here. */
    stack_base1 = calloc(pagesize * 4, 1);
    if (stack_base1 == NULL) {
	perror("calloc 1 failed:");
	return -1;
    }
    stack_base2 = calloc(pagesize * 4, 1);
    if (stack_base2 == NULL) {
	perror("calloc 2 failed:");
	return -1;
    }

    /*
     * Get top of stacks.  According to the clone() manpage:
     *   Stacks grow downwards on all processors that run Linux
     *   (except the HP PA processors), so child_stack usually points
     *   to the topmost address of the memory space set up for the
     *   child stack. 
     */
    stack_top1 = stack_base1 + (pagesize * 4);
    stack_top2 = stack_base2 + (pagesize * 4);

    /* Create the first clone (which will create the 2nd). */
    if (TEST_MAIN_ENABLED()) {
	TEST_MAIN(getpid());
    }
    pid = clone(uprobes_ns_child1, stack_top1, CLONE_FLAGS, NULL);
    if (TEST_CHILD1_PID_ENABLED()) {
	TEST_CHILD1_PID(pid);
    }
    if (pid < 0) {
	/* error */
	perror("clone 1 failed:");
	rc = -1;
    }
    else {
	int status;

	/* Wait on the first clone to finish. */
	waitpid(pid, &status, 0);
	rc = WEXITSTATUS(status);
    }
    /* Cleanup */
    free(stack_base1);
    free(stack_base2);
    if (TEST_MAIN2_ENABLED()) {
	TEST_MAIN2();
    }

    return rc;
}
