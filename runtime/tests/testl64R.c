#include "test.h"

#define LLONG_MAX    9223372036854775807LL
#define LLONG_MIN    (-LLONG_MAX - 1LL)

/* testl64R.c - DO NOT EDIT without updating the expected results in map.test. */

/* 
   key - long
   val - INT64

   Testing range of values
*/

int main ()
{
  struct map_node_int64 *ptr;
  MAP mymap = map_new(4, INT64);

  map_key_long (mymap, 1);
  map_set_int64 (mymap, LLONG_MIN);
  map_key_long (mymap, 2);
  map_set_int64 (mymap, LLONG_MAX);
  map_key_long (mymap, 3);
  map_set_int64 (mymap, 0); /* will not be saved */
  map_key_long (mymap, 4);
  map_set_int64 (mymap, -1);
  map_key_long (mymap, 5);
  map_set_int64 (mymap, 5);


  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %lld\n", key1int(ptr), (long long)ptr->val);


  /* stress test - create a million entries then print last 4 */
  int i;
  for (i = 0; i < 1000000; i++)
    {
      map_key_long (mymap, i);
      map_set_int64 (mymap, i+i);
    }

  for (ptr = (struct map_node_int64 *)map_start(mymap); ptr; 
       ptr = (struct map_node_int64 *)map_iter (mymap, (struct map_node *)ptr))
    printf ("mymap[%ld] = %lld\n", key1int(ptr), (long long)ptr->val);

  return 0;
}
