/* COVERAGE: ftruncate truncate */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


int main()
{
  int fd;


  fd = creat("foobar",S_IREAD|S_IWRITE);
  ftruncate(fd, 1024);
  // ftruncate (NNNN, 1024) = 0
  close(fd);

  truncate("foobar", 2048);
  // truncate ("foobar", 2048) = 0

  return 0;
}
