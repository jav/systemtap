/* Regression test for bugzilla 6850 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PASS_MARKER "./bz6850_pass"

/* All this in an attempt to defeat gcc's over-aggressive inlining... */
typedef pid_t (*forker)(int);
static forker call_chain[];

/*
 * Both parent and child return from fork2() and fork1().  Both
 * processes will hit the uretprobe trampolines.  The handlers should
 * run in the parent.  With the bug fix in place, the child will return
 * correctly and do the exec (but won't run the handlers).
 */
static pid_t fork2(int ignored)
{
	return fork();
}

static pid_t fork1(int func_index)
{
	++func_index;
	return call_chain[func_index](func_index);	/* fork2() */
}

static pid_t fork_and_exec2(int func_index)
{
	pid_t child;
	++func_index;
	child = call_chain[func_index](func_index);	/* fork1() */
	if (child == 0) {
		/* I'm the child.  Create the marker file.  */
		char *child_args[] = { "/bin/touch", PASS_MARKER, NULL };
		char *child_env[] = { NULL };
		execve(child_args[0], child_args, child_env);
		perror("execve");
		fprintf(stderr, "FAIL: child couldn't exec.\n");
		exit(2);
	}
	return child;
}

static pid_t fork_and_exec1(int func_index)
{
	++func_index;
	return call_chain[func_index](func_index);	/* fork_and_exec2() */
}

static forker call_chain[] = {
	fork_and_exec1,
	fork_and_exec2,
	fork1,
	fork2,
	NULL
};

main()
{
	pid_t child, wait_child;
	int status = 0;

	(void) unlink(PASS_MARKER);
	child = call_chain[0](0);	/* fork_and_exec1() */
	if (child < 0) {
		fprintf(stderr, "FAIL: fork_and_exec1() failed.\n");
		exit(1);
	}
	wait_child = wait(&status);
	if (wait_child != child) {
		fprintf(stderr, "FAIL: waited for %d but got %d\n",
						child, wait_child);
		exit(1);
	}
	if (WEXITSTATUS(status) != 0) {
		fprintf(stderr, "FAIL: child died with status = %d\n",
			WEXITSTATUS(status));
		exit(1);
	}
	exit(0);
}
