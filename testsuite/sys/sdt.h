/* This wrapper is used by testsuite files that do #include "sys/sdt.h".
   We get either the current sys/sdt.h, or sdt-compat.h.  */

#if defined STAP_SDT_V1 || defined STAP_SDT_V2 || \
    defined EXPERIMENTAL_KPROBE_SDT
# include "../../sdt-compat.h"
#else
# include_next <sys/sdt.h>
#endif
