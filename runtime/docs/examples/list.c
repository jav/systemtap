
struct map_node_str *ptr;

MAP map = _stp_list_new(10, STRING);

for (i = 0; i < 10; i++) {
     sprintf (buf, "Item%d", i);
     _stp_list_add (map, buf);
 }

foreach (map, ptr)
     printf ("map[%ld] = %s\n", key1int(ptr), ptr->str);


