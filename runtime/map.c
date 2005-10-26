#ifndef _MAP_C_ /* -*- linux-c -*- */
#define _MAP_C_

/** @file map.c
 * @brief Implements maps (associative arrays) and lists
 */

#include "map-values.c"
#include "alloc.c"
#include "sym.c"

static int map_sizes[] = {
        sizeof(int64_t),
        MAP_STRING_LENGTH,
        sizeof(stat),
        0
};

unsigned int int64_hash (const int64_t v)
{
	return (unsigned int)hash_long ((unsigned long)v, HASH_TABLE_BITS);
}

int int64_eq_p (int64_t key1, int64_t key2)
{
	return key1 == key2;
}

void str_copy(char *dest, char *src)
{
	int len = strlen(src);
	if (len > MAP_STRING_LENGTH - 1)
		len = MAP_STRING_LENGTH - 1;
	strncpy (dest, src, len);
	dest[len] = 0;
}

int str_eq_p (char *key1, char *key2)
{
	return strncmp(key1, key2, MAP_STRING_LENGTH - 1) == 0;
}

unsigned int str_hash(const char *key1)
{
	int hash = 0, count = 0;
	char *v1 = (char *)key1;
	while (*v1 && count++ < 5) {
		hash += *v1++;
	}
	return (unsigned int)hash_long((unsigned long)hash, HASH_TABLE_BITS);
}

/** @addtogroup maps 
 * Implements maps (associative arrays) and lists
 * @{ 
 */

/** Return an int64 from a map node.
 * This function will return the int64 value of a map_node
 * from a map containing int64s. You can get the map_nodes in a map
 * with _stp_map_start(), _stp_map_iter() and foreach().
 * @param m pointer to the map_node. 
 * @returns an int64 value.
 */
int64_t _stp_get_int64(struct map_node *m)
{
	return *(int64_t *)((long)m + m->map->data_offset);
}

/** Return a string from a map node.
 * This function will return the string value of a map_node
 * from a map containing strings. You can get the map_nodes in a map
 * with _stp_map_start(), _stp_map_iter() and foreach().
 * @param m pointer to the map_node.
 * @returns a pointer to a string. 
 */
char *_stp_get_str(struct map_node *m)
{
	return (char *)((long)m + m->map->data_offset);
}

/** Return a stat pointer from a map node.
 * This function will return the stats of a map_node
 * from a map containing stats. You can get the map_nodes in a map
 * with _stp_map_start(), _stp_map_iter() and foreach().
 * @param m pointer to the map_node.
 * @returns A pointer to the stats.  
 */
stat *_stp_get_stat(struct map_node *m)
{
	return (stat *)((long)m + m->map->data_offset);
}

/** Return an int64 key from a map node.
 * This function will return an int64 key from a map_node.
 * @param mn pointer to the map_node.
 * @param n key number
 * @returns an int64
 * @sa key1int(), key2int()
 */
int64_t _stp_key_get_int64 (struct map_node *mn, int n)
{
	if (mn)
		return (*mn->map->get_key)(mn, n, NULL).val;
	return 0;
}

/** Return a string key from a map node.
 * This function will return an string key from a map_node.
 * @param mn pointer to the map_node.
 * @param n key number
 * @returns a pointer to a string
 * @sa key1str(), key2str()
 */
char *_stp_key_get_str (struct map_node *mn, int n)
{
	if (mn)
		return (*mn->map->get_key)(mn, n, NULL).strp;
	return "";
}

/** Create a new map.
 * Maps must be created at module initialization time.
 * @param max_entries The maximum number of entries allowed. Currently that number will
 * be preallocated.  If more entries are required, the oldest ones will be deleted. This makes
 * it effectively a circular buffer.  If max_entries is 0, there will be no maximum and entries
 * will be allocated dynamically.
 * @param type Type of values stored in this map. 
 * @return A MAP on success or NULL on failure.
 * @ingroup map_create
 */

static MAP _stp_map_new(unsigned max_entries, int type, int key_size, int data_size)
{
	int size;
	MAP m = (MAP) _stp_valloc(sizeof(struct map_root));
	if (m == NULL)
		return NULL;

	INIT_LIST_HEAD(&m->head);

	m->maxnum = max_entries;
	m->type = type;
	if (type >= END) {
		_stp_error("map_new: unknown type %d\n", type);
		return NULL;
	}
	if (max_entries) {
		void *tmp;
		int i;
		struct list_head *e;

		INIT_LIST_HEAD(&m->pool);
		
		/* size is the size of the map_node. */
		/* add space for the value. */
		key_size = ALIGN(key_size,4);
		m->data_offset = key_size;
		if (data_size == 0)
			data_size = map_sizes[type];
		data_size = ALIGN(data_size,4);
		size = key_size + data_size;

		tmp = _stp_valloc(max_entries * size);

		for (i = max_entries - 1; i >= 0; i--) {
			e = i * size + tmp;
			dbug ("e=%lx\n", (long)e);
			list_add(e, &m->pool);
			((struct map_node *)e)->map = m;
		}
		m->membuf = tmp;
	}
	if (type == STAT)
		m->hist_type = HIST_NONE;
	return m;
}


