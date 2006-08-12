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
  // sync () = 0

  fsync(fd);
  // fsync (4) = 0

  fdatasync(fd);
  // fdatasync (4) = 0

  close(fd);

  return 0;
}
