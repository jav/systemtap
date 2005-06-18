
struct map_node *ptr;

MAP map = _stp_list_new(10, STRING);

for (i = 0; i < 10; i++) {
     sprintf (buf, "Item%d", i);
     _stp_list_add (map, buf);
 }

/* old way to print a list of strings */
foreach (map, ptr)
     _stp_printf ("list[%ld] = %s\n", key1int(ptr), _stp_get_str(ptr));


/* new way to print a list of strings */
 _stp_map_print(map, "list[%1d] = %s");
