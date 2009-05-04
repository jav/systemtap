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
  //staptest// open ("foobar", O_WRONLY|O_CREAT, 0666) = NNNN

  chmod("foobar", 0644);
  //staptest// chmod ("foobar", 0644)
  //staptest//   fchmodat (AT_FDCWD, "foobar", 0644) = 0

  fchmod(fd, 0444);
  //staptest// fchmod (NNNN, 0444) = 0

  chown("foobar", 5000, -1);
#ifdef __i386__
  //staptest// chown ("foobar", 5000, -1) =
#else
  //staptest// chown ("foobar", 5000, NNNN) =
#endif

  chown("foobar", -1, 5001);
#ifdef __i386__
  //staptest// chown ("foobar", -1, 5001) =
#else
  //staptest// chown ("foobar", NNNN, 5001) =
#endif

  fchown(fd, 5002, -1);
#ifdef __i386__
  //staptest// fchown (NNNN, 5002, -1) =
#else
  //staptest// fchown (NNNN, 5002, NNNN) =
#endif

  fchown(fd, -1, 5003);
#ifdef __i386__
  //staptest// fchown (NNNN, -1, 5003) =
#else
  //staptest// fchown (NNNN, NNNN, 5003) =
#endif

  lchown("foobar", 5004, -1);
#ifdef __i386__
  //staptest// lchown ("foobar", 5004, -1) =
#else
  //staptest// lchown ("foobar", 5004, NNNN) =
#endif

  lchown("foobar", -1, 5005);
#ifdef __i386__
  //staptest// lchown ("foobar", -1, 5005) =
#else
  //staptest// lchown ("foobar", NNNN, 5005) =
#endif

#ifdef __i386__
  syscall(SYS_chown, "foobar", 5000, -1);
  //staptest// chown16 ("foobar", 5000, -1) =
  syscall(SYS_chown, "foobar", -1, 5001);
  //staptest// chown16 ("foobar", -1, 5001) =
  syscall(SYS_fchown, fd, 5002, -1);
  //staptest// fchown16 (NNNN, 5002, -1) =
  syscall(SYS_fchown, fd, -1, 5003);
  //staptest// fchown16 (NNNN, -1, 5003) =
  syscall(SYS_lchown, "foobar", 5004, -1);
  //staptest// lchown16 ("foobar", 5004, -1) =
  syscall(SYS_lchown, "foobar", -1, 5005);
  //staptest// lchown16 ("foobar", -1, 5005) =
#endif

  close(fd);
  return 0;
}
