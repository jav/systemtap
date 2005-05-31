#include "runtime.h"

/* torture test of map formatting */

#define KEY1_TYPE INT64
#define KEY2_TYPE INT64
#define KEY3_TYPE STRING
#include "map-keys.c"

#define KEY1_TYPE STRING
#define KEY2_TYPE STRING
#include "map-keys.c"

#define VALUE_TYPE STRING
#include "map-values.c"

#define VALUE_TYPE INT64
#include "map-values.c"

#define VALUE_TYPE STAT
#include "map-values.c"

#include "map.c"

int main ()
{
  MAP mapiis = _stp_map_new_int64_int64_str(4, STRING);
  _stp_map_key_int64_int64_str (mapiis, 1,2,"Ohio");
  _stp_map_set_str (mapiis, "Columbus" );
  _stp_map_key_int64_int64_str (mapiis, 3,4,"California");
  _stp_map_add_str (mapiis, "Sacramento" );
  _stp_map_key_int64_int64_str (mapiis, 5,6,"Washington");
  _stp_map_set_str (mapiis, "Olympia" );
  _stp_map_key_int64_int64_str (mapiis, 7,8,"Oregon");
  _stp_map_set_str (mapiis, "Salem" );
  _stp_map_print (mapiis, "%s -> mapiis %1d %2d %3s");

  /* test printing of '%' */
  _stp_map_print (mapiis, "%s %% %3s");

  /* very bad string.  don't crash */
  _stp_map_print (mapiis, "%s -> mapiis %1s %2s %3d %4d");

  MAP mapss = _stp_map_new_str_str(4, INT64);
  _stp_map_key_str_str (mapss, "Riga", "Latvia");
  _stp_map_set_int64 (mapss, 0x0000c0dedbad0000);
  _stp_map_key_str_str (mapss, "Sofia", "Bulgaria");
  _stp_map_set_int64 (mapss, 0xdeadf00d12345678);
  _stp_map_key_str_str (mapss, "Valletta", "Malta");
  _stp_map_set_int64 (mapss, 1);
  _stp_map_key_str_str (mapss, "Nicosia", "Cyprus");
  _stp_map_set_int64 (mapss, -1);
  _stp_map_print (mapss, "The capitol of %1s is %2s and the nerd population is %d");
  _stp_map_print (mapss, "The capitol of %1s is %2s and the nerd population is %x");
  _stp_map_print (mapss, "The capitol of %1s is %2s and the nerd population is %X");
  _stp_map_print (mapss, "The capitol of %1s is %2s and the nerd population is %p");

  MAP mapsst = _stp_map_new_str_str(4, HSTAT_LINEAR, 0, 100, 10 );
  int i,j;

  _stp_map_key_str_str (mapsst, "Riga", "Latvia");
  for (i = 0; i < 100; i++)
    for (j = 0; j <= i*10 ; j++ )
      _stp_map_add_int64_stat (mapsst, i);
  
  _stp_map_key_str_str (mapsst, "Sofia", "Bulgaria");
  for (i = 0; i < 10; i++)
    for (j = 0; j < 10 ; j++ )
      _stp_map_add_int64_stat (mapsst, j * i );

  _stp_map_key_str_str (mapsst, "Valletta", "Malta");
  for (i = 0; i < 100; i += 10)
    for (j = 0; j < i/10 ; j++ )  
      _stp_map_add_int64_stat (mapsst, i);

    _stp_map_print (mapsst, "Bogons per packet for %1s\ncount:%C  sum:%S  avg:%A  min:%m  max:%M\n%H");

    _stp_map_print (mapsst, "%C was the count for %1s, %2s");

    /* here's how to print a map without using _stp_map_print(). */
    struct map_node *ptr;
    foreach (mapsst, ptr)
      _stp_printf ("mapsst[%09s,%09s] = %llX\n", key1str(ptr), key2str(ptr), _stp_get_stat(ptr)->sum);
    _stp_print_flush();
    
    return 0;
}
