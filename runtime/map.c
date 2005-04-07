#ifndef _MAP_C_
#define _MAP_C_

/* -*- linux-c -*- */
/** @file map.c
 * @brief Implements maps (associative arrays) and lists
 */

#include "map.h"
#include "alloc.c"
#include "string.c"

static int map_sizes[] = {
	sizeof(struct map_node_int64),
	sizeof(struct map_node_stat),
	sizeof(struct map_node_str),
	0
};

static unsigned string_hash(const char *key1, const char *key2)
{
	int hash = 0, count = 0;
	char *v1 = (char *)key1;
	char *v2 = (char *)key2;
	while (*v1 && count++ < 5) {
		hash += *v1++;
	}
	while (v2 && *v2 && count++ < 5) {
		hash += *v2++;
	}
	return hash_long((unsigned long)hash, HASH_TABLE_BITS);
}

static unsigned mixed_hash(const char *key1, long key2)
{
	int hash = 0, count = 0;
	char *v = (char *)key1;
	while (v && *v && count++ < 5)
		hash += *v++;
	return hash_long((unsigned long)(hash ^ key2), HASH_TABLE_BITS);
}

/** @addtogroup maps 
 * Implements maps (associative arrays) and lists
 * @{ 
 */

/** Create a new map.
 * Maps must be created at module initialization time.
 * @param max_entries The maximum number of entries allowed. Currently that number will
 * be preallocated.  If more entries are required, the oldest ones will be deleted. This makes
 * it effectively a circular buffer.  If max_entries is 0, there will be no maximum and entries
 * will be allocated dynamically.
 * @param type Type of values stored in this map. 
 * @return A MAP on success or NULL on failure.
 */

MAP _stp_map_new(unsigned max_entries, enum valtype type)
{
	size_t size;
	MAP m = (MAP) _stp_valloc(sizeof(struct map_root));
	if (m == NULL)
		return NULL;

	INIT_LIST_HEAD(&m->head);

	m->maxnum = max_entries;
	m->type = type;
	if (type >= END) {
		dbug ("map_new: unknown type %d\n", type);
		return NULL;
	}

	if (max_entries) {
		void *tmp;
		int i;
		struct list_head *e;

		INIT_LIST_HEAD(&m->pool);
		size = map_sizes[type];
		tmp = _stp_valloc(max_entries * size);

		for (i = max_entries - 1; i >= 0; i--) {
			e = i * size + tmp;
			dbug ("e=%lx\n", (long)e);
			list_add(e, &m->pool);
		}
		m->membuf = tmp;
	}
	return m;
}

static void map_free_strings(MAP map, struct map_node *n)
{
	struct map_node_str *m = (struct map_node_str *)n;
	dbug ("n = %lx\n", (long)n);
	if (map->type == STRING) {
		dbug ("val STRING %lx\n", (long)m->str);
		if (m->str)
			_stp_free(m->str);
	}
	if (m->n.key1type == STR) {
		dbug ("key1 STR %lx\n", (long)key1str(m));
		if (key1str(m))
			_stp_free(key1str(m));
	}
	if (m->n.key2type == STR) {
		dbug ("key2 STR %lx\n", (long)key2str(m));
		if (key2str(m))
			_stp_free(key2str(m));
	}
}

/** Deletes the current element.
 * If no current element (key) for this map is set, this function does nothing.
 * @param map 
 */

void _stp_map_key_del(MAP map)
{
	struct map_node *m;

	dbug ("create=%d key=%lx\n", map->create, (long)map->key);
	if (map == NULL)
		return;

	if (map->create) {
		map->create = 0;
		map->key = NULL;
		return;
	}

	if (map->key == NULL)
		return;

	m = (struct map_node *)map->key;

	/* remove node from old hash list */
	hlist_del_init(&m->hnode);

	/* remove from entry list */
	list_del(&m->lnode);

	/* remove any allocated string storage */
	map_free_strings(map, (struct map_node *)map->key);

	if (map->maxnum)
		list_add(&m->lnode, &map->pool);
	else
		_stp_free(m);

	map->key = NULL;
	map->num--;
}

/** Get the first element in a map.
 * @param map 
 * @returns a pointer to the first element.
 * This is typically used with _stp_map_iter().  See the foreach() macro
 * for typical usage.  It probably does what you want anyway.
 * @sa foreach
 */

struct map_node *_stp_map_start(MAP map)
{
	if (map == NULL)
		return NULL;

