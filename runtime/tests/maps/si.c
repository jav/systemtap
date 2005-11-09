#include "runtime.h"

/* test of maps with keys of string and value of int64 */
#define VALUE_TYPE INT64
#define KEY1_TYPE STRING
#include "map-gen.c"
#include "map.c"

int main ()
{
  int res;
  MAP map = _stp_map_new_si(4);
  map->wrap = 1;

  /* map[Ohio] = 1 */
  _stp_map_set_si (map, "Ohio", 1);
  printf ("map[Ohio]=%lld\n", _stp_map_get_si(map,"Ohio"));
  _stp_map_print(map,"map[%1s] = %d");

  /* map[Washington] = 2 */
  _stp_map_set_si (map, "Washington", 2);
  _stp_map_print (map, "map[%1s] = %d");

  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */

  /* bad map */
  res = _stp_map_set_si(0,"foo",100);
  if (res != -2)
	printf("WARNING: got result of %d when expected -2\n", res);

  /* bad key */
  res = _stp_map_set_si(map,0,0);
  if (res != -2)
	printf("WARNING: got result of %d when expected -2\n", res);

  /* bad key */
  res = _stp_map_set_si(map,0,42);
  if (res != -2)
	printf("WARNING: got result of %d when expected -2\n", res);

  res = _stp_map_set_si(map,"",0);
  if (res)
	printf("WARNING: got result of %d when expected 0\n", res);
  _stp_map_print (map, "map[%1s] = %d");

  /* create and delete a key */
  _stp_map_set_si (map, "1024", 2048);
  _stp_map_print (map, "map[%1s] = %d");
  _stp_map_set_si (map, "1024", 0);
  _stp_map_print (map, "map[%1s] = %d");
  _stp_map_set_si (map, "1024", 2048);
  _stp_map_print (map, "map[%1s] = %d");
  _stp_map_set_si (map, "1024", 0);
  _stp_map_print (map, "map[%1s] = %d");

  /* check that unset values are 0 */
  res = _stp_map_get_si (map, "California"); 
  if (res)
    printf("ERROR: map[California] = %d (should be 0)\n", res);

  /* map[California] = 3 */
  _stp_map_set_si (map, "California", 3); 
  _stp_map_print (map, "map[%1s] = %d");

  /* test an empty string as key */
  _stp_map_set_si (map, "", 7777); 
  _stp_map_print (map, "map[%1s] = %d");
  _stp_map_set_si (map, "", 8888);
  _stp_map_print (map, "map[%1s] = %d");
  _stp_map_set_si (map, "", 0); 
  _stp_map_print (map, "map[%1s] = %d");


  /* add 4 new entries, pushing the others out */
  int i;
  for (i = 6; i < 10; i++)
    {
      char buf[32];
      sprintf (buf, "String %d", i);
      res = _stp_map_set_si (map, buf, 100 + i); 
      if (res)
	printf("WARNING: During wrap test, got result of %d when expected 0\n", res);
    }
  _stp_map_print (map, "map[%1s] = %d");  

  /* turn off wrap and repeat */
  map->wrap = 0;
  for (i = 16; i < 20; i++) {
      char buf[32];
      sprintf (buf, "BAD String %d", i);
      res = _stp_map_set_si (map, buf, 100 + i); 
      if (res != -1)
	printf("WARNING: During wrap test, got result of %d when expected -1\n", res);
  }
  _stp_map_print (map, "map[%1s] = %d");  

  /* test addition */
  for (i = 6; i < 10; i++)
    {
      char buf[32];
      sprintf (buf, "String %d", i);
      res = _stp_map_add_si (map, buf, 1000 * i); 
      if (res)
	printf("WARNING: During wrap test, got result of %d when expected 0\n", res);
    }
  _stp_map_print (map, "map[%1s] = %d");  

  /* reset all */
  for (i = 6; i < 10; i++)
    {
      char buf[32];
      sprintf (buf, "String %d", i);
      res = _stp_map_set_si (map, buf, i); 
      if (res)
	printf("WARNING: During wrap test, got result of %d when expected 0\n", res);
    }
  _stp_map_print (map, "map[%1s] = %d");  

  _stp_map_clear(map);
  _stp_map_print (map, "map[%1s] = %d");  

  _stp_map_del (map);
  return 0;
}
