/* COVERAGE: umask */
#include <sys/types.h>
#include <sys/stat.h>

int main()
{
  umask (0);
  // umask (00) = NNNN
  umask (7);
  // umask (07) = 00
  umask (077);
  // umask (077) = 07
  umask (0666);
  // umask (0666) = 077
  umask (0777);
  // umask (0777) = 0666
  umask (01777);
  // umask (01777) = 0777
  return 0;
}
