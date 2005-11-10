#include "runtime.h"

/* test of maps and pmaps with keys of int64 and value of int64 */

/* Make sure we can cleanly generate both */

#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#include "pmap-gen.c"

#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#include "map-gen.c"

#include "map.c"

int main ()
{
  MAP map = _stp_map_new_ii(4);
  MAP pmap = _stp_pmap_new_ii(4);
  int64_t x;

  /* put some data in. _processor_number is a global hack that allows */
  /* us to set the current emulated cpu number for our userspace tests. */
  /* Note that we set values based on the cpu number just to show that */
  /* different values are stored in each cpu */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    _stp_pmap_add_ii(pmap, 1, _processor_number);
    _stp_pmap_add_ii(pmap, 2, 10 *_processor_number + 1);
    _stp_pmap_add_ii(pmap, 3, _processor_number * _processor_number);
    _stp_pmap_add_ii(pmap, 4, 1);
    _stp_map_add_ii(map, 1, _processor_number);
    _stp_map_add_ii(map, 2, 10 *_processor_number + 1);
    _stp_map_add_ii(map, 3, _processor_number * _processor_number);
    _stp_map_add_ii(map, 4, 1);
  }
  
  _processor_number = 0;

  /* print the aggregated data */
  _stp_map_print(map,"map[%1d] = %d");
  _stp_pmap_print(pmap,"pmap[%1d] = %d");
 
  _stp_map_del (map);
  _stp_pmap_del (pmap);
  return 0;
}

