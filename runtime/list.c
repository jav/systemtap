#ifndef _LIST_C_ /* -*- linux-c -*- */
#define _LIST_C_

#include "map.c"
#include "copy.c"

/**********************  List Functions *********************/
/** @file list.c
 * @brief List Functions
 */

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
  map->list = 1;
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
		_stp_warn ("list is supposed to be empty (has %d)\n", map->num);
	}
}

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

/** Get the number of elements in a list.
 * @param map
 * @returns The number of elements in a list.
 */

inline int _stp_list_size(MAP map)
{
	return map->num;
}

/** Copy an argv from user space to a List.
 *
 * @param list A list.
 * @param argv Source argv, in user space.
 * @return number of elements in <i>list</i>
 *
 * @b Example:
 * @include argv.c
 */

int _stp_copy_argv_from_user (MAP list, char __user *__user *argv)
{
	char str[128];
	char __user *vstr;
	int len;

	if (argv)
		argv++;

	while (argv != NULL) {
		if (get_user (vstr, argv))
			break;
		
		if (vstr == NULL)
			break;
		
		len = _stp_strncpy_from_user(str, vstr, 128);
		str[len] = 0;
		_stp_list_add_str (list, str);
		argv++;
	}
	return list->num;
}

/** @} */
#endif /* _LIST_C_ */
