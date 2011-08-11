#include <unistd.h>
#include <stdio.h>

int
main(int argc, char **argv)
{
    char *new_argv[] = { NULL, "3", NULL };

    if (argc != 2) {
	fprintf(stderr, "Usage: %s exepath\n", argv[0]);
	return -1;
    }

    new_argv[0] = argv[1];

    /* Actually run the command. */
    if (execv(new_argv[0], new_argv) < 0)
	perror("execv");
    _exit(1);
}
