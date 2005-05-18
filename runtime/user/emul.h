#define vscnprintf vsnprintf
#define _stp_log printf

#undef smp_processor_id
#define smp_processor_id(x) 0

#include <stdarg.h>
unsigned long strtoul(const char *nptr, char **endptr, int base);
#define simple_strtoul strtoul
