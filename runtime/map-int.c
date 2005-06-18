/* -*- linux-c -*- */

/** @file map-int.c
 * @brief Map functions to set and get int64s
 */

void __stp_map_set_int64 (MAP map, int64_t val, int add)
{
	struct map_node *m;

	if (map == NULL)
		return;

	if (map->create) {
		if (val == 0 && !map->no_wrap)
			return;

		m = __stp_map_create (map);
		if (!m)
			return;
		
		/* set the value */
		dbug ("m=%lx offset=%lx\n", (long)m, (long)map->data_offset);
		*(int64_t *)((long)m + map->data_offset) = val;
	} else {
		if (map->key == NULL)
			return;
		
		if (val) {
			if (add)
				*(int64_t *)((long)map->key + map->data_offset) += val;
			else
				*(int64_t *)((long)map->key + map->data_offset) = val;
		} else if (!add) {
			/* setting value to 0 is the same as deleting */
			_stp_map_key_del(map);
		}
	}
}
/** Set the current element's value to an int64.
 * This sets the current element's value to an int64. The map must have been created
 * to hold int64s using <i>_stp_map_new_(xxx, INT64)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val new value
 * @sa _stp_map_add_int64()
 * @sa _stp_map_set()
 * @ingroup map_set
 */
#define _stp_map_set_int64(map,val) __stp_map_set_int64 (map,val,0)

/** Get the value of a map.
 * This gets the current element's int64 value. The map must have been created
 * to hold int64s using <i>_stp_map_new_(xxx, INT64)</i>
 *
 * If no current element (key) is set for the map, this function returns 0.
 *
 * @ingroup map_set
 * @param map 
 * @returns an int64 value.
 */
int64_t _stp_map_get_int64 (MAP map)
{
	struct map_node *m;
	if (map == NULL || map->create || map->key == NULL)
		return 0;
	dbug ("key %lx\n", (long)map->key);
	m = (struct map_node *)map->key;
	return *(int64_t *)((long)m + map->data_offset);
}

