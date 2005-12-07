#include "runtime.h"

/* test of pmaps with keys of string and value of int64 */

/* It's not clear this would ever be used in the systemtap language. 
   It would be useful as an array of counters. */

#define VALUE_TYPE INT64
#define KEY1_TYPE STRING
#include "pmap-gen.c"

#include "map.c"

int main ()
{
  PMAP map = _stp_pmap_new_si(4);
  int64_t x;

  if (!map)
    return -1;

  /* put some data in. _processor_number is a global hack that allows */
  /* us to set the current emulated cpu number for our userspace tests. */
  /* Note that we set values based on the cpu number just to show that */
  /* different values are stored in each cpu */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_pmap_add_si(map, "ONE", _processor_number);
    _stp_pmap_add_si(map, "TWO", 10 *_processor_number + 1);
    _stp_pmap_add_si(map, "THREE", _processor_number * _processor_number);
    _stp_pmap_add_si(map, "FOUR", 1);
  }
  
  /* read it back out and verify. Use the special get_cpu call to get non-aggregated data */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    x = _stp_pmap_get_cpu_si (map, "THREE");
    if (x != _processor_number * _processor_number)
      printf("ERROR: Got %lld when expected %lld\n", x, (long long)(_processor_number * _processor_number));
    x = _stp_pmap_get_cpu_si (map, "ONE");
    if (x != _processor_number)
      printf("ERROR: Got %lld when expected %lld\n", x, (long long)_processor_number);
    x = _stp_pmap_get_cpu_si (map, "TWO");
    if (x != 10 * _processor_number + 1)
      printf("ERROR: Got %lld when expected %lld\n", x, (long long)(10 * _processor_number + 1));
    x = _stp_pmap_get_cpu_si (map, "FOUR");
    if (x != 1LL)
      printf("ERROR: Got %lld when expected %lld\n", x, 1LL);
  }

  /* now print the per-cpu data */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    printf("CPU #%d\n", _processor_number);
    _stp_pmap_printn_cpu (map,0, "map[%1s] = %d", _processor_number);
  }  
  _processor_number = 0;

  /* print the aggregated data */
  _stp_pmap_print(map,"map[%1s] = %d");
 
  _stp_pmap_del (map);
  return 0;
}

