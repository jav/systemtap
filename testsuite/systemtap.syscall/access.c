/* COVERAGE: access */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


int main()
{
  int fd1;

  fd1 = creat("foobar1",S_IREAD|S_IWRITE);

  access("foobar1", F_OK);
  // access ("foobar1", F_OK)
  //   faccessat (AT_FDCWD, "foobar1", F_OK) = 0

  access("foobar1", R_OK);
  // access ("foobar1", R_OK)
  //  faccessat (AT_FDCWD, "foobar1", R_OK) = 0

  access("foobar1", W_OK);
  // access ("foobar1", W_OK)
  //   faccessat (AT_FDCWD, "foobar1", W_OK) = 0

  access("foobar1", X_OK);
  // access ("foobar1", X_OK)
  //   faccessat (AT_FDCWD, "foobar1", X_OK) = -NNNN (EACCES)

  access("foobar1", R_OK|W_OK);
  // access ("foobar1", W_OK |R_OK)
  //   faccessat (AT_FDCWD, "foobar1", W_OK |R_OK) = 0

  access("foobar1", R_OK|W_OK|X_OK);
  // access ("foobar1", X_OK |W_OK |R_OK)
  //   faccessat (AT_FDCWD, "foobar1", X_OK |W_OK |R_OK) = -NNNN (EACCES)

  return 0;
}
