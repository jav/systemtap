#include "test.h"

/* teststr.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - str
   val - INT64

   Testing circular buffers, set, get 
*/

int main ()
{
  struct map_node_int64 *ptr;

  MAP mymap = map_new(4, INT64);

  map_key_str (mymap, "two");
  map_set_int64 (mymap, 2);  
  printf ("mymap[two]=%ld\n", map_get_int64(mymap));

  map_key_str (mymap, "eighty-four");
  map_set_int64 (mymap, 83);

  map_key_str (mymap, "two-oh-two");
  map_set_int64 (mymap, 202);
  /* at this point, we have 2, 83, and 202 in the array */

  printf ("mymap[two-oh-two]=%ld  ",map_get_int64(mymap));
  map_key_str (mymap, "two");  
  printf ("mymap[two]=%ld  ", map_get_int64(mymap));
  map_key_str (mymap, "eighty-four");
  printf ("mymap[eighty-four]=%ld\n", map_get_int64(mymap));

  map_set_int64 (mymap, 84);  
  printf ("mymap[eighty-four]=%ld\n", map_get_int64(mymap));


  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  map_key_str (mymap, NULL);  
  map_key_del (mymap);

  map_key_str (mymap, "0123456789");  
  map_set_int64 (mymap,1000000);

  printf ("\n");

  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s] = %d\n", key1str(ptr), (int)ptr->val);

  map_key_str (mymap, "77");  
  map_key_del (mymap);
  map_key_del (mymap);
  map_set_int64 (mymap,99999999);

  /* create and delete a key */
  map_key_str (mymap, "2048");
  map_set_int64 (mymap, 2048);  

  //map_key_str (mymap, "2048");
  printf ("mymap[2048]=%ld\n", map_get_int64(mymap));
  map_key_del (mymap);

  printf ("mymap[2048]=%ld\n", map_get_int64(mymap));
  map_key_str (mymap, "10");
  printf ("mymap[10]=%ld\n", map_get_int64(mymap));

  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s] = %d\n", key1str(ptr), (int)ptr->val);

  printf ("\n");
  map_key_str (mymap, "two-oh-two");   map_key_del (mymap);
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s] = %d\n", key1str(ptr), (int)ptr->val);

  printf ("\n");
  map_key_str (mymap, "eighty-four");   map_key_del (mymap);
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s] = %d\n", key1str(ptr), (int)ptr->val);

  printf ("\n");
  map_key_str (mymap, "0123456789");   map_key_del (mymap);
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s] = %d\n", key1str(ptr), (int)ptr->val);
  
  printf ("\n");

  map_key_str (mymap, "Ohio"); map_set_int64 (mymap, 10123456);
  printf ("mymap[Ohio]=%ld\n",map_get_int64(mymap));

  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s] = %d\n", key1str(ptr), (int)ptr->val);
  printf ("\n");

  /* add 4 new entries, pushing "Ohio" out */
  int i;
  for (i = 0; i < 4; i++)
    {
      char buf[32];
      sprintf (buf, "test_number_%d", i);
      map_key_str (mymap, buf);
      map_set_int64 (mymap, 1000 + i);
    }
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%s] = %d\n", key1str(ptr), (int)ptr->val);

  return 0;
}
