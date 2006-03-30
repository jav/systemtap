#define kmalloc(size,flags) malloc(size)
#define kmalloc(size,flags) malloc(size)
#define kmalloc_node(size,flags,cpu) malloc(size)

#define kfree(ptr) free(ptr)

int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i;
	i=vsnprintf(buf,size,fmt,args);
	return (i >= size) ? (size - 1) : i;
}

//#define _stp_log printf

/* cpu emulation */
#undef smp_processor_id
#undef get_cpu
#undef put_cpu
#undef for_each_cpu
#define for_each_cpu(cpu)			\
  for (cpu = 0; cpu < NR_CPUS; cpu++)

int _processor_number = 0;
#define smp_processor_id() _processor_number
#define get_cpu() _processor_number
#define put_cpu() ;

#include "alloc.c"
void *__alloc_percpu(size_t size, size_t align)
{
	int i;
	struct percpu_data *pdata = malloc(sizeof (*pdata));

	if (!pdata)
		return NULL;

	for (i = 0; i < NR_CPUS; i++) {
	  pdata->ptrs[i] = malloc(size);
	  
	  if (!pdata->ptrs[i])
	    return NULL;

	  memset(pdata->ptrs[i], 0, size);
	}

	/* Catch derefs w/o wrappers */
	return (void *) (~(unsigned long) pdata);
}

void
free_percpu(const void *objp)
{
	int i;
	struct percpu_data *p = (struct percpu_data *) (~(unsigned long) objp);

	for (i = 0; i < NR_CPUS; i++) {
		free(p->ptrs[i]);
	}
	free(p);
}

#include <stdarg.h>
unsigned long strtoul(const char *nptr, char **endptr, int base);
#define simple_strtoul strtoul

const char *_stp_kallsyms_lookup (unsigned long addr,
			     unsigned long *symbolsize,
			     unsigned long *offset,
			     char **modname, 
			     char *namebuf)
{
  static char buf[32];
  sprintf (namebuf, "foobar");
  sprintf (buf, "foobar_mod");
  *offset = 1;
  modname = (char **)&buf;
  return namebuf;
}

int __lockfunc _spin_trylock(spinlock_t *lock)
{
  return 1;
}

void __lockfunc _spin_lock(spinlock_t *lock)
{
}

void __lockfunc _spin_unlock(spinlock_t *lock)
{
}

size_t strlcat(char *dest, const char *src, size_t count)
{
	size_t dsize = strlen(dest);
	size_t len = strlen(src);
	size_t res = dsize + len;

	/* This would be a bug */
	BUG_ON(dsize >= count);

	dest += dsize;
	count -= dsize;
	if (len >= count)
		len = count-1;
	memcpy(dest, src, len);
	dest[len] = 0;
	return res;
}
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
