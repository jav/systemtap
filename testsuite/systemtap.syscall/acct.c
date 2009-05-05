/* COVERAGE: acct */
#include <unistd.h>

int main()
{
  acct("foobar");
  //staptest// acct ("foobar") = -NNNN

  return 0;
}
