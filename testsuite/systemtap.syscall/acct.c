/* COVERAGE: acct */
#include <unistd.h>

int main()
{
  acct("foobar");
  // acct ("foobar") = -NNNN (EPERM)

  return 0;
}
