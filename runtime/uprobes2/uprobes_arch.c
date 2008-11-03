#if defined (__x86_64__) || defined(__i386)
#include "uprobes_x86.c"
#elif defined (__powerpc64__)
#include "../uprobes/uprobes_ppc64.c"
#elif defined (__s390__) || defined (__s390x__)
#include "../uprobes/uprobes_s390.c"
#elif defined (__ia64__)
#include "../uprobes/uprobes_ia64.c"
#else
#error "Unsupported architecture"
#endif
