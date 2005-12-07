#include "runtime.h"

/* test of pmaps with keys of int64 and value of int64 */

/* It's not clear this would ever be used in the systemtap language. 
   It would be useful as an array of counters. */

#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#include "pmap-gen.c"

#include "map.c"

int main ()
{
  PMAP map = _stp_pmap_new_ii(4);
  int i;

  /* put some data in. _processor_number is a global hack that allows */
  /* us to set the current emulated cpu number for our userspace tests. */
  /* Note that we set values based on the cpu number just to show that */
  /* different values are stored in each cpu */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_pmap_add_ii(map, 1, _processor_number);
    _stp_pmap_add_ii(map, 2, 10 *_processor_number + 1);
    _stp_pmap_add_ii(map, 3, _processor_number * _processor_number);
    _stp_pmap_add_ii(map, 4, 1);
  }

  _processor_number = 0;  

  /* get the data with get calls. this is not very efficient */
  for (i = 1; i < 5; i++)
    printf("map[%d] = %lld\n", i, _stp_pmap_get_ii(map, i));
  printf("\n");

  /* do it again. test that the aggregation map got cleared */
  for (i = 1; i < 5; i++)
    printf("map[%d] = %lld\n", i, _stp_pmap_get_ii(map, i));
  printf("\n");

  /* print the aggregated data */
  _stp_pmap_print(map,"map[%1d] = %d");

  /* delete an entry and repeat */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++)
    _stp_pmap_set_ii(map, 2, 0);
  _processor_number = 0;  

  for (i = 1; i < 5; i++)
    printf("map[%d] = %lld\n", i, _stp_pmap_get_ii(map, i));
  printf("\n");

  _stp_pmap_print(map,"map[%1d] = %d");

  _stp_pmap_del (map);
  return 0;
}

