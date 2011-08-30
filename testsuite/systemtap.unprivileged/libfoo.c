#include "sys/sdt.h"

inline void ilibfoofunc (void) {
  STAP_PROBE(_test_, libfoofunc_enter);
}

void libfoofunc (void) {
  ilibfoofunc ();
}
