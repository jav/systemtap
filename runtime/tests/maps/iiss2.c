#include "runtime.h"

/* test of maps with keys of int64,int64,string and value of string */
#define VALUE_TYPE STRING
#define KEY1_TYPE INT64
#define KEY2_TYPE INT64
#define KEY3_TYPE STRING
#include "map-gen.c"

#include "map.c"

int main ()
{
  MAP map = _stp_map_new_iiss(4);
  map->wrap = 1;

  _stp_map_set_iiss (map, 1,2,"Ohio", "Columbus" );
  _stp_map_set_iiss (map, 3,4,"California", "Sacramento" );
  _stp_map_set_iiss (map, 5,6,"Washington", "Seattle" );
  _stp_map_set_iiss (map, 7,8,"Oregon", "Salem" );
  _stp_map_print (map, "map[%1d, %2d, %3s] = %s");

  _stp_map_set_iiss (map, -9,-10,"Nevada", "Carson City" );
  _stp_map_print (map, "map[%1d, %2d, %3s] = %s");

  _stp_map_set_iiss (map, 5,6,"Washington", "Olymp" );
  _stp_map_print (map, "map[%1d, %2d, %3s] = %s");

  _stp_map_add_iiss (map, 5,6,"Washington", "is" );
  _stp_map_print (map, "map[%1d, %2d, %3s] = %s");

  _stp_map_set_iiss (map, 5,6,"Washington", "Olympia" );
  _stp_map_print (map, "map[%1d, %2d, %3s] = %s");

  /* delete */
  _stp_map_set_iiss (map, -9,-10,"Nevada", 0);
  _stp_map_print (map, "map[%1d, %2d, %3s] = %s");

  /* should add nothing */
  _stp_map_set_iiss(map, 0,0,"", "");
  _stp_map_print (map, "map[%1d, %2d, %3s] = %s");

  _stp_map_del (map);
  return 0;
}
