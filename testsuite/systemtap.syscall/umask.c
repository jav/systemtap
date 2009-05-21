/* COVERAGE: umask */
#include <sys/types.h>
#include <sys/stat.h>

int main()
{
  umask (0);
  //staptest// umask (00) = NNNN
  umask (7);
  //staptest// umask (07) = 00
  umask (077);
  //staptest// umask (077) = 07
  umask (0666);
  //staptest// umask (0666) = 077
  umask (0777);
  //staptest// umask (0777) = 0666
  umask (01777);
  //staptest// umask (01777) = 0777
  return 0;
}
