#include "runtime.h"

/* test of maps with keys of string and value of int64 */

#define KEY1_TYPE STRING
#include "map-keys.c"

#define VALUE_TYPE INT64
#include "map-values.c"

#include "map.c"

int main ()
{
  MAP map = _stp_map_new_str(4, INT64);

  /* map[Ohio] = 1 */
  _stp_map_key_str (map, "Ohio");
  _stp_map_set_int64 (map, 1);
  printf ("map[%s]=%lld\n", key1str(map->key), _stp_map_get_int64(map));
  _stp_map_print(map,"map");

  /* map[Washington] = 2 */
  /* try it with macros this time */
  _stp_map_key (map, "Washington");
  _stp_map_set (map, 2);  
  _stp_map_print (map, "map");

  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_key_str (map, "");  
  _stp_map_key_del (map);
  _stp_map_key_str (map, "77");  
  _stp_map_key_del (map);
  _stp_map_key_del (map);
  _stp_map_set_int64 (map,1000000);

  _stp_map_print (map, "map");

  /* create and delete a key */
  _stp_map_key_str (map, "1024");
  _stp_map_set_int64 (map, 2048);  
  _stp_map_key_str (map, "1024");
  _stp_map_key_del (map);

  _stp_map_print (map, "map");

  /* create and delete a key again*/
  _stp_map_key_str (map, "1024");
  _stp_map_set_int64 (map, 2048);  
  _stp_map_key_del (map);

  _stp_map_print (map, "map");

  /* check that unset values are 0 */
  _stp_map_key_str (map, "California");
  printf ("map[%lld]=%lld\n", key1int(map->key), _stp_map_get_int64(map));

  /* map[California] = 3 */
  _stp_map_set (map, 3);
  _stp_map_print (map, "map");

  /* test an empty string as key */
  _stp_map_key (map, "");
  _stp_map_set_int64 (map, 7777);
  _stp_map_print (map, "map");
  _stp_map_key (map, "");
  _stp_map_set_int64 (map, 8888);
  _stp_map_print (map, "map");

  /* add 4 new entries, pushing the others out */
  int i;
  for (i = 6; i < 10; i++)
    {
      char buf[32];
      sprintf (buf, "String %d", i);
      _stp_map_key (map, buf);
      _stp_map_set_int64 (map, 100 + i);
    }

  _stp_map_print (map, "map");  


  /* 5, 382, 526, and 903 all hash to the same value (23) */
  /* use them to test the hash chain */
  _stp_map_key (map, "5"); _stp_map_set_int64 (map, 1005);
  _stp_map_key (map, "382"); _stp_map_set_int64 (map, 1382);
  _stp_map_key (map, "526"); _stp_map_set_int64 (map, 1526);
  _stp_map_key (map, "903"); _stp_map_set_int64 (map, 1903);

  _stp_map_print (map, "map");  

  /* now delete all 4 nodes, one by one */
  _stp_map_key (map, "382"); _stp_map_key_del (map);

  _stp_map_print (map, "map");  

  _stp_map_key (map, "5"); _stp_map_key_del (map);

  _stp_map_print (map, "map");  

  _stp_map_key (map, "903"); _stp_map_key_del (map);

  _stp_map_print (map, "map");  

  _stp_map_key (map, "526"); _stp_map_key_del (map);

  _stp_map_print (map, "map");  

  _stp_map_del (map);
  return 0;
}
