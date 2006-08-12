/* COVERAGE: unlink */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


int main()
{
  int fd1;

  fd1 = creat("foobar1",S_IREAD|S_IWRITE);
  close (fd1);
  
  unlink("foobar1");
  // unlink ("foobar1") = 0

  unlink("foobar1");
  // unlink ("foobar1") = -NNNN (ENOENT)

  unlink("foobar2");
  // unlink ("foobar2") = -NNNN (ENOENT)

  unlink(0);
  // unlink (NULL) = -NNNN (EFAULT)

  unlink("..");
  // unlink ("..") = -NNNN (EISDIR)

  unlink("");
  // unlink ("") = -NNNN (ENOENT)

  return 0;
}
