#include "test.h"

/* teststrstr.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - str,str
   val - INT64

   Testing circular buffers, set, get 
*/

int main ()
{
  struct map_node_int64 *ptr;
  MAP mymap = map_new(4, INT64);

  map_key_str_str (mymap, "two", "three");
  map_set_int64 (mymap, 6);  
  printf ("mymap[two,three]=%ld\n", map_get_int64(mymap));

  map_key_str_str (mymap, "eighty-four", "two");
  map_set_int64 (mymap, 167);

  map_key_str_str (mymap, "two-oh-two", "four");
  map_set_int64 (mymap, 808);
  /* at this point, we have 6, 167, and 808 in the array */

  printf ("mymap[two-oh-two,four]=%ld  ",map_get_int64(mymap));
  map_key_str_str (mymap, "two", "three");  
  printf ("mymap[two,three]=%ld  ", map_get_int64(mymap));
  map_key_str_str (mymap, "eighty-four", "two");
  printf ("mymap[eighty-four,two]=%ld\n", map_get_int64(mymap));

  map_set_int64 (mymap, 168);  
  printf ("mymap[eighty-four,two]=%ld\n", map_get_int64(mymap));


  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  map_key_str_str (mymap, NULL, NULL);  
  map_key_del (mymap);

  map_key_str_str (mymap, "0123456789", "foo");  
  map_set_int64 (mymap,1000000);

  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s,%s] = %d\n", key1str(ptr), key2str(ptr), (int)ptr->val);

  map_key_str_str (mymap, "77", "66");  
  map_key_del (mymap);
  map_key_del (mymap);
  map_set_int64 (mymap,99999999);

  /* create and delete a key */
  map_key_str_str (mymap, "2048", "2");
  map_set_int64 (mymap, 4096);  

  map_key_str_str (mymap, "2048", "2");
  printf ("mymap[2048,2]=%ld\n", map_get_int64(mymap));
  map_key_del (mymap);

  printf ("mymap[2048,2]=%ld\n", map_get_int64(mymap));
  map_key_str_str (mymap, "10", "six");
  printf ("mymap[10,six]=%ld\n", map_get_int64(mymap));

  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s,%s] = %d\n", key1str(ptr), key2str(ptr), (int)ptr->val);

  map_key_str_str (mymap, "two-oh-two", "four");   map_key_del (mymap);
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s,%s] = %d\n", key1str(ptr), key2str(ptr), (int)ptr->val);


  map_key_str_str (mymap, "Ohio", "1801"); map_set_int64 (mymap, 10123456);
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s,%s] = %d\n", key1str(ptr), key2str(ptr), (int)ptr->val);

  /* add 4 new entries, pushing "Ohio" out */
  int i;
  for (i = 2; i < 6; i++)
    {
      char buf[32], buf2[32];
      sprintf (buf, "test_number_%d", i);
      sprintf (buf2, "**test number %d**", i*i);
      map_key_str_str (mymap, buf, buf2);
      map_set_int64 (mymap, i*i*i);
    }
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s,%s] = %d\n", key1str(ptr), key2str(ptr), (int)ptr->val);


  return 0;
}
