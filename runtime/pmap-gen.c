/* -*- linux-c -*- 
 * pmap API generator
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

/** @file pmap-gen.c
 * @brief Pmap function generator
 * This file is a template designed to be included as many times as
 * needed to generate the necessary pmap functions.
 */

#define JOIN(x,y) JOINx(x,y)
#define JOINx(x,y) x##_##y
#define JOIN2(x,y,z) JOIN2x(x,y,z)
#define JOIN2x(x,y,z) x##_##y##z
#define JOIN3(a,b,c,d) JOIN3x(a,b,c,d)
#define JOIN3x(a,b,c,d) a##_##b##c##d
#define JOIN4(a,b,c,d,e) JOIN4x(a,b,c,d,e)
#define JOIN4x(a,b,c,d,e) a##_##b##c##d##e
#define JOIN5(a,b,c,d,e,f) JOIN5x(a,b,c,d,e,f)
#define JOIN5x(a,b,c,d,e,f) a##_##b##c##d##e##f
#define JOIN6(a,b,c,d,e,f,g) JOIN6x(a,b,c,d,e,f,g)
#define JOIN6x(a,b,c,d,e,f,g) a##_##b##c##d##e##f##g

#include "map.h"

#if !defined(VALUE_TYPE)
#error Need to define VALUE_TYPE as STRING, STAT, or INT64
#endif

#if VALUE_TYPE == STRING
#define VALTYPE char*
#define VSTYPE char*
#define VALNAME str
#define VALN s
#define MAP_SET_VAL(a,b,c,d) _new_map_set_str(a,b,c,d)
#define MAP_GET_VAL(n) _stp_get_str(n)
#define VAL_IS_ZERO(val,add) (val == 0 || *val == 0)
#elif VALUE_TYPE == INT64
#define VALTYPE int64_t
#define VSTYPE int64_t
#define VALNAME int64
#define VALN i
#define MAP_SET_VAL(a,b,c,d) _new_map_set_int64(a,b,c,d)
#define MAP_GET_VAL(n) _stp_get_int64(n)
#define VAL_IS_ZERO(val,add) (val == 0)
#elif VALUE_TYPE == STAT
#define VALTYPE stat*
#define VSTYPE int64_t
#define VALNAME stat
#define VALN x
#define MAP_SET_VAL(a,b,c,d) _new_map_set_stat(a,b,c,d)
#define MAP_GET_VAL(n) _stp_get_stat(n)
#define VAL_IS_ZERO(val,add) (val == 0 && !add)
#else
#error Need to define VALUE_TYPE as STRING, STAT, or INT64
#endif /* VALUE_TYPE */

//#define MAP_SET_VAL(a,b,c,d) _new_map_set_##VALNAME(a,b,c,d)

#if defined (KEY1_TYPE)
#define KEY_ARITY 1
#if KEY1_TYPE == STRING
#define KEY1TYPE char*
#define KEY1NAME str
#define KEY1N s
#define KEY1STOR char key1[MAP_STRING_LENGTH]
#define KEY1CPY(m) str_copy(m->key1, key1)
#else
#define KEY1TYPE int64_t
#define KEY1NAME int64
#define KEY1N i
#define KEY1STOR int64_t key1
#define KEY1CPY(m) m->key1=key1
#endif
#define KEY1_EQ_P JOIN(KEY1NAME,eq_p)
#define KEY1_HASH JOIN(KEY1NAME,hash)
#endif /* defined(KEY1_TYPE) */

#if defined (KEY2_TYPE)
#undef KEY_ARITY
#define KEY_ARITY 2
#if KEY2_TYPE == STRING
#define KEY2TYPE char*
#define KEY2NAME str
#define KEY2N s
#define KEY2STOR char key2[MAP_STRING_LENGTH]
#define KEY2CPY(m) str_copy(m->key2, key2)
#else
#define KEY2TYPE int64_t
#define KEY2NAME int64
#define KEY2N i
#define KEY2STOR int64_t key2
#define KEY2CPY(m) m->key2=key2
#endif
#define KEY2_EQ_P JOIN(KEY2NAME,eq_p)
#define KEY2_HASH JOIN(KEY2NAME,hash)
#endif /* defined(KEY2_TYPE) */

