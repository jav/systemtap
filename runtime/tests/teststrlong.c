#include "test.h"

/* teststrlong.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - str,long
   val - INT64
*/



static void
map_dump (MAP map)
{
  struct map_node_int64 *ptr;
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(map); ptr; 
       ptr = (struct map_node_int64 *)map_iter (map, (struct map_node *)ptr))
    printf ("map[%s,%ld] = %lld\n", key1str(ptr), key2int(ptr), 
	    (long long)ptr->val);
  printf ("\n");
}

static void m_print (MAP map)
{
  struct map_node_int64 *m = (struct map_node_int64 *)map->key;
  printf ("map[%s,%ld]=%lld\n", key1str(m), key2int(m), 
	  (long long)map_get_int64(map));
}
int main ()
{
  MAP mymap = map_new(4, INT64);

  _stp_map_key2 (mymap, "two", 3);
  _stp_map_set (mymap, 6);
  m_print (mymap);

  _stp_map_key2 (mymap, "eighty-four", 2);
  map_set_int64 (mymap, 167);

  _stp_map_key2 (mymap, "two-oh-two", 4);
  map_set_int64 (mymap, 808);
  /* at this point, we have 6, 167, and 808 in the array */

  m_print (mymap);
  _stp_map_key2 (mymap, "two", 3);  
  m_print (mymap);
  _stp_map_key2 (mymap, "eighty-four", 2);
  m_print (mymap);

  map_set_int64 (mymap, 168);  
  m_print (mymap);


  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_key2 (mymap, NULL, 0);  
  map_key_del (mymap);

  _stp_map_key2 (mymap, "0123456789", 4444);  
  map_set_int64 (mymap,1000000);

  map_dump (mymap);

  _stp_map_key2 (mymap, "77", 66);  
  map_key_del (mymap);
  map_key_del (mymap);
  map_set_int64 (mymap,99999999);

  /* create and delete a key */
  _stp_map_key2 (mymap, "2048", 2);
  map_set_int64 (mymap, 4096);

  _stp_map_key2 (mymap, "2048", 2);
  m_print (mymap);
  map_key_del (mymap);

  printf ("mymap[2048,2]=%ld\n", map_get_int64(mymap));
  _stp_map_key2 (mymap, "six", 10);
  printf ("mymap[six,10]=%ld\n", map_get_int64(mymap));

  map_dump(mymap);

  _stp_map_key2 (mymap, "two-oh-two", 4);   map_key_del (mymap);
  map_dump(mymap);


  _stp_map_key2 (mymap, "Ohio", 1801); map_set_int64 (mymap, 10123456);
  map_dump(mymap);

  /* add 4 new entries, pushing "Ohio" out */
  int i;
  for (i = 2; i < 6; i++)
    {
      char buf[32];
      sprintf (buf, "test_number_%d", i);
      _stp_map_key2 (mymap, buf, (long)i);
      _stp_map_set_int64 (mymap, i*i*i);
    }
  map_dump (mymap);

  /* delete all entries */
  for (i = 2; i < 6; i++)
    {
      char buf[32];
      sprintf (buf, "test_number_%d", i);
      _stp_map_key2 (mymap, buf, (long)i);
      map_key_del (mymap);
    }
  
  printf ("Should be empty: ");
  map_dump (mymap);
  return 0;
}
