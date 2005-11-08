#include "runtime.h"

/* test of pmaps with keys of int64 and value of string */

/* It's not clear this would ever be used in the systemtap language. 
   It is not clear this would be useful. */

#define VALUE_TYPE STRING
#define KEY1_TYPE INT64
#include "pmap-gen.c"

#include "map.c"

int main ()
{
  MAP map = _stp_pmap_new_is(4);
  char *x;
  char buf[32];

  /* put some data in. _processor_number is a global hack that allows */
  /* us to set the current emulated cpu number for our userspace tests. */
  /* Note that we set values based on the cpu number just to show that */
  /* different values are stored in each cpu */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    sprintf(buf, "%d,", _processor_number);
    _stp_pmap_add_is(map, 1, buf);
    sprintf(buf, "%d,", 10 *_processor_number + 1);
    _stp_pmap_add_is(map, 2, buf);
    sprintf(buf, "%d,", _processor_number * _processor_number);
    _stp_pmap_add_is(map, 3, buf);
    _stp_pmap_add_is(map, 4, "1,");
  }
  
  /* read it back out and verify. Use the special get_cpu call to get non-aggregated data */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    x = _stp_pmap_get_cpu_is (map, 3);
    sprintf(buf, "%d,", _processor_number * _processor_number);
    if (strcmp(x, buf))
	printf("ERROR: Got %s when expected %s\n", x, buf);
    x = _stp_pmap_get_cpu_is (map, 1);
    sprintf(buf, "%d,", _processor_number);
    if (strcmp(x, buf))
	printf("ERROR: Got %s when expected %s\n", x, buf);
    x = _stp_pmap_get_cpu_is (map, 4);
    sprintf(buf, "%d,", 1);
    if (strcmp(x, buf))
	printf("ERROR: Got %s when expected %s\n", x, buf);
    x = _stp_pmap_get_cpu_is (map, 2);
    sprintf(buf, "%d,", 10 * _processor_number +1);
    if (strcmp(x, buf))
	printf("ERROR: Got %s when expected %s\n", x, buf);
  }

  /* now print the per-cpu data */
  for (_processor_number = 0; _processor_number < NR_CPUS; _processor_number++) {
    printf("CPU #%d\n", _processor_number);
    _stp_pmap_printn_cpu (map,0, "map[%1d] = %s", _processor_number);
  }  
  _processor_number = 0;

  /* print the aggregated data */
  _stp_pmap_print(map,"map[%1d] = %s");
 
  _stp_pmap_del (map);
  return 0;
}

