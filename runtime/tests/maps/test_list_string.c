#include "runtime.h"

/* test of list with value of STRING */

#define KEY1_TYPE INT64
#include "map-keys.c"

#define VALUE_TYPE STRING
#include "map-values.c"

#include "map.c"
#include "list.c"

int main ()
{
  int i;
  char buf[32];

  MAP map = _stp_list_new (10, STRING);

  for (i = 0; i < 10; i++)
    {
      sprintf (buf, "Item%d", i);
      _stp_list_add_str (map, buf);
    }

  _stp_map_print(map, "list[%1d] = %s");
  printf ("size is %d\n\n", _stp_list_size(map));

  /* we set a limit of 10 elements so these */
  /* won't be added to the list */
  for (i = 50; i < 55; i++)
    {
      sprintf (buf, "Item%d", i);
      _stp_list_add_str (map, buf);
    }
  _stp_map_print(map, "list[%1d] = %s");


  _stp_list_clear (map);
  _stp_map_print(map, "list[%1d] = %s");

  for (i = 50; i < 55; i++)
    {
      sprintf (buf, "Item%d", i);
      _stp_list_add_str (map, buf);
    }
  _stp_map_print(map, "newlist[%1d] = %s");

  _stp_map_del (map);

  return 0;
}
