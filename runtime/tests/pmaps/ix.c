#include "runtime.h"

/* test of pmaps with keys of int64 and value of stat */

#define VALUE_TYPE STAT
#define KEY1_TYPE INT64
#include "pmap-gen.c"

#include "map.c"

int main ()
{
  PMAP map = _stp_pmap_new_ix(4, HIST_LINEAR, 0, 100, 10);
  int64_t x;

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
  
#if 0
  /* read it back out and verify. Use the special get_cpu call to get non-aggregated data */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    x = _stp_pmap_get_cpu_ix (map, 3);
    if (x != _processor_number * _processor_number)
      printf("ERROR: Got %lld when expected %lld\n", x, (long long)(_processor_number * _processor_number));
    x = _stp_pmap_get_cpu_ix (map, 1);
    if (x != _processor_number)
      printf("ERROR: Got %lld when expected %lld\n", x, (long long)_processor_number);
    x = _stp_pmap_get_cpu_ix (map, 2);
    if (x != 10 * _processor_number + 1)
      printf("ERROR: Got %lld when expected %lld\n", x, (long long)(10 * _processor_number + 1));
    x = _stp_pmap_get_cpu_ix (map, 4);
    if (x != 1LL)
      printf("ERROR: Got %lld when expected %lld\n", x, 1LL);
  }
#endif

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

  _stp_pmap_del (map);
  return 0;
}

