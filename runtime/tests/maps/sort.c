#include "runtime.h"

/* torture test of map formatting */
#define NEED_INT64_VALS
#define NEED_STRING_VALS
#define NEED_STAT_VALS

#define KEY1_TYPE INT64
#define KEY2_TYPE INT64
#define KEY3_TYPE STRING
#include "map-keys.c"

#define KEY1_TYPE STRING
#define KEY2_TYPE STRING
#include "map-keys.c"

#include "map.c"

int main ()
{
  MAP mapiis = _stp_map_new_int64_int64_str(10, STRING);

  /* try to crash the sorts with sorting an empty list */
  _stp_map_sort (mapiis, 0, -1);
  _stp_map_sort (mapiis, 0, 1);
  _stp_map_sortn (mapiis, 3, 0, -1);
  _stp_map_sortn (mapiis, 0, 0, -1);

  /* load some test data */
  _stp_map_key_int64_int64_str (mapiis, 3,4,"California");
  _stp_map_add_str (mapiis, "Sacramento" );
  _stp_map_key_int64_int64_str (mapiis, 5,6,"Washington");
  _stp_map_set_str (mapiis, "Olympia" );
  _stp_map_key_int64_int64_str (mapiis, 7,8,"Oregon");
  _stp_map_set_str (mapiis, "Salem" );
  _stp_map_key_int64_int64_str (mapiis, 7,8,"Nevada");
  _stp_map_set_str (mapiis, "Carson City" );
  _stp_map_key_int64_int64_str (mapiis, 1, 4,"New Mexico");
  _stp_map_set_str (mapiis, "Santa Fe" );
  _stp_map_key_int64_int64_str (mapiis, -1,9,"North Carolina");
  _stp_map_set_str (mapiis, "Raleigh" );
  _stp_map_key_int64_int64_str (mapiis, 5,5,"Massachusetts");
  _stp_map_set_str (mapiis, "Boston" );
  _stp_map_key_int64_int64_str (mapiis, 2,2,"Vermont");
  _stp_map_set_str (mapiis, "Montpelier" );
  _stp_map_key_int64_int64_str (mapiis, 8,8,"Iowa");
  _stp_map_set_str (mapiis, "Des Moines" );
  _stp_map_key_int64_int64_str (mapiis, 1,2,"Ohio");
  _stp_map_set_str (mapiis, "Columbus" );

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

  MAP mapss = _stp_map_new_str_str(4, INT64);
  _stp_map_key_str_str (mapss, "Riga", "Latvia");
  _stp_map_set_int64 (mapss, 135786);
  _stp_map_key_str_str (mapss, "Sofia", "Bulgaria");
  _stp_map_set_int64 (mapss, 138740);
  _stp_map_key_str_str (mapss, "Valletta", "Malta");
  _stp_map_set_int64 (mapss, 1);
  _stp_map_key_str_str (mapss, "Nicosia", "Cyprus");
  _stp_map_set_int64 (mapss, -1);

  _stp_printf("sorted by population from low to high\n");
  _stp_map_sort (mapss, 0, -1);
  _stp_map_print (mapss, "%1s is the capitol of %2s and the nerd population is %d");
  _stp_printf("sorted by population from high to low\n");
  _stp_map_sort (mapss, 0, 1);
  _stp_map_print (mapss, "%1s is the capitol of %2s and the nerd population is %d");

#if 0
  MAP mapsst = _stp_map_new_str_str(4, HSTAT_LINEAR, 0, 100, 10 );
  int i,j;

  _stp_map_key_str_str (mapsst, "Riga", "Latvia");
  for (i = 0; i < 100; i++)
    for (j = 0; j <= i*10 ; j++ )
      _stp_map_add_int64 (mapsst, i);
  
  _stp_map_key_str_str (mapsst, "Sofia", "Bulgaria");
  for (i = 0; i < 10; i++)
    for (j = 0; j < 10 ; j++ )
      _stp_map_add_int64 (mapsst, j * i );

  _stp_map_key_str_str (mapsst, "Valletta", "Malta");
  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/10 ; j++ )  
      _stp_map_add_int64 (mapsst, i);
  
  _stp_map_print (mapsst, "Bogons per packet for %1s\ncount:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");
#endif

  return 0;
}
