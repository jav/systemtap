#include "test.h"

/* teststat.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - str,long
   val - stat
*/



static void
map_dump (MAP map)
{
  struct map_node_stat *ptr;
  printf ("\n");
  foreach (map, ptr)
    printf ("map[%s,%ld] = [c=%lld s=%lld minmax =%lld,%lld]\n", key1str(ptr), 
	    key2int(ptr), ptr->stats.count, ptr->stats.sum, ptr->stats.min, ptr->stats.max);
  printf ("\n");
}

static void m_print (MAP map)
{
  struct map_node_stat *m = (struct map_node_stat *)map->key;
  stat *st = _stp_map_get_stat (map);
  printf ("map[%s,%ld] = [c=%ld s=%ld minmax =%ld,%ld]\n", key1str(m), key2int(m), 
	  (long)st->count, (long)st->sum, (long)st->min, (long)st->max);
}
int main ()
{
  stat st, *stp;

  MAP mymap = map_new(4, STAT);

  st.count = 5; st.sum = 125; st.min = 2; st.max = 42;
  _stp_map_key2 (mymap, "created with set", 2001 );
  map_set_stat (mymap, &st);
  m_print (mymap);

  _stp_map_stat_add (mymap, 17);
  m_print (mymap);

  _stp_map_key2 (mymap, "created with add", 2020 );
  _stp_map_stat_add (mymap, 1700);
  m_print (mymap);
  _stp_map_stat_add (mymap, 2);
  m_print (mymap);
  _stp_map_stat_add (mymap, 2345);
  m_print (mymap);

  map_dump(mymap);
  _stp_map_key2 (mymap, "created with set", 2001 ); map_key_del (mymap);
  map_dump(mymap);
  _stp_map_key2 (mymap, "created with add", 2020 ); 
  st.sum=123456;
  map_set_stat (mymap, &st);
  map_dump(mymap);
  map_key_del (mymap);
  map_dump(mymap);

  mymap = map_new(4, STAT);
  _stp_map_key2 (mymap, "created with add", 1234 );
  _stp_map_stat_add (mymap, 42);
  _stp_map_stat_add (mymap, 58);
  stp = _stp_map_get_stat (mymap);
  m_print(mymap);
  map_dump(mymap);
  _stp_map_del (mymap);

  return 0;
}
