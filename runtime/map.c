#ifndef _MAP_C_ /* -*- linux-c -*- */
#define _MAP_C_

/** @file map.c
 * @brief Implements maps (associative arrays) and lists
 */

#include "alloc.c"

static int map_sizes[] = {
        sizeof(int64_t),
        MAP_STRING_LENGTH,
        sizeof(stat),
        0
};

#ifdef NEED_INT64_KEYS
unsigned int int64_hash (const int64_t v)
{
	return (unsigned int)hash_long ((unsigned long)v, HASH_TABLE_BITS);
}

int int64_eq_p (int64_t key1, int64_t key2)
{
	return key1 == key2;
}
#endif /* NEED_INT64_KEYS */


#ifdef NEED_INT64_VALS
void int64_copy (void *dest, int64_t val)
{
	*(int64_t *)dest = val;
}

void int64_add (void *dest, int64_t val)
{
	*(int64_t *)dest += val;
}

int64_t int64_get (void *ptr)
{
	return *(int64_t *)ptr;
}
#endif /* NEED_INT64_VALS */


#ifdef NEED_STAT_VALS
void stat_copy (void *dest, stat *src)
{
	memcpy (dest, src, sizeof(stat));
}

void stat_add (void *dest, stat *src)
{
	stat *d = (stat *)dest;

	d->count =+ src->count;
        d->sum += src->sum;
        if (src->max > d->max)
                d->max = src->max;
        if (src->min < d->min)
                d->min = src->min;
	/* FIXME: do histogram */
}

stat *stat_get(void *ptr)
{
	return (stat *)ptr;
}

/* implements a log base 2 function, or Most Significant Bit */
/* with bits from 1 (lsb) to 64 (msb) */
/* msb64(0) = 0 */
/* msb64(1) = 1 */
/* msb64(8) = 4 */
/* msb64(512) = 10 */

int msb64(int64_t val)
{
  int res = 64;

  if (val == 0)
    return 0;

  /* shortcut. most values will be 16-bit */
  if (val & 0xffffffffffff0000ull) {
    if (!(val & 0xffffffff00000000ull)) {
      val <<= 32;
      res -= 32;
    }

    if (!(val & 0xffff000000000000ull)) {
      val <<= 16;
      res -= 16;
    }
  } else {
      val <<= 48;
      res -= 48;
  }

  if (!(val & 0xff00000000000000ull)) {
    val <<= 8;
    res -= 8;
  }

  if (!(val & 0xf000000000000000ull)) {
    val <<= 4;
    res -= 4;
  }

  if (!(val & 0xc000000000000000ull)) {
    val <<= 2;
    res -= 2;
  }

  if (!(val & 0x8000000000000000ull)) {
    val <<= 1;
    res -= 1;
  }

  return res;
}

#endif /* NEED_STAT_VALS */

int64_t _stp_key_get_int64 (struct map_node *mn, int n)
{
	if (mn)
		return (*mn->map->get_key)(mn, n, NULL).val;
	return 0;
}

char *_stp_key_get_str (struct map_node *mn, int n)
{
	if (mn)
		return (*mn->map->get_key)(mn, n, NULL).strp;
	return "";
}



#if defined(NEED_STRING_VALS) || defined (NEED_STRING_KEYS)
void str_copy(char *dest, char *src)
{
	int len = strlen(src);
	if (len > MAP_STRING_LENGTH - 1)
		len = MAP_STRING_LENGTH - 1;
	strncpy (dest, src, len);
	dest[len] = 0;
}
#endif

#ifdef NEED_STRING_VALS
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

char *str_get (void *ptr)
{
  return ptr;
}

/** Set the current element's value to String.
 * This sets the current element's value to a String. The map must have been created
 * to hold int64s using <i>_stp_map_new(xxx, STRING)</i>
 *
 * If the element doesn't exist, it is created.  If no current element (key)
 * is set for the map, this function does nothing.
 * @param map
 * @param str String containing new value.
 * @sa _stp_map_set()
 */

void _stp_map_set_string (MAP map, String str)
{
  _stp_map_set_str (map, str->buf);
}

#endif /* NEED_STRING_VALS */

#ifdef NEED_STRING_KEYS
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
#endif /* NEED_STRING_KEYS */

int64_t _stp_get_int64(struct map_node *m)
{
	return *(int64_t *)((long)m + m->map->data_offset);
}

char *_stp_get_str(struct map_node *m)
{
	return (char *)((long)m + m->map->data_offset);
}

