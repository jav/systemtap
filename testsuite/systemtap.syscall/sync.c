/* COVERAGE: fdatasync fsync sync */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


int main()
{
  int fd;

  fd = creat("foobar",S_IREAD|S_IWRITE);

  sync();
  //staptest// sync () = 0

  fsync(fd);
  //staptest// fsync (NNNN) = 0

  fdatasync(fd);
  //staptest// fdatasync (NNNN) = 0

  close(fd);

  return 0;
}
