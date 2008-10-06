/* COVERAGE: link symlink readlink */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main()
{
  int fd;
  char buf[128];

  fd = open("foobar",O_WRONLY|O_CREAT, S_IRWXU);
  close(fd);

  link("foobar", "foobar2");
  // link ("foobar", "foobar2")
  //   linkat (AT_FDCWD, "foobar", AT_FDCWD, "foobar2", 0x0) = 0

  link("foobar", "foobar");
  // link ("foobar", "foobar")
  //   linkat (AT_FDCWD, "foobar", AT_FDCWD, "foobar", 0x0) = -NNNN (EEXIST)

  link("nonexist", "foo");
  // link ("nonexist", "foo")
  //   linkat (AT_FDCWD, "nonexist", AT_FDCWD, "foo", 0x0) = -NNNN (ENOENT)

  symlink("foobar", "Sfoobar");
  // symlink ("foobar", "Sfoobar")
  //   symlinkat ("foobar", AT_FDCWD, "Sfoobar") = 0

  readlink("Sfoobar", buf, sizeof(buf));
  // readlink ("Sfoobar", XXXX, 128)
  //   readlinkat (AT_FDCWD, "Sfoobar", XXXX, 128)

  return 0;
}
