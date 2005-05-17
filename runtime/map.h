#ifndef _MAP_H_ /* -*- linux-c -*- */
#define _MAP_H_

/** @file map.h
 * @brief Header file for maps and lists 
 */
/** @addtogroup maps 
 * @todo Needs to be made SMP-safe for when the big lock is removed from kprobes.
 * @{
 */

#ifndef HASH_TABLE_BITS
#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#endif

#ifndef MAX_KEY_ARITY
#define MAX_KEY_ARITY 5
#endif

#ifndef MAP_STRING_LENGTH
#define MAP_STRING_LENGTH 256
#endif

/** histogram type */
enum histtype { HIST_NONE, HIST_LOG, HIST_LINEAR };

#define INT64 0
#define STRING 1
#define STAT 2
#define END 3 /* end marker */

#define HSTAT_LOG (STAT | (HIST_LOG << 8))
#define HSTAT_LINEAR (STAT | (HIST_LINEAR <<  8))

/* Statistics are stored in this struct */
typedef struct {
	int64_t count;
	int64_t sum;
	int64_t min, max;
	int64_t histogram[];
} stat;


/* Keys are either int64 or strings */
typedef union {
	int64_t val;
	char *strp;
} key_data;


/* basic map element */
struct map_node {
	/* list of other nodes in the map */
	struct list_head lnode;
	/* list of nodes with the same hash value */
	struct hlist_node hnode;
	/* pointer back to the map struct */
	struct map_root *map;
};

/* This structure contains all information about a map.
 * It is allocated once when _stp_map_new() is called. 
 */
struct map_root {
	/* type of the value stored in the array */
	int type;
	
 	/* maximum number of elements allowed in the array. */
	int maxnum;

	/* current number of used elements */
	int num;

	/* when more than maxnum elements, wrap or discard? */
	int no_wrap;

	/* linked list of current entries */
	struct list_head head;

	/* pool of unused entries. */
	struct list_head pool;

	/* saved key entry for lookups */
	struct map_node *key;

	/* This is information about the structure of the map_nodes used */
	/* for this map. It is stored here instead of the map_node to save */
	/* space. */
	void (*copy_keys)(struct map_root *, struct map_node *);
	key_data (*get_key)(struct map_node *mn, int n, int *type);
	int data_offset;

	/* this is the creation data saved between the key functions and the
	    set/get functions */
	u_int8_t create;
	key_data c_key[MAX_KEY_ARITY];
	struct hlist_head *c_keyhead;

	/* the hash table for this array */
	struct hlist_head hashes[HASH_TABLE_SIZE];

	/* pointer to allocated memory space. Used for freeing memory. */
	void *membuf;

	/* used if this map's nodes contain stats */
	enum histtype hist_type;
	int hist_start;
	int hist_stop;
	int hist_int;
	int hist_buckets;
};

/** All maps are of this type. */
typedef struct map_root *MAP;

/** Extracts string from key1 union */
#define key1str(ptr) (_stp_key_get_str(ptr,1))
/** Extracts string from key2 union */
#define key2str(ptr) (_stp_key_get_str(ptr,2))
/** Extracts int from key1 union */
#define key1int(ptr) (_stp_key_get_int64(ptr,1))
/** Extracts int from key2 union */
#define key2int(ptr) (_stp_key_get_int64(ptr,2))

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
      _stp_map_key_int64 (map, (int64_t)(key));				\
  })

/** Macro to call the proper _stp_map_set function based on the
 * type of the argument. 
 * @note May cause compiler warning on some GCCs 
 */
#define _stp_map_set(map, val)					\
  ({								\
    if (__builtin_types_compatible_p (typeof (val), char[]))		\
      _stp_map_set_str (map, (char *)(val));				\
    else  if (__builtin_types_compatible_p (typeof (val), String))	\
      _stp_map_set_string (map, (String)(val));				\
    else								\
      _stp_map_set_int64 (map, (int64_t)(val));				\
  })

/** Loop through all elements of a map or list.
 * @param map 
 * @param ptr pointer to a map_node_stat, map_node_int64 or map_node_str
 *
 * @b Example:
 * @include foreach.c
 */

#define foreach(map, ptr)				\
  for (ptr = (typeof(ptr))_stp_map_start(map); ptr; \
       ptr = (typeof(ptr))_stp_map_iter (map, (struct map_node *)ptr))

/** @} */

/** @ingroup lists
 * @brief Macro to call the proper _stp_list_add function based on the
 * types of the argument. 
 *
 * @note May cause compiler warning on some GCCs 
 */

#define _stp_list_add(map, val)					\
  ({									\
    if (__builtin_types_compatible_p (typeof (val), char[]))		\
      _stp_list_add_str (map, (char *)(val));				\
    else if (__builtin_types_compatible_p (typeof (val), String))	\
      _stp_list_add_string (map, (String)(val));			\
    else								\
      _stp_list_add_int64 (map, (int64_t)(val));			\
  })


/************* prototypes for map.c ****************/

int int64_eq_p(int64_t key1, int64_t key2);
void int64_copy(void *dest, int64_t val);
void int64_add(void *dest, int64_t val);
int64_t int64_get(void *ptr);
void stat_copy(void *dest, stat *src);
void stat_add(void *dest, stat *src);
stat * stat_get(void *ptr);
int64_t _stp_key_get_int64(struct map_node *mn, int n);
char * _stp_key_get_str(struct map_node *mn, int n);
unsigned int int64_hash(const int64_t v);
char * str_get(void *ptr);
void str_copy(char *dest, char *src);
void str_add(void *dest, char *val);
int str_eq_p(char *key1, char *key2);
int64_t _stp_get_int64(struct map_node *m);
char * _stp_get_str(struct map_node *m);
stat * _stp_get_stat(struct map_node *m);
int msb64(int64_t x);
unsigned int str_hash(const char *key1);
static MAP _stp_map_new(unsigned max_entries, int type, int key_size, int data_size);
MAP _stp_map_new_hstat_log(unsigned max_entries, int key_size, int buckets);
MAP _stp_map_new_hstat_linear(unsigned max_entries, int ksize, int start, int stop, int interval);
void _stp_map_key_del(MAP map);
struct map_node * _stp_map_start(MAP map);
struct map_node * _stp_map_iter(MAP map, struct map_node *m);
void _stp_map_del(MAP map);
void _stp_map_print_histogram(MAP map, stat *s);
void _stp_map_print(MAP map, const char *name);
static struct map_node * __stp_map_create(MAP map);

/* these prototypes suppress warnings from macros */
void _stp_map_key_str(MAP, char *);
void _stp_map_set_str(MAP, char *);
void _stp_map_set_string(MAP, String);
void _stp_list_add_str(MAP, char*);
void _stp_list_add_string(MAP, String);

void _stp_map_key_int64(MAP, int64_t);
void _stp_map_set_int64(MAP, int64_t);
int64_t _stp_map_get_int64(MAP);
#endif /* _MAP_H_ */