	dbug ("%lx\n", (long)map->head.next);

	if (list_empty(&map->head))
		return NULL;

	return (struct map_node *)map->head.next;
}

/** Get the next element in a map.
 * @param map 
 * @param m a pointer to the current element, returned from _stp_map_start()
 * or _stp_map_iter().
 * @returns a pointer to the next element.
 * This is typically used with _stp_map_start().  See the foreach() macro
 * for typical usage.  It probably does what you want anyway.
 * @sa foreach
 */

struct map_node *_stp_map_iter(MAP map, struct map_node *m)
{
	if (map == NULL)
		return NULL;

	dbug ("%lx next=%lx  prev=%lx  map->head.next=%lx\n", (long)m, 
	      (long)m->lnode.next, (long)m->lnode.prev, (long)map->head.next);

	if (m->lnode.next == &map->head)
		return NULL;

	return (struct map_node *)m->lnode.next;
}

/** Deletes a map.
 * Deletes a map, freeing all memory in all elements.  Normally done only when the module exits.
 * @param map
 */

void _stp_map_del(MAP map)
{
	if (map == NULL)
		return;

	if (!list_empty(&map->head)) {
		struct map_node *ptr = (struct map_node *)map->head.next;
		while (ptr && ptr != (struct map_node *)&map->head) {
			map_free_strings(map, ptr);
			ptr = (struct map_node *)ptr->lnode.next;
		}
	}
	_stp_vfree(map->membuf);
	_stp_vfree(map);
}

/**********************  KEY FUNCTIONS *********************/


/** Set the map's key to two longs.
 * This sets the current element based on a key of two strings. If the keys are
 * not found, a new element will not be created until a <i>_stp_map_set_xxx</i>
 * call.
 * @param map
 * @param key1 first key
 * @param key2 second key
 */

void _stp_map_key_long_long(MAP map, long key1, long key2)
{
	unsigned hv;
	struct hlist_head *head;
	struct hlist_node *e;

	if (map == NULL)
		return;

	hv = hash_long(key1 ^ key2, HASH_TABLE_BITS);
	head = &map->hashes[hv];

	dbug ("hash for %ld,%ld is %d\n", key1, key2, hv);

	hlist_for_each(e, head) {
		struct map_node *n =
			(struct map_node *)((long)e - sizeof(struct hlist_node));
		dbug ("n =%lx  key=%ld,%ld\n", (long)n, n->key1.val, n->key2.val);
		if (key1 == n->key1.val && key2 == n->key2.val) {
			map->key = n;
			dbug ("saving key %lx\n", (long)map->key);
			map->create = 0;
			return;
		}
	}

	map->c_key1.val = key1;
	map->c_key2.val = key2;
	map->c_key1type = LONG;
	map->c_key2type = LONG;
	map->c_keyhead = head;
	map->create = 1;
}

/** Set the map's key to two strings.
 * This sets the current element based on a key of two strings. If the keys are
 * not found, a new element will not be created until a <i>_stp_map_set_xxx</i>
 * call.
 * @param map
 * @param key1 first key
 * @param key2 second key
 */

void _stp_map_key_str_str(MAP map, char *key1, char *key2)
{
	unsigned hv;
	struct hlist_head *head;
	struct hlist_node *e;

	if (map == NULL)
		return;

	if (key1 == NULL) {
		map->key = NULL;
		return;
	}

	hv = string_hash(key1, key2);
	head = &map->hashes[hv];

	dbug ("hash for %s,%s is %d\n", key1, key2, hv);

	hlist_for_each(e, head) {
		struct map_node *n =
			(struct map_node *)((long)e - sizeof(struct hlist_node));
		dbug ("e =%lx  key=%s,%s\n", (long)e, n->key1.str,n->key2.str);
		if (strcmp(key1, n->key1.str) == 0
		    && (key2 == NULL || strcmp(key2, n->key2.str) == 0)) {
			map->key = n;
			dbug ("saving key %lx\n", (long)map->key);
			map->create = 0;
			return;
		}
	}

	map->c_key1.str = key1;
	map->c_key2.str = key2;
	map->c_key1type = STR;
	map->c_key2type = STR;
	map->c_keyhead = head;
	map->create = 1;
}

/** Set the map's key to a string and a long.
 * This sets the current element based on a key of a string and a long. If the keys are
 * not found, a new element will not be created until a <i>_stp_map_set_xxx</i>
 * call.
 * @param map
 * @param key1 first key
 * @param key2 second key
 */

