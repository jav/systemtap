
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int i;
	i=vsnprintf(buf,size,fmt,args);
	return (i >= size) ? (size - 1) : i;
}

#define _stp_log printf

#undef smp_processor_id
#define smp_processor_id(x) 0

#include <stdarg.h>
unsigned long strtoul(const char *nptr, char **endptr, int base);
#define simple_strtoul strtoul
