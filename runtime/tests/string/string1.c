/* test of Strings */

/* use very small buffer size for testing */
#define STP_PRINT_BUF_LEN 20
#define STP_NUM_STRINGS 4
#include "runtime.h"

int main ()
{
  String str[4];
  int i;

  for (i = 0; i < 4; i++)
    str[i] = _stp_string_init (i);

  _stp_sprintf(str[0], "Hello world");
  _stp_sprintf(str[1], "Red Hat");
  _stp_sprintf(str[2], "Intel");
  _stp_sprintf(str[3], "IBM");

  for (i = 0; i < 4; i++)
    _stp_print(str[i]);
  _stp_print_cstr("\n");

  for (i = 0; i < 4; i++) {
    _stp_print(str[i]);
    _stp_print(" / ");
  }
  _stp_print_cstr("\n");

  _stp_string_cat_cstr (str[1], " Inc.");
  _stp_print(str[1]);
  _stp_print("\n");

  _stp_string_cat_cstr (str[0], " ");
  _stp_string_cat_string (str[0], str[1]);
  _stp_print(str[0]);
  _stp_print("\n");

  _stp_sprintf(str[2], "%s\n", _stp_string_ptr(str[3]));
  _stp_print(str[2]);
  _stp_print_flush();
  return 0;
}
