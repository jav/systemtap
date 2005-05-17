#include "runtime.h"

/* test of maps with keys of int64,int64,string and value of string */

#define KEY1_TYPE INT64
#define KEY2_TYPE INT64
#define KEY3_TYPE STRING
#include "map-keys.c"

#define VALUE_TYPE STRING
#include "map-values.c"

#include "map.c"

int main ()
{
  MAP map = _stp_map_new_int64_int64_str(4, STRING);

  _stp_map_key_int64_int64_str (map, 1,2,"Ohio");
  _stp_map_set_str (map, "Columbus" );
  _stp_map_key_int64_int64_str (map, 3,4,"California");
  _stp_map_add_str (map, "Sacramento" );
  _stp_map_key_int64_int64_str (map, 5,6,"Washington");
  _stp_map_set_str (map, "Seattle" );
  _stp_map_key_int64_int64_str (map, 7,8,"Oregon");
  _stp_map_set_str (map, "Salem" );

  _stp_map_print (map, "map");

  _stp_map_key_int64_int64_str (map, -9,-10,"Nevada");
  _stp_map_set_str (map, "Carson City" );
  _stp_map_print (map, "map");

  _stp_map_key_int64_int64_str (map, 5,6,"Washington");  
  _stp_map_set (map, "Olymp" );
  _stp_map_print (map, "map");

  _stp_map_add_str (map, "ia" );
  _stp_map_print (map, "map");

  _stp_map_key_int64_int64_str (map, -9,-10,"Nevada");
  _stp_map_key_del (map);
  _stp_map_print (map, "map");

  _stp_map_key_int64_int64_str (map, 0,0,"");
  _stp_map_set_str (map, "XX" );
  _stp_map_print (map, "map");

  _stp_map_del (map);
  return 0;
}
