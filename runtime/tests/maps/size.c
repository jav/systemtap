#include "runtime.h"

/* test of _stp_map_size() */
#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#include "map-gen.c"

#include "map.c"

#define check(map,num)				\
  {                                             \
    int size = _stp_map_size(map);		\
    if (size != num)				\
      printf("ERROR at line %d: expected size %d and got %d instead.\n", __LINE__, num, size); \
  }

int main ()
{
  MAP map = _stp_map_new_ii(4);
  int64_t x;

  check (map, 0);

  /* map[1] = 2 */
  _stp_map_set_ii(map, 1, 2);
  check (map, 1);

  /* map[3] = 4 */
  _stp_map_set_ii(map, 3, 4);
  check (map,2);

  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_set_ii(0,1,100);
  _stp_map_set_ii(map,0,0);
  _stp_map_set_ii(map,100,0);  
  check (map,2);

  /* map[5] = 6 */
  _stp_map_set_ii(map, 5, 6);
  check (map,3);

  /* set wrap */
  map->wrap = 1;
  /* add 4 new entries, pushing the others out */
  int i, res;
  for (i = 6; i < 10; i++) {
      res = _stp_map_set_ii (map, i, 100 + i);
      if (res)
	printf("WARNING: During wrap test, got result of %d when expected 0\n", res);
  }
  check (map,4);

  /* turn off wrap and repeat */
  map->wrap = 0;
  for (i = 16; i < 20; i++) {
      res = _stp_map_set_ii (map, i, 100 + i);
      if (res != -1)
	printf("WARNING: During wrap test, got result of %d when expected -1\n", res);
  }
  check (map,4);
  
  map->wrap = 1;

  /* 5, 382, 526, and 903 all hash to the same value (23) */
  /* use them to test the hash chain */
  _stp_map_set_ii (map, 5, 1005);
  _stp_map_set_ii (map, 382, 1382);
  _stp_map_set_ii (map, 526, 1526);
  _stp_map_set_ii (map, 903, 1903);
  check (map,4);

  /* now delete all 4 nodes, one by one */
  _stp_map_set_ii (map, 382, 0);
  check (map,3);

  _stp_map_set_ii (map, 5, 0);
  check (map,2);

  _stp_map_set_ii (map, 903, 0);
  check (map,1);

  _stp_map_set_ii (map, 526, 0);
  check (map,0);

  /* finally check clearing the map */
  _stp_map_clear(map);
  check (map,0);

  map->wrap = 0;
  for (i = 33; i < 99; i+=11)
    _stp_map_set_ii (map, i, 100*i+i);
  check (map,4);

  _stp_map_clear(map);
  check (map,0);

  _stp_map_set_ii (map, 1970, 1799); 
  check (map,1);

  _stp_map_del (map);
  return 0;
}
