#include "test.h"

/* testliat.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   Tests maps acting as Lists
*/



static void
map_dump (MAP map)
{
  struct map_node_str *ptr;
  foreach (map, ptr)
    printf ("map[%ld] = %s\n", key1int(ptr), ptr->str);
  printf ("\n");
}


int main ()
{
  char buf[32];
  int i;
  MAP map = _stp_list_new(10, STRING);

  for (i = 0; i < 10; i++)
    {
      sprintf (buf, "Item%d", i);
      _stp_list_add_str (map, buf);
    }

  map_dump(map);
  printf ("size is %d\n", _stp_list_size(map));

  /* we set a limit of 10 elements so these push */
  /* the first entries out of the list */
  for (i = 50; i < 55; i++)
    {
      sprintf (buf, "Item%d", i);
      _stp_list_add_str (map, buf);
    }

  map_dump(map);

  for (i = 0; i < 10; i++)
    {
      sprintf (buf, "Item%d", i);
      _stp_list_add_str (map, buf);
    }

  map_dump(map);
  _stp_list_clear (map);
  map_dump(map);
  for (i = 50; i < 55; i++)
    {
      sprintf (buf, "Item%d", i);
      _stp_list_add_str (map, buf);
    }
  map_dump(map);
  _stp_map_del (map);
  return 0;
}