/** Deletes the current element.
 * If no current element (key) for this map is set, this function does nothing.
 * @param map 
 */

void _stp_map_key_del(MAP map)
{
	struct map_node *m;

	//dbug("create=%d key=%lx\n", map->create, (long)map->key);
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

	list_add(&m->lnode, &map->pool);

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

	//dbug ("%lx\n", (long)map->head.next);

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

	if (m->lnode.next == &map->head)
		return NULL;

	return (struct map_node *)m->lnode.next;
}

/** Clears all the elements in a map.
 * @param map 
 */

void _stp_map_clear(MAP map)
{
	struct map_node *m;

	if (map == NULL)
		return;

	map->create = 0;
	map->key = NULL;
	map->num = 0;

	while (!list_empty(&map->head)) {
		m = (struct map_node *)map->head.next;
		
		/* remove node from old hash list */
		hlist_del_init(&m->hnode);
		
		/* remove from entry list */
		list_del(&m->lnode);
		
		/* add to free pool */
		list_add(&m->lnode, &map->pool);
	}
}

/** Deletes a map.
 * Deletes a map, freeing all memory in all elements.  Normally done only when the module exits.
 * @param map
 */

void _stp_map_del(MAP map)
{
	if (map == NULL)
		return;
	_stp_vfree(map->membuf);
	_stp_vfree(map);
}

/* comparison function for sorts. */
static int _stp_cmp (struct list_head *a, struct list_head *b, int keynum, int dir, int type)
{
	struct map_node *m1 = (struct map_node *)a;
	struct map_node *m2 = (struct map_node *)b;
	if (type == STRING) {
		int ret;
		if (keynum)
			ret = strcmp(_stp_key_get_str(m1, keynum), _stp_key_get_str(m2, keynum));
		else
			ret = strcmp(_stp_get_str(m1), _stp_get_str(m2));
		if ((ret < 0 && dir > 0) || (ret > 0 && dir < 0))
			ret = 1;
		else
			ret = 0;
		//_stp_log ("comparing %s and %s and returning %d\n", _stp_get_str(m1), _stp_get_str(m2), ret);
		return ret;
	} else {
		int64_t a,b;
		if (keynum) {
			a = _stp_key_get_int64(m1, keynum);
			b = _stp_key_get_int64(m2, keynum);
		} else {
			a = _stp_get_int64(m1);
			b = _stp_get_int64(m2);
		}
		if ((a < b && dir > 0) || (a > b && dir < 0))
			return 1;
		return 0;
	}
}

/* swap function for bubble sort */
static inline void _stp_swap (struct list_head *a, struct list_head *b)
{
	a->prev->next = b;
	b->next->prev = a;
	a->next = b->next;
	b->prev = a->prev;
	a->prev = b;
	b->next = a;
}


/** Sort an entire array.
 * Sorts an entire array using merge sort.
 *
 * @param map Map
 * @param keynum 0 for the value, or a positive number for the key number to sort on.
 * @param dir Sort Direction. -1 for low-to-high. 1 for high-to-low.
 * @sa _stp_map_sortn()
 */

void _stp_map_sort (MAP map, int keynum, int dir)
{
        struct list_head *p, *q, *e, *tail;
        int nmerges, psize, qsize, i, type, insize = 1;
	struct list_head *head = &map->head;

	if (list_empty(head))
		return;

	if (keynum)
		(*map->get_key)((struct map_node *)head->next, keynum, &type);
	else
		type = ((struct map_node *)head->next)->map->type;

        do {
		tail = head;
		p = head->next;
                nmerges = 0;

                while (p) {
                        nmerges++;
                        q = p;
                        psize = 0;
                        for (i = 0; i < insize; i++) {
                                psize++;
                                q = q->next == head ? NULL : q->next;
                                if (!q)
                                        break;
                        }

                        qsize = insize;
                        while (psize > 0 || (qsize > 0 && q)) {
                                if (psize && (!qsize || !q || !_stp_cmp(p, q, keynum, dir, type))) {
                                        e = p;
                                        p = p->next == head ? NULL : p->next;
                                        psize--;
                                } else {
                                        e = q;
                                        q = q->next == head ? NULL : q->next;
                                        qsize--;
                                }
				
				/* now put 'e' on tail of list and make it our new tail */
				list_del(e);
				list_add(e, tail);
				tail = e;
                        }
                        p = q;
                }
                insize += insize;
        } while (nmerges > 1);
}

