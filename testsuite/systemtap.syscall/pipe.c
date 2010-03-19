/* COVERAGE: pipe pipe2 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main()
{
  int pipefd[2];
  pipefd[0] = 0;
  pipefd[1] = 0;

  pipe (pipefd);
  //staptest// pipe (\[0, 0\]) = 0

#ifdef O_CLOEXEC
  pipe2 (pipefd, O_CLOEXEC);
  //staptest// pipe2 (\[NNNN, NNNN\], O_CLOEXEC) = 0

  pipe2 (pipefd, O_CLOEXEC|O_NONBLOCK);
  //staptest// pipe2 (\[NNNN, NNNN\], O_NONBLOCK|O_CLOEXEC) = 0
#endif

  return 0;
}
