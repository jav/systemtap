/* -*- linux-c -*- 
 * Map Functions
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _MAP_C_
#define _MAP_C_

/** @file map.c
 * @brief Implements maps (associative arrays) and lists
 */

#include "alloc.c"
#include "sym.c"
#include "stat-common.c"
#include "map-stat.c"

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
	if (!m || m->map->type != INT64)
		return 0;
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
	if (!m || m->map->type != STRING)
		return "bad type";
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
	if (!m || m->map->type != STAT)
		return 0;
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
	int type;
	int64_t res = 0;

	if (mn) {
		res = (*mn->map->get_key)(mn, n, &type).val;
		dbug("type=%d\n", type);
		if (type != INT64)
			res = 0;
	}
	return res;
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
	int type;
	char *str = "";

	if (mn) {
		str = (*mn->map->get_key)(mn, n, &type).strp;
		dbug("type=%d\n", type);
		if (type != STRING)
			str = "bad type";
	}
	return str;
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

static int _stp_map_init(MAP m, unsigned max_entries, int type, int key_size, int data_size, int cpu)
{
	int size;

	m->maxnum = max_entries;
	m->type = type;
	if (type >= END) {
		_stp_error("unknown map type %d\n", type);
		return -1;
	}
	if (max_entries) {
		void *tmp;
		int i;
		
		/* size is the size of the map_node. */
		/* add space for the value. */
		key_size = ALIGN(key_size,4);
		m->data_offset = key_size;
		if (data_size == 0)
			data_size = map_sizes[type];
		data_size = ALIGN(data_size,4);
		size = key_size + data_size;
		
		
		for (i = 0; i < max_entries; i++) {
			if (cpu < 0)
				tmp = kmalloc(size, GFP_KERNEL);
			else
				tmp = kmalloc_node(size, GFP_KERNEL, cpu);
		
			if (!tmp) {
				_stp_error("Allocating memory while creating map failed.\n");
				return -1;
			}
			
			dbug ("allocated %lx\n", (long)tmp);
			list_add((struct list_head *)tmp, &m->pool);
			((struct map_node *)tmp)->map = m;
		}
	}
	if (type == STAT)
		m->hist.type = HIST_NONE;
	return 0;
      }


static MAP _stp_map_new(unsigned max_entries, int type, int key_size, int data_size)
{
	MAP m = (MAP) kmalloc(sizeof(struct map_root), GFP_KERNEL);
	if (m == NULL)
		return NULL;

	memset (m, 0, sizeof(struct map_root));
	INIT_LIST_HEAD(&m->pool);
	INIT_LIST_HEAD(&m->head);
	if (_stp_map_init(m, max_entries, type, key_size, data_size, -1)) {
		_stp_map_del(m);
		return NULL;
	}
	return m;
}

