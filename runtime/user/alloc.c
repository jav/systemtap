#ifndef _ALLOC_C_
#define _ALLOC_C_

/* -*- linux-c -*- */
/** @file alloc.c
 * @brief Memory functions.
 */
/** @addtogroup alloc Memory Functions
 * Basic malloc/calloc/free functions. These will be changed so 
 * that memory allocation errors will call a handler.  The default will
 * send a signal to the user-space daemon that will trigger the module to
 * be unloaded.
 * @todo Need error handling for memory allocations
 * @todo Some of these currently use kmalloc (GFP_ATOMIC) for
 * small allocations.  This should be evaluated for performance
 * and stability.
 * @{
 */

void *malloc(size_t size);
void free(void *ptr);

enum errorcode { ERR_NONE=0, ERR_NO_MEM };
enum errorcode _stp_errorcode = ERR_NONE;

/** Allocates memory within a probe.
 * This is used for small allocations from within a running
 * probe where the process cannot sleep. 
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 * @bug Currently uses kmalloc (GFP_ATOMIC).
 */

void *_stp_alloc(size_t len)
{
	void *ptr = malloc(len);
	if (unlikely(ptr == NULL))
		_stp_errorcode = ERR_NO_MEM;
	return ptr;
}
#define _stp_alloc_cpu(len, cpu) _stp_alloc(len)

/** Allocates and clears memory within a probe.
 * This is used for small allocations from within a running
 * probe where the process cannot sleep. 
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 * @bug Currently uses kmalloc (GFP_ATOMIC).
 */

void *_stp_calloc(size_t len)
{
	void *ptr = malloc(len);
	if (likely(ptr))
		memset(ptr, 0, len);
	return ptr;
}

/** Allocates and clears memory outside a probe.
 * This is typically used in the module initialization to
 * allocate new maps, lists, etc.
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 */

void *_stp_valloc(size_t len)
{
	void *ptr = malloc(len);
	if (likely(ptr))
		memset(ptr, 0, len);
	else
		_stp_errorcode = ERR_NO_MEM;
	return ptr;
}
#define _stp_valloc_cpu(len, cpu) _stp_valloc(len)
#define __stp_valloc_percpu(size,align) __alloc_percpu(size,align)
#define _stp_vfree_percpu(objp) free_percpu(objp)
#define _stp_valloc_percpu(type) \
	((type *)(__stp_valloc_percpu(sizeof(type), __alignof__(type))))
#define _stp_percpu_dptr(ptr)  (((struct percpu_data *)~(unsigned long)(ptr))->blkp)
/** Frees memory allocated by _stp_alloc or _stp_calloc.
 * @param ptr pointer to memory to free
 */

void _stp_free(void *ptr)
{
	if (likely(ptr))
		free(ptr);
}

/** Frees memory allocated by _stp_valloc.
 * @param ptr pointer to memory to free
 */

void _stp_vfree(void *ptr)
{
	if (likely(ptr))
		free(ptr);
}

/** @} */
#endif /* _ALLOC_C_ */
