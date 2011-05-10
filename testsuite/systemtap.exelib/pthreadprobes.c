#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

pthread_mutex_t mutex;
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t rwlock;
pthread_cond_t cond;

static int do_output;

static void output(char *string)
{
 if (!do_output)
   return;

 printf("%s", string);
}

void *thread_func(void *p)
{
 output("thread_func\n");

 pthread_mutex_lock(&mutex);
 output("got mutex lock\n");

 sleep(5);

 pthread_mutex_unlock(&mutex);

 output("thread done\n");

 return NULL;
}

void *wrlock_thread(void *p)
{
 pthread_rwlock_rdlock(&rwlock);
 output("thread got read lock\n");

 sleep(2);
 pthread_rwlock_unlock(&rwlock);

 pthread_rwlock_wrlock(&rwlock);
 output("thread got write lock\n");

 return NULL;
}

void *condvar_thread(void *p)
{
 int i;

 pthread_mutex_lock(&count_mutex);

 for (i=0; i < 10; i++)
 {
  output("Going for sleep for 1 sec\n");
  sleep(1);
 }

 pthread_cond_signal(&cond);

 pthread_mutex_unlock(&count_mutex);

 return NULL;
}

void *condvar_wait_thread(void *p)
{
 pthread_mutex_lock(&count_mutex);
 pthread_cond_wait(&cond, &count_mutex);

 output("thread wakeup on cond var\n");

 pthread_mutex_unlock(&count_mutex);

 return NULL;
}


int main(int argc, char *argv[])
{
 pthread_t tid, tid2;
 struct timespec abs_time;

 do_output = (argc > 1 && strcmp("-v", argv[1]) == 0);

 /* Simple mutex lock */

 pthread_mutex_init(&mutex, NULL);

 pthread_mutex_lock(&mutex);

 pthread_create(&tid, NULL, thread_func, NULL);

 pthread_mutex_unlock(&mutex);

 sleep(1);

 while (1)
 {
  int ret;

  clock_gettime(CLOCK_REALTIME, &abs_time);
  abs_time.tv_sec += 1;

  ret = pthread_mutex_timedlock(&mutex, &abs_time);

  if (ret == ETIMEDOUT)
  {
   output("ETIMEDOUT\n");
  }
  else if (ret == 0)
  {
   output("pthread_mutex_timedlock succ.\n");
   break;
  }
 }

 pthread_mutex_destroy(&mutex);

 pthread_join(tid, NULL);

 /* Read-Write lock */

 pthread_rwlock_init(&rwlock, NULL);

 pthread_create(&tid, NULL, wrlock_thread, NULL);

 pthread_rwlock_rdlock(&rwlock);
 sleep(1);

 pthread_rwlock_unlock(&rwlock);

 pthread_join(tid, NULL);

 /* Condition Variable */

 pthread_cond_init(&cond, NULL);
 pthread_mutex_lock(&count_mutex);

 pthread_create(&tid, NULL, condvar_thread, NULL);

 pthread_cond_wait(&cond, &count_mutex);


 pthread_cond_init(&cond, NULL);
 pthread_mutex_init(&count_mutex, NULL);

 pthread_mutex_lock(&count_mutex);

 pthread_create(&tid, NULL, condvar_thread, NULL);

 clock_gettime(CLOCK_REALTIME, &abs_time);
 abs_time.tv_sec += 1;

 if (pthread_cond_timedwait(&cond, &count_mutex, &abs_time) == ETIMEDOUT)
   output("cond_timedwait ETIMEDOUT\n");

 /* main thread calls pthread_cond_broadcast() this time */

 pthread_cond_init(&cond, NULL);
 pthread_mutex_init(&count_mutex, NULL);

 pthread_create(&tid, NULL, condvar_wait_thread, NULL);
 pthread_create(&tid2, NULL, condvar_wait_thread, NULL);

 sleep(1);

 pthread_mutex_lock(&count_mutex);

 pthread_cond_broadcast(&cond);

 pthread_mutex_unlock(&count_mutex);

 pthread_join(tid, NULL);
 pthread_join(tid2, NULL);

 return 0;
}
