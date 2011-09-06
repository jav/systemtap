#include "sys/sdt.h"

inline void ilibfoofunc (void) {
  STAP_PROBE(_test_, ilibfoofunc_enter);
}

void libfoofunc (void) {
  STAP_PROBE(_test_, libfoofunc_enter);
  ilibfoofunc ();
}
