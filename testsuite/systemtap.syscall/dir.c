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
  // mkdir ("foobar", 0765) =

  chdir("foobar");
  // chdir ("foobar") = 0

  chdir("..");
  // chdir ("..") = 0

  fd = open("foobar", O_RDONLY);
  // open ("foobar", O_RDONLY) = 4

  fchdir(fd);
  // fchdir (4) = 0

  chdir("..");
  // chdir ("..") = 0

  close(fd);
  // close (4) = 0

  rmdir("foobar");
  // rmdir ("foobar") = 0

  fd = open(".", O_RDONLY);
  // open (".", O_RDONLY) = 4

#ifdef SYS_mkdirat
  mkdirat(fd, "xyzzy", 0765);
  // mkdirat (4, "xyzzy", 0765) = 0

#endif

  close(fd);
  // close (4) = 0

  rmdir("xyzzy");
  // rmdir ("xyzzy") =

  return 0;
}