#if defined (KEY3_TYPE)
#undef KEY_ARITY
#define KEY_ARITY 3
#if KEY3_TYPE == STRING
#define KEY3TYPE char*
#define KEY3NAME str
#define KEY3N s
#define KEY3STOR char key3[MAP_STRING_LENGTH]
#define KEY3CPY(m) str_copy(m->key3, key3)
#else
#define KEY3TYPE int64_t
#define KEY3NAME int64
#define KEY3N i
#define KEY3STOR int64_t key3
#define KEY3CPY(m) m->key3=key3
#endif
#define KEY3_EQ_P JOIN(KEY3NAME,eq_p)
#define KEY3_HASH JOIN(KEY3NAME,hash)
#endif /* defined(KEY3_TYPE) */

#if defined (KEY4_TYPE)
#undef KEY_ARITY
#define KEY_ARITY 4
#if KEY4_TYPE == STRING
#define KEY4TYPE char*
#define KEY4NAME str
#define KEY4N s
#define KEY4STOR char key4[MAP_STRING_LENGTH]
#define KEY4CPY(m) str_copy(m->key4, key4)
#else
#define KEY4TYPE int64_t
#define KEY4NAME int64
#define KEY4N i
#define KEY4STOR int64_t key4
#define KEY4CPY(m) m->key4=key4
#endif
#define KEY4_EQ_P JOIN(KEY4NAME,eq_p)
#define KEY4_HASH JOIN(KEY4NAME,hash)
#endif /* defined(KEY4_TYPE) */

#if defined (KEY5_TYPE)
#undef KEY_ARITY
#define KEY_ARITY 5
#if KEY5_TYPE == STRING
#define KEY5TYPE char*
#define KEY5NAME str
#define KEY5N s
#define KEY5STOR char key5[MAP_STRING_LENGTH]
#define KEY5CPY(m) str_copy(m->key5, key5)
#else
#define KEY5TYPE int64_t
#define KEY5NAME int64
#define KEY5N i
#define KEY5STOR int64_t key5
#define KEY5CPY(m) m->key5=key5
#endif
#define KEY5_EQ_P JOIN(KEY5NAME,eq_p)
#define KEY5_HASH JOIN(KEY5NAME,hash)
#endif /* defined(KEY5_TYPE) */

#if KEY_ARITY == 1
#define KEYSYM(x) JOIN2(x,KEY1N,VALN)
#define ALLKEYS(x) x##1
#define ALLKEYSD(x) KEY1TYPE x##1
#define KEYCPY(m) {KEY1CPY(m);}
#elif KEY_ARITY == 2
#define KEYSYM(x) JOIN3(x,KEY1N,KEY2N,VALN)
#define ALLKEYS(x) x##1, x##2
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2
#define KEYCPY(m) {KEY1CPY(m);KEY2CPY(m);}
#elif KEY_ARITY == 3
#define KEYSYM(x) JOIN4(x,KEY1N,KEY2N,KEY3N,VALN)
#define ALLKEYS(x) x##1, x##2, x##3
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2, KEY3TYPE x##3
#define KEYCPY(m) {KEY1CPY(m);KEY2CPY(m);KEY3CPY(m);}
#elif KEY_ARITY == 4
#define KEYSYM(x) JOIN5(x,KEY1N,KEY2N,KEY3N,KEY4N,VALN)
#define ALLKEYS(x) x##1, x##2, x##3, x##4
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2, KEY3TYPE x##3, KEY4TYPE x##4
#define KEYCPY(m) {KEY1CPY(m);KEY2CPY(m);KEY3CPY(m);KEY4CPY(m);}
#elif KEY_ARITY == 5
#define KEYSYM(x) JOIN6(x,KEY1N,KEY2N,KEY3N,KEY4N,KEY5N,VALN)
#define ALLKEYS(x) x##1, x##2, x##3, x##4, x##5
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2, KEY3TYPE x##3, KEY4TYPE x##4, KEY5TYPE x##5
#define KEYCPY(m) {KEY1CPY(m);KEY2CPY(m);KEY3CPY(m);KEY4CPY(m);KEY5CPY(m);}
#endif