static PMAP _stp_pmap_new(unsigned max_entries, int type, int key_size, int data_size)
{
	int i;
	MAP map, m;

	PMAP pmap = (PMAP) kmalloc(sizeof(struct pmap), GFP_KERNEL);
	if (pmap == NULL)
		return NULL;

	memset (pmap, 0, sizeof(struct pmap));
	pmap->map = map = (MAP) alloc_percpu (struct map_root);
	if (map == NULL) 
		goto err;

	/* initialize the memory lists first so if allocations fail */
        /* at some point, it is easy to clean up. */
	for_each_cpu(i) {
		m = per_cpu_ptr (map, i);
		INIT_LIST_HEAD(&m->pool);
		INIT_LIST_HEAD(&m->head);
	}
	INIT_LIST_HEAD(&pmap->agg.pool);
	INIT_LIST_HEAD(&pmap->agg.head);

	for_each_cpu(i) {
		m = per_cpu_ptr (map, i);
		if (_stp_map_init(m, max_entries, type, key_size, data_size, i)) {
			goto err1;
		}
	}

	if (_stp_map_init(&pmap->agg, max_entries, type, key_size, data_size, -1))
		goto err1;
	
	return pmap;

err1:
	for_each_cpu(i) {
		m = per_cpu_ptr (map, i);
		__stp_map_del(m);
	}
	free_percpu(map);
err:
	kfree(pmap);
	return NULL;
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


static void __stp_map_del(MAP map)
{
	struct list_head *p, *tmp;

	/* free unused pool */
	list_for_each_safe(p, tmp, &map->pool) {
		list_del(p);
		kfree(p);
	}

	/* free used list */
	list_for_each_safe(p, tmp, &map->head) {
		list_del(p);
		kfree(p);
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

	__stp_map_del(map);

	kfree(map);
}

void _stp_pmap_del(PMAP pmap)
{
	int i;

	if (pmap == NULL)
		return;

	for_each_cpu(i) {
		MAP m = per_cpu_ptr (pmap->map, i);
		__stp_map_del(m);
	}
	free_percpu(pmap->map);

	/* free agg map elements */
	__stp_map_del(&pmap->agg);
	
	kfree(pmap);
}

/* sort keynum values */
#define SORT_COUNT -5
#define SORT_SUM   -4
#define SORT_MIN   -3
#define SORT_MAX   -2
#define SORT_AVG   -1

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
		if (keynum > 0) {
			a = _stp_key_get_int64(m1, keynum);
			b = _stp_key_get_int64(m2, keynum);
		} else if (keynum < 0) {
			stat *sd1 = (stat *)((long)m1 + m1->map->data_offset);
			stat *sd2 = (stat *)((long)m2 + m2->map->data_offset);
			switch (keynum) {
			case SORT_COUNT:
				a = sd1->count;
				b = sd2->count;
				break;
			case SORT_SUM:
				a = sd1->sum;
				b = sd2->sum;
				break;
			case SORT_MIN:
				a = sd1->min;
				b = sd2->min;
				break;
			case SORT_MAX:
				a = sd1->max;
				b = sd2->max;
				break;
			case SORT_AVG:
				a = _stp_div64 (NULL, sd1->sum, sd1->count);
				b = _stp_div64 (NULL, sd2->sum, sd2->count);
				break;
			default:
				/* should never happen */
				a = b = 0;
			}
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

	if (keynum > 0)
		(*map->get_key)((struct map_node *)head->next, keynum, &type);
	else if (keynum < 0)
		type = INT64;
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
		
		if (keynum > 0)
			(*map->get_key)((struct map_node *)head->next, keynum, &type);
		else if (keynum < 0)
			type = INT64;
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
		stat *sd = _stp_get_stat(ptr);
		_stp_stat_print_valtype (fmt, &map->hist, sd, 0); 
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
	dbug ("print map %lx fmt=%s\n", (long)map, fmt);

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

void _stp_pmap_printn_cpu (PMAP pmap, int n, const char *fmt, int cpu)
{
	MAP m = per_cpu_ptr (pmap->map, cpu);
	_stp_map_printn (m, n, fmt);
}

static struct map_node *_stp_new_agg(MAP agg, struct hlist_head *ahead, struct map_node *ptr)
{
	struct map_node *aptr;
	/* copy keys and aggregate */
	dbug("creating new entry in %lx\n", (long)agg);
	aptr = _new_map_create(agg, ahead);
	if (aptr == NULL)
		return NULL;
	(*agg->copy)(aptr, ptr);
	switch (agg->type) {
	case INT64:
		_new_map_set_int64(agg, 
				   aptr, 
				   *(int64_t *)((long)ptr + ptr->map->data_offset),
				   0);
		break;
	case STRING:
		_new_map_set_str(agg, 
				 aptr, 
				 (char *)((long)ptr + ptr->map->data_offset),
				 0);
		break;
	case STAT: {
		stat *sd1 = (stat *)((long)aptr + agg->data_offset);
		stat *sd2 = (stat *)((long)ptr + ptr->map->data_offset);
		Hist st = &agg->hist;
		sd1->count = sd2->count;
		sd1->sum = sd2->sum;
		sd1->min = sd2->min;
		sd1->max = sd2->max;
		if (st->type != HIST_NONE) {
			int j;
			for (j = 0; j < st->buckets; j++)
				sd1->histogram[j] = sd2->histogram[j];
		}
		break;
	}
	default:
		_stp_error("Attempted to aggregate map of type %d\n", agg->type);
	}
	return aptr;
}

static void _stp_add_agg(struct map_node *aptr, struct map_node *ptr)
{
	switch (aptr->map->type) {
	case INT64:
		_new_map_set_int64(aptr->map, 
				   aptr, 
				   *(int64_t *)((long)ptr + ptr->map->data_offset),
				   1);
		break;
	case STRING:
		_new_map_set_str(aptr->map, 
				 aptr, 
				 (char *)((long)ptr + ptr->map->data_offset),
				 1);
		break;
	case STAT: {
		stat *sd1 = (stat *)((long)aptr + aptr->map->data_offset);
		stat *sd2 = (stat *)((long)ptr + ptr->map->data_offset);
		Hist st = &aptr->map->hist;
		if (sd1->count == 0) {
			sd1->count = sd2->count;
			sd1->min = sd2->min;
			sd1->max = sd2->max;
			sd1->sum = sd2->sum;
		} else {
			sd1->count += sd2->count;
			sd1->sum += sd2->sum;
			if (sd2->min < sd1->min)
				sd1->min = sd2->min;
			if (sd2->max > sd1->max)
				sd1->max = sd2->max;
		}
		if (st->type != HIST_NONE) {
			int j;
			for (j = 0; j < st->buckets; j++)
				sd1->histogram[j] += sd2->histogram[j];
		}
		break;
	}
	default:
		_stp_error("Attempted to aggregate map of type %d\n", aptr->map->type);
	}
}

/** Aggregate per-cpu maps.
 * This function aggregates the per-cpu maps into an aggregated
 * map. A pointer to that aggregated map is returned.
 * 
 * A write lock must be held on the map during this function.
 *
 * @param map A pointer to a pmap.
 * @returns a pointer to an aggregated map. 
 */
MAP _stp_pmap_agg (PMAP pmap)
{
	int i, hash;
	MAP m, agg;
	struct map_node *ptr, *aptr;
	struct hlist_head *head, *ahead;
	struct hlist_node *e, *f;

	agg = &pmap->agg;
	
        /* FIXME. we either clear the aggregation map or clear each local map */
	/* every time we aggregate. which would be best? */
	_stp_map_clear (agg);

	for_each_cpu(i) {
		m = per_cpu_ptr (pmap->map, i);
		/* walk the hash chains. */
		for (hash = 0; hash < HASH_TABLE_SIZE; hash++) {
			head = &m->hashes[hash];
			ahead = &agg->hashes[hash];
			hlist_for_each(e, head) {
				int match = 0;
				ptr = (struct map_node *)((long)e - sizeof(struct list_head));
				hlist_for_each(f, ahead) {
					aptr = (struct map_node *)((long)f - sizeof(struct list_head));
					if ((*m->cmp)(ptr, aptr)) {
						match = 1;
						break;
					}
				}
				if (match)
					_stp_add_agg(aptr, ptr);
				else
					_stp_new_agg(agg, ahead, ptr);
			}
		}
	}
	return agg;
}

/** Get the aggregation map for a pmap.
 * This function returns a pointer to the aggregation map.
 * It does not do any aggregation.
 * @param map A pointer to a pmap.
 * @returns a pointer to an aggregated map. 
 * @sa _stp_pmap_agg()
 */
#define _stp_pmap_get_agg(pmap) (&pmap->agg)

#define _stp_pmap_printn(map,n,fmt) _stp_map_printn (_stp_pmap_agg(map), n, fmt)
#define _stp_pmap_print(map,fmt) _stp_map_printn(_stp_pmap_agg(map),0,fmt)

static void _new_map_clear_node (struct map_node *m)
{
	switch (m->map->type) {
	case INT64:
		*(int64_t *)((long)m + m->map->data_offset) = 0;
		break;
	case STRING:
		*(char *)((long)m + m->map->data_offset) = 0;
		break;
	case STAT: 
	{
		stat *sd = (stat *)((long)m + m->map->data_offset);
		Hist st = &m->map->hist;
		sd->count = 0;
		if (st->type != HIST_NONE) {
			int j;
			for (j = 0; j < st->buckets; j++)
				sd->histogram[j] = 0;
		}
		break;
	}
	}
}

static struct map_node *_new_map_create (MAP map, struct hlist_head *head)
{
	struct map_node *m;
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
		map->num++;
		dbug ("got %lx off pool\n", (long)m);
	}
	list_move_tail(&m->lnode, &map->head);
	
	/* add node to new hash list */
	hlist_add_head(&m->hnode, head);
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
	}
	return 0;
}

static int _new_map_set_str (MAP map, struct map_node *n, char *val, int add)
{
	if (map == NULL ||  n == NULL)
		return -2;

	if ((val && *val)|| map->list) {
		if (add)
			str_add((void *)((long)n + map->data_offset), val);
		else
			str_copy((void *)((long)n + map->data_offset), val);
	}
	return 0;
}

static int _new_map_set_stat (MAP map, struct map_node *n, int64_t val, int add)
{
	stat *sd;

	if (map == NULL || n == NULL)
		return -2;

	sd = (stat *)((long)n + map->data_offset);
	if (!add) {
		Hist st = &map->hist;
		sd->count = 0;
		if (st->type != HIST_NONE) {
			int j;
			for (j = 0; j < st->buckets; j++)
				sd->histogram[j] = 0;
		}
	}
	__stp_stat_add (&map->hist, sd, val);
	return 0;
}

/** Return the number of elements in a map
 * This function will return the number of active elements
 * in a map.
 * @param map 
 * @returns an int
 */
#define _stp_map_size(map) (map->num)

/** Return the number of elements in a pmap
 * This function will return the number of active elements
 * in all the per-cpu maps in a pmap. This is a quick sum and is
 * not the same as the number of unique elements that would
 * be in the aggragated map.
 * @param pmap 
 * @returns an int
 */
int _stp_pmap_size (PMAP pmap)
{
	int i, num = 0;

	for_each_cpu(i) {
		MAP m = per_cpu_ptr (pmap->map, i);
		num += m->num;
	}
	return num;
}
#endif /* _MAP_C_ */

