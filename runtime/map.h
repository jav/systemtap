/* -*- linux-c -*- 
 * Map Header File
 * Copyright (C) 2005 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _MAP_H_
#define _MAP_H_

/** @file map.h
 * @brief Header file for maps and lists 
 */
/** @addtogroup maps 
 * @todo Needs a spinlock variable to help when locks are required on the map.
 * @{
 */

/* This sets the size of the hash table. */
#ifndef HASH_TABLE_BITS
#define HASH_TABLE_BITS 8
/* This sets the size of the hash table. */
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#endif

/* The maximum number of keys allowed. Reducing this can save a small
amount of memory. Do not increase above 5. */
#ifndef MAX_KEY_ARITY
#define MAX_KEY_ARITY 5
#endif

/** Maximum length of strings in maps. This sets the amount of space reserved
    for each string. */
#ifndef MAP_STRING_LENGTH
#define MAP_STRING_LENGTH 256
#endif

/** @cond DONT_INCLUDE */
#define INT64 0
#define STRING 1
#define STAT 2
#define END 3
/** @endcond */

#include "stat.h"

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
	int wrap;

	/* is this a list? */
	int list;

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
	void (*copy)(struct map_node *dst, struct map_node *src);
	int (*cmp)(struct map_node *dst, struct map_node *src);

	int data_offset;

	/* the hash table for this array */
	struct hlist_head hashes[HASH_TABLE_SIZE];

	/* pointer to allocated memory space. Used for freeing memory. */
	void *membuf;

	/* used if this map's nodes contain stats */
	struct _Hist hist;
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


/** Loop through all elements of a map or list.
 * @param map 
 * @param ptr pointer to a map_node_stat, map_node_int64 or map_node_str
 *
 * @b Example:
 * @include foreach.c
 */

#define foreach(map, ptr)						\
	for (ptr = _stp_map_start(map); ptr; ptr = _stp_map_iter (map, ptr))

/** @} */


/** @cond DONT_INCLUDE */
/************* prototypes for map.c ****************/

int int64_eq_p(int64_t key1, int64_t key2);
void int64_copy(void *dest, int64_t val);
void int64_add(void *dest, int64_t val);
int64_t int64_get(void *ptr);
void stat_copy(void *dest, stat *src);
void stat_add(void *dest, stat *src);
stat *stat_get(void *ptr);
int64_t _stp_key_get_int64(struct map_node *mn, int n);
char * _stp_key_get_str(struct map_node *mn, int n);
unsigned int int64_hash(const int64_t v);
char * str_get(void *ptr);
void str_copy(char *dest, char *src);
void str_add(void *dest, char *val);
int str_eq_p(char *key1, char *key2);
int64_t _stp_get_int64(struct map_node *m);
char * _stp_get_str(struct map_node *m);
stat *_stp_get_stat(struct map_node *m);
unsigned int str_hash(const char *key1);
static MAP _stp_map_new(unsigned max_entries, int type, int key_size, int data_size);
static MAP _stp_pmap_new(unsigned max_entries, int type, int key_size, int data_size);
static int msb64(int64_t x);
static MAP _stp_map_new_hstat_log(unsigned max_entries, int key_size, int buckets);
static MAP _stp_map_new_hstat_linear(unsigned max_entries, int ksize, int start, int stop, int interval);
static void _stp_map_print_histogram(MAP map, stat *s);
struct map_node * _stp_map_start(MAP map);
struct map_node * _stp_map_iter(MAP map, struct map_node *m);
void _stp_map_del(MAP map);
void _stp_map_clear(MAP map);
void _stp_map_print(MAP map, const char *fmt);

static struct map_node *_new_map_create (MAP map, struct hlist_head *head);
static int _new_map_set_int64 (MAP map, struct map_node *n, int64_t val, int add);
static int64_t _new_map_get_int64 (MAP map, struct map_node *n);
static char *_new_map_get_str (MAP map, struct map_node *n);
static int _new_map_set_str (MAP map, struct map_node *n, char *val, int add);
static stat *_new_map_get_stat (MAP map, struct map_node *n);
static int _new_map_set_stat (MAP map, struct map_node *n, int64_t val, int add);
static void _new_map_clear_node (struct map_node *);
static void _new_map_del_node (MAP map, struct map_node *n);
static MAP _stp_pmap_new_hstat_linear (unsigned max_entries, int ksize, int start, int stop, int interval);
static MAP _stp_pmap_new_hstat_log (unsigned max_entries, int key_size, int buckets);
static void _stp_add_agg(struct map_node *aptr, struct map_node *ptr);
static struct map_node *_stp_new_agg(MAP agg, struct hlist_head *ahead, struct map_node *ptr);
/** @endcond */
#endif /* _MAP_H_ */
