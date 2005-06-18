#include "runtime.h"

/* test of Counters */
#include "counter.c"

int main ()
{
  int i;
  Counter cnt1 = _stp_counter_init();
  Counter cnt2 = _stp_counter_init();

  /* testing _stp_counter_add().  These will only be correct if _stp_counter_init() */
  /* set all values to 0 and add works properly. */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_counter_add (cnt1, _processor_number + 1);
    _stp_counter_add (cnt2, _processor_number + 10);
  }
  
  /* testing _stp_counter_get_cpu() */
  for (i = 0; i < NR_CPUS; i++) {
    printf ("cnt1[%d] = %lld\n", i, _stp_counter_get_cpu(cnt1, i, 0));
    printf ("cnt2[%d] = %lld\n", i, _stp_counter_get_cpu(cnt2, i, 0));
  }

  /* testing _stp_counter_get() */
  printf ("cnt1 = %d\n", _stp_counter_get(cnt1, 0));
  printf ("cnt2 = %d\n", _stp_counter_get(cnt2, 0));
  printf ("--------------------\n");

  /* testing _stp_counter_add() */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_counter_add (cnt1, _processor_number + 1);
    _stp_counter_add (cnt2, _processor_number + 10);
  }

  for (i = 0; i < NR_CPUS; i++) {
    printf ("cnt1[%d] = %lld\n", i, _stp_counter_get_cpu(cnt1, i, 0));
    printf ("cnt2[%d] = %lld\n", i, _stp_counter_get_cpu(cnt2, i, 0));
  }
  printf ("cnt1 = %d\n", _stp_counter_get(cnt1, 1));
  printf ("cnt2 = %d\n", _stp_counter_get(cnt2, 1));

  printf ("--------------------\n");

  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_counter_add (cnt1, _processor_number * _processor_number);
    _stp_counter_add (cnt2, _processor_number * _processor_number * _processor_number);
  }

  printf ("cnt1 = %d\n", _stp_counter_get(cnt1, 1));
  printf ("cnt2 = %d\n", _stp_counter_get(cnt2, 1));
  printf ("cnt1 = %d\n", _stp_counter_get(cnt1, 0));
  printf ("cnt2 = %d\n", _stp_counter_get(cnt2, 0));
  printf ("--------------------\n");


  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_counter_add (cnt1, _processor_number * _processor_number);
    _stp_counter_add (cnt2, _processor_number * _processor_number * _processor_number);
  }

  for (i = 0; i < NR_CPUS; i++) {
    printf ("cnt1[%d] = %lld\n", i, _stp_counter_get_cpu(cnt1, i, 0));
    printf ("cnt2[%d] = %lld\n", i, _stp_counter_get_cpu(cnt2, i, 0));
  }
  printf ("cnt1 = %d\n", _stp_counter_get(cnt1, 0));
  printf ("cnt2 = %d\n", _stp_counter_get(cnt2, 0));

  _stp_counter_free (cnt1);
  _stp_counter_free (cnt2);
  return 0;
}
