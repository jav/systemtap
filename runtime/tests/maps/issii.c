#include "runtime.h"

/* test of maps with keys of int64,string.string.int64 and value of int64 */
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

  _stp_map_set_issii (map, 1, "Boston", "MA", 1970, 5224303 );
  _stp_map_set_issii (map, 2, "Boston", "MA", 2000, 6057826 );
  _stp_map_set_issii (map, 3, "Chicago", "IL", 2000, 8272768 );

  foreach (map, ptr)
    printf ("map[%lld, %s, %s, %lld] = %lld\n", 
	    key1int(ptr), 
	    key2str(ptr), 
	    _stp_key_get_str(ptr,3), 
	    _stp_key_get_int64(ptr,4),
	    _stp_get_int64(ptr));


  _stp_map_print(map,"%1d. The population of %2s, %3s in %4d was %d");
  _stp_map_del (map);
  return 0;
}
