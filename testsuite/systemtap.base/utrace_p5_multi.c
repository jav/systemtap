#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

void *thread_function( void *ptr );

int
main()
{
    pthread_t thread1, thread2;
    int iret1, iret2;

    /* Create independent threads each of which will execute function */
    iret1 = pthread_create(&thread1, NULL, thread_function, (void*) 1);
    iret2 = pthread_create(&thread2, NULL, thread_function, (void*) 2);

    /* Wait till threads are complete before main continues. Unless we
     * wait we run the risk of executing an exit which will terminate
     * the process and all threads before the threads have
     * completed. */
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL); 

    printf("Thread 1 returns: %d\n", iret1);
    printf("Thread 2 returns: %d\n", iret2);
    exit(0);
}

void *thread_function(void *ptr)
{
    int tnum = (int)ptr;
    if (tnum == 1) {
	int fd = open("/dev/null", O_RDONLY);
	close(fd);
    }
}