void _stp_map_key_str_long(MAP map, char *key1, long key2)
{
	unsigned hv;
	struct hlist_head *head;
	struct hlist_node *e;

	if (map == NULL)
		return;

	if (key1 == NULL) {
		map->key = NULL;
		return;
	}

	hv = mixed_hash(key1, key2);
	head = &map->hashes[hv];

	dbug ("hash for %s,%ld is %d\n", key1, key2, hv);

	hlist_for_each(e, head) {
		struct map_node *n =
			(struct map_node *)((long)e - sizeof(struct hlist_node));
		dbug ("e =%lx  key=%s,%ld\n", (long)e, n->key1.str,(long)n->key2.val);
		if (strcmp(key1, n->key1.str) == 0 && key2 == n->key2.val) {
			map->key = n;
			dbug ("saving key %lx\n", (long)map->key);
			map->create = 0;
			return;
		}
	}

	map->c_key1.str = key1;
	map->c_key2.val = key2;
	map->c_key1type = STR;
	map->c_key2type = LONG;
	map->c_keyhead = head;
	map->create = 1;
}

/** Set the map's key to a long and a string.
 * This sets the current element based on a key of a long and a string. If the keys are
 * not found, a new element will not be created until a <i>_stp_map_set_xxx</i>
 * call.
 * @param map
 * @param key1 first key
 * @param key2 second key
 */

void _stp_map_key_long_str(MAP map, long key1, char *key2)
{
	unsigned hv;
	struct hlist_head *head;
	struct hlist_node *e;

	if (map == NULL)
		return;

	hv = mixed_hash(key2, key1);
	head = &map->hashes[hv];
	dbug ("hash for %ld,%s is %d\n", key1, key2, hv);

	hlist_for_each(e, head) {
		struct map_node *n =
			(struct map_node *)((long)e - sizeof(struct hlist_node));
		dbug ("e =%lx  key=%ld,%s\n", (long)e, n->key1.val,n->key2.str);
		if (key1 == n->key1.val && strcmp(key2, n->key2.str) == 0) {
			map->key = n;
			dbug ("saving key %lx\n", (long)map->key);
			map->create = 0;
			return;
		}
	}

	map->c_key1.val = key1;
	map->c_key2.str = key2;
	map->c_key1type = LONG;
	map->c_key2type = STR;
	map->c_keyhead = head;
	map->create = 1;
}

/** Set the map's key to a string.
 * This sets the current element based on a string key. If the key is
 * not found, a new element will not be created until a <i>_stp_map_set_xxx</i>
 * call.
 * @param map
 * @param key
 */

void _stp_map_key_str(MAP map, char *key)
{
	if (map == NULL)
		return;
	_stp_map_key_str_str(map, key, NULL);
	map->c_key2type = NONE;
}

/** Set the map's key to a long.
 * This sets the current element based on a long key. If the key is
 * not found, a new element will not be created until a <i>_stp_map_set_xxx</i>
 * call.
 * @param map
 * @param key 
 */

void _stp_map_key_long(MAP map, long key)
{
	if (map == NULL)
		return;
	_stp_map_key_long_long(map, key, 0);
	map->c_key2type = NONE;
}

/**********************  SET/GET VALUES *********************/

static void map_copy_keys(MAP map, struct map_node *m)
{
	m->key1type = map->c_key1type;
	m->key2type = map->c_key2type;
	switch (map->c_key1type) {
	case STR:
		m->key1.str = _stp_alloc(strlen(map->c_key1.str) + 1);
		strcpy(m->key1.str, map->c_key1.str);
		break;
	case LONG:
		m->key1.val = map->c_key1.val;
		break;
	case NONE:
		/* ERROR */
		break;
	}
	switch (map->c_key2type) {
	case STR:
		m->key2.str = _stp_alloc(strlen(map->c_key2.str) + 1);
		strcpy(m->key2.str, map->c_key2.str);
		break;
	case LONG:
		m->key2.val = map->c_key2.val;
		break;
	case NONE:
		break;
	}

	/* add node to new hash list */
	hlist_add_head(&m->hnode, map->c_keyhead);
	
	map->key = m;
	map->create = 0;
	map->num++;
}

