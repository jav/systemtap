/* more printf tests */

/* use very small buffer size for testing */
#define STP_PRINT_BUF_LEN 20
#include "runtime.h"

#define LLONG_MAX    9223372036854775807LL
#define LLONG_MIN    (-LLONG_MAX - 1LL)


int main ()
{
  int i;

  /* a couple of loops showing continuous output */
  for (i = 0; i < 20; i++)
    _stp_sprintf(_stp_stdout, "i=%d ", i);
  _stp_printf("\n");
  _stp_print_flush();

  for (i = 0; i < 5; i++)
    _stp_printf("[%d  %d  %d] ", i, i*i, i*i*i);
  _stp_printf("\n");
  _stp_print_flush();

  int64_t x,y;
  x = LLONG_MAX;
  y = LLONG_MIN;

  _stp_printf("%lld ",x);
  _stp_printf("(%llx) ", x);
  _stp_printf("%lld ",y);
  _stp_printf("(%llx) ", y);
  _stp_printf("\n");
  _stp_print_flush();
  return 0;
}
