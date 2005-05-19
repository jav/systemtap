/* test of Strings */

/* use very small buffer size for testing */
#define STP_STRING_SIZE 20
#define STP_NUM_STRINGS 1
#include "runtime.h"

int main ()
{
  String str = _stp_string_init (0);

 /* can we see output? */
  _stp_sprintf(str, "ABCDE\n");
  _stp_print(str);


  /* overflow */
  str = _stp_string_init (0);
  _stp_sprintf(str, "1234567890123456789012345\n");
  _stp_sprintf(str, "XYZZY\n");
  _stp_print(str);
  _stp_printf("\n");

  /* small string then overflow string */
  str = _stp_string_init (0);
  _stp_sprintf(str,"XYZZY\n");
  _stp_sprintf(str,"1234567890123456789012345");
  _stp_print(str);
  _stp_printf("\n");

  /* two small string that overflow */
  str = _stp_string_init (0);
  _stp_sprintf(str,"abcdefghij");
  _stp_sprintf(str,"123456789");
  _stp_print(str);
  _stp_printf("\n");

  /* two small string that overflow */
  str = _stp_string_init (0);
  _stp_sprintf(str,"abcdefghij");
  _stp_sprintf(str,"1234567890X");
  _stp_print(str);
  _stp_printf("\n");

  str = _stp_string_init (0);
  _stp_sprintf(str,"12345\n");
  _stp_sprintf(str,"67890\n");
  _stp_sprintf(str,"abcde\n");
  _stp_print(str);

  str = _stp_string_init (0);
  _stp_sprintf(str,"12345");
  _stp_sprintf(str,"67890");
  _stp_sprintf(str,"abcde");
  _stp_sprintf(str,"fghij");
  _stp_print(str);
  _stp_printf("\n");

  /* null string */
  str = _stp_string_init (0);
  _stp_sprintf(str,"");
  _stp_sprintf(str,"");
  _stp_sprintf(str,"Q\n");
  _stp_print(str);

  _stp_print_flush();
  return 0;
}