static void __stp_map_set_int64(MAP map, int64_t val, int add)
{
	struct map_node_int64 *m;

	if (map == NULL)
		return;

	if (map->create) {
		if (val == 0)
			return;

		if (map->maxnum) {
			if (list_empty(&map->pool)) {
				if (map->no_wrap) {
					/* ERROR. FIXME */
					return;
				}
				m = (struct map_node_int64 *)map->head.next;
				hlist_del_init(&m->n.hnode);
				map_free_strings(map, (struct map_node *)m);
				dbug ("got %lx off head\n", (long)m);
			} else {
				m = (struct map_node_int64 *)map->pool.next;
				dbug ("got %lx off pool\n", (long)m);
			}
			list_move_tail(&m->n.lnode, &map->head);
		} else {
			m = (struct map_node_int64 *)
			    _stp_calloc(sizeof(struct map_node_int64));
			/* add node to list */
			list_add_tail(&m->n.lnode, &map->head);
		}

		/* copy the key(s) */
		map_copy_keys(map, &m->n);

		/* set the value */
		m->val = val;
	} else {
		if (map->key == NULL)
			return;

		if (val) {
			m = (struct map_node_int64 *)map->key;
			if (add)
				m->val += val;
			else
				m->val = val;
		} else if (!add) {
			/* setting value to 0 is the same as deleting */
			_stp_map_key_del(map);
		}
	}
}

/** Set the current element's value to an int64.
 * This sets the current element's value to an int64. The map must have been created
 * to hold int64s using _stp_map_new()
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val new value
 * @sa _stp_map_add_int64
 */
void _stp_map_set_int64(MAP map, int64_t val)
{
	__stp_map_set_int64 (map, val, 0);
}


/** Adds an int64 to the current element's value.
 * This adds an int64 to the current element's value. The map must have been created
 * to hold int64s using _stp_map_new()
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val value
 * @sa _stp_map_set_int64
 */

void _stp_map_add_int64(MAP map, int64_t val)
{
	__stp_map_set_int64 (map, val, 1);
}

/** Gets the current element's value.
 * @param map
 * @returns The value. If the current element is not set or doesn't exist, returns 0.
 */

int64_t _stp_map_get_int64(MAP map)
{
	struct map_node_int64 *m;
	if (map == NULL || map->create || map->key == NULL)
		return 0;
	dbug ("%lx\n", (long)map->key);
	m = (struct map_node_int64 *)map->key;
	return m->val;
}

/** Set the current element's value to a string.
 * This sets the current element's value to an string. The map must have been created
 * to hold int64s using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val new string
 */

void _stp_map_set_str(MAP map, char *val)
{
	struct map_node_str *m;

	if (map == NULL)
		return;

	if (map->create) {
		if (val == NULL)
			return;

		if (map->maxnum) {
			if (list_empty(&map->pool)) {
				if (map->no_wrap) {
					/* ERROR. FIXME */
					return;
				}
				m = (struct map_node_str *)map->head.next;
				hlist_del_init(&m->n.hnode);
				map_free_strings(map, (struct map_node *)m);
				dbug ("got %lx off head\n", (long)m);
			} else {
				m = (struct map_node_str *)map->pool.next;
				dbug ("got %lx off pool\n", (long)m);
			}
			list_move_tail(&m->n.lnode, &map->head);
		} else {
			m = (struct map_node_str *)
			    _stp_calloc(sizeof(struct map_node_str));
			/* add node to list */
			list_add_tail(&m->n.lnode, &map->head);
		}

		/* copy the key(s) */
		map_copy_keys(map, &m->n);

		/* set the value */
		m->str = _stp_alloc(strlen(val) + 1);
		strcpy(m->str, val);
	} else {
		if (map->key == NULL)
			return;

		if (val) {
			m = (struct map_node_str *)map->key;
			if (m->str)
				_stp_free(m->str);
			m->str = _stp_alloc(strlen(val) + 1);
			strcpy(m->str, val);
		} else {
			/* setting value to 0 is the same as deleting */
			_stp_map_key_del(map);
		}
	}
}

void _stp_map_set_string (MAP map, String str)
{
  _stp_map_set_str (map, str->buf);
}

/** Gets the current element's value.
 * @param map
 * @returns A string pointer. If the current element is not set or doesn't exist, returns NULL.
 */

char *_stp_map_get_str(MAP map)
{
	struct map_node_str *m;
	if (map == NULL || map->create || map->key == NULL)
		return NULL;
	dbug ("%lx\n", (long)map->key);
	m = (struct map_node_str *)map->key;
	return m->str;
}

