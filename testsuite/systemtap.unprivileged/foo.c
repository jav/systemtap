#include <pthread.h>
#include "sys/sdt.h"

extern libfoofunc (void);

/* Thread entry point */
void *bar (void *b) {
 a:
  return b;
}

/* We need an inline function. */
inline void __attribute__ ((always_inline)) ibar (void) {
/* We need a threaded app. */
  void *x;
  STAP_PROBE(_test_, main_enter);
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_create (& thread, & attr, bar, 0);
  pthread_join (thread, & x);
}

main () {
  ibar ();
  libfoofunc ();
  return 0;
}
