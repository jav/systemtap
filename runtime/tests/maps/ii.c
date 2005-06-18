#include "runtime.h"

/* test of maps with keys of int64 and value of int64 */
#define NEED_INT64_VALS
#define KEY1_TYPE INT64
#include "map-keys.c"

#include "map.c"

int main ()
{
  MAP map = _stp_map_new_int64(4, INT64);

  /* map[1] = 2 */
  _stp_map_key_int64 (map, 1);
  _stp_map_set_int64 (map, 2);
  printf ("map[%lld]=%lld\n", key1int(map->key), _stp_map_get_int64(map));
  _stp_map_print(map,"map[%1d] = %d");

  /* map[3] = 4 */
  /* try it with macros this time */
  _stp_map_key (map, 3);
  _stp_map_set (map, 4);  
  _stp_map_print(map,"map[%1d] = %d");

  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_key_int64 (map, 0);  
  _stp_map_key_del (map);
  _stp_map_key_int64 (map, 77);  
  _stp_map_key_del (map);
  _stp_map_key_del (map);
  _stp_map_set_int64 (map,1000000);

  _stp_map_print(map,"map[%1d] = %d");

  /* create and delete a key */
  _stp_map_key_int64 (map, 1024);
  _stp_map_set_int64 (map, 2048);  
  _stp_map_key_int64 (map, 1024);
  _stp_map_key_del (map);

  _stp_map_print(map,"map[%1d] = %d");

  /* create and delete a key again*/
  _stp_map_key_int64 (map, 1024);
  _stp_map_set_int64 (map, 2048);  
  _stp_map_key_del (map);

  _stp_map_print(map,"map[%1d] = %d");

  /* check that unset values are 0 */
  _stp_map_key_int64 (map, 5);
  printf ("map[%lld]=%lld\n", key1int(map->key), _stp_map_get_int64(map));

  /* map[5] = 6 */
  _stp_map_set (map, 6);
  _stp_map_print(map,"map[%1d] = %d");

  /* add 4 new entries, pushing the others out */
  int i;
  for (i = 6; i < 10; i++)
    {
      _stp_map_key_int64 (map, i);
      _stp_map_set_int64 (map, 100 + i);
    }

  _stp_map_print(map,"map[%1d] = %d");  

  /* 5, 382, 526, and 903 all hash to the same value (23) */
  /* use them to test the hash chain */
  _stp_map_key_int64 (map, 5); _stp_map_set_int64 (map, 1005);
  _stp_map_key_int64 (map, 382); _stp_map_set_int64 (map, 1382);
  _stp_map_key_int64 (map, 526); _stp_map_set_int64 (map, 1526);
  _stp_map_key_int64 (map, 903); _stp_map_set_int64 (map, 1903);

  _stp_map_print(map,"map[%1d] = %d");  

  /* now delete all 4 nodes, one by one */
  _stp_map_key_int64 (map, 382); _stp_map_key_del (map);

  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_key_int64 (map, 5); _stp_map_key_del (map);

  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_key_int64 (map, 903); _stp_map_key_del (map);

  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_key_int64 (map, 526); _stp_map_key_del (map);

  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_del (map);
  return 0;
}
