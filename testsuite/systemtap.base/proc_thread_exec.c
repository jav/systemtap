#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

char *new_argv[] = { NULL, "3", NULL };

void *tfunc(void *arg)
{
    /* Actually run the command. */
    if (execv(new_argv[0], new_argv) < 0)
	perror("execv");
    return NULL;
}
 
int
main(int argc, char **argv)
{
    pthread_t thr;

    if (argc != 2) {
	fprintf(stderr, "Usage: %s exepath\n", argv[0]);
	return -1;
    }

    new_argv[0] = argv[1];

    pthread_create(&thr, NULL, tfunc, NULL);
    pause();
    return 0;
}

