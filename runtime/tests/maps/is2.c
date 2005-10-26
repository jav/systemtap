#include "runtime.h"

/* test of maps with keys of int64 and value of string */
#define KEY1_TYPE INT64
#define VALUE_TYPE STRING
#include "map-gen.c"

#include "map.c"

int main ()
{
  MAP map = _stp_map_new_is(4);
  map->wrap = 1;

  /* map[1] = one */
  _stp_map_set_is (map, 1, "one");

  printf ("map[1]=%s\n", _stp_map_get_is(map,1));
  _stp_map_print(map,"map[%1d] = %s");

  /* map[3] = "three" */
  _stp_map_set_is (map, 3, "three");
  _stp_map_print(map,"map[%1d] = %s");

  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_set_is(0,1,"foobar");
  _stp_map_set_is(map,0,0);
  _stp_map_set_is(map,100,0);  
  _stp_map_print(map,"map[%1d] = %s");

  /* create and delete a key */
  _stp_map_set_is (map, 1024, "2048");
  _stp_map_set_is (map, 1024, 0);
  _stp_map_print(map,"map[%1d] = %s");

  /* create and delete a key again*/
  _stp_map_set_is (map, 1024, "2048");
  _stp_map_print(map,"map[%1d] = %s");
  _stp_map_set_is (map, 1024, 0);
  _stp_map_print(map,"map[%1d] = %s");


  /* check that unset values are 0 */
  if (_stp_map_get_is(map, 5))
    printf("ERROR: unset key has nonzero value\n");

  /* map[5] = "five" */
  _stp_map_set_is (map, 5, "five");
  _stp_map_print(map,"map[%1d] = %s");

  /* test empty string */
  _stp_map_set_is (map, 5, "");
  _stp_map_print(map,"map[%1d] = %s");


  /* add 4 new entries, pushing the others out */
  int i;
  for (i = 6; i < 10; i++)
    {
      char buf[32];
      sprintf(buf, "value of %d", i);
      _stp_map_set_is (map, i, buf);
    }
  _stp_map_print(map,"map[%1d] = %s");  

  /* 5, 382, 526, and 903 all hash to the same value (23) */
  /* use them to test the hash chain */
  _stp_map_set_is (map, 5, "1005");
  _stp_map_set_is (map, 382, "1382");
  _stp_map_set_is (map, 526, "1526");
  _stp_map_set_is (map, 903, "1903");

  _stp_map_print(map,"map[%1d] = %s");  

  /* now delete all 4 nodes, one by one */
  _stp_map_set_is (map, 382, 0);
  _stp_map_print(map,"map[%1d] = %s");  

  _stp_map_set_is (map, 5, 0);
  _stp_map_print(map,"map[%1d] = %s");  

  _stp_map_set_is (map, 903, 0);
  _stp_map_print(map,"map[%1d] = %s");  

  _stp_map_set_is (map, 526, 0);
  _stp_map_print(map,"map[%1d] = %s");  

  /* test overflow errors */
  map->wrap = 0;
  for (i = 6; i < 10; i++)
    {
      char buf[32];
      sprintf(buf, "value of %d", i);
      _stp_map_set_is (map, i, buf);
    }

  for (i = 6; i < 10; i++)
    {
      char buf[32];
      int res;
      sprintf(buf, "new value of %d", i);
      res = _stp_map_set_is (map, i, buf);
      if (res)
	printf("WARNING: During wrap test, got result of %d when expected 0\n", res);
    }
  for (i = 16; i < 20; i++)
    {
      char buf[32];
      int res;
      sprintf(buf, "BAD value of %d", i);
      res = _stp_map_set_is (map, i, buf);
      if (res != -1)
	printf("WARNING: During wrap test, got result of %d when expected -1\n", res);
    }
  _stp_map_print(map,"map[%1d] = %s");  
  _stp_map_del (map);
  return 0;
}
