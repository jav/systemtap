#include "test.h"

/* testlongstr.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - long,str
   val - INT64
*/



static void
map_dump (MAP map)
{
  struct map_node_int64 *ptr;
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(map); ptr; 
       ptr = (struct map_node_int64 *)map_iter (map, (struct map_node *)ptr))
    printf ("map[%ld,%s] = %lld\n", key1int(ptr), key2str(ptr), 
	    (long long)ptr->val);
  printf ("\n");
}

static void m_print (MAP map)
{
  struct map_node_int64 *m = (struct map_node_int64 *)map->key;
  printf ("map[%ld,%s]=%lld\n", key1int(m), key2str(m), 
	  (long long)map_get_int64(map));
}
int main ()
{
  MAP mymap = map_new(4, INT64);

  _stp_map_key2 (mymap, 3, "two");
  _stp_map_set (mymap, 6);
  m_print (mymap);

  _stp_map_key2 (mymap, 2, "eighty-four");
  map_set_int64 (mymap, 167);

  _stp_map_key2 (mymap, 4, "two-oh-two");
  map_set_int64 (mymap, 808);
  /* at this point, we have 6, 167, and 808 in the array */

  m_print (mymap);
  _stp_map_key2 (mymap, 3, "two");  
  m_print (mymap);
  _stp_map_key2 (mymap, 2, "eighty-four");
  m_print (mymap);

  map_set_int64 (mymap, 168);  
  m_print (mymap);


  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  _stp_map_key2 (mymap, 0, NULL);  
  map_key_del (mymap);

  _stp_map_key2 (mymap, 4444, "0123456789");
  map_set_int64 (mymap,1000000);

  map_dump (mymap);

  _stp_map_key2 (mymap, 77, "66");  
  map_key_del (mymap);
  map_key_del (mymap);
  map_set_int64 (mymap,99999999);

  /* create and delete a key */
  _stp_map_key2 (mymap, 2048, "2");
  map_set_int64 (mymap, 4096);

  _stp_map_key2 (mymap, 2048, "2");
  m_print (mymap);
  map_key_del (mymap);

  printf ("mymap[2048,2]=%ld\n", map_get_int64(mymap));
  _stp_map_key2 (mymap, 10, "six");
  printf ("mymap[10,six]=%ld\n", map_get_int64(mymap));

  map_dump(mymap);

  _stp_map_key2 (mymap, 4, "two-oh-two");   map_key_del (mymap);
  map_dump(mymap);

  _stp_map_key2 (mymap, 4444, "0123456789");   map_key_del (mymap);
  map_dump(mymap);

  _stp_map_key2 (mymap, 2, "eighty-four");   map_key_del (mymap);
  map_dump(mymap);


  _stp_map_key2 (mymap, 1801, "Ohio"); map_set_int64 (mymap, 10123456);
  map_dump(mymap);

  /* add 4 new entries, pushing "Ohio" out */
  int i;
  for (i = 2; i < 6; i++)
    {
      char buf[32];
      sprintf (buf, "test_number_%d", i);
      _stp_map_key2 (mymap, (long)i, buf);
      _stp_map_set_int64 (mymap, i*i*i);
    }
  map_dump (mymap);

  /* delete all entries */
  for (i = 2; i < 6; i++)
    {
      char buf[32];
      sprintf (buf, "test_number_%d", i);
      _stp_map_key2 (mymap, (long)i, buf);
      map_key_del (mymap);
    }
  
  printf ("Should be empty: ");
  map_dump (mymap);
  return 0;
}