/* */

struct KEYSYM(pmap_node) {
	/* list of other nodes in the map */
	struct list_head lnode;
	/* list of nodes with the same hash value */
	struct hlist_node hnode;
	/* pointer back to the map struct */
	struct map_root *map;

	KEY1STOR;
#if KEY_ARITY > 1
	KEY2STOR;
#if KEY_ARITY > 2
	KEY3STOR;
#if KEY_ARITY > 3
	KEY4STOR;
#if KEY_ARITY > 4
	KEY5STOR;
#endif
#endif
#endif
#endif
};

#define type_to_enum(type)						\
	({								\
		int ret;						\
		if (__builtin_types_compatible_p (type, char*)) 	\
			ret = STRING;					\
		else							\
			ret = INT64;					\
		ret;							\
	})

/* returns 1 on match, 0 otherwise */
static int KEYSYM(pmap_key_cmp) (struct map_node *m1, struct map_node *m2)
{
	struct KEYSYM(pmap_node) *n1 = (struct KEYSYM(pmap_node) *)m1;
	struct KEYSYM(pmap_node) *n2 = (struct KEYSYM(pmap_node) *)m2;
		if (KEY1_EQ_P(n1->key1, n2->key1)
#if KEY_ARITY > 1
		    && KEY2_EQ_P(n1->key2, n2->key2)
#if KEY_ARITY > 2
		    && KEY3_EQ_P(n1->key3, n2->key3)
#if KEY_ARITY > 3
		    && KEY4_EQ_P(n1->key4, n2->key4)
#if KEY_ARITY > 4
		    && KEY5_EQ_P(n1->key5, n2->key5)
#endif
#endif
#endif
#endif
			)
			return 1;
		else
			return 0;
}

/* copy keys for m2 -> m1 */
static void KEYSYM(pmap_copy_keys) (struct map_node *m1, struct map_node *m2)
{
	struct KEYSYM(pmap_node) *dst = (struct KEYSYM(pmap_node) *)m1;
	struct KEYSYM(pmap_node) *src = (struct KEYSYM(pmap_node) *)m2;
#if KEY1_TYPE == STRING
	str_copy (dst->key1, src->key1); 
#else
	dst->key1 = src->key1;
#endif
#if KEY_ARITY > 1
#if KEY2_TYPE == STRING
	str_copy (dst->key2, src->key2); 
#else
	dst->key2 = src->key2;
#endif
#if KEY_ARITY > 2
#if KEY3_TYPE == STRING
	str_copy (dst->key3, src->key3); 
#else
	dst->key3 = src->key3;
#endif
#if KEY_ARITY > 3
#if KEY4_TYPE == STRING
	str_copy (dst->key4, src->key4); 
#else
	dst->key4 = src->key4;
#endif
#if KEY_ARITY > 4
#if KEY5_TYPE == STRING
	str_copy (dst->key5, src->key5); 
#else
	dst->key5 = src->key5;
#endif
#endif
#endif
#endif
#endif
}

static key_data KEYSYM(pmap_get_key) (struct map_node *mn, int n, int *type)
{
	key_data ptr;
	struct KEYSYM(pmap_node) *m = (struct KEYSYM(pmap_node) *)mn;	

	if (n > KEY_ARITY || n < 1) {
		if (type)
			*type = END;
		return (key_data)(int64_t)0;
	}

	switch (n) {
	case 1:
		ptr = (key_data)m->key1;
		if (type)
			*type = type_to_enum(KEY1TYPE);
		break;
#if KEY_ARITY > 1
	case 2:
		ptr = (key_data)m->key2;
		if (type)
			*type = type_to_enum(KEY2TYPE);

		break;
#if KEY_ARITY > 2
	case 3:
		ptr = (key_data)m->key3;
		if (type)
			*type = type_to_enum(KEY3TYPE);
		break;
#if KEY_ARITY > 3
	case 4:
		ptr = (key_data)m->key4;
		if (type)
			*type = type_to_enum(KEY4TYPE);
		break;
#if KEY_ARITY > 4
	case 5:
		ptr = (key_data)m->key5;
		if (type)
			*type = type_to_enum(KEY5TYPE);
		break;
#endif
#endif
#endif
#endif
	default:
		ptr = (key_data)(int64_t)0;
		if (type)
			*type = END;
	}
	return ptr;
}


