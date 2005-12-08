#include "runtime.h"

/* test of _stp_pmap_size() */

/* It's not clear this would ever be used in the systemtap language. 
   It would be useful as an array of counters. */

#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#include "pmap-gen.c"

#include "map.c"

#define check(map,num)                          \
  {                                             \
    int size = _stp_pmap_size(map);              \
    if (size != num)							\
      printf("ERROR at line %d: expected size %d and got %d instead.\n", __LINE__, num, size); \
  }

int main ()
{
  PMAP map = _stp_pmap_new_ii(8);
  int64_t x;

  check(map,0);

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

  check(map,4*NR_CPUS-2);

  _stp_pmap_add_ii(map, 1, 1);
  _stp_pmap_add_ii(map, 3, 1);
  check(map,4*NR_CPUS);  

  _stp_pmap_add_ii(map, 5, 100);
  check(map,4*NR_CPUS+1);

  _processor_number = 1;
  _stp_pmap_add_ii(map, 5, 100);
  check(map,4*NR_CPUS+2);

  _stp_pmap_set_ii(map, 5, 0);
  check(map,4*NR_CPUS+1);

  _processor_number = 0;
  _stp_pmap_set_ii(map, 5, 0);
  check(map,4*NR_CPUS);

  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_pmap_set_ii(map, 1, 0);
    _stp_pmap_set_ii(map, 2, 0);
    _stp_pmap_set_ii(map, 3, 0);
    _stp_pmap_set_ii(map, 4, 0);
  }  
  _processor_number = 0;
  check(map,0);

  _stp_pmap_del (map);
  return 0;
}

