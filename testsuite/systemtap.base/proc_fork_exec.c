#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

int
main(int argc, char **argv)
{
    pid_t pid;
    int rstatus;
    char *new_argv[] = { NULL, "3", NULL };

    if (argc != 2) {
	fprintf(stderr, "Usage: %s exepath\n", argv[0]);
	return -1;
    }

    new_argv[0] = argv[1];

    pid = fork();
    if (pid < 0)
    {
	perror("fork");
	return -1;
    }

    if (pid == 0)			/* child process */
    {
	/* Actually run the command. */
	if (execv(new_argv[0], new_argv) < 0)
	    perror("execv");
	_exit(1);
    }

    if (waitpid(pid, &rstatus, 0) < 0) {
	perror("waitpid");
	return -1;
    }

    if (WIFEXITED(rstatus))
	return WEXITSTATUS(rstatus);
    return -1;
}
