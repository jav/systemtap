/* COVERAGE: eventfd eventfd2 */
#include <sys/eventfd.h>

int main()
{
  int fd = eventfd(0, 0);
  //staptest// eventfd (0) = NNNN

#ifdef EFD_NONBLOCK
  fd = eventfd(1, EFD_NONBLOCK);
  //staptest// eventfd2 (1, EFD_NONBLOCK) = NNNN

  fd = eventfd(2, EFD_CLOEXEC);
  //staptest// eventfd2 (2, EFD_CLOEXEC) = NNNN

  fd = eventfd(3, EFD_NONBLOCK|EFD_CLOEXEC);
  //staptest// eventfd2 (3, EFD_NONBLOCK|EFD_CLOEXEC) = NNNN
#endif

  return 0;
}
