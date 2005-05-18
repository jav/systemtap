#include "runtime.h"

/* test of list with value of STRING */

#define KEY1_TYPE INT64
#include "map-keys.c"

#define VALUE_TYPE INT64
#include "map-values.c"

#include "list.c"

int main ()
{
  int i;

  MAP map = _stp_list_new (10, INT64);

  for (i = 0; i < 10; i++)
    _stp_list_add_int64 (map, (int64_t)i);


  _stp_map_print(map, "list");
  printf ("size is %d\n\n", _stp_list_size(map));

  /* we set a limit of 10 elements so these */
  /* won't be added to the list */
  for (i = 50; i < 55; i++)
    _stp_list_add_int64 (map, i);
  _stp_map_print(map, "list");


  _stp_list_clear (map);
  _stp_map_print(map, "list");

  for (i = 50; i < 55; i++)
    _stp_list_add_int64 (map, i);

  _stp_map_print(map, "newlist");

  _stp_map_del (map);

  return 0;
}
