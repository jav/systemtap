#ifndef _MAP_VALUES_C_ /* -*- linux-c -*- */
#define _MAP_VALUES_C_

/** @file map-values.c
 * @brief Includes the proper value functions for maps.
 */

#include "map.h"

#if !defined(NEED_STRING_VALS) && !defined(NEED_INT64_VALS) && !defined(NEED_STAT_VALS)
#error Need to define at least one of NEED_STRING_VALS, NEED_INT64_VALS and NEED_STAT_VALS
#endif

#ifdef NEED_STRING_VALS
#include "map-str.c"
#endif

#ifdef NEED_STAT_VALS
#include "map-stat.c"
#endif

#ifdef NEED_INT64_VALS
#include "map-int.c"
#endif


#if defined(NEED_INT64_VALS) || defined (NEED_STAT_VALS)
/** Adds an int64 to the current element's value.
 * This adds an int64 to the current element's value. The map must have been created
 * to hold int64s or stats.
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val value
 * @ingroup map_set
 */
void _stp_map_add_int64 (MAP map, int64_t val)
{
	if (map == NULL)
		return;

#ifdef NEED_INT64_VALS
	if (map->type == INT64) 
		__stp_map_set_int64 (map, val, 1);
#endif
#ifdef NEED_STAT_VALS
	if (map->type == STAT) 
		_stp_map_add_stat (map, val);
#endif
}
#endif

#endif /* _MAP_VALUES_C_ */

