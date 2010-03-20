/* COVERAGE: dup dup2 dup3 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
  dup(2);
  //staptest// dup (2) = NNNN
  
  dup(256);
  //staptest// dup (256) = -9 (EBADF)
  
  dup2(3, 4);
  //staptest// dup2 (3, 4) = 4

  dup2(255, 256);
  //staptest// dup2 (255, 256) = -9 (EBADF)

  /* weird corner case oldfd == newfd */
  dup2(1, 1);
  //staptest// dup2 (1, 1) = 1

#ifdef O_CLOEXEC
  dup3 (4, 5, O_CLOEXEC);
  //staptest// dup3 (4, 5, O_CLOEXEC) = 5

  dup3 (256, 255, O_CLOEXEC);
  //staptest// dup3 (256, 255, O_CLOEXEC) = -9 (EBADF)
  
  dup3 (5, 6, 666);
  //staptest// dup3 (5, 6, UNKNOWN) = -22 (EINVAL)

  /* corner case not valid for dup3 */
  dup3 (1, 1, O_CLOEXEC);
  //staptest// dup3 (1, 1, O_CLOEXEC) = -22 (EINVAL)
#endif

  return 0;
}
