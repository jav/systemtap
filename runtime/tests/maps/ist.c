#include "runtime.h"

/* test of maps with keys of int64 and value of stat */

#define KEY1_TYPE INT64
#include "map-keys.c"

#define VALUE_TYPE STAT
#include "map-values.c"

#include "map.c"

int main ()
{
  int i, j;
  MAP map = _stp_map_new_int64(4, HSTAT_LINEAR, 0, 100, 10 );
  MAP map2 = _stp_map_new_int64(4, HSTAT_LOG, 11);

  _stp_map_key_int64 (map, 3);
  for (i = 0; i < 100; i++)
    for (j = 0; j <= i*10 ; j++ )
      _stp_map_add_int64_stat (map, i);

  _stp_map_key_int64 (map, 2);
  for (i = 0; i < 10; i++)
    for (j = 0; j < 10 ; j++ )
      _stp_map_add_int64_stat (map, j * i );

  _stp_map_key_int64 (map, 1);
  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/10 ; j++ )  
      _stp_map_add_int64_stat (map, i);

  _stp_map_key_int64 (map2, 1);
  for (i = 0; i < 128; i++)
    for (j = 0; j < 128 ; j++ )
      _stp_map_add_int64_stat (map2, i);      

  _stp_map_key_int64 (map2, 2);
  for (i = 0; i < 1024; i++)
    for (j = 0; j < 1024 ; j++ )
      _stp_map_add_int64_stat (map2, i);      

  _stp_map_print (map, "map[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");
  _stp_map_print (map2, "map2[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");

  _stp_map_del (map);
  _stp_map_del (map2);
  return 0;
}
