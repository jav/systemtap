/* -*- linux-c -*- */

#include "map.h"

#define JOIN(x,y) JOINx(x,y)
#define JOINx(x,y) x##_##y

#if defined (KEY1_TYPE)
#define KEY_ARITY 1
#if KEY1_TYPE == STRING
#define KEY1TYPE char*
#define KEY1NAME str
#define KEY1STOR char key1[MAP_STRING_LENGTH]
#define NEED_STRING_KEYS
#else
#define KEY1TYPE int64_t
#define KEY1NAME int64
#define KEY1STOR int64_t key1
#define NEED_INT64_KEYS
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
#define KEY2STOR char key2[MAP_STRING_LENGTH]
#define NEED_STRING_KEYS
#else
#define KEY2TYPE int64_t
#define KEY2NAME int64
#define KEY2STOR int64_t key2
#define NEED_INT64_KEYS
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
#define KEY3STOR char key3[MAP_STRING_LENGTH]
#define NEED_STRING_KEYS
#else
#define KEY3TYPE int64_t
#define KEY3NAME int64
#define KEY3STOR int64_t key3
#define NEED_INT64_KEYS
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
#define KEY4STOR char key4[MAP_STRING_LENGTH]
#define NEED_STRING_KEYS
#else
#define KEY4TYPE int64_t
#define KEY4NAME int64
#define KEY4STOR int64_t key4
#define NEED_INT64_KEYS
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
#define KEY5STOR char key5[MAP_STRING_LENGTH]
#define NEED_STRING_KEYS
#else
#define KEY5TYPE int64_t
#define KEY5NAME int64
#define KEY5STOR int64_t key5
#define NEED_INT64_KEYS
#endif
#define KEY5_EQ_P JOIN(KEY5NAME,eq_p)
#define KEY5_HASH JOIN(KEY5NAME,hash)
#endif /* defined(KEY5_TYPE) */

#if KEY_ARITY == 1
#define KEYSYM(x) JOIN(x,KEY1NAME)
#define ALLKEYS(x) x##1
#define ALLKEYSD(x) KEY1TYPE x##1
#elif KEY_ARITY == 2
#define JOIN2(x,y,z) JOIN2x(x,y,z)
#define JOIN2x(x,y,z) x##_##y##_##z
#define KEYSYM(x) JOIN2(x,KEY1NAME,KEY2NAME)
#define ALLKEYS(x) x##1, x##2
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2
#elif KEY_ARITY == 3
#define JOIN3(a,b,c,d) JOIN3x(a,b,c,d)
#define JOIN3x(a,b,c,d) a##_##b##_##c##_##d
#define KEYSYM(x) JOIN3(x,KEY1NAME,KEY2NAME,KEY3NAME)
#define ALLKEYS(x) x##1, x##2, x##3
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2, KEY3TYPE x##3
#elif KEY_ARITY == 4
#define JOIN4(a,b,c,d,e) JOIN4x(a,b,c,d,e)
#define JOIN4x(a,b,c,d,e) a##_##b##_##c##_##d##_##e
#define KEYSYM(x) JOIN4(x,KEY1NAME,KEY2NAME,KEY3NAME,KEY4NAME)
#define ALLKEYS(x) x##1, x##2, x##3, x##4
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2, KEY3TYPE x##3, KEY4TYPE x##4
#elif KEY_ARITY == 5
#define JOIN5(a,b,c,d,e,f) JOIN5x(a,b,c,d,e,f)
#define JOIN5x(a,b,c,d,e,f) a##_##b##_##c##_##d##_##e##_##f
#define KEYSYM(x) JOIN5(x,KEY1NAME,KEY2NAME,KEY3NAME,KEY4NAME,KEY5NAME)
#define ALLKEYS(x) x##1, x##2, x##3, x##4, x##5
#define ALLKEYSD(x) KEY1TYPE x##1, KEY2TYPE x##2, KEY3TYPE x##3, KEY4TYPE x##4, KEY5TYPE x##5
#endif

