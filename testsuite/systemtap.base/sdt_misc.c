#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifdef LOOP
static struct timespec req = {0, 200000000};
#endif

#ifndef ONLY_MAIN
#include "sdt_misc_.h"

void
sem_display ()
{
    printf("%s epilogue %s=%ld\n", (SDT_MISC_TEST_PROBE_0_ENABLED() ? "FAIL" : "PASS"), "test_probe_0_semaphore", SDT_MISC_TEST_PROBE_0_ENABLED());
    printf("%s epilogue %s=%ld\n", (SDT_MISC_TEST_PROBE_2_ENABLED() ? "FAIL" : "PASS"), "test_probe_2_semaphore", SDT_MISC_TEST_PROBE_2_ENABLED());
    printf("%s epilogue %s=%ld\n", (SDT_MISC_TEST_PROBE_3_ENABLED() ? "FAIL" : "PASS"), "test_probe_3_semaphore", SDT_MISC_TEST_PROBE_3_ENABLED());
 printf("%s epilogue %s=%ld\n", (SDT_MISC_TEST_PROBE_4_ENABLED() ? "FAIL" : "PASS"), "test_probe_4_semaphore", SDT_MISC_TEST_PROBE_4_ENABLED());
}

#ifdef LOOP
int
loop_check()
{
    return SDT_MISC_TEST_PROBE_0_ENABLED();
}
#endif

void
bar (int i)
{
#ifdef LOOP
  while (!loop_check())
    {
      nanosleep(&req, NULL);
    }
#endif
#ifndef NO_SLEEP
  sleep (3);
#endif

  SDT_MISC_TEST_PROBE_2(i);
  if (i == 0)
    i = 1000;
  if (SDT_MISC_TEST_PROBE_2_ENABLED())
     STAP_PROBE1(sdt_misc,test_probe_2,i);
}

void
baz (int i, char* s)
{
  if (SDT_MISC_TEST_PROBE_0_ENABLED())
     STAP_PROBE1(sdt_misc,test_probe_0,i);
  if (i == 0)
    i = 1000;
  if (SDT_MISC_TEST_PROBE_3_ENABLED())
     SDT_MISC_TEST_PROBE_3(i,s);
}

void
buz (int parm)
{
 struct astruct
  {
    int a;
    int b;
    int *c;
  };
  struct astruct bstruct = {parm, parm + 1};
  struct astruct *cstruct = &bstruct;
  if (parm == 0)
    parm = 1000;
  if (SDT_MISC_TEST_PROBE_4_ENABLED())
     DTRACE_PROBE1(sdt_misc,test_probe_4,&bstruct);
  SDT_MISC_TEST_PROBE_1(cstruct->c != ((void*)0));
}
#endif

#ifndef NO_MAIN
void int_handler(int sig)
{
  sem_display();
  exit(1);
}

void alrm_handler(int sig)
{
  exit (1);
}

#ifdef LOOP
  #include <signal.h>
#endif

int
main ()
{
#ifdef LOOP
  signal (SIGINT, &int_handler);
  // signal (SIGALRM, &alrm_handler);
  // alarm (300);
#endif
  bar(2);
  baz(3,(char*)"abc");
  buz(4);
#ifdef LOOP
  while (1) {nanosleep(&req, NULL);}
#endif
  return 0;
}
#endif