/** Get the top values from an array.
 * Sorts an array such that the start of the array contains the top
 * or bottom 'n' values. Use this when sorting the entire array
 * would be too time-consuming and you are only interested in the
 * highest or lowest values.
 *
 * @param map Map
 * @param n Top (or bottom) number of elements. 0 sorts the entire array.
 * @param keynum 0 for the value, or a positive number for the key number to sort on.
 * @param dir Sort Direction. -1 for low-to-high. 1 for high-to-low.
 * @sa _stp_map_sort()
 */
void _stp_map_sortn(MAP map, int n, int keynum, int dir)
{
	if (n == 0) {
		_stp_map_sort(map, keynum, dir);
	} else {
		struct list_head *head = &map->head;
		struct list_head *c, *a, *last, *tmp;
		int type, num, swaps = 1;
		
		if (list_empty(head))
			return;
		
		if (keynum)
			(*map->get_key)((struct map_node *)head->next, keynum, &type);
		else
			type = ((struct map_node *)head->next)->map->type;
		
		/* start off with a modified bubble sort of the first n elements */
		while (swaps) {
			num = n;
			swaps = 0;
			a = head->next;
			c = a->next->next;
			while ((a->next != head) && (--num > 0)) {
				if (_stp_cmp(a, a->next, keynum, dir, type)) {
					swaps++;
					_stp_swap(a, a->next);
				}
				a = c->prev;
				c = c->next;
			}
		}
		
		/* Now use a kind of insertion sort for the rest of the array. */
		/* Each element is tested to see if it should be be in the top 'n' */
		last = a;
		a = a->next;
		while (a != head) {
			tmp = a->next;
			c = last;
			while (c != head && _stp_cmp(c, a, keynum, dir, type)) 
				c = c->prev;
			if (c != last) {
				list_del(a);
				list_add(a, c);
				last = last->prev;
			}
			a = tmp;
		}
	}
}

static int print_keytype (char *fmt, int type, key_data *kd)
{
	//dbug ("*fmt = %c\n", *fmt);
	switch (type) {
	case STRING:
		if (*fmt != 's')
			return 1;
		_stp_print_cstr (kd->strp);
		break;
	case INT64:
		if (*fmt == 'x')
			_stp_printf("%llx", kd->val);
		else if (*fmt == 'X')
			_stp_printf("%llX", kd->val);
		else if (*fmt == 'd')
			_stp_printf("%lld", kd->val);
		else if (*fmt == 'p') {
#if BITS_PER_LONG == 64
			_stp_printf("%016llx", kd->val);
#else
			_stp_printf("%08llx", kd->val);
#endif
		} else if (*fmt == 'P')
			_stp_symbol_print ((unsigned long)kd->val);
		else
			return 1;
		break;
	default:
		return 1;
		break;
	}
	return 0;
}

static void print_valtype (MAP map, char *fmt, struct map_node *ptr)
{
	switch (map->type) {
	case STRING:
		if (*fmt == 's')
			_stp_print_cstr(_stp_get_str(ptr));
		break;
	case INT64:
	{
		int64_t val = _stp_get_int64(ptr);
		if (*fmt == 'x')
			_stp_printf("%llx", val);
		else if (*fmt == 'X')
			_stp_printf("%llX", val);
		else if (*fmt == 'd')
			_stp_printf("%lld", val);
		else if (*fmt == 'p') {
#if BITS_PER_LONG == 64
			_stp_printf("%016llx", val);
#else
			_stp_printf("%08llx", val);
#endif
		} else if (*fmt == 'P')
			_stp_symbol_print ((unsigned long)val);
		break;
	}
	case STAT:
	{
		Stat st = (Stat)((long)map + offsetof(struct map_root, hist_type));
		stat *sd = _stp_get_stat(ptr);
		_stp_stat_print_valtype (fmt, st, sd, 0); 
		break;
	}
	default:
		break;
	}
}

/** Print a Map.
 * Print a Map using a format string.
 *
 * @param map Map
 * @param fmt @ref format_string
 */
void _stp_map_printn (MAP map, int n, const char *fmt)
{
	struct map_node *ptr;
	int type, num;
	key_data kd;
	//dbug ("print map %lx fmt=%s\n", (long)map, fmt);

	if (n < 0)
		return;

	foreach (map, ptr) {
		char *f = (char *)fmt;
		while (*f) {
			f = next_fmt (f, &num);
			if (num) {
				/* key */
				kd = (*map->get_key)(ptr, num, &type);
				if (type != END)
					print_keytype (f, type, &kd);
			} else {
				/* value */
				print_valtype (map, f, ptr);
			}
			if (*f)
				f++;
		}
		_stp_print_cstr ("\n");
		if (n && (--n <= 0))
			break;
	}
	_stp_print_cstr ("\n");
	_stp_print_flush();
}