/* */
struct KEYSYM(map_node) {
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

static key_data KEYSYM(map_get_key) (struct map_node *mn, int n, int *type)
{
	key_data ptr;
	struct KEYSYM(map_node) *m = (struct KEYSYM(map_node) *)mn;	

	dbug ("m=%lx\n", (long)m);
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


static void KEYSYM(map_copy_keys) (MAP map, struct map_node *n)
{
	struct KEYSYM(map_node) *m = (struct KEYSYM(map_node) *)n;
#if KEY1_TYPE == STRING
	str_copy (m->key1, map->c_key[0].strp); 
#else
	m->key1 = map->c_key[0].val;
#endif
#if KEY_ARITY > 1
#if KEY2_TYPE == STRING
	str_copy (m->key2, map->c_key[1].strp); 
#else
	m->key2 = map->c_key[1].val;
#endif
#if KEY_ARITY > 2
#if KEY3_TYPE == STRING
	str_copy (m->key3, map->c_key[2].strp); 
#else
	m->key3 = map->c_key[2].val;
#endif
#if KEY_ARITY > 3
#if KEY4_TYPE == STRING
	str_copy (m->key4, map->c_key[3].strp); 
#else
	m->key4 = map->c_key[3].val;
#endif
#if KEY_ARITY > 4
#if KEY5_TYPE == STRING
	str_copy (m->key5, map->c_key[4].strp); 
#else
	m->key5 = map->c_key[4].val;
#endif
#endif
#endif
#endif
#endif
}


static unsigned int KEYSYM(hash) (ALLKEYSD(key))
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

/* _stp_map_new_key1_key2 (num, STAT, LINEAR, start, end, interval) */
/* _stp_map_new_key1_key2 (num, STAT, LOG, buckets) */ 

MAP KEYSYM(_stp_map_new) (unsigned max_entries, int valtype, ...)
{
	int htype, buckets=0, start=0, stop=0, interval=0;
	MAP m;

	htype = valtype >> 8;
	dbug ("htype=%d\n", htype);

	if (htype != HIST_NONE) {
		va_list ap;
		va_start (ap, valtype);
		
		if (htype == HIST_LOG) {
			buckets = va_arg(ap, int);
			dbug ("buckets=%d\n", buckets);
		} else {
			start = va_arg(ap, int);
			stop = va_arg(ap, int);
			interval = va_arg(ap, int);
			dbug ("start=%d stop=%d interval=%d\n", start, stop, interval);
		}
		va_end (ap);
	}
	switch (htype) {
	case HIST_NONE:
		m = _stp_map_new (max_entries, valtype & 0x0f, 
				  sizeof(struct KEYSYM(map_node)), 0);
		break;
	case HIST_LOG:
		m = _stp_map_new_hstat_log (max_entries, sizeof(struct KEYSYM(map_node)), 
					    buckets);
		break;
	case HIST_LINEAR:
		m = _stp_map_new_hstat_linear (max_entries, sizeof(struct KEYSYM(map_node)),
					       start, stop, interval);
		break;
	default:
		dbug ("ERROR: unknown histogram type %d\n", htype);
		m = NULL;
	}

	if (m) {
		m->copy_keys = KEYSYM(map_copy_keys);
		m->get_key = KEYSYM(map_get_key);
	}
	return m;
}


void KEYSYM(_stp_map_key) (MAP map, ALLKEYSD(key))
{
	unsigned int hv;
	struct hlist_head *head;
	struct hlist_node *e;

	if (map == NULL)
		return;

	hv = KEYSYM(hash) (ALLKEYS(key));
	head = &map->hashes[hv];

	hlist_for_each(e, head) {
		struct KEYSYM(map_node) *n =
			(struct KEYSYM(map_node) *)((long)e - sizeof(struct hlist_node));
		//dbug ("n =%lx  key=" EACHKEY(%ld) "\n", (long)n, n->key1.val, n->key2.val);
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
			map->key = (struct map_node *)n;
			dbug ("saving key %lx\n", (long)map->key);
			map->create = 0;
			return;
		}
	}

	dbug ("key not found\n");
	map->c_key[0] = (key_data)key1;
#if KEY_ARITY > 1
	map->c_key[1] = (key_data)key2;
#if KEY_ARITY > 2
	map->c_key[2] = (key_data)key3;
#if KEY_ARITY > 3
	map->c_key[3] = (key_data)key4;
#if KEY_ARITY > 4
	map->c_key[4] = (key_data)key5;
#endif
#endif
#endif
#endif

	map->c_keyhead = head;
	map->create = 1;
}


#undef KEY1NAME
#undef KEY1TYPE
#undef KEY1_TYPE
#undef KEY1STOR

#undef KEY2NAME
#undef KEY2TYPE
#undef KEY2_TYPE
#undef KEY2STOR

#undef KEY3NAME
#undef KEY3TYPE
#undef KEY3_TYPE
#undef KEY3STOR

#undef KEY4NAME
#undef KEY4TYPE
#undef KEY4_TYPE
#undef KEY4STOR

#undef KEY5NAME
#undef KEY5TYPE
#undef KEY5_TYPE
#undef KEY5STOR

#undef KEY_ARITY
