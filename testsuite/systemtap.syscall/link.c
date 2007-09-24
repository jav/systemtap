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
  // link ("foobar", "foobar2") = 0

  link("foobar", "foobar");
  // link ("foobar", "foobar") = -NNNN (EEXIST)

  link("nonexist", "foo");
  // link ("nonexist", "foo") = -NNNN (ENOENT)

  symlink("foobar", "Sfoobar");
  // symlink ("foobar", "Sfoobar") = 0

  readlink("Sfoobar", buf, sizeof(buf));
  // readlink ("Sfoobar", XXXX, 128)

  return 0;
}
