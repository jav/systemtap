/* -*- linux-c -*- 
 * Map String Functions
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

/** @file map-str.c
 * @brief Map functions to set and get strings
 */

/* from map.c */
void str_copy(char *dest, char *src);

void str_add(void *dest, char *val)
{
	char *dst = (char *)dest;
	int len = strlen(val);
	int len1 = strlen(dst);
	int num = MAP_STRING_LENGTH - 1 - len1;

	if (len > num)
		len = num;
	strncpy (&dst[len1], val, len);
	dst[len + len1] = 0;
}

int __stp_map_set_str (MAP map, char *val, int add)
{
	struct map_node *m;

	if (map == NULL)
		return -2;

	if (map->create) {
		if (val == 0 && !map->list)
			return 0;

		m = __stp_map_create (map);
		if (!m)
			return -1;
		
		/* set the value */
		//dbug ("m=%lx offset=%lx\n", (long)m, (long)map->data_offset);
		str_copy((void *)((long)m + map->data_offset), val);
	} else {
		if (map->key == NULL)
			return -2;
		
		if (val) {
			if (add)
				str_add((void *)((long)map->key + map->data_offset), val);
			else
				str_copy((void *)((long)map->key + map->data_offset), val);
		} else if (!add) {
			/* setting value to 0 is the same as deleting */
			_stp_map_key_del(map);
		}
	}
	return 0;
}

/** Set the current element's value to a string.
 * This sets the current element's value to a string. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param str String containing new value.
 * @returns \li \c 0 on success \li \c -1 on overflow \li \c -2 on bad map or key
 * @sa _stp_map_set()
 * @ingroup map_set
 */
#define _stp_map_set_str(map,val) __stp_map_set_str(map,val,0)
/** Add to the current element's string value.
 * This sets the current element's value to a string consisting of the old
 * contents followed by the new string. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val String containing value to append.
 * @returns \li \c 0 on success \li \c -1 on overflow \li \c -2 on bad map or key
 * @ingroup map_set
 */
#define _stp_map_add_str(map,val) __stp_map_set_str(map,val,1)

/** Get the current element's string value.
 * This gets the current element's string value. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If no current element (key) is set for the map, this function 
 * returns NULL.
 * @param map
 * @sa _stp_map_set()
 * @ingroup map_set
 */
char *_stp_map_get_str (MAP map)
{
	struct map_node *m;
	if (map == NULL || map->create || map->key == NULL)
		return 0;
	//dbug ("key %lx\n", (long)map->key);
	m = (struct map_node *)map->key;
	return (char *)((long)m + map->data_offset);
}

/** Set the current element's value to String.
 * This sets the current element's value to a String. The map must have been created
 * to hold strings using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param str String containing new value.
 * @returns 0 on success, -1 on error.
 * @sa _stp_map_set()
 * @ingroup map_set
 */

void _stp_map_set_string (MAP map, String str)
{
	__stp_map_set_str (map, str->buf, 0);
}

