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

void *_stp_alloc_percpu(size_t size)
{
	int i;
	struct percpu_data *pdata = malloc(sizeof (*pdata));
	if (!pdata)
		return NULL;

	for_each_cpu(i) {
		pdata->ptrs[i] = malloc(size);
		if (!pdata->ptrs[i])
			goto unwind_oom;
		memset(pdata->ptrs[i], 0, size);
	}

	/* Catch derefs w/o wrappers */
	return (void *) (~(unsigned long) pdata);

unwind_oom:
	while (--i >= 0) {
		free(pdata->ptrs[i]);
	}
	free(pdata);
	return NULL;
}

void _stp_free_percpu(const void *objp)
{
	int i;
	struct percpu_data *p = (struct percpu_data *) (~(unsigned long) objp);

	for_each_cpu(i)
	  free(p->ptrs[i]);
	free(p);
}

#endif /* _ALLOC_C_ */
