#include "sys/sdt.h"
#include <stdlib.h>

inline int ilibloopfunc (void) {
  int i, j = 0;
  STAP_PROBE(_test_, ilibloopfunc_enter);
  for (i = 0; i < 10; ++i)
    j += i;
  (void) malloc(100); /* trigger some plt activity */
  return j;
}

int libloopfunc (void) {
  int i, j = 0;
  if (0) goto a;
 a:
  STAP_PROBE(_test_, libloopfunc_enter);
  for (i = 0; i < 10; ++i)
    j += ilibloopfunc ();
  return j;
}
