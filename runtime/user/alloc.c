#ifndef _ALLOC_C_
#define _ALLOC_C_

/* emulated memory allocation functions */

void *malloc(size_t size);
void free(void *ptr);

enum errorcode { ERR_NONE=0, ERR_NO_MEM };
enum errorcode _stp_errorcode = ERR_NONE;

void *__kmalloc(size_t size, gfp_t flags)
{
  return malloc(size);
}

#endif /* _ALLOC_C_ */
