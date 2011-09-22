#include "sys/sdt.h"

inline int ilibloopfunc (void) {
  int i, j = 0;
  STAP_PROBE(_test_, ilibloopfunc_enter);
  for (i = 0; i < 10; ++i)
    j += i;
  return j;
}

int libloopfunc (void) {
  int i, j = 0;
  STAP_PROBE(_test_, libloopfunc_enter);
  for (i = 0; i < 10; ++i)
    j += ilibloopfunc ();
  return j;
}
