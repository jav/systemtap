/*
 * itest - timed test program for use with bench2
 * Copyright (C) 2007 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>

typedef unsigned long long uint64;
struct timeval tstart, tstop;
pthread_t tester[256];
sem_t go;
int ncpus;
int iterations;

void start(void)
{
  gettimeofday (&tstart, NULL);
}

uint64 usecs (struct timeval *tv)
{
  return tv->tv_sec * 1000000 + tv->tv_usec;
}

uint64 stop(void)
{
  gettimeofday (&tstop, NULL);
  return  usecs(&tstop) - usecs(&tstart);
}

void usage(char *name)
{
  printf ("Usage %s [num_threads] [total_iterations]\n", name);
  printf("%s will call sys_getuid() total_iterations [default 2,000,000]\n", name);
  printf("times divided across num_threads [default 1].\n\n");
  exit(1);
}


void *null_thread (void *data)
{
  int cpu;
  cpu_set_t cpu_mask;
  int thread_num  = (int)(long)data;

  cpu = thread_num % ncpus;
  CPU_ZERO(&cpu_mask);
  CPU_SET(cpu, &cpu_mask);
  if( sched_setaffinity( 0, sizeof(cpu_mask), &cpu_mask ) < 0 ) {
    perror("sched_setaffinity");
  }


  // fprintf(stderr, "starting thread %d on cpu %d num=%d\n", thread_num, cpu, iterations);
  while (sem_wait(&go) == -1) ;

  return NULL;
}

void *caller_thread (void *data)
{
  int i, cpu;
  cpu_set_t cpu_mask;
  int thread_num  = (int)(long)data;

  /* Force threads to be distributed across all cpus. */
  /* The scheduler would probably do the right thing without this. */
  cpu = thread_num % ncpus;
  CPU_ZERO(&cpu_mask);
  CPU_SET(cpu, &cpu_mask);
  if( sched_setaffinity( 0, sizeof(cpu_mask), &cpu_mask ) < 0 ) {
    perror("sched_setaffinity");
  }
  
  // fprintf(stderr, "starting thread %d on cpu %d num=%d\n", thread_num, cpu, iterations);
  while (sem_wait(&go) == -1) ;

  for (i = 0; i < iterations; i++)
    getuid();

  return NULL;
}

int main(int argc, char *argv[])
{
  int i, n = 1;
  uint64 nsecs, null_usecs, caller_usecs;
  int total_iterations = 2000000;

  if (argc > 3)
    usage(argv[0]);

  if (argc >= 2) {
    n = strtol(argv[1], NULL, 10);
    if (n <= 0)
      usage(argv[0]);
  }
  if (argc > 2) {
    total_iterations = strtol(argv[2], NULL, 10);  
    if (total_iterations < 100000)
      usage(argv[0]);
  }

  ncpus = sysconf(_SC_NPROCESSORS_ONLN);
  sem_init (&go, 0, 0);
  iterations = total_iterations/n;

  for (i = 0; i < n; i++) {
    if (pthread_create(&tester[i], NULL, null_thread, (void *)(long)i) < 0) {
      perror("Error creating thread");
      return -1;
    }
  }

  start();
  for (i = 0; i < n; i++)
    sem_post (&go);
  
  for (i = 0; i < n; i++)
    pthread_join(tester[i], NULL);

  null_usecs = stop();

  for (i = 0; i < n; i++) {
    if (pthread_create(&tester[i], NULL, caller_thread, (void *)(long)i) < 0) {
      perror("Error creating thread");
      return -1;
    }
  }

  start();
  for (i = 0; i < n; i++)
    sem_post (&go);
  
  for (i = 0; i < n; i++)
    pthread_join(tester[i], NULL);

  caller_usecs = stop();
  
  /* returns nanosecs per call  */
  nsecs = ((caller_usecs - null_usecs) * 1000LL) / ((uint64)(n * iterations));
  printf("%lld\n", nsecs);
  return 0;
}
