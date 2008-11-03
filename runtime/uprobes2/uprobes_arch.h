#if defined (__x86_64__) || defined(__i386)
#include "uprobes_x86.h"
#elif defined (__powerpc64__)
#include "../uprobes/uprobes_ppc64.h"
#elif defined (__s390__) || defined (__s390x__)
#include "../uprobes/uprobes_s390.h"
#elif defined (__ia64__)
#include "../uprobes/uprobes_ia64.h"
#else
#error "Unsupported architecture"
#endif
