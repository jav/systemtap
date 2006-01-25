#include "runtime.h"

/* test of Stats */
#include "stat.c"

int main ()
{
  int i;
  struct stat_data *st;
  Stat st1 = _stp_stat_init(HIST_NONE);
  Stat st2 = _stp_stat_init(HIST_LOG, 7);
  Stat st3 = _stp_stat_init(HIST_LINEAR, 0, 100, 5);
  
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_stat_add (st1, _processor_number + 1);
    _stp_stat_add (st2, _processor_number + 10);
    _stp_stat_add (st3, _processor_number + 100);
  }
  _processor_number = 0;

  /* this is for internal testing only.  Not recommended */
  for (i = 0; i < NR_CPUS; i++) {
    st = _stp_stat_get_cpu(st1, i);
    printf ("st1[%d] = count: %lld  sum:%lld\n", i, st->count, st->sum);
    STAT_UNLOCK(st1);
    st = _stp_stat_get_cpu(st2, i);
    printf ("st2[%d] = count: %lld  sum:%lld\n", i, st->count, st->sum);
    STAT_UNLOCK(st2);
    st = _stp_stat_get_cpu(st3, i);
    printf ("st3[%d] = count: %lld  sum:%lld\n", i, st->count, st->sum);
    STAT_UNLOCK(st3);
  }
  _stp_printf ("--------------------\n");

  /* normal way to print per-cpu stats */
  _stp_stat_print_cpu (st1, "CPU: %c\tCount: %C\tSum: %S", 0);
  _stp_stat_print_cpu (st2, "CPU: %c\tCount: %C\tSum: %S", 0);
  _stp_stat_print_cpu (st3, "CPU: %c\tCount: %C\tSum: %S", 0);
  printf ("--------------------\n");

  /* basic aggregated stats */
  _stp_stat_print (st1, "Count: %C\tSum: %S", 0);
  _stp_stat_print (st2, "Count: %C\tSum: %S", 0);
  _stp_stat_print (st3, "Count: %C\tSum: %S", 0);
  printf ("--------------------\n");

  /* now print full stats */
  _stp_stat_print (st1, "count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 1);
  _stp_stat_print (st2, "count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 1);
  _stp_stat_print (st3, "count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 1);

  /* and print again, after they were cleared */
  _stp_stat_print (st1, "count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 1);
  _stp_stat_print (st2, "count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 1);
  _stp_stat_print (st3, "count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H", 1);

  _stp_print_flush();
  _stp_stat_del(st1);
  _stp_stat_del(st2);
  _stp_stat_del(st3);
  return 0;
}
