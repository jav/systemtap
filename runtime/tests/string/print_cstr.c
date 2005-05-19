/* test of _stp_print_cstr() */

/* use very small buffer size for testing */
#define STP_PRINT_BUF_LEN 20
#include "runtime.h"

int main ()
{
  /* can we see output? */
  _stp_print_cstr("ABCDE\n");
  _stp_print_flush();


  /* overflow */
  _stp_print_cstr("1234567890123456789012345\n");
  _stp_print_cstr("XYZZY\n");
  _stp_print_flush();

  /* small string then overflow string */
  _stp_print_cstr("XYZZY\n");
  _stp_print_cstr("1234567890123456789012345");
  _stp_print_cstr("\n");
  _stp_print_flush();

  /* two small string that overflow */
  _stp_print_cstr("abcdefghij");
  _stp_print_cstr("1234567890");
  _stp_print_cstr("\n");
  _stp_print_flush();

  /* two small string that overflow */
  _stp_print_cstr("abcdefghij");
  _stp_print_cstr("1234567890X");
  _stp_print_cstr("\n");
  _stp_print_flush();

  _stp_print_cstr("12345\n");
  _stp_print_cstr("67890\n");
  _stp_print_cstr("abcde\n");
  _stp_print_flush();
  _stp_print_flush();
  _stp_print_cstr("12345");
  _stp_print_cstr("67890");
  _stp_print_cstr("abcde");
  _stp_print_cstr("fghij");
  _stp_print_cstr("\n");
  _stp_print_flush();

  /* null string */
  _stp_print_cstr("");
  _stp_print_flush();  
  _stp_print_cstr("");
  _stp_print_cstr("Q\n");
  _stp_print_flush();  
  return 0;
}
