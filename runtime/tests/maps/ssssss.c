#include "runtime.h"

/* test of maps with keys 5 strings and values of string */
#define VALUE_TYPE STRING
#define KEY1_TYPE STRING
#define KEY2_TYPE STRING
#define KEY3_TYPE STRING
#define KEY4_TYPE STRING
#define KEY5_TYPE STRING
#include "map-gen.c"

#include "map.c"

int main ()
{
  struct map_node *ptr;
  MAP map = _stp_map_new_ssssss(4);

  _stp_map_set_ssssss (map, "1ABC", "2ABC", "3ABC", "4ABC", "5ABC", "666");
  _stp_map_set_ssssss (map, "1QRS", "2QRS", "3QRS", "4QRS", "5QRS", "777");
  _stp_map_set_ssssss (map, "1abc", "2abc", "3abc", "4abc", "5abc", "888");
  _stp_map_set_ssssss (map, "1XYZ", "2XYZ", "3XYZ", "4XYZ", "5XYZ", "999");

  foreach (map, ptr)
    printf ("map[%s, %s, %s, %s, %s] = %s\n", 
	    _stp_key_get_str(ptr,1),
	    _stp_key_get_str(ptr,2),
	    _stp_key_get_str(ptr,3),
	    _stp_key_get_str(ptr,4),
	    _stp_key_get_str(ptr,5),
	    _stp_get_str(ptr));


  _stp_map_print(map,"%1s and %2s and %3s and %4s and %5s  ---> %s");
  _stp_map_del (map);
  return 0;
}
