#include "runtime.h"

/* test of pmaps with keys of int64 and value of stat */

#define VALUE_TYPE STAT
#define KEY1_TYPE INT64
#include "pmap-gen.c"

#include "map.c"

int main ()
{
  MAP map = _stp_pmap_new_ix(4, HIST_LINEAR, 0, 100, 10);
  int i;

  /* put some data in. _processor_number is a global hack that allows */
  /* us to set the current emulated cpu number for our userspace tests. */
  /* Note that we set values based on the cpu number just to show that */
  /* different values are stored in each cpu */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_pmap_add_ix(map, 1, _processor_number);
    _stp_pmap_add_ix(map, 2, 10 *_processor_number + 1);
    _stp_pmap_add_ix(map, 3, _processor_number * _processor_number);
    _stp_pmap_add_ix(map, 4, 1);
  }
  
  /* now print the per-cpu data */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    printf("CPU #%d\n", _processor_number);
    _stp_pmap_printn_cpu (map, 
			  0, 
			  "map[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M", 
			  _processor_number);
  }  
  _processor_number = 0;

  /* print the aggregated data */
  _stp_pmap_print(map,"map[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");

  /* now use GET */
  for (i = 1; i < 5; i++)
    printf("map[%d]  Sum = %lld\n", i, _stp_pmap_get_ix(map, i)->sum);
  printf("\n");

  /* delete an entry and repeat */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++)
    _stp_pmap_set_ix(map, 2, 0);
  _processor_number = 0;  

  /* print the aggregated data */
  _stp_pmap_print(map,"map[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");

  /* now use GET */
  for (i = 1; i < 5; i++) {
    stat *sd = _stp_pmap_get_ix(map, i);
    if (sd)
      printf("map[%d]  Sum = %lld\n", i, sd->sum);
  }
  printf("\n");

  _stp_pmap_del (map);
  return 0;
}