static unsigned int KEYSYM(pkeycheck) (ALLKEYSD(key))
{
#if KEY1_TYPE == STRING
	if (key1 == NULL)
		return 0;
#endif

#if KEY_ARITY > 1
#if KEY2_TYPE == STRING
	if (key2 == NULL)
		return 0;
#endif

#if KEY_ARITY > 2
#if KEY3_TYPE == STRING
	if (key3 == NULL)
		return 0;
#endif

#if KEY_ARITY > 3
#if KEY4_TYPE == STRING
	if (key4 == NULL)
		return 0;
#endif

#if KEY_ARITY > 4
#if KEY5_TYPE == STRING
	if (key5 == NULL)
		return 0;
#endif
#endif
#endif
#endif
#endif
	return 1;
}

static unsigned int KEYSYM(phash) (ALLKEYSD(key))
{
	unsigned int hash = KEY1_HASH(key1);
#if KEY_ARITY > 1
	hash ^= KEY2_HASH(key2);
#if KEY_ARITY > 2
	hash ^= KEY3_HASH(key3);
#if KEY_ARITY > 3
	hash ^= KEY4_HASH(key4);
#if KEY_ARITY > 4
	hash ^= KEY5_HASH(key5);
#endif
#endif
#endif
#endif
	return (unsigned int) hash;
}


#if VALUE_TYPE == INT64 || VALUE_TYPE == STRING
MAP KEYSYM(_stp_pmap_new) (unsigned max_entries)
{
	MAP map = _stp_pmap_new (max_entries, VALUE_TYPE, sizeof(struct KEYSYM(pmap_node)), 0);
	if (map) {
		int i;
		MAP m;
		for_each_cpu(i) {
			m = per_cpu_ptr (map, i);
			m->get_key = KEYSYM(pmap_get_key);
			m->copy = KEYSYM(pmap_copy_keys);
			m->cmp = KEYSYM(pmap_key_cmp);
		}
		m = _stp_percpu_dptr(map);
		m->get_key = KEYSYM(pmap_get_key);
		m->copy = KEYSYM(pmap_copy_keys);
		m->cmp = KEYSYM(pmap_key_cmp);
	}
	return map;
}
#else
/* _stp_pmap_new_key1_key2...val (num, HIST_LINEAR, start, end, interval) */
/* _stp_pmap_new_key1_key2...val (num, HIST_LOG, buckets) */ 

MAP KEYSYM(_stp_pmap_new) (unsigned max_entries, int htype, ...)
{
	int buckets=0, start=0, stop=0, interval=0;
	MAP m, map;
	va_list ap;

	if (htype != HIST_NONE) {
		va_start (ap, htype);		
		if (htype == HIST_LOG) {
			buckets = va_arg(ap, int);
			// dbug ("buckets=%d\n", buckets);
		} else {
			start = va_arg(ap, int);
			stop = va_arg(ap, int);
			interval = va_arg(ap, int);
			// dbug ("start=%d stop=%d interval=%d\n", start, stop, interval);
		}
		va_end (ap);
	}

	switch (htype) {
	case HIST_NONE:
		map = _stp_pmap_new (max_entries, STAT, sizeof(struct KEYSYM(pmap_node)), 0);
		break;
	case HIST_LOG:
		map = _stp_pmap_new_hstat_log (max_entries, sizeof(struct KEYSYM(pmap_node)), 
					    buckets);
		break;
	case HIST_LINEAR:
		map = _stp_pmap_new_hstat_linear (max_entries, sizeof(struct KEYSYM(pmap_node)),
					       start, stop, interval);
		break;
	default:
		_stp_warn ("Unknown histogram type %d\n", htype);
		map = NULL;
	}

	if (map) {
		int i;
		MAP m;
		for_each_cpu(i) {
			m = per_cpu_ptr (map, i);
			m->get_key = KEYSYM(pmap_get_key);
			m->copy = KEYSYM(pmap_copy_keys);
			m->cmp = KEYSYM(pmap_key_cmp);
		}
		m = _stp_percpu_dptr(map);
		m->get_key = KEYSYM(pmap_get_key);
		m->copy = KEYSYM(pmap_copy_keys);
		m->cmp = KEYSYM(pmap_key_cmp);
	}
	return map;
}

