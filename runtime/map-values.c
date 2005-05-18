/* -*- linux-c -*- */

#include "map.h"

#if VALUE_TYPE == STRING
#define VALUETYPE char*
#define VALUENAME str
#define NEED_STRING_VALS
#elif VALUE_TYPE == INT64
#define VALUETYPE int64_t
#define VALUENAME int64
#define NEED_INT64_VALS
#elif VALUE_TYPE == STAT
#define VALUETYPE stat*
#define VALUENAME stat
#define NEED_STAT_VALS
#else
#error VALUE_TYPE has unimplemented value.
#endif

#define VALSYM(x) JOIN(x,VALUENAME)
#define VALUE_TYPE_COPY JOIN(VALUENAME,copy)
#define VALUE_TYPE_ADD JOIN(VALUENAME,add)
#define VALUE_GET JOIN(VALUENAME,get)

void VALSYM(__stp_map_set) (MAP map, VALUETYPE val, int add)
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
		VALUE_TYPE_COPY((void *)((long)m + map->data_offset), val);
		//m->val = val;
	} else {
		if (map->key == NULL)
			return;
		
		if (val) {
			if (add)
				VALUE_TYPE_ADD((void *)((long)map->key + map->data_offset), val);
			else
				VALUE_TYPE_COPY((void *)((long)map->key + map->data_offset), val);
		} else if (!add) {
			/* setting value to 0 is the same as deleting */
			_stp_map_key_del(map);
		}
	}
}

void VALSYM(_stp_map_set) (MAP map, VALUETYPE val)
{
	VALSYM(__stp_map_set)(map, val, 0);
}


#if VALUE_TYPE == STAT
/** Adds an int64 to a stats map */
void VALSYM(_stp_map_add_int64) (MAP map, int64_t val)
{
	stat *d;
	int n;

	if (map == NULL)
		return;
	
	if (map->create) {
		struct map_node *m = __stp_map_create (map);
		if (!m)
			return;
		
		/* set the value */
		d = (stat *)((long)m + map->data_offset);
		d->count = 1;
		d->sum = d->min = d->max = val;
	} else {
		if (map->key == NULL)
			return;
		d = (stat *)((long)map->key + map->data_offset);
		d->count++;
		d->sum += val;
		if (val > d->max)
			d->max = val;
		if (val < d->min)
			d->min = val;
	}
	/* histogram */
	switch (map->hist_type) {
	case HIST_LOG:
		n = msb64 (val);
		if (n >= map->hist_buckets)
			n = map->hist_buckets - 1;
		d->histogram[n]++;
		break;
	case HIST_LINEAR:
		/* n = (val - map->hist_start) / map->hist_int; */
		val -= map->hist_start;
		do_div (val, map->hist_int);
		n = val;
		if (n < 0)
			n = 0;
		if (n >= map->hist_buckets)
			n = map->hist_buckets - 1;
		d->histogram[n]++;
	default:
		break;
	}
}
#endif /* VALUE_TYPE == STAT */

void VALSYM(_stp_map_add) (MAP map, VALUETYPE val)
{
	VALSYM(__stp_map_set)(map, val, 1);
}

VALUETYPE VALSYM(_stp_map_get) (MAP map)
{
	struct map_node *m;
	if (map == NULL || map->create || map->key == NULL)
		return 0;
	dbug ("key %lx\n", (long)map->key);
	m = (struct map_node *)map->key;
	return VALUE_GET ((void *)((long)m + map->data_offset));
}

#undef VALUE_TYPE
#undef VALUETYPE
#undef VALUENAME
