#ifndef _MAP_VALUES_C_ /* -*- linux-c -*- */
#define _MAP_VALUES_C_

/** @file map-values.c
 * @brief Includes the proper value functions for maps.
 */

#include "map.h"
#include "map-str.c"
#include "map-stat.c"
#include "map-int.c"

/** Adds an int64 to the current element's value.
 * This adds an int64 to the current element's value. The map must have been created
 * to hold int64s or stats.
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val value
 * @returns \li \c 0 on success \li \c -1 on overflow \li \c -2 on bad map or key
 * @ingroup map_set
 */
int _stp_map_add_int64 (MAP map, int64_t val)
{
	if (map == NULL)
		return -2;

	if (map->type == INT64) 
		return __stp_map_set_int64 (map, val, 1);

	if (map->type == STAT) 
		return _stp_map_add_stat (map, val);

	/* shouldn't get here */
	return -2;
}

unsigned _stp_map_entry_exists (MAP map)
{
	if (map == NULL || map->create || map->key == NULL)
		return 0;
	return 1;
}


#endif /* _MAP_VALUES_C_ */

