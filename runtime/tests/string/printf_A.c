/* basic printf tests */

/* use very small buffer size for testing */
#define STP_PRINT_BUF_LEN 20
#include "runtime.h"

int main ()
{
  /* can we see output? */
  _stp_printf("ABCDE\n");
  _stp_print_flush();


  /* overflow */
  _stp_printf("1234567890123456789012345\n");
  _stp_printf("XYZZY\n");
  _stp_print_flush();

  /* small string then overflow string */
  _stp_printf("XYZZY\n");
  _stp_printf("1234567890123456789012345");
  _stp_printf("\n");
  _stp_print_flush();

  /* two small string that overflow */
  _stp_printf("abcdefghij");
  _stp_printf("1234567890");
  _stp_printf("\n");
  _stp_print_flush();

  /* two small string that overflow */
  _stp_printf("abcdefghij");
  _stp_printf("1234567890X");
  _stp_printf("\n");
  _stp_print_flush();

  _stp_printf("12345\n");
  _stp_printf("67890\n");
  _stp_printf("abcde\n");
  _stp_print_flush();
  _stp_print_flush();
  _stp_printf("12345");
  _stp_printf("67890");
  _stp_printf("abcde");
  _stp_printf("fghij");
  _stp_printf("\n");
  _stp_print_flush();

  /* null string */
  _stp_printf("");
  _stp_print_flush();  
  _stp_printf("");
  _stp_printf("Q\n");
  _stp_print_flush();  
  return 0;
}
