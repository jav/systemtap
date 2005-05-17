#ifndef _LIST_C_ /* -*- linux-c -*- */
#define _LIST_C_

#ifndef NEED_INT64_KEYS
#error Before including list.c, "#define KEY1_TYPE INT64" and include "map-keys.c"
#endif

#if !defined(NEED_STRING_VALS) && !defined(NEED_INT64_VALS)
#error Before including list.c, "#define VALUE_TYPE" to "INT64" or "STRING and include "map-values.c"
#endif

#include "map.c"

/**********************  List Functions *********************/

/** @addtogroup lists
 * Lists are special cases of maps.
 * @b Example:
 * @include list.c
 * @{ */

/** Create a new list.
 * A list is a map that internally has an incrementing long key for each member.
 * Lists do not wrap if elements are added to exceed their maximum size.
 * @param max_entries The maximum number of entries allowed. Currently that number will
 * be preallocated.  If max_entries is 0, there will be no maximum and entries
 * will be allocated dynamically.
 * @param type Type of values stored in this list. 
 * @return A MAP on success or NULL on failure.
 * @sa foreach
 */

MAP _stp_list_new(unsigned max_entries, int type)
{
  MAP map = _stp_map_new_int64 (max_entries, type);
  map->no_wrap = 1;
  return map;
}

/** Clears a list.
 * All elements in the list are deleted.
 * @param map 
 */

void _stp_list_clear(MAP map)
{
	if (map == NULL)
		return;

	if (!list_empty(&map->head)) {
		struct map_node *ptr = (struct map_node *)map->head.next;

		while (ptr && ptr != (struct map_node *)&map->head) {
			struct map_node *next = (struct map_node *)ptr->lnode.next;

			/* remove node from old hash list */
			hlist_del_init(&ptr->hnode);

			/* remove from entry list */
			list_del(&ptr->lnode);
			
			list_add(&ptr->lnode, &map->pool);

			map->num--;
			ptr = next;
		}
	}

	if (map->num != 0) {
		_stp_log ("ERROR: list is supposed to be empty (has %d)\n", map->num);
	}
}

#ifdef NEED_STRING_VALS
/** Adds a C string to a list.
 * @param map
 * @param str
 * @sa _stp_list_add()
 */

inline void _stp_list_add_str(MAP map, char *str)
{
	_stp_map_key_int64 (map, map->num);
	_stp_map_set_str(map, str);
}

/** Adds a String to a list.
 * @param map
 * @param str String to add.
 * @sa _stp_list_add()
 */

inline void _stp_list_add_string (MAP map, String str)
{
	_stp_map_key_int64 (map, map->num);
	_stp_map_set_str(map, str->buf);
}
#endif /* NEED_STRING_VALS */

#ifdef NEED_INT64_VALS
/** Adds an int64 to a list.
 * @param map
 * @param val
 * @sa _stp_list_add()
 */

inline void _stp_list_add_int64(MAP map, int64_t val)
{
	_stp_map_key_int64 (map, map->num);
	_stp_map_set_int64(map, val);
}
#endif /* NEED_INT64_VALS */

/** Get the number of elements in a list.
 * @param map
 * @returns The number of elements in a list.
 */

inline int _stp_list_size(MAP map)
{
	return map->num;
}
/** @} */
#endif /* _LIST_C_ */
