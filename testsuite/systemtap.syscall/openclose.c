/* COVERAGE: open close creat */
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>


int main()
{
  int fd1, fd2;

  fd2 = creat("foobar1",S_IREAD|S_IWRITE);
  //staptest// open ("foobar1", O_WRONLY|O_CREAT|O_TRUNC, 0600) = NNNN

  fd1 = open("foobar2",O_WRONLY|O_CREAT, S_IRWXU);
  //staptest// open ("foobar2", O_WRONLY|O_CREAT, 0700) = NNNN
  close(fd1);
  //staptest// close (NNNN) = 0

  fd1 = open("foobar2",O_RDONLY);
  //staptest// open ("foobar2", O_RDONLY) = NNNN
  close(fd1);
  //staptest// close (NNNN) = 0

  fd1 = open("foobar2",O_RDWR);
  //staptest// open ("foobar2", O_RDWR) = NNNN
  close(fd1);
  //staptest// close (NNNN) = 0

  fd1 = open("foobar2",O_APPEND|O_WRONLY);
  //staptest// open ("foobar2", O_WRONLY|O_APPEND) = NNNN
  close(fd1);
  //staptest// close (NNNN) = 0

  fd1 = open("foobar2",O_DIRECT|O_RDWR);
  //staptest// open ("foobar2", O_RDWR|O_DIRECT) = NNNN
  close(fd1);
  //staptest// close (NNNN) = 0

  fd1 = open("foobar2",O_NOATIME|O_SYNC|O_RDWR);
  //staptest// open ("foobar2", O_RDWR|O_NOATIME|O_SYNC) = NNNN
  close(fd1);
  //staptest// close (NNNN) = 0

  /* Now test some bad opens */
  fd1 = open("/",O_WRONLY);
  //staptest// open ("/", O_WRONLY) = -NNNN (EISDIR)
  close (fd1);
  //staptest// close (NNNN) = -NNNN (EBADF)

  fd1 = open("foobar2",O_WRONLY|O_CREAT|O_EXCL, S_IRWXU);
  //staptest// open ("foobar2", O_WRONLY|O_CREAT|O_EXCL, 0700) = -NNNN (EEXIST)

  return 0;
}
