#include "runtime.h"

/* verify correct set and add behavior */
#define VALUE_TYPE INT64
#define KEY1_TYPE INT64
#define STP_MAP_II
#include "map-gen.c"

#define VALUE_TYPE STRING
#define KEY1_TYPE INT64
#include "map-gen.c"

#define VALUE_TYPE STAT
#define KEY1_TYPE INT64
#include "map-gen.c"

#include "map.c"

int main ()
{
  int i, res;
  MAP mapi = _stp_map_new_ii(4);
  MAP maps = _stp_map_new_is(4);
  MAP mapx = _stp_map_new_ix(4, HIST_NONE);

  /* use add to set initial values */
  for (i = 1; i < 5; i++) 
    {
      res = _stp_map_add_ii (mapi, i, i*i);
      if (res)
	printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(mapi,"mapi[%1d] = %d");

  for (i = 1; i < 5; i++)
    {
      char buf[32];
      sprintf(buf, "value of %d", i);
      res = _stp_map_add_is (maps, i, buf);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(maps,"maps[%1d] = %s");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_ix (mapx, i, i);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print (mapx, "mapx[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M");

  /*************** now add some values *******************/

  for (i = 1; i < 5; i++) 
    {
      res = _stp_map_add_ii (mapi, i, i*i);
      if (res)
	printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(mapi,"mapi[%1d] = %d");

  for (i = 1; i < 5; i++)
    {
      char buf[32];
      sprintf(buf, "*****", i);
      res = _stp_map_add_is (maps, i, buf);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(maps,"maps[%1d] = %s");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_ix (mapx, i, i+i);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print (mapx, "mapx[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M");

  /*************** now add 0 *******************/
  printf ("Adding 0\n");
  for (i = 1; i < 5; i++) 
    {
      res = _stp_map_add_ii (mapi, i, 0);
      if (res)
	printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(mapi,"mapi[%1d] = %d");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_is (maps, i, "");
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_is (maps, i, 0);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(maps,"maps[%1d] = %s");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_ix (mapx, i, 0);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print (mapx, "mapx[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M");

  /*************** now set to 0 (clear) *******************/
  printf ("setting everything to 0\n");
  for (i = 1; i < 5; i++) 
    {
      res = _stp_map_set_ii (mapi, i, 0);
      if (res)
	printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(mapi,"mapi[%1d] = %d");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_set_is (maps, i, 0);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(maps,"maps[%1d] = %s");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_set_ix (mapx, i, 0);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print (mapx, "mapx[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M");

  /*************** now add 0 *******************/
  printf ("Adding 0\n");
  for (i = 1; i < 5; i++) 
    {
      res = _stp_map_add_ii (mapi, i, 0);
      if (res)
	printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(mapi,"mapi[%1d] = %d");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_is (maps, i, 0);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_is (maps, i, 0);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(maps,"maps[%1d] = %s");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_ix (mapx, i, 0);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print (mapx, "mapx[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M");


  /*************** now set to -1 *******************/
  printf ("setting everything to -1\n");
  for (i = 1; i < 5; i++) 
    {
      res = _stp_map_set_ii (mapi, i, -1);
      if (res)
	printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(mapi,"mapi[%1d] = %d");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_set_ix (mapx, i, -1);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print (mapx, "mapx[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M");

  /*************** now add -1 *******************/
  printf ("adding -1\n");
  for (i = 1; i < 5; i++) 
    {
      res = _stp_map_add_ii (mapi, i, -1);
      if (res)
	printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print(mapi,"mapi[%1d] = %d");

  for (i = 1; i < 5; i++)
    {
      res = _stp_map_add_ix (mapx, i, -1);
      if (res)
        printf("ERROR: got result of %d when expected 0\n", res);
    }
  _stp_map_print (mapx, "mapx[%1d] = count:%C  sum:%S  avg:%A  min:%m  max:%M");


  _stp_map_del (mapi);
  _stp_map_del (maps);
  _stp_map_del (mapx);
  return 0;
}
