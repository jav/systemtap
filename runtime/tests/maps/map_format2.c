#include "runtime.h"

/* torture test of map formatting */
#define VALUE_TYPE STRING
#define KEY1_TYPE INT64
#define KEY2_TYPE INT64
#define KEY3_TYPE STRING
#include "map-gen.c"

#define VALUE_TYPE INT64
#define KEY1_TYPE STRING
#define KEY2_TYPE STRING
#include "map-gen.c"

#define VALUE_TYPE STAT
#define KEY1_TYPE STRING
#define KEY2_TYPE STRING
#include "map-gen.c"

#include "map.c"

int main ()
{
  MAP mapiis = _stp_map_new_iiss(4);
  _stp_map_set_iiss (mapiis, 1,2,"Ohio", "Columbus" );
  _stp_map_set_iiss (mapiis, 3,4,"California", "Sacramento" );
  _stp_map_set_iiss (mapiis, 5,6,"Washington", "Olympia" );
  _stp_map_set_iiss (mapiis, 7,8,"Oregon", "Salem" );
  _stp_map_print (mapiis, "%s -> mapiis %1d %2d %3s");

  /* test printing of '%' */
  _stp_map_print (mapiis, "%s %% %3s");

  /* very bad string.  don't crash */
  _stp_map_print (mapiis, "%s -> mapiis %1s %2s %3d %4d");

  MAP mapss = _stp_map_new_ssi(4);
  _stp_map_set_ssi (mapss, "Riga", "Latvia", 0x0000c0dedbad0000LL);
  _stp_map_set_ssi (mapss, "Sofia", "Bulgaria", 0xdeadf00d12345678LL);
  _stp_map_set_ssi (mapss, "Valletta", "Malta", 1);
  _stp_map_set_ssi (mapss, "Nicosia", "Cyprus", -1);
  _stp_map_print (mapss, "The capitol of %1s is %2s and the nerd population is %d");
  _stp_map_print (mapss, "The capitol of %1s is %2s and the nerd population is %x");
  _stp_map_print (mapss, "The capitol of %1s is %2s and the nerd population is %X");

  MAP mapsst = _stp_map_new_ssx (4, HIST_LINEAR, 0, 100, 10 );
  int i,j;

  for (i = 0; i < 100; i++)
    for (j = 0; j <= i*10 ; j++ )
      _stp_map_add_ssx (mapsst, "Riga", "Latvia", i);
  
  for (i = 0; i < 10; i++)
    for (j = 0; j < 10 ; j++ )
      _stp_map_add_ssx (mapsst, "Sofia", "Bulgaria", j * i );

  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/10 ; j++ )  
      _stp_map_add_ssx (mapsst, "Valletta", "Malta", i);

    _stp_map_print (mapsst, "Bogons per packet for %1s\ncount:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");

    _stp_map_print (mapsst, "%C was the count for %1s, %2s");

    /* here's how to print a map without using _stp_map_print(). */
    struct map_node *ptr;
    foreach (mapsst, ptr)
      _stp_printf ("mapsst[%09s,%09s] = %llX\n", key1str(ptr), key2str(ptr), _stp_get_stat(ptr)->sum);
    _stp_print_flush();
    
    return 0;
}
