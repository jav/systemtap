#include "runtime.h"

/* test of map sorting */

#define VALUE_TYPE STAT
#define KEY1_TYPE STRING
#define KEY2_TYPE STRING
#include "map-gen.c"

#include "map.c"

int main ()
{
  MAP mapssx = _stp_map_new_ssx (8, HIST_LINEAR, 0, 100, 10 );
  int i,j;

  for (i = 0; i < 100; i++)
    for (j = 0; j <= i*10 ; j++ )
      _stp_map_add_ssx (mapssx, "California", "Sacramento", i);
  
  for (i = 0; i < 10; i++)
    for (j = 0; j < 10 ; j++ )
      _stp_map_add_ssx (mapssx, "Washington", "Olympia", j * i );

  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/5 ; j++ )  
      _stp_map_add_ssx (mapssx, "Oregon", "Salem", i);

  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/10 ; j++ )  
      _stp_map_add_ssx (mapssx, "Nevada", "Carson City", i + j);

  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/20 ; j++ )  
      _stp_map_add_ssx (mapssx, "Ohio", "Columbus", 50);

  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/10 ; j++ )  
      _stp_map_add_ssx (mapssx, "North Carolina", "Raleigh", 100 - j * i);

  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i ; j++ )  
      _stp_map_add_ssx (mapssx, "New Mexico", "Santa Fe", 50 - j);


  _stp_map_print (mapssx, "Bogons per packet for %1s\ncount:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");

  _stp_printf("SORTED BY COUNT\n");
  _stp_map_sort (mapssx, SORT_COUNT, 1);
  _stp_map_print (mapssx, "%C %1s");

  _stp_printf("SORTED BY COUNT (low to high)\n");
  _stp_map_sort (mapssx, SORT_COUNT, -1);
  _stp_map_print (mapssx, "%C %1s");

  _stp_printf("SORTED BY SUM\n");
  _stp_map_sort (mapssx, SORT_SUM, 1);
  _stp_map_print (mapssx, "%S %1s");

  _stp_printf("SORTED BY SUM (low to high)\n");
  _stp_map_sort (mapssx, SORT_SUM, -1);
  _stp_map_print (mapssx, "%S %1s");

  _stp_printf("SORTED BY MIN\n");
  _stp_map_sort (mapssx, SORT_MIN, 1);
  _stp_map_print (mapssx, "%m %1s");

  _stp_printf("SORTED BY MIN (low to high)\n");
  _stp_map_sort (mapssx, SORT_MIN, -1);
  _stp_map_print (mapssx, "%m %1s");

  _stp_printf("SORTED BY MAX\n");
  _stp_map_sort (mapssx, SORT_MAX, 1);
  _stp_map_print (mapssx, "%M %1s");

  _stp_printf("SORTED BY MAX (low to high)\n");
  _stp_map_sort (mapssx, SORT_MAX, -1);
  _stp_map_print (mapssx, "%M %1s");

  _stp_printf("SORTED BY AVG\n");
  _stp_map_sort (mapssx, SORT_AVG, 1);
  _stp_map_print (mapssx, "%A %1s");

  _stp_printf("SORTED BY AVG (low to high)\n");
  _stp_map_sort (mapssx, SORT_AVG, -1);
  _stp_map_print (mapssx, "%A %1s");

  _stp_map_del (mapssx);
  return 0;
}
