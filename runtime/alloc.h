/* -*- linux-c -*- */

enum errorcode { ERR_NONE=0, ERR_NO_MEM };
enum errorcode _stp_error = ERR_NONE;

/** Allocates memory within a probe.
 * This is used for small allocations from within a running
 * probe where the process cannot sleep. 
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 * @bug Currently uses kmalloc (GFP_ATOMIC).
 */

void *_stp_alloc(size_t len)
{
	void *ptr = kmalloc(len, GFP_ATOMIC);
	if (unlikely(ptr == NULL))
		_stp_error = ERR_NO_MEM;
	return ptr;
}

/** Allocates and clears memory within a probe.
 * This is used for small allocations from within a running
 * probe where the process cannot sleep. 
 * @param len Number of bytes to allocate.
 * @return a valid pointer on success or NULL on failure.
 * @bug Currently uses kmalloc (GFP_ATOMIC).
 */

void *_stp_calloc(size_t len)
{
	void *ptr = _stp_alloc(len);
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
	void *ptr = vmalloc(len);
	if (likely(ptr))
		memset(ptr, 0, len);
	else
		_stp_error = ERR_NO_MEM;
	return ptr;
}

/** Frees memory allocated by _stp_alloc or _stp_calloc.
 * @param ptr pointer to memory to free
 */

void _stp_free(void *ptr)
{
	if (likely(ptr))
		kfree(ptr);
}

/** Frees memory allocated by _stp_valloc.
 * @param ptr pointer to memory to free
 */

void _stp_vfree(void *ptr)
{
	if (likely(ptr))
		vfree(ptr);
}
