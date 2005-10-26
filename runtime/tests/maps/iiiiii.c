#include "runtime.h"

/* test of maps with 5 keys of int64 and value of int64 */
#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#define KEY2_TYPE INT64
#define KEY3_TYPE INT64
#define KEY4_TYPE INT64
#define KEY5_TYPE INT64
#include "map-gen.c"

#include "map.c"

int main ()
{
  struct map_node *ptr;
  MAP map = _stp_map_new_iiiiii(4);

  _stp_map_set_iiiiii (map,1,2,3,4,5, 10);
  _stp_map_set_iiiiii (map,10,20,30,40,50, 100);
  _stp_map_set_iiiiii (map,-1,-2,-3,-4,-5, -10);
  _stp_map_set_iiiiii (map,100,200,300,400,500, 1000);

  foreach (map, ptr)
    printf ("map[%lld, %lld, %lld, %lld, %lld] = %lld\n", 
	    _stp_key_get_int64(ptr,1),
	    _stp_key_get_int64(ptr,2),
	    _stp_key_get_int64(ptr,3),
	    _stp_key_get_int64(ptr,4),
	    _stp_key_get_int64(ptr,5),
	    _stp_get_int64(ptr));

  _stp_map_print(map,"%1d - %2d - %3d - %4d - %5d *** %d");
  _stp_map_del (map);
  return 0;
}
