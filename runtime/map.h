/* -*- linux-c -*- */
/** @file map.h
 *  @brief Header file for maps and lists
 */

#include <linux/types.h>

/** Statistics are stored in this struct */
typedef struct {
	int64_t count;
	int64_t sum;
	int64_t min, max;
	int64_t histogram[BUCKETS];
} stat;

/** Keys are either longs or char * */
union key_data {
	long val;
	char *str;
};

/** keys can be longs or strings */
enum keytype { NONE, LONG, STR } __attribute__ ((packed));
/** values can be either int64, stats or strings */
enum valtype { INT64, STAT, STRING, END };


/** basic map element */
struct map_node {
	/** list of other nodes in the map */
	struct list_head lnode;
	/** list of nodes with the same hash value */
	struct hlist_node hnode; 
	union key_data key1;
	union key_data key2;
	enum keytype key1type;
	enum keytype key2type;
};

/** map element containing int64 */
struct map_node_int64 {
	struct map_node n;
	int64_t val;
};

/** map element containing string */
struct map_node_str {
	struct map_node n;
	char *str;
};

/** map element containing stats */
struct map_node_stat {
	struct map_node n;
	stat stats;
};

/** This structure contains all information about a map.
 * It is allocated once when _stp_map_new() is called. 
 */
struct map_root {
	/** type of the values stored in the array */
	enum valtype type;
	
 	/** maximum number of elements allowed in the array. */
	int maxnum;

	/** current number of used elements */
	int num;

	/** when more than maxnum elements, wrap or discard? */
	int no_wrap;

	/** linked list of current entries */
	struct list_head head;

	/** pool of unused entries.  Used only when entries are statically allocated
	    at startup. */
	struct list_head pool;

	/** saved key entry for lookups */
	struct map_node *key;

	/** this is the creation data saved between the key functions and the
	    set/get functions 
	    @todo Needs to be per-cpu data for SMP support */
	u_int8_t create;
	enum keytype c_key1type;
	enum keytype c_key2type;
	struct hlist_head *c_keyhead;
	union key_data c_key1;
	union key_data c_key2;

	/** the hash table for this array */
	struct hlist_head hashes[HASH_TABLE_SIZE];

	/** pointer to allocated memory space. Used for freeing memory. */
	void *membuf;
};

/** All maps are of this type. */
typedef struct map_root *MAP;

/** Extracts string from key1 union */
#define key1str(ptr) (ptr->n.key1.str)
/** Extracts string from key2 union */
#define key2str(ptr) (ptr->n.key2.str)
/** Extracts int from key1 union */
#define key1int(ptr) (ptr->n.key1.val)
/** Extracts int from key2 union */
#define key2int(ptr) (ptr->n.key2.val)

/** Macro to call the proper _stp_map_key functions based on the
 * types of the arguments. 
 * @note May cause compiler warning on some GCCs 
 */
#define _stp_map_key2(map, key1, key2)				\
  ({								\
    if (__builtin_types_compatible_p (typeof (key1), char[]))	\
      if (__builtin_types_compatible_p (typeof (key2), char[])) \
	_stp_map_key_str_str (map, (char *)(key1), (char *)(key2));	\
      else							\
	_stp_map_key_str_long (map, (char *)(key1), (long)(key2));	\
    else							\
      if (__builtin_types_compatible_p (typeof (key2), char[])) \
	_stp_map_key_long_str (map, (long)(key1), (char *)(key2));	\
      else							\
	_stp_map_key_long_long (map, (long)(key1), (long)(key2));	\
  })

/** Macro to call the proper _stp_map_key function based on the
 * type of the argument. 
 * @note May cause compiler warning on some GCCs 
 */
#define _stp_map_key(map, key)				\
  ({								\
    if (__builtin_types_compatible_p (typeof (key), char[]))	\
      _stp_map_key_str (map, (char *)(key));				\
    else							\
      _stp_map_key_long (map, (long)(key));				\
  })

/** Macro to call the proper _stp_map_set function based on the
 * type of the argument. 
 * @note May cause compiler warning on some GCCs 
 */
#define _stp_map_set(map, val)				\
  ({								\
    if (__builtin_types_compatible_p (typeof (val), char[]))	\
      _stp_map_set_str (map, (char *)(val));				\
    else							\
      _stp_map_set_int64 (map, (int64_t)(val));			\
  })

/** Macro to call the proper _stp_list_add function based on the
 * types of the argument. 
 * @note May cause compiler warning on some GCCs 
 */
#define _stp_list_add(map, val)				\
  ({								\
    if (__builtin_types_compatible_p (typeof (val), char[]))	\
      _stp_list_add_str (map, (char *)(val));				\
    else							\
      _stp_list_add_int64 (map, (int64_t)(val));			\
  })


/** Loop through all elements of a map.
 * @param map 
 * @param ptr pointer to a map_node_stat, map_node_int64 or map_node_str
 *
 * @b Example:
 * @include foreach.c
 */

#define foreach(map, ptr)				\
  for (ptr = (typeof(ptr))_stp_map_start(map); ptr; \
       ptr = (typeof(ptr))_stp_map_iter (map, (struct map_node *)ptr))

