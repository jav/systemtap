#include "runtime.h"

/* test of reading key values */
#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#define KEY2_TYPE STRING
#define KEY3_TYPE STRING
#define KEY4_TYPE INT64
#include "map-gen.c"

#include "map.c"

int main ()
{
  struct map_node *ptr;
  MAP map = _stp_map_new_issii(4);

  _stp_map_set_issii (map, 0, "Boston", "MA", 1970, 5224303 );
  _stp_map_set_issii (map, 1, "Chicago", "IL", 2000, 8272768 );
  _stp_map_set_issii (map, -1, "unknown", "",  2010, 1000000000 );

  foreach (map, ptr)
    printf ("map[%lld, %s, %s, %lld] = %lld\n", 
	    _stp_key_get_int64(ptr,1),
	    _stp_key_get_str(ptr,2),
	    _stp_key_get_str(ptr,3), 
	    _stp_key_get_int64(ptr,4),
	    _stp_get_int64(ptr));

  /* get all the key and value types wrong */
  foreach (map, ptr)
    printf ("map[%s, %lld, %lld, %s] = %s\n", 
	    _stp_key_get_str(ptr,1),
	    _stp_key_get_int64(ptr,2),
	    _stp_key_get_int64(ptr,3), 
	    _stp_key_get_str(ptr,4),
	    _stp_get_str(ptr));

  _stp_map_del (map);
  return 0;
}
