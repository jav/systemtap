/* example showing how to print all the stats in a map using foreach() */

struct map_node_stat *ptr;

foreach (map, ptr)
     printf ("map[%s,%ld] = [c=%lld s=%lld min=%lld max=%lld]\n", key1str(ptr), 
	     key2int(ptr), ptr->stats.count, ptr->stats.sum, ptr->stats.min, 
	     ptr->stats.max);