stat *_stp_get_stat(struct map_node *m)
{
	return (stat *)((long)m + m->map->data_offset);
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
		dbug ("map_new: unknown type %d\n", type);
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

MAP _stp_map_new_hstat_log (unsigned max_entries, int key_size, int buckets)
{
	/* add size for buckets */
	int size = buckets * sizeof(int64_t) + sizeof(stat);
	MAP m = _stp_map_new (max_entries, STAT, key_size, size);
	if (m) {
		m->hist_type = HIST_LOG;
		m->hist_buckets = buckets;
		if (buckets < 1 || buckets > 64) {
			dbug ("histogram: Bad number of buckets.  Must be between 1 and 64\n");
			m->hist_type = HIST_NONE;
			return m;
		}
	}
	return m;
}

MAP _stp_map_new_hstat_linear (unsigned max_entries, int ksize, int start, int stop, int interval)
{
	MAP m;
	int size;
	int buckets = (stop - start) / interval;
	if ((stop - start) % interval) buckets++;

        /* add size for buckets */
	size = buckets * sizeof(int64_t) + sizeof(stat);

	m = _stp_map_new (max_entries, STAT, ksize, size);
	if (m) {
		m->hist_type = HIST_LINEAR;
		m->hist_start = start;
		m->hist_stop = stop;
		m->hist_int = interval;
		m->hist_buckets = buckets;
		if (m->hist_buckets <= 0) {
			dbug ("histogram: bad stop, start and/or interval\n");
			m->hist_type = HIST_NONE;
			return m;
		}
		
	}
	return m;
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
	_stp_vfree(map->membuf);
	_stp_vfree(map);
}

#ifdef NEED_STAT_VALS

static int needed_space(int64_t v)
{
	int space = 0;

	if (v == 0)
		return 1;

	if (v < 0) {
		space++;
		v = -v;
	}
	while (v) {
		v /= 10;
		space++;
	}
	return space;
}

static void reprint (int num, char *s)
{
	while (num > 0) {
		_stp_print_cstr (s);
		num--;
	}
}

#define HIST_WIDTH 50

void _stp_map_print_histogram (MAP map, stat *s)
{
	int scale, i, j, val_space, cnt_space;
	int64_t val, v, max = 0;

	if (map->hist_type != HIST_LOG && map->hist_type != HIST_LINEAR)
		return;
	/* get the maximum value, for scaling */

	for (i = 0; i < map->hist_buckets; i++)
		if (s->histogram[i] > max)
			max = s->histogram[i];
	
	if (max <= HIST_WIDTH)
		scale = 1;
	else {
		scale = max / HIST_WIDTH;
		if (max % HIST_WIDTH) scale++;
	}

	cnt_space = needed_space (max);
	if (map->hist_type == HIST_LINEAR)
		val_space = needed_space (map->hist_start +  map->hist_int * (map->hist_buckets - 1));
	else
		val_space = needed_space (1 << (map->hist_buckets - 1));
	dbug ("max=%lld scale=%d val_space=%d\n", max, scale, val_space);

	/* print header */
	j = 0;
	if (val_space > 5)		/* 5 = sizeof("value") */
		j = val_space - 5;
	else
		val_space = 5;
	for ( i = 0; i < j; i++)
		_stp_print_cstr (" ");
	_stp_print_cstr("value |");
	reprint (HIST_WIDTH, "-");
	_stp_print_cstr (" count\n");
	_stp_print_flush();
	if (map->hist_type == HIST_LINEAR)
		val = map->hist_start;
	else
		val = 0;
	for (i = 0; i < map->hist_buckets; i++) {
		reprint (val_space - needed_space(val), " ");
		_stp_printf("%d", val);
		_stp_print_cstr (" |");
		v = s->histogram[i] / scale;
		reprint (v, "@");
		reprint (HIST_WIDTH - v + 1 + cnt_space - needed_space(s->histogram[i]), " ");
		_stp_printf ("%lld\n", s->histogram[i]);
		if (map->hist_type == HIST_LINEAR) 
			val += map->hist_int;
		else if (val == 0)
			val = 1;
		else
			val *= 2;
		_stp_print_flush();
	}
}
#endif /* NEED_STAT_VALS */

void _stp_map_print (MAP map, const char *name)
{
	struct map_node *ptr;
	int type, n, first;
	key_data kd;

	dbug ("print map %lx\n", (long)map);
	for (ptr = _stp_map_start(map); ptr; ptr = _stp_map_iter (map, ptr)) {
		n = 1; first = 1;
		_stp_print_cstr (name);
		_stp_print_cstr ("[");
		do {
			kd = (*map->get_key)(ptr, n, &type);
			if (type == END)
				break;
			if (!first) 
				_stp_print_cstr (", ");
			first = 0;
			if (type == STRING) 
				_stp_print_cstr (kd.strp);
			else 
				_stp_printf("%lld", kd.val);
			n++;
		} while (1);
		_stp_print_cstr ("] = ");
		if (map->type == STRING)
			_stp_print_cstr(_stp_get_str(ptr));
		else if (map->type == INT64)
			_stp_printf("%d", _stp_get_int64(ptr));
#ifdef NEED_STAT_VALS
		else {
			stat *s = _stp_get_stat(ptr);
			_stp_printf("count:%lld  sum:%lld  avg:%lld  min:%lld  max:%lld\n",
				    s->count, s->sum, s->sum/s->count, s->min, s->max);
			_stp_print_flush();
			_stp_map_print_histogram (map, s);
		}
#endif
		_stp_print_cstr ("\n");
		_stp_print_flush();
	}
	_stp_print_cstr ("\n");
	_stp_print_flush();
}

static struct map_node *__stp_map_create (MAP map)
{
	struct map_node *m;
	if (list_empty(&map->pool)) {
		if (map->no_wrap) {
			/* ERROR. FIXME */
			return NULL;
		}
		m = (struct map_node *)map->head.next;
		hlist_del_init(&m->hnode);
		dbug ("got %lx off head\n", (long)m);
	} else {
		m = (struct map_node *)map->pool.next;
		dbug ("got %lx off pool\n", (long)m);
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
#endif

