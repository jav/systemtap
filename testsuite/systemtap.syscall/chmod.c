/* COVERAGE: chmod fchmod chown fchown lchown */
/* COVERAGE: chown16 fchown16 lchown16 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>

int main()
{
  int fd;

  fd = open("foobar",O_WRONLY|O_CREAT, 0666);
  // open ("foobar", O_WRONLY|O_CREAT, 0666) = 4

  chmod("foobar", 0644);
  // chmod ("foobar", 0644) = 0

  fchmod(fd, 0444);
  // fchmod (4, 0444) = 0

  chown("foobar", 5000, -1);
#ifdef __i386__
  // chown ("foobar", 5000, -1) =
#else
  // chown ("foobar", 5000, NNNN) =
#endif

  chown("foobar", -1, 5001);
#ifdef __i386__
  // chown ("foobar", -1, 5001) =
#else
  // chown ("foobar", NNNN, 5001) =
#endif

  fchown(fd, 5002, -1);
#ifdef __i386__
  // fchown (4, 5002, -1) =
#else
  // fchown (4, 5002, NNNN) =
#endif

  fchown(fd, -1, 5003);
#ifdef __i386__
  // fchown (4, -1, 5003) =
#else
  // fchown (4, NNNN, 5003) =
#endif

  lchown("foobar", 5004, -1);
#ifdef __i386__
  // lchown ("foobar", 5004, -1) =
#else
  // lchown ("foobar", 5004, NNNN) =
#endif

  lchown("foobar", -1, 5005);
#ifdef __i386__
  // lchown ("foobar", -1, 5005) =
#else
  // lchown ("foobar", NNNN, 5005) =
#endif

#ifdef __i386__
  syscall(SYS_chown, "foobar", 5000, -1);
  // chown16 ("foobar", 5000, -1) =
  syscall(SYS_chown, "foobar", -1, 5001);
  // chown16 ("foobar", -1, 5001) =
  syscall(SYS_fchown, fd, 5002, -1);
  // fchown16 (4, 5002, -1) =
  syscall(SYS_fchown, fd, -1, 5003);
  // fchown16 (4, -1, 5003) =
  syscall(SYS_lchown, "foobar", 5004, -1);
  // lchown16 ("foobar", 5004, -1) =
  syscall(SYS_lchown, "foobar", -1, 5005);
  // lchown16 ("foobar", -1, 5005) =
#endif

  close(fd);
  return 0;
}
