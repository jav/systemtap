/* COVERAGE: mkdir chdir open fchdir close rmdir mkdirat */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

int main()
{
  int fd;

  mkdir("foobar", 0765);
  //staptest// mkdir ("foobar", 0765) =

  chdir("foobar");
  //staptest// chdir ("foobar") = 0

  chdir("..");
  //staptest// chdir ("..") = 0

  fd = open("foobar", O_RDONLY);
  //staptest// open ("foobar", O_RDONLY) = NNNN

  fchdir(fd);
  //staptest// fchdir (NNNN) = 0

  chdir("..");
  //staptest// chdir ("..") = 0

  close(fd);
  //staptest// close (NNNN) = 0

  rmdir("foobar");
  //staptest// rmdir ("foobar") = 0

  fd = open(".", O_RDONLY);
  //staptest// open (".", O_RDONLY) = NNNN

#ifdef SYS_mkdirat
  mkdirat(fd, "xyzzy", 0765);
  //staptest// mkdirat (NNNN, "xyzzy", 0765) = 0

#endif

  close(fd);
  //staptest// close (NNNN) = 0

  rmdir("xyzzy");
  //staptest// rmdir ("xyzzy") =

  return 0;
}
