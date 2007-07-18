/* COVERAGE: epoll_create epoll_ctl epoll_wait poll ppoll */
#define _GNU_SOURCE
#include <sys/epoll.h>
#include <poll.h>
#include <signal.h>

int main()
{
  struct epoll_event ev;
  struct pollfd pfd = {7, 0x23, 0};
  int fd;
  struct timespec tim = {.tv_sec=0, .tv_nsec=200000000};
  sigset_t sigs;

  sigemptyset(&sigs);
  sigaddset(&sigs,SIGUSR2);

  fd = epoll_create(32);
  // epoll_create (32)

  epoll_ctl(fd, EPOLL_CTL_ADD, 13, &ev);
  // epoll_ctl (NNNN, EPOLL_CTL_ADD, 13, XXXX)

  epoll_wait(fd, &ev, 17,0);
  // epoll_wait (NNNN, XXXX, 17, 0)
  close(fd);

  poll(&pfd, 1, 0);
  // poll (XXXX, 1, 0)

#ifdef SYS_ppoll
  ppoll(&pfd, 1, &tim, &sigs);
  //  ppoll (XXXX, 1, \[0.200000000\], XXXX, 8)
#endif

  return 0;
}