/** Set the current element's value to a stat.
 * This sets the current element's value to an stat struct. The map must have been created
 * to hold stats using <i>_stp_map_new(xxx, STAT)</i>.  This function would only be used
 * if we wanted to set stats to something other than the normal initial values (count = 0,
 * sum = 0, etc).  It may be deleted if it doesn't turn out to be useful.
 * @sa _stp_map_stat_add 
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param stats pointer to stats struct.
 * @todo Histograms don't work yet.
 */

void _stp_map_set_stat(MAP map, stat * stats)
{
	struct map_node_stat *m;

	if (map == NULL)
		return;
	dbug ("set_stat %lx\n", (long)map->key);

	if (map->create) {
		if (stats == NULL)
			return;

		if (map->maxnum) {
			if (list_empty(&map->pool)) {
				if (map->no_wrap) {
					/* ERROR. FIXME */
					return;
				}
				m = (struct map_node_stat *)map->head.next;
				hlist_del_init(&m->n.hnode);
				map_free_strings(map, (struct map_node *)m);
				dbug ("got %lx off head\n", (long)m);
			} else {
				m = (struct map_node_stat *)map->pool.next;
				dbug ("got %lx off pool\n", (long)m);
			}
			list_move_tail(&m->n.lnode, &map->head);
		} else {
			m = (struct map_node_stat *)
			    _stp_calloc(sizeof(struct map_node_stat));
			/* add node to list */
			list_add_tail(&m->n.lnode, &map->head);
		}

		/* copy the key(s) */
		map_copy_keys(map, &m->n);

		/* set the value */
		memcpy(&m->stats, stats, sizeof(stat));
	} else {
		if (map->key == NULL)
			return;

		if (stats) {
			m = (struct map_node_stat *)map->key;
			memcpy(&m->stats, stats, sizeof(stat));
		} else {
			/* setting value to NULL is the same as deleting */
			_stp_map_key_del(map);
		}
	}
}

/** Gets the current element's value.
 * @param map
 * @returns A pointer to the stats struct. If the current element is not set 
 * or doesn't exist, returns NULL.
 */

stat *_stp_map_get_stat(MAP map)
{
	struct map_node_stat *m;
	if (map == NULL || map->create || map->key == NULL)
		return NULL;
	dbug ("%lx\n", (long)map->key);
	m = (struct map_node_stat *)map->key;
	return &m->stats;
}

/** Add to the current element's statistics.
 * Increments the statistics counter by one and the sum by <i>val</i>.
 * Adjusts minimum, maximum, and histogram.
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param val value to add to the statistics
 * @todo Histograms don't work yet.
 */

void _stp_map_stat_add(MAP map, int64_t val)
{
	struct map_node_stat *m;
	if (map == NULL)
		return;

	if (map->create) {
		stat st = { 1, val, val, val };
		/* histogram */
		_stp_map_set_stat(map, &st);
		return;
	}

	if (map->key == NULL)
		return;

	dbug ("add_stat %lx\n", (long)map->key);
	m = (struct map_node_stat *)map->key;
	m->stats.count++;
	m->stats.sum += val;
	if (val > m->stats.max)
		m->stats.max = val;
	if (val < m->stats.min)
		m->stats.min = val;
	/* histogram */
}

/** @} */

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

MAP _stp_list_new(unsigned max_entries, enum valtype type)
{
  MAP map = _stp_map_new (max_entries, type);
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
			
			/* remove any allocated string storage */
			map_free_strings(map, ptr);
			
			if (map->maxnum)
				list_add(&ptr->lnode, &map->pool);
			else
				_stp_free(ptr);

			map->num--;
			ptr = next;
		}
	}

	if (map->num != 0) {
		_stp_log ("ERROR: list is supposed to be empty (has %d)\n", map->num);
	}
}

/** Adds a string to a list.
 * @param map
 * @param str
 */

inline void _stp_list_add_str(MAP map, char *str)
{
	_stp_map_key_long(map, map->num);
	_stp_map_set_str(map, str);
}

inline void _stp_list_add_string (MAP map, String str)
{
	_stp_map_key_long(map, map->num);
	_stp_map_set_str(map, str->buf);
}

/** Adds an int64 to a list.
 * @param map
 * @param val
 */

inline void _stp_list_add_int64(MAP map, int64_t val)
{
	_stp_map_key_long(map, map->num);
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
/** @} */
#endif /* _MAP_C_ */
