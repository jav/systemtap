#include "runtime.h"

/* test of map sorting.  Just like sort.c, except test with an odd number of nodes */
#define VALUE_TYPE STRING
#define KEY1_TYPE INT64
#define KEY2_TYPE INT64
#define KEY3_TYPE STRING
#include "map-gen.c"

#define VALUE_TYPE INT64
#define KEY1_TYPE STRING
#define KEY2_TYPE STRING
#include "map-gen.c"

#include "map.c"

int main ()
{
  MAP mapiis = _stp_map_new_iiss(10);

  /* try to crash the sorts with sorting an empty list */
  _stp_map_sort (mapiis, 0, -1);
  _stp_map_sort (mapiis, 0, 1);
  _stp_map_sortn (mapiis, 3, 0, -1);
  _stp_map_sortn (mapiis, 0, 0, -1);

  /* load some test data */
  _stp_map_add_iiss (mapiis, 3,4,"California","Sacramento" );
  _stp_map_set_iiss (mapiis, 5,6,"Washington","Olympia" );
  _stp_map_set_iiss (mapiis, 7,8,"Oregon","Salem" );
  _stp_map_set_iiss (mapiis, 7,8,"Nevada","Carson City" );
  _stp_map_set_iiss (mapiis, 1, 4,"New Mexico","Santa Fe" );
  _stp_map_set_iiss (mapiis, -1,9,"North Carolina","Raleigh" );
  _stp_map_set_iiss (mapiis, 5,5,"Massachusetts","Boston" );
  _stp_map_set_iiss (mapiis, 8,8,"Iowa","Des Moines" );
  _stp_map_set_iiss (mapiis, 1,2,"Ohio","Columbus" );

  _stp_printf("sorting from A-Z on value\n");
  _stp_map_sort (mapiis, 0, -1);
  _stp_map_print (mapiis, "%s -> %1d %2d %3s");

  _stp_printf("\nsorting from Z-A on value\n");
  _stp_map_sort (mapiis, 0, 1);
  _stp_map_print (mapiis, "%s -> %1d %2d %3s");

  _stp_printf("\nsorting from low to high on key 1\n");  
  _stp_map_sort (mapiis, 1, -1);
  _stp_map_print (mapiis, "%1d %2d %3s -> %s");

  _stp_printf("\nsorting from high to low on key 1\n");  
  _stp_map_sort (mapiis, 1, 1);
  _stp_map_print (mapiis, "%1d %2d %3s -> %s");

  _stp_printf("\nsorting from low to high on key 2\n");  
  _stp_map_sort (mapiis, 2, -1);
  _stp_map_print (mapiis, "%1d %2d %3s -> %s");

  _stp_printf("\nsorting from high to low on key 2\n");  
  _stp_map_sort (mapiis, 2, 1);
  _stp_map_print (mapiis, "%1d %2d %3s -> %s");


  _stp_printf("\nsorting from low to high on key 3\n");  
  _stp_map_sort (mapiis, 3, -1);
  _stp_map_print (mapiis, "%3s\t\t%1d %2d -> %s");

  _stp_printf("\nsorting from high to low on key 3\n");  
  _stp_map_sort (mapiis, 3, 1);
  _stp_map_print (mapiis, "%3s\t\t%1d %2d -> %s");

  _stp_printf("\ntop 3 alphabetical by value\n");
  _stp_map_sortn (mapiis, 3, 0, -1);
  _stp_map_printn (mapiis, 3, "%s -> %1d %2d %3s");

  _stp_printf("\nbottom 2 alphabetical by value\n");
  _stp_map_sortn (mapiis, 2, 0, 1);
  _stp_map_printn (mapiis, 2, "%s -> %1d %2d %3s");


  _stp_printf("\ntop 5 sorted by key 1\n");
  _stp_map_sortn (mapiis, 5, 1, 1);
  _stp_map_printn (mapiis, 5, "%1d %2d %3s -> %s");
  _stp_printf("\nbottom 5 sorted by key 1\n");
  _stp_map_sortn (mapiis, 5, 1, -1);
  _stp_map_printn (mapiis, 5, "%1d %2d %3s -> %s");

  MAP mapss = _stp_map_new_ssi(5);
  _stp_map_set_ssi (mapss, "Riga", "Latvia", 135786);
  _stp_map_set_ssi (mapss, "Sofia", "Bulgaria", 138740);
  _stp_map_set_ssi (mapss, "Valletta", "Malta", 1);
  _stp_map_set_ssi (mapss, "Nicosia", "Cyprus", -1);
  _stp_map_set_ssi (mapss, "Chisinau", "Moldova", 1024);

  _stp_printf("sorted by population from low to high\n");
  _stp_map_sort (mapss, 0, -1);
  _stp_map_print (mapss, "%1s is the capitol of %2s and the nerd population is %d");
  _stp_printf("sorted by population from high to low\n");
  _stp_map_sort (mapss, 0, 1);
  _stp_map_print (mapss, "%1s is the capitol of %2s and the nerd population is %d");

  _stp_map_del(mapss);
  _stp_map_del(mapiis);

  return 0;
}
