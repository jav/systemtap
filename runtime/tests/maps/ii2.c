#include "runtime.h"

/* test of maps with keys of int64 and value of int64 */
#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#include "map-gen.c"

#include "map.c"

int main ()
{
  MAP map = _stp_map_new_ii(4);
  int64_t x;

  dbug("Hello World\n");

  /* map[1] = 2 */
  _stp_map_set_ii(map, 1, 2);
  x = _stp_map_get_ii(map, 1);
  printf ("map[1]=%lld\n", x);

  /* map[3] = 4 */
  _stp_map_set_ii(map, 3, 4);
  _stp_map_print(map,"map[%1d] = %d");

  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_set_ii(0,1,100);
  _stp_map_set_ii(map,0,0);
  _stp_map_set_ii(map,100,0);  
  _stp_map_print(map,"map[%1d] = %d");

  /* check that unset values are 0 */
  printf ("%lld (should be 0)\n", _stp_map_get_ii(map, 5));

  /* map[5] = 6 */
  _stp_map_set_ii(map, 5, 6);
  _stp_map_print(map,"map[%1d] = %d");

  /* set wrap */
  map->wrap = 1;
  /* add 4 new entries, pushing the others out */
  int i, res;
  for (i = 6; i < 10; i++) {
      res = _stp_map_set_ii (map, i, 100 + i);
      if (res)
	printf("WARNING: During wrap test, got result of %d when expected 0\n", res);
  }
  _stp_map_print(map,"map[%1d] = %d");  

  /* turn off wrap and repeat */
  map->wrap = 0;
  for (i = 16; i < 20; i++) {
      res = _stp_map_set_ii (map, i, 100 + i);
      if (res != -1)
	printf("WARNING: During wrap test, got result of %d when expected -1\n", res);
  }
  
  map->wrap = 1;

  /* 5, 382, 526, and 903 all hash to the same value (23) */
  /* use them to test the hash chain */
  _stp_map_set_ii (map, 5, 1005);
  _stp_map_set_ii (map, 382, 1382);
  _stp_map_set_ii (map, 526, 1526);
  _stp_map_set_ii (map, 903, 1903);
  _stp_map_print(map,"map[%1d] = %d");  


  /* now delete all 4 nodes, one by one */
  _stp_map_set_ii (map, 382, 0);
  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_set_ii (map, 5, 0);
  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_set_ii (map, 903, 0);
  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_set_ii (map, 526, 0);
  _stp_map_print(map,"map[%1d] = %d");  

  /* finally check clearing the map */
  for (i = 33; i < 77; i+=11)
    _stp_map_set_ii (map, i, 100*i+i);

  _stp_map_print(map,"map[%1d] = %d");  

  _stp_map_clear(map);
  _stp_map_print(map,"map[%1d] = %d");
  _stp_map_set_ii (map, 1970, 1799); 
  _stp_map_print(map,"map[%1d] = %d"); 

  _stp_map_del (map);
  return 0;
}
