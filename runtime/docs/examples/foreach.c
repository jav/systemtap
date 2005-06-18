/* example showing how to print all the stats in a map using foreach() */

    struct map_node *ptr;

    foreach (mapsst, ptr)
      _stp_printf ("mapsst[%09s,%09s] = %llX\n", key1str(ptr), key2str(ptr), _stp_get_stat(ptr)->sum);



