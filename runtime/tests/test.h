/* include file for testing maps without running in the kernel */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

static inline unsigned long hash_long(unsigned long val, unsigned int bits)
{
    unsigned long hash = val;
    
#if __WORDSIZE == 64
        /*  Sigh, gcc can't optimise this alone like it does for 32 bits. */
        unsigned long n = hash;
        n <<= 18;
        hash -= n;
        n <<= 33;
        hash -= n;
        n <<= 3;
        hash += n;
        n <<= 3;
        hash -= n;
        n <<= 4;
        hash += n;
        n <<= 2;
        hash += n;
#else
        /* On some cpus multiply is faster, on others gcc will do shifts */
        hash *= 0x9e370001UL;
#endif

    /* High bits are more random, so use them. */
    return hash >> (8*sizeof(long) - bits);
}


#define STRINGLEN 128
#define HASH_ELEM_NUM 5000
#define HASH_TABLE_BITS 8
#define HASH_TABLE_SIZE (1<<HASH_TABLE_BITS)
#define BUCKETS 16 /* largest histogram width */
#define PK 0 /* sprinkle debug printk into probe code */

#define LIST_POISON1  ((void *) 0x00100100)
#define LIST_POISON2  ((void *) 0x00200200)

struct list_head {
        struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
        struct list_head name = LIST_HEAD_INIT(name)

#define INIT_LIST_HEAD(ptr) do { \
        (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next)
{
        next->prev = new;
        new->next = next;
        new->prev = prev;
        prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head)
{
        __list_add(new, head, head->next);
}


static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
        __list_add(new, head->prev, head);
}
/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
        next->prev = prev;
        prev->next = next;
}

static inline void list_del(struct list_head *entry)
{
        __list_del(entry->prev, entry->next);
        entry->next = LIST_POISON1;
        entry->prev = LIST_POISON2;
}

static inline int list_empty(const struct list_head *head)
{
        return head->next == head;
}

static inline void list_move_tail(struct list_head *list,
                                  struct list_head *head)
{
        __list_del(list->prev, list->next);
        list_add_tail(list, head);
}


struct hlist_head {
        struct hlist_node *first;
};

struct hlist_node {
        struct hlist_node *next, **pprev;
};
#define HLIST_HEAD_INIT { .first = NULL }
#define HLIST_HEAD(name) struct hlist_head name = {  .first = NULL }
#define INIT_HLIST_HEAD(ptr) ((ptr)->first = NULL)
#define INIT_HLIST_NODE(ptr) ((ptr)->next = NULL, (ptr)->pprev = NULL)

static inline void hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
        struct hlist_node *first = h->first;
        n->next = first;
        if (first)
                first->pprev = &n->next;
        h->first = n;
        n->pprev = &h->first;
}

#define hlist_for_each(pos, head) \
  for (pos = (head)->first; pos; pos = pos->next)

#define hlist_entry(ptr, type, member) container_of(ptr,type,member)

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

static inline void __hlist_del(struct hlist_node *n)
{
        struct hlist_node *next = n->next;
        struct hlist_node **pprev = n->pprev;
        *pprev = next;
        if (next)
                next->pprev = pprev;
}

static inline void hlist_del(struct hlist_node *n)
{
        __hlist_del(n);
        n->next = LIST_POISON1;
        n->pprev = LIST_POISON2;
}

static inline void hlist_del_init(struct hlist_node *n)
{
        if (n->pprev)  {
                __hlist_del(n);
                INIT_HLIST_NODE(n);
        }
}

static inline void hlist_add_before(struct hlist_node *n,
                                        struct hlist_node *next)
{
        n->pprev = next->pprev;
        n->next = next;
        next->pprev = &n->next;
        *(n->pprev) = n;
}

#define GFP_ATOMIC 0

void *kmalloc (size_t len, int flags)
{
  return malloc (len);
}

void *vmalloc (size_t len)
{
  return malloc (len);
}

#define kfree(x) free(x)
#define vfree(x) free(x)

/*****  END OF KERNEL STUFF ********/

#ifdef DEBUG
#define dbug(args...) \
  {						\
    printf("%s:%d: ", __FUNCTION__, __LINE__);	\
    printf(args);				\
  }
#else
#define dbug(args...) ;
#endif

#define dlog(args...) printf(args);

#include "../alloc.h"
#include "../map.h"
#include "../map.c"

/* handle renamed functions */
#define map_new _stp_map_new
#define map_key_del _stp_map_key_del
#define map_start _stp_map_start
#define map_iter _stp_map_iter
#define map_get_str _stp_map_get_str
#define map_set_int64 _stp_map_set_int64
#define map_get_int64 _stp_map_get_int64
#define map_key_str_str _stp_map_key_str_str
#define map_key_str _stp_map_key_str
#define map_key_long _stp_map_key_long
#define map_key_long_long _stp_map_key_long_long
#define map_set_stat _stp_map_set_stat
