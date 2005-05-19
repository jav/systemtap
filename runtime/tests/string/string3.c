/* test of Strings */

/* use very small buffer size for testing */
#define STP_STRING_SIZE 20
#define STP_NUM_STRINGS 4
#include "runtime.h"

int main ()
{
  String str[4];
  int i;

  for (i = 0; i < 4; i++)
    str[i] = _stp_string_init (i);

  _stp_string_cat(str[0], "1234567890");
  _stp_string_cat(str[1], "abc");
  _stp_string_cat(str[2], "ABCDE");
  _stp_string_cat(str[3], "vwxyz");

  for (i = 0; i < 4; i++)
    _stp_print(str[i]);
  _stp_print("\n");

  _stp_string_cat (str[1], "de");
  _stp_print(str[1]);
  _stp_print("\n");

  _stp_string_cat(str[0], str[1]);
  _stp_print(str[0]);
  _stp_print("\n");

  _stp_string_cat(str[0], str[2]);
  _stp_print(str[0]);
  _stp_print("\n");

  _stp_string_cat(str[0], str[2]);
  _stp_print(str[0]);
  _stp_print("\n");

  _stp_sprintf(str[2], "%s\n", _stp_string_ptr(str[3]));
  _stp_print(str[2]);
  _stp_print_flush();
  return 0;
}