#endif /* VALUE_TYPE */
int KEYSYM(__stp_pmap_set) (MAP map, ALLKEYSD(key), VSTYPE val, int add)
{
	unsigned int hv;
	struct hlist_head *head;
	struct hlist_node *e;
	struct KEYSYM(pmap_node) *n;

	if (map == NULL)
		return -2;

	if (KEYSYM(pkeycheck) (ALLKEYS(key)) == 0)
		return -2;

	hv = KEYSYM(phash) (ALLKEYS(key));
	head = &map->hashes[hv];

	hlist_for_each(e, head) {
		n = (struct KEYSYM(pmap_node) *)((long)e - sizeof(struct list_head));
		dbug("map_node =%lx\n", (long)n);
		if (KEY1_EQ_P(n->key1, key1)
#if KEY_ARITY > 1
		    && KEY2_EQ_P(n->key2, key2)
#if KEY_ARITY > 2
		    && KEY3_EQ_P(n->key3, key3)
#if KEY_ARITY > 3
		    && KEY4_EQ_P(n->key4, key4)
#if KEY_ARITY > 4
		    && KEY5_EQ_P(n->key5, key5)
#endif
#endif
#endif
#endif
			) {
			if VAL_IS_ZERO(val,add) {
				if (!add)
					_new_map_del_node(map,(struct map_node *)n);
				return 0;
				
			}
			return MAP_SET_VAL(map,(struct map_node *)n, val, add);
		}
	}
	/* key not found */
	dbug("key not found\n");
	if VAL_IS_ZERO(val,add)
		return 0;

	n = (struct KEYSYM(pmap_node)*)_new_map_create (map, head);
	if (n == NULL)
		return -1;
	KEYCPY(n);
	return MAP_SET_VAL(map,(struct map_node *)n, val, 0);
}

int KEYSYM(_stp_pmap_set) (MAP map, ALLKEYSD(key), VSTYPE val)
{
	MAP m = per_cpu_ptr (map, get_cpu());
	int res = KEYSYM(__stp_pmap_set) (m, ALLKEYS(key), val, 0);
	put_cpu();
	return res;
}

int KEYSYM(_stp_pmap_add) (MAP map, ALLKEYSD(key), VSTYPE val)
{
	MAP m = per_cpu_ptr (map, get_cpu());
	int res = KEYSYM(__stp_pmap_set) (m, ALLKEYS(key), val, 1);
	put_cpu();
	return res;
}


VALTYPE KEYSYM(_stp_pmap_get_cpu) (MAP pmap, ALLKEYSD(key))
{
	unsigned int hv;
	struct hlist_head *head;
	struct hlist_node *e;
	struct KEYSYM(pmap_node) *n;
	VALTYPE res;
	MAP map;

	if (pmap == NULL)
		return (VALTYPE)0;

	map = per_cpu_ptr (pmap, get_cpu());

	hv = KEYSYM(phash) (ALLKEYS(key));
	head = &map->hashes[hv];

	hlist_for_each(e, head) {
		n = (struct KEYSYM(pmap_node) *)((long)e - sizeof(struct list_head));
		dbug("map_node =%lx\n", (long)n);
		if (KEY1_EQ_P(n->key1, key1)
#if KEY_ARITY > 1
		    && KEY2_EQ_P(n->key2, key2)
#if KEY_ARITY > 2
		    && KEY3_EQ_P(n->key3, key3)
#if KEY_ARITY > 3
		    && KEY4_EQ_P(n->key4, key4)
#if KEY_ARITY > 4
		    && KEY5_EQ_P(n->key5, key5)
#endif
#endif
#endif
#endif
			) {
			res = MAP_GET_VAL((struct map_node *)n);
			put_cpu();
			return res;
		}
	}
	/* key not found */
	put_cpu();
#if VALUE_TYPE == STRING
	return "";
#else
	return (VALTYPE)0;
#endif
}

