#include "test.h"

/* testl64_alloc.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - long
   val - INT64
   uses kalloc to allocate buffers dynamically. No circular buffers
*/

int main ()
{
  MAP mymap = map_new(0, INT64);

  map_key_long (mymap, 1);
  map_set_int64 (mymap, 2);  
  printf ("mymap[%d]=%ld\n", 1, map_get_int64(mymap));

  map_key_long (mymap, 42);
  map_set_int64 (mymap, 83);

  map_key_long (mymap, 101);
  map_set_int64 (mymap, 202);
  /* at this point, we have 1, 42, and 101 in the array */

  printf ("mymap[%d]=%ld  ", 101, map_get_int64(mymap));
  map_key_long (mymap, 1);  
  printf ("mymap[%d]=%ld  ", 1, map_get_int64(mymap));
  map_key_long (mymap, 42);
  printf ("mymap[%d]=%ld\n", 42, map_get_int64(mymap));

  map_set_int64 (mymap, 84);  
  printf ("mymap[%d]=%ld\n", 42, map_get_int64(mymap));

  /* now try to confuse things */
  /* These won't do anything useful, but shouldn't crash */
  map_key_long (mymap, 0);  
  map_key_del (mymap);
  map_key_long (mymap, 77);  
  map_key_del (mymap);
  map_key_del (mymap);
  map_set_int64 (mymap,1000000);

  /* create and delete a key */
  map_key_long (mymap, 1024);
  map_set_int64 (mymap, 2048);  
  map_key_long (mymap, 1024);
  printf ("mymap[%d]=%ld\n", 1024, map_get_int64(mymap));
  map_key_del (mymap);
  printf ("mymap[%d]=%ld\n", 1024, map_get_int64(mymap));
  map_key_long (mymap, 10);
  printf ("mymap[%d]=%ld\n", 10, map_get_int64(mymap));

  printf ("\n");
  struct map_node_int64 *ptr;
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);


  printf ("\n");
  map_key_long (mymap, 1);   map_key_del (mymap);
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);

  printf ("\n");
  map_key_long (mymap, 42);   map_key_del (mymap);
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);


  printf ("\n");
  map_key_long (mymap, 101);   map_key_del (mymap);
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);
  
  printf ("\n");

  map_key_long (mymap, 10); map_set_int64 (mymap, 20);
  printf ("mymap[%d]=%ld\n", 10, map_get_int64(mymap));

  /* add 4 new entries, NOT pushing "10" out */
  int i;
  for (i = 0; i < 4; i++)
    {
      map_key_long (mymap, i);
      map_set_int64 (mymap, 100 + 2 * i);
    }
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);

  map_key_long (mymap, 10); map_key_del (mymap);
  map_key_long (mymap, 3); map_key_del (mymap);
  map_key_long (mymap, 2); map_key_del (mymap);
  map_key_long (mymap, 1); map_key_del (mymap);
  map_key_long (mymap, 0); map_key_del (mymap);

  /* 5, 382, 526, and 903 all hash to the same value (23) */
  /* use them to test the hash chain */
  map_key_long (mymap, 5); map_set_int64 (mymap, 1005);
  map_key_long (mymap, 382); map_set_int64 (mymap, 1382);
  map_key_long (mymap, 526); map_set_int64 (mymap, 1526);
  map_key_long (mymap, 903); map_set_int64 (mymap, 1903);

  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);
    
  map_key_long (mymap, 382); map_key_del (mymap);
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);

  map_key_long (mymap, 5); map_key_del (mymap);
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);

  map_key_long (mymap, 903); map_key_del (mymap);
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);

  map_key_long (mymap, 526); map_key_del (mymap);
  printf ("\n");
  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %d\n", key1int(ptr), (int)ptr->val);

  return 0;
}