/** Print a Map.
 * Print a Map using a format string.
 *
 * @param map Map
 * @param fmt @ref format_string
 */
#define _stp_map_print(map,fmt) _stp_map_printn(map,0,fmt)

static struct map_node *__stp_map_create (MAP map)
{
	struct map_node *m;
	if (list_empty(&map->pool)) {
		if (!map->wrap) {
			/* ERROR. no space left */
			return NULL;
		}
		m = (struct map_node *)map->head.next;
		hlist_del_init(&m->hnode);
		//dbug ("got %lx off head\n", (long)m);
	} else {
		m = (struct map_node *)map->pool.next;
		//dbug ("got %lx off pool\n", (long)m);
	}
	list_move_tail(&m->lnode, &map->head);
	
	/* copy the key(s) */
	(map->copy_keys)(map, m);
	
	/* add node to new hash list */
	hlist_add_head(&m->hnode, map->c_keyhead);
	
	map->key = m;
	map->create = 0;
	map->num++;
	return m;
}

static struct map_node *_new_map_create (MAP map, struct hlist_head *head)
{
	struct map_node *m;
	dbug("map=%lx\n", map);
	if (list_empty(&map->pool)) {
		if (!map->wrap) {
			/* ERROR. no space left */
			return NULL;
		}
		m = (struct map_node *)map->head.next;
		dbug ("got %lx off head\n", (long)m);
		hlist_del_init(&m->hnode);
	} else {
		m = (struct map_node *)map->pool.next;
		dbug ("got %lx off pool\n", (long)m);
	}
	list_move_tail(&m->lnode, &map->head);
	
	/* add node to new hash list */
	hlist_add_head(&m->hnode, head);
	
	map->num++;
	return m;
}

static void _new_map_del_node (MAP map, struct map_node *n)
{
	/* remove node from old hash list */
	hlist_del_init(&n->hnode);
	
	/* remove from entry list */
	list_del(&n->lnode);
	
	/* add it back to the pool */
	list_add(&n->lnode, &map->pool);
	
	map->num--;
}

static int _new_map_set_int64 (MAP map, struct map_node *n, int64_t val, int add)
{
	if (map == NULL || n == NULL)
		return -2;

	if (val || map->list) {
		if (add)
			*(int64_t *)((long)n + map->data_offset) += val;
		else
			*(int64_t *)((long)n + map->data_offset) = val;
	} else if (!add) {
		/* setting value to 0 is the same as deleting */
		_new_map_del_node (map, n);
	}
	return 0;
}

static int _new_map_set_str (MAP map, struct map_node *n, char *val, int add)
{
	if (map == NULL ||  n == NULL)
		return -2;

	if (val || map->list) {
		if (add)
			str_add((void *)((long)n + map->data_offset), val);
		else
			str_copy((void *)((long)n + map->data_offset), val);
	} else if (!add) {
		/* setting value to 0 is the same as deleting */
		_new_map_del_node (map, n);
	}
	return 0;
}

static int64_t _new_map_get_int64 (MAP map, struct map_node *n)
{
	if (map == NULL || n == NULL)
		return 0;
	return *(int64_t *)((long)n + map->data_offset);
}

static char *_new_map_get_str (MAP map, struct map_node *n)
{
	if (map == NULL || n == NULL)
		return 0;
	return (char *)((long)n + map->data_offset);
}

static stat *_new_map_get_stat (MAP map, struct map_node *n)
{
	if (map == NULL || n == NULL)
		return 0;
	return (stat *)((long)n + map->data_offset);
}

static int _new_map_set_stat (MAP map, struct map_node *n, int64_t val, int add, int new)
{
	stat *sd;
	Stat st;

	if (map == NULL || n == NULL)
		return -2;

	if (val == 0 && !add) {
		_new_map_del_node (map, n);
		return 0;
	}

	sd = (stat *)((long)n + map->data_offset);
	st = (Stat)((long)map + offsetof(struct map_root, hist_type));

	if (new || !add) {
		int j;
		sd->count = sd->sum = sd->min = sd->max = 0;
		if (st->hist_type != HIST_NONE) {
			for (j = 0; j < st->hist_buckets; j++)
				sd->histogram[j] = 0;
		}
	}

	__stp_stat_add (st, sd, val);

	return 0;
}


#endif /* _MAP_C_ */