#if 0
VALTYPE KEYSYM(_stp_pmap_get) (MAP pmap, ALLKEYSD(key))
{
	unsigned int hv;
	struct hlist_head *head;
	struct hlist_node *e;
	struct KEYSYM(pmap_node) *n, *anode = NULL;
	VALTYPE res;
	MAP map, agg;

	if (pmap == NULL)
		return (VALTYPE)0;

	hv = KEYSYM(phash) (ALLKEYS(key));

	/* first look it up in the aggregation map */
	agg = _stp_percpu_dptr(pmap);
	ahead = &agg->hashes[hv];
	hlist_for_each(e, ahead) {
		n = (struct KEYSYM(pmap_node) *)((long)e - sizeof(struct list_head));
		dbug("map_node =%lx\n", (long)n);
		if (KEY1_EQ_P(n->key1, key1)
#if KEY_ARITY > 1
		    && KEY2_EQ_P(n->key2, key2)
#if KEY_ARITY > 2
		    && KEY3_EQ_P(n->key3, key3)
#if KEY_ARITY > 3
		    && KEY4_EQ_P(n->key4, key4)
#if KEY_ARITY > 4
		    && KEY5_EQ_P(n->key5, key5)
#endif
#endif
#endif
#endif
			) {
			anode = n;
			break;
		}
	}
	if (anode)
		return MAP_SET_VAL(map,(struct map_node *)n, val, 0);

	for_each_cpu(cpu) {
		map = per_cpu_ptr (pmap, get_cpu());
		head = &map->hashes[hv];
		hlist_for_each(e, head) {
			n = (struct KEYSYM(pmap_node) *)((long)e - sizeof(struct list_head));
			dbug("map_node =%lx\n", (long)n);
			if (KEY1_EQ_P(n->key1, key1)
#if KEY_ARITY > 1
			    && KEY2_EQ_P(n->key2, key2)
#if KEY_ARITY > 2
			    && KEY3_EQ_P(n->key3, key3)
#if KEY_ARITY > 3
			    && KEY4_EQ_P(n->key4, key4)
#if KEY_ARITY > 4
			    && KEY5_EQ_P(n->key5, key5)
#endif
#endif
#endif
#endif
				) {
				res = MAP_GET_VAL((struct map_node *)n);
				put_cpu();
				return res;
			}
		}
	}
	/* key not found */
	put_cpu();
#if VALUE_TYPE == STRING
	return "";
#else
	return (VALTYPE)0;
#endif
}
#endif /* 0 */

#undef KEY1NAME
#undef KEY1N
#undef KEY1TYPE
#undef KEY1_TYPE
#undef KEY1STOR
#undef KEY1CPY

#undef KEY2NAME
#undef KEY2N
#undef KEY2TYPE
#undef KEY2_TYPE
#undef KEY2STOR
#undef KEY2CPY

#undef KEY3NAME
#undef KEY3N
#undef KEY3TYPE
#undef KEY3_TYPE
#undef KEY3STOR
#undef KEY3CPY

#undef KEY4NAME
#undef KEY4N
#undef KEY4TYPE
#undef KEY4_TYPE
#undef KEY4STOR
#undef KEY4CPY

#undef KEY5NAME
#undef KEY5N
#undef KEY5TYPE
#undef KEY5_TYPE
#undef KEY5STOR
#undef KEY5CPY

#undef KEY_ARITY
#undef ALLKEYS
#undef ALLKEYSD
#undef KEYCPY
#undef KEYSYM 

#undef VALUE_TYPE
#undef VALNAME
#undef VALTYPE
#undef VSTYPE
#undef VALN

#undef MAP_SET_VAL
#undef MAP_GET_VAL
#undef VAL_IS_ZERO

